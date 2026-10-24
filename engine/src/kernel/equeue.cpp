#include "hle/kernel/equeue.h"
#include "hle/guest_memory.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace gravity::hle::equeue {
namespace {
struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Event> pending;
    bool deleted{};
};
std::mutex registry_mutex;
std::map<Handle, std::shared_ptr<Queue>> registry;
Handle next_handle = 0x10000;
std::shared_ptr<Queue> find(Handle handle) {
    std::lock_guard lock(registry_mutex);
    const auto it = registry.find(handle);
    return it == registry.end() ? nullptr : it->second;
}
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelCreateEqueue(Handle* output, const char* name) {
    if (!output || !name) return invalid;
    if (!guest_memory::accessible(output, sizeof(*output), true)) return fault;
    char safe_name[33]{};
    if (!guest_memory::string(name, safe_name, sizeof(safe_name)))
        return static_cast<std::int32_t>(0x8002003fU); // ENAMETOOLONG.
    auto queue = std::make_shared<Queue>();
    std::lock_guard lock(registry_mutex);
    const auto handle = next_handle++;
    if (!guest_memory::write(output, &handle, sizeof(handle))) return fault;
    registry.emplace(handle, std::move(queue));
    std::fprintf(stderr, "[Equeue] Created %s handle=0x%llx\n", safe_name, static_cast<unsigned long long>(handle));
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelDeleteEqueue(Handle handle) {
    std::shared_ptr<Queue> queue;
    {
        std::lock_guard lock(registry_mutex);
        const auto it = registry.find(handle);
        if (it == registry.end()) return bad_handle;
        queue = it->second;
        registry.erase(it);
    }
    { std::lock_guard lock(queue->mutex); queue->deleted = true; }
    queue->changed.notify_all();
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelWaitEqueue(
    Handle handle, Event* events, std::int32_t capacity, std::int32_t* count, const std::uint32_t* timeout_us) {
    const auto queue = find(handle);
    if (!queue) return bad_handle;
    if (capacity < 1 || capacity > 4096) return invalid;
    if (!guest_memory::accessible(events, sizeof(Event) * capacity, true) ||
        !guest_memory::accessible(count, sizeof(*count), true)) return fault;
    std::uint32_t micros{};
    if (timeout_us && !guest_memory::read(timeout_us, &micros, sizeof(micros))) return fault;
    std::unique_lock lock(queue->mutex);
    const auto ready = [&] { return queue->deleted || !queue->pending.empty(); };
    if (!timeout_us) queue->changed.wait(lock, ready);
    else queue->changed.wait_for(lock, std::chrono::microseconds(micros), ready);
    if (queue->deleted) return canceled;
    const auto size = static_cast<std::int32_t>((std::min)(queue->pending.size(), static_cast<std::size_t>(capacity)));
    if (!guest_memory::write(count, &size, sizeof(size))) return fault;
    if (!size) return timed_out;
    std::vector<Event> snapshot(queue->pending.begin(), queue->pending.begin() + size);
    if (!guest_memory::write(events, snapshot.data(), snapshot.size() * sizeof(Event))) return fault;
    queue->pending.erase(queue->pending.begin(), queue->pending.begin() + size);
    return 0;
}
bool post(Handle handle, const Event& event) {
    const auto queue = find(handle);
    if (!queue) return false;
    {
        std::lock_guard lock(queue->mutex);
        if (queue->deleted || queue->pending.size() >= 4096) return false;
        queue->pending.push_back(event);
    }
    queue->changed.notify_one();
    return true;
}
}
