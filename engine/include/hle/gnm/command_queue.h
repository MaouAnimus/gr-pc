#pragma once
#include "hle/gnm/pm4.h"
#include <vulkan/vulkan.h>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace gravity::hle::gnm {
struct GraphicsState {
    std::array<std::uint32_t,0x10000> registers{};
    std::uint64_t index_base{};
    std::uint32_t index_limit{}, instances{1};
    bool index32{};
};
struct DrawRequest {
    bool indexed{};
    std::uint32_t count{}, first_index{}, instances{};
    std::uint64_t index_address{};
    bool index32{};
};
// Resolver owns pipeline/descriptor/resource validation and Vulkan object lifetime.
// It receives ALL raw register state, and must reject state it cannot implement.
// Objects must remain alive until the caller's GPU submission fence completes.
struct NativeDraw {
    VkPipeline pipeline{};
    VkBuffer index_buffer{};
    VkDeviceSize index_offset{}, index_bytes{};
    VkPipelineLayout layout{};
    std::vector<VkDescriptorSet> descriptors;
    std::vector<VkBuffer> vertex_buffers;
    std::vector<VkDeviceSize> vertex_offsets;
    std::optional<VkViewport> viewport;
    std::optional<VkRect2D> scissor;
    std::uint32_t first_vertex{}, first_instance{};
    std::int32_t vertex_offset{};
};
using DrawResolver=std::function<bool(const GraphicsState&,const DrawRequest&,NativeDraw&)>;
struct QueueStats {
    std::uint64_t accepted{}, submitted{}, submitted_draws{};
    std::size_t pending{}, pending_words{};
};
class CommandQueue;
struct RecordingTicket { std::uint64_t id{}; const CommandQueue* owner{}; };
class CommandQueue {
public:
    explicit CommandQueue(std::size_t max_batches=64,std::size_t max_words=4U<<20);
    pm4::Diagnostic submit(std::span<const std::span<const std::uint32_t>> buffers,
                           const pm4::MemoryReader& reader={},pm4::Limits limits={});
    pm4::Diagnostic set_resolver(DrawResolver resolver);
    // Called inside an active dynamic-rendering instance. Fully prepares all
    // pending draws before emitting commands. Does not remove pending batches.
    pm4::Diagnostic record(VkCommandBuffer command_buffer,VkExtent2D extent,RecordingTicket& ticket);
    // Call only AFTER vkQueueSubmit succeeds. This is NOT GPU completion.
    bool acknowledge(RecordingTicket ticket);
    void abort(RecordingTicket ticket);
    void reset();
    QueueStats stats() const;
    pm4::Diagnostic last_diagnostic() const;
private:
    struct Batch { std::uint64_t id; pm4::Decoded decoded; };
    struct PendingRecording {
        std::uint64_t id{}; std::size_t batches{}; std::uint64_t draws{};
        std::shared_ptr<GraphicsState> state;
    };
    mutable std::mutex mutex_;
    std::deque<Batch> batches_;
    std::shared_ptr<GraphicsState> state_;
    DrawResolver resolver_;
    QueueStats stats_;
    pm4::Diagnostic diagnostic_;
    PendingRecording recording_;
    std::uint64_t next_batch_{1},next_ticket_{1};
    std::size_t max_batches_,max_words_;
};
CommandQueue& command_queue();
} // namespace gravity::hle::gnm

