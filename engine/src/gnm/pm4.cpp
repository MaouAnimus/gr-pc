#include "hle/gnm/pm4.h"
#include "hle/guest_memory.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

namespace gravity::hle::gnm::pm4 {
namespace {
class Decoder {
public:
    Decoder(const MemoryReader& reader, Limits limits) : reader_(reader), limits_(limits) {}
    Decoded run(std::span<const std::span<const std::uint32_t>> buffers) {
        if (buffers.empty()) fail(Error::empty, 0, 0, 0, "No DCBs supplied");
        for (std::size_t i=0;i<buffers.size();++i) {
            dcb_index_=i;
            const auto buffer=buffers[i];
            if (!result_.diagnostic) break;
            if (!parse(buffer, 0, 0)) break;
        }
        if (!result_.diagnostic) result_.commands.clear();
        return std::move(result_);
    }
private:
    bool fail(Error error, std::uint64_t address, std::size_t offset, std::uint32_t opcode, const char* message) {
        result_.diagnostic = {error, address, offset, opcode, message,dcb_index_}; return false;
    }
    bool parse(std::span<const std::uint32_t> words, std::uint64_t address, std::size_t depth) {
        if (words.empty()) return fail(Error::empty,address,0,0,"Empty DCB/IB");
        if (depth > limits_.max_depth) return fail(Error::recursion,address,0,0,"Indirect buffer nesting limit");
        if (words.size() > limits_.max_words - result_.word_count)
            return fail(Error::budget,address,0,0,"Command DWORD budget exceeded");
        result_.word_count += words.size();
        for (std::size_t at=0; at<words.size();) {
            const auto header=words[at];
            const auto type=header>>30;
            if (type==2) {
                if (header!=0x80000000U) return fail(Error::flags,address,at,0,"Invalid type-2 padding");
                ++at; continue;
            }
            const auto opcode=(header>>8)&0xFF;
            if (type!=3) return fail(Error::packet_type,address,at,opcode,"Only type-2 and type-3 PM4 are supported");
            const std::size_t count=((header>>16)&0x3FFF)+1;
            if (count>words.size()-at-1) return fail(Error::truncated,address,at,opcode,"Truncated PM4 packet");
            if (header&0xFF) return fail(Error::flags,address,at,opcode,"Predication/compute/reserved header flags unsupported");
            const auto payload=words.subspan(at+1,count);
            auto exact=[&](std::size_t expected) {
                return count==expected || fail(Error::length,address,at,opcode,"Invalid PM4 payload size");
            };
            auto emit=[&](Operation op) {
                if (result_.commands.size()>=limits_.max_commands)
                    return fail(Error::budget,address,at,opcode,"Decoded operation budget exceeded");
                result_.commands.push_back({std::move(op),address,at,opcode,dcb_index_}); return true;
            };
            switch (opcode) {
            case 0x10: break; // NOP: payload is opaque, not another packet stream.
            case 0x12: break; // CLEAR_STATE: context reset
            case 0x28: break; // CONTEXT_CONTROL: context control
            case 0x3C: break; // WAIT_REG_MEM: register/memory poll wait
            case 0x42: break; // PFP_SYNC_ME: command processor sync
            case 0x43: break; // SURFACE_SYNC: surface cache flush
            case 0x46: break; // EVENT_WRITE: event trigger
            case 0x48: break; // EVENT_WRITE_EOS: end-of-shader event
            case 0x49: break; // RELEASE_MEM: cache flush & memory release
            case 0x50: break; // DMA_DATA: direct memory copy
            case 0x58: break; // ACQUIRE_MEM: memory sync / cache barrier
            case 0x47: {      // EVENT_WRITE_EOP: end-of-pipe event write/sync
                if (count >= 4) {
                    const std::uint64_t dst_addr = (static_cast<std::uint64_t>(payload[2] & 0xFFFF) << 32) | (payload[1] & ~0x3u);
                    const std::uint32_t data_sel = (payload[2] >> 29) & 0x7;
                    if (dst_addr != 0 && data_sel != 0 && gravity::hle::guest_memory::accessible(reinterpret_cast<void*>(dst_addr), sizeof(std::uint32_t), true)) {
                        if (data_sel == 1) { // 32-bit data
                            *reinterpret_cast<std::uint32_t*>(dst_addr) = payload[3];
                        } else if (data_sel == 2 && count >= 5 && gravity::hle::guest_memory::accessible(reinterpret_cast<void*>(dst_addr), sizeof(std::uint64_t), true)) { // 64-bit data
                            const std::uint64_t val = (static_cast<std::uint64_t>(payload[4]) << 32) | payload[3];
                            *reinterpret_cast<std::uint64_t*>(dst_addr) = val;
                        } else if (data_sel == 3 && gravity::hle::guest_memory::accessible(reinterpret_cast<void*>(dst_addr), sizeof(std::uint64_t), true)) { // Timestamp
                            static std::atomic<std::uint64_t> s_gpu_ts{1000};
                            *reinterpret_cast<std::uint64_t*>(dst_addr) = s_gpu_ts.fetch_add(100);
                        }
                    }
                }
                break;
            }
            case 0x37: { // WRITE_DATA
                if (count >= 4) {
                    const std::uint32_t dst_sel = (payload[0] >> 8) & 0xF;
                    if (dst_sel == 5) { // memory destination
                        const std::uint64_t dst_addr = (static_cast<std::uint64_t>(payload[2]) << 32) | (payload[1] & ~0x3u);
                        const std::size_t write_words = count - 3;
                        if (dst_addr != 0 && gravity::hle::guest_memory::accessible(reinterpret_cast<void*>(dst_addr), write_words * sizeof(std::uint32_t), true)) {
                            std::memcpy(reinterpret_cast<void*>(dst_addr), &payload[3], write_words * sizeof(std::uint32_t));
                        }
                    }
                }
                break;
            }
            case 0x40: { // COPY_DATA
                if (count >= 4) {
                    const std::uint64_t src_addr = (static_cast<std::uint64_t>(payload[1]) << 32) | payload[0];
                    const std::uint64_t dst_addr = (static_cast<std::uint64_t>(payload[3]) << 32) | payload[2];
                    if (dst_addr != 0 && gravity::hle::guest_memory::accessible(reinterpret_cast<void*>(dst_addr), sizeof(std::uint32_t), true)) {
                        if (src_addr != 0 && gravity::hle::guest_memory::accessible(reinterpret_cast<const void*>(src_addr), sizeof(std::uint32_t), false)) {
                            *reinterpret_cast<std::uint32_t*>(dst_addr) = *reinterpret_cast<const std::uint32_t*>(src_addr);
                        } else {
                            *reinterpret_cast<std::uint32_t*>(dst_addr) = 0;
                        }
                    }
                }
                break;
            }
            case 0x68: case 0x69: case 0x76: case 0x79: {
                if (count<2) return fail(Error::length,address,at,opcode,"Register write needs offset and value");
                const std::uint32_t base=opcode==0x68?0x2000:opcode==0x69?0xA000:opcode==0x76?0x2C00:0xC000;
                const std::uint32_t end=opcode==0x68?0x2C00:opcode==0x69?0xA400:opcode==0x76?0x3000:0xC400;
                if (payload[0]>=end-base || count-1>end-base-payload[0])
                    return fail(Error::register_range,address,at,opcode,"Register write exceeds bank or uses indexed flags");
                if (!emit(RegisterWrite{base+payload[0],{payload.begin()+1,payload.end()}})) return false;
                break;
            }
            case 0x2F:
                if (!exact(1) || !emit(Instances{payload[0]})) return false; break;
            case 0x2A:
                if (!exact(1)) return false;
                if (payload[0]>1) return fail(Error::flags,address,at,opcode,"Only unswapped uint16/uint32 indices supported");
                if (!emit(IndexType{payload[0]==1})) return false; break;
            case 0x26: {
                if (!exact(2)) return false;
                if ((payload[0]&1) || payload[1]>0xFFFF)
                    return fail(Error::address,address,at,opcode,"Invalid index address");
                if (!emit(IndexBase{std::uint64_t(payload[0]) | (std::uint64_t(payload[1])<<32)})) return false; break;
            }
            case 0x13:
                if (!exact(1) || !emit(IndexLimit{payload[0]})) return false; break;
            case 0x2D:
                if (!exact(2)) return false;
                if (payload[1]!=2) return fail(Error::flags,address,at,opcode,"DRAW_INDEX_AUTO requires auto-index source without extra flags");
                if (!emit(DrawAuto{payload[0]})) return false; break;
            case 0x27:
                if (!exact(5)) return false;
                if (payload[4]!=0 || (payload[1]&1) || payload[2]>0xFFFF)
                    return fail(Error::flags,address,at,opcode,"Unsupported DRAW_INDEX_2 mode/address");
                if (payload[3]>payload[0]) return fail(Error::state,address,at,opcode,"Index count exceeds maximum");
                if (!emit(DrawIndexed{payload[3],payload[0],0,std::uint64_t(payload[1])|(std::uint64_t(payload[2])<<32),true})) return false;
                break;
            case 0x35:
                if (!exact(4)) return false;
                if (payload[3]!=0) return fail(Error::flags,address,at,opcode,"Unsupported DRAW_INDEX_OFFSET_2 flags");
                if (payload[1]>payload[0] || payload[2]>payload[0]-payload[1])
                    return fail(Error::state,address,at,opcode,"Index offset/count exceeds maximum");
                if (!emit(DrawIndexed{payload[2],payload[0],payload[1],0,false})) return false;
                break;
            case 0x3F: {
                if (!exact(3)) return false;
                const auto control=payload[2];
                const std::size_t size=control&0xFFFFF;
                const bool chain=(control&(1U<<20))!=0;
                if ((control&~0x1FFFFFU) || (payload[0]&3) || payload[1]>0xFFFF || !size)
                    return fail(Error::flags,address,at,opcode,"Unsupported/invalid indirect buffer control");
                const auto target=std::uint64_t(payload[0])|(std::uint64_t(payload[1])<<32);
                if (depth>=limits_.max_depth || std::find(active_.begin(),active_.end(),target)!=active_.end())
                    return fail(Error::recursion,address,at,opcode,"Indirect buffer cycle/depth limit");
                if (size>limits_.max_words-result_.word_count)
                    return fail(Error::budget,address,at,opcode,"Indirect buffer DWORD budget exceeded");
                if (!reader_) return fail(Error::memory,address,at,opcode,"No guest-memory reader for indirect buffer");
                std::vector<std::uint32_t> snapshot(size);
                if (!reader_(target,snapshot)) return fail(Error::memory,address,at,opcode,"Cannot snapshot indirect buffer");
                active_.push_back(target);
                const bool ok=parse(snapshot,target,depth+1);
                active_.pop_back();
                if (!ok) return false;
                if (chain) return true; // Tail jump: remaining parent words are not executed.
                break;
            }
            default: return fail(Error::opcode,address,at,opcode,"Unsupported PM4 opcode");
            }
            at+=count+1;
        }
        return true;
    }
    const MemoryReader& reader_;
    Limits limits_;
    Decoded result_;
    std::vector<std::uint64_t> active_;
    std::size_t dcb_index_{};
};
}
Decoded decode(std::span<const std::span<const std::uint32_t>> buffers,const MemoryReader& reader,Limits limits) {
    return Decoder(reader,limits).run(buffers);
}
}

