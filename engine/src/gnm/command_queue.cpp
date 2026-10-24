#include "hle/gnm/command_queue.h"
#include "hle/guest_memory.h"
#include <type_traits>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <atomic>

namespace gravity::hle::gnm {
namespace {
pm4::Diagnostic error(pm4::Error e,const char* message) { return {e,0,0,0,message}; }
struct Prepared { DrawRequest request; NativeDraw native; };
}
CommandQueue::CommandQueue(std::size_t batches,std::size_t words)
    : state_(std::make_shared<GraphicsState>()),max_batches_(batches),max_words_(words) {}
pm4::Diagnostic CommandQueue::submit(std::span<const std::span<const std::uint32_t>> buffers,
                                    const pm4::MemoryReader& reader,pm4::Limits limits) {
    auto decoded=pm4::decode(buffers,reader,limits);
    std::lock_guard lock(mutex_);
    if (!decoded.diagnostic) { diagnostic_=decoded.diagnostic; return diagnostic_; }
    if (batches_.size()>=max_batches_ || decoded.word_count>max_words_-stats_.pending_words) {
        diagnostic_=error(pm4::Error::busy,"PM4 submission queue capacity exceeded"); return diagnostic_;
    }
    const auto words=decoded.word_count;
    batches_.push_back({next_batch_++,std::move(decoded)});
    ++stats_.accepted; ++stats_.pending; stats_.pending_words+=words;
    diagnostic_={}; return {};
}
pm4::Diagnostic CommandQueue::set_resolver(DrawResolver resolver) {
    std::lock_guard lock(mutex_);
    if (recording_.id) return error(pm4::Error::busy,"Cannot replace resolver while recording");
    resolver_=std::move(resolver); return {};
}
pm4::Diagnostic CommandQueue::record(VkCommandBuffer cmd,VkExtent2D extent,RecordingTicket& ticket) {
    ticket={};
    try {
    // Single consumer. Resolver must not re-enter this queue.
    std::lock_guard lock(mutex_);
    if (recording_.id) return diagnostic_=error(pm4::Error::busy,"Previous Vulkan recording is unacknowledged");
    if (batches_.empty()) return diagnostic_={};
    {
        static std::atomic<unsigned> rec_calls{};
        if (rec_calls.fetch_add(1) < 5) {
            std::fprintf(stderr, "[CommandQueue::record] batches=%zu, first_batch_cmds=%zu\n",
                         batches_.size(), batches_.front().decoded.commands.size());
        }
    }
    if (!cmd || !extent.width || !extent.height)
        return diagnostic_=error(pm4::Error::state,"Recording needs an active Vulkan command buffer and extent");
    auto staged=std::make_shared<GraphicsState>(*state_);
    std::vector<Prepared> prepared;
    for (const auto& batch:batches_) {
        for (const auto& command:batch.decoded.commands) {
            auto failure=[&](pm4::Error e,const char* message) {
                return pm4::Diagnostic{e,command.buffer_address,command.word_offset,command.opcode,message,command.dcb_index};
            };
            pm4::Diagnostic status;
            std::visit([&](const auto& op) {
                using T=std::decay_t<decltype(op)>;
                if constexpr(std::is_same_v<T,pm4::RegisterWrite>) {
                    std::copy(op.values.begin(),op.values.end(),staged->registers.begin()+op.first);
                } else if constexpr(std::is_same_v<T,pm4::IndexBase>) staged->index_base=op.address;
                else if constexpr(std::is_same_v<T,pm4::IndexType>) staged->index32=op.index32;
                else if constexpr(std::is_same_v<T,pm4::IndexLimit>) staged->index_limit=op.count;
                else if constexpr(std::is_same_v<T,pm4::Instances>) staged->instances=op.count;
                else {
                    DrawRequest request;
                    request.count=op.count; request.instances=staged->instances;
                    if constexpr(std::is_same_v<T,pm4::DrawIndexed>) {
                        if(op.replace_base) staged->index_base=op.address;
                        staged->index_limit=op.maximum;
                        request.indexed=true; request.first_index=op.first;
                        request.index_address=staged->index_base; request.index32=staged->index32;
                        if(!request.index_address || request.index_address%(request.index32?4:2)) {
                            status=failure(pm4::Error::address,"Indexed draw has no valid index base"); return;
                        }
                    }
                    if (!request.count || !request.instances) return;
                    {
                        static std::atomic<unsigned> draw_calls{};
                        if (draw_calls.fetch_add(1) < 4) {
                            std::fprintf(stderr, "[CommandQueue::record DRAW] count=%u, instances=%u, indexed=%d, index_addr=0x%llx\n",
                                         request.count, request.instances, static_cast<int>(request.indexed),
                                         static_cast<unsigned long long>(request.index_address));
                        }
                    }
                    NativeDraw native;
                    if (!resolver_ || !resolver_(*staged,request,native) || !native.pipeline) {
                        status=failure(pm4::Error::resources,"Draw has no native pipeline/resources for its register state"); return;
                    }
                    if (native.vertex_buffers.size()!=native.vertex_offsets.size() ||
                        (!native.descriptors.empty() && !native.layout)) {
                        status=failure(pm4::Error::resources,"Incomplete native vertex/descriptor binding"); return;
                    }
                    for(auto buffer:native.vertex_buffers) if(!buffer) {
                        status=failure(pm4::Error::resources,"Null native vertex buffer"); return;
                    }
                    for(auto set:native.descriptors) if(!set) {
                        status=failure(pm4::Error::resources,"Null native descriptor set"); return;
                    }
                    if (request.indexed) {
                        const auto element=request.index32?4U:2U;
                        const std::uint64_t end=std::uint64_t(request.first_index)+request.count;
                        if(!native.index_buffer || native.index_offset%element || end>native.index_bytes/element) {
                            status=failure(pm4::Error::resources,"Native index-buffer range is too small or misaligned"); return;
                        }
                    }
                    if (native.viewport) {
                        const auto& v=*native.viewport;
                        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.width) ||
                            !std::isfinite(v.height) || !std::isfinite(v.minDepth) || !std::isfinite(v.maxDepth) ||
                            v.width<=0 || v.height<=0 || v.x<0 || v.y<0 ||
                            v.x+v.width>extent.width || v.y+v.height>extent.height ||
                            v.minDepth<0 || v.maxDepth>1 || v.minDepth>v.maxDepth) {
                            status=failure(pm4::Error::resources,"Native viewport is invalid for this target"); return;
                        }
                    }
                    if (native.scissor) {
                        const auto& s=*native.scissor;
                        if (s.offset.x<0 || s.offset.y<0 || std::uint64_t(s.offset.x)+s.extent.width>extent.width ||
                            std::uint64_t(s.offset.y)+s.extent.height>extent.height) {
                            status=failure(pm4::Error::resources,"Native scissor is invalid for this target"); return;
                        }
                    }
                    prepared.push_back({request,std::move(native)});
                }
            },command.operation);
            if (!status) return diagnostic_=status;
        }
    }
    // All validation is complete. No Vulkan command is emitted on preflight failure.
    if (!prepared.empty()) {
        static std::atomic<unsigned> prep_calls{};
        if (prep_calls.fetch_add(1) < 10) {
            std::fprintf(stderr, "[CommandQueue::record PREPARED] prepared_count=%zu\n", prepared.size());
        }
    }
    for(const auto& draw:prepared) {
        const auto& n=draw.native; const auto& r=draw.request;
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,n.pipeline);
        const auto viewport=n.viewport.value_or(VkViewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1});
        const auto scissor=n.scissor.value_or(VkRect2D{{0,0},extent});
        vkCmdSetViewport(cmd,0,1,&viewport);
        vkCmdSetScissor(cmd,0,1,&scissor);
        if (!n.vertex_buffers.empty()) vkCmdBindVertexBuffers(cmd,0,static_cast<std::uint32_t>(n.vertex_buffers.size()),n.vertex_buffers.data(),n.vertex_offsets.data());
        if (!n.descriptors.empty()) vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,n.layout,0,
            static_cast<std::uint32_t>(n.descriptors.size()),n.descriptors.data(),0,nullptr);
        if (r.indexed) {
            vkCmdBindIndexBuffer(cmd,n.index_buffer,n.index_offset,r.index32?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cmd,r.count,r.instances,r.first_index,n.vertex_offset,n.first_instance);
        } else vkCmdDraw(cmd,r.count,r.instances,n.first_vertex,n.first_instance);
    }
    recording_={next_ticket_++,batches_.size(),prepared.size(),std::move(staged)};
    ticket={recording_.id,this};
    diagnostic_={}; return {};
    } catch (...) {
        // A resolver/allocation failure occurs during preflight, before emit.
        std::lock_guard lock(mutex_);
        return diagnostic_=error(pm4::Error::resources,"Native draw preparation threw; submission retained");
    }
}
bool CommandQueue::acknowledge(RecordingTicket ticket) {
    std::lock_guard lock(mutex_);
    if(!ticket.id || ticket.owner!=this || ticket.id!=recording_.id) return false;
    for(std::size_t i=0;i<recording_.batches;++i) {
        stats_.pending_words-=batches_.front().decoded.word_count;
        batches_.pop_front(); --stats_.pending; ++stats_.submitted;
    }
    stats_.submitted_draws+=recording_.draws;
    state_=std::move(recording_.state); recording_={}; return true;
}
void CommandQueue::abort(RecordingTicket ticket) {
    std::lock_guard lock(mutex_);
    if(ticket.id && ticket.owner==this && ticket.id==recording_.id) recording_={};
}
void CommandQueue::reset() {
    auto state=std::make_shared<GraphicsState>();
    std::lock_guard lock(mutex_);
    batches_.clear(); state_=std::move(state); resolver_={}; recording_={}; stats_={}; diagnostic_={};
    // IDs remain monotonic so tickets from a previous lifecycle cannot match.
}
QueueStats CommandQueue::stats() const { std::lock_guard lock(mutex_); return stats_; }
pm4::Diagnostic CommandQueue::last_diagnostic() const { std::lock_guard lock(mutex_); return diagnostic_; }
CommandQueue& command_queue() { static CommandQueue queue; return queue; }
}

