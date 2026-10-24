#include "hle/kernel/pthread.h"
#include "hle/guest_memory.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>

namespace gravity::hle::pthread {
namespace {
struct Attribute { std::size_t stack_size{1024 * 1024}; bool detached{}; };
struct Thread {
    ~Thread() { if (handle) CloseHandle(handle); }
    HANDLE handle{};
    Opaque token{};
    Entry entry{};
    void* argument{};
    void* result{};
    std::atomic<int> priority{256};
    bool detached{};
    std::atomic<bool> joining{};
    runner::RunnerConfig config;
};
std::mutex registry_mutex;
std::map<Opaque, Attribute> attributes;
std::map<Opaque, std::shared_ptr<Thread>> threads;
Opaque next_token = 0x80000;
runner::RunnerConfig process_config;
thread_local Opaque current_token{};
thread_local int current_priority = 256;
std::shared_ptr<Thread> find(Opaque id) {
    std::lock_guard lock(registry_mutex);
    const auto it = threads.find(id);
    return it == threads.end() ? nullptr : it->second;
}
DWORD WINAPI worker(void* parameter) {
    std::unique_ptr<std::shared_ptr<Thread>> owner(static_cast<std::shared_ptr<Thread>*>(parameter));
    const auto thread = *owner;
    current_token = thread->token;
    const auto report = runner::execute_current_sysv(reinterpret_cast<std::uintptr_t>(thread->entry),
        reinterpret_cast<std::uint64_t>(thread->argument), thread->config);
    if (!report.succeeded) {
        std::fprintf(stderr, "[Pthread] Callback failed: %s\n", report.error_message.c_str());
    }
    thread->result = reinterpret_cast<void*>(report.return_value);
    if (thread->detached) { std::lock_guard lock(registry_mutex); threads.erase(thread->token); }
    return report.succeeded ? 0 : 1;
}
}
void configure(const runner::RunnerConfig& config) {
    std::lock_guard lock(registry_mutex);
    process_config = config;
    process_config.terminate_process_on_exception = true;
}
extern "C" GRAVITY_SYSV_ABI int attr_init(Opaque* output) {
    std::lock_guard lock(registry_mutex);
    const auto token = next_token++;
    if (!guest_memory::write(output, &token, sizeof(token))) return 22;
    attributes.emplace(token, Attribute{});
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int attr_destroy(Opaque* slot) {
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    const Opaque zero{};
    if (!guest_memory::read(slot, &token, sizeof(token)) || !attributes.contains(token) ||
        !guest_memory::write(slot, &zero, sizeof(zero))) return 22;
    attributes.erase(token);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int attr_setstacksize(Opaque* slot, std::size_t size) {
    if (size < 16384 || size > 64 * 1024 * 1024) return 22;
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    if (!guest_memory::read(slot, &token, sizeof(token)) || !attributes.contains(token)) return 22;
    attributes[token].stack_size = size;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int attr_setdetachstate(Opaque* slot, int state) {
    if (state != 0 && state != 1) return 22;
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    if (!guest_memory::read(slot, &token, sizeof(token)) || !attributes.contains(token)) return 22;
    attributes[token].detached = state == 1;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int create(Opaque* output, const Opaque* attr, Entry entry, void* argument, const char* name) {
    if (!entry || !guest_memory::accessible(output, sizeof(*output), true)) return 22;
    char safe_name[33]{};
    if (name && !guest_memory::string(name, safe_name, sizeof(safe_name))) return 22;
    auto thread = std::make_shared<Thread>();
    Attribute settings;
    {
        std::lock_guard lock(registry_mutex);
        if (attr) {
            Opaque token{};
            if (!guest_memory::read(attr, &token, sizeof(token)) || !attributes.contains(token)) return 22;
            settings = attributes[token];
        }
        thread->token = next_token++;
        thread->config = process_config;
    }
    thread->entry = entry;
    thread->argument = argument;
    thread->detached = settings.detached;
    auto owner = std::make_unique<std::shared_ptr<Thread>>(thread);
    // Reserve additional stack for native C++ adapters and Windows diagnostics.
    const auto host_stack = (std::max)(settings.stack_size, std::size_t(1024 * 1024));
    thread->handle = CreateThread(nullptr, host_stack, worker, owner.get(),
        CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread->handle) return 35; // EAGAIN.
    if (!guest_memory::write(output, &thread->token, sizeof(thread->token))) {
        TerminateThread(thread->handle, 1); // Still suspended; callback never ran.
        return 22;
    }
    { std::lock_guard lock(registry_mutex); threads.emplace(thread->token, thread); }
    if (ResumeThread(thread->handle) == static_cast<DWORD>(-1)) {
        { std::lock_guard lock(registry_mutex); threads.erase(thread->token); }
        TerminateThread(thread->handle, 1);
        const Opaque zero{}; (void)guest_memory::write(output, &zero, sizeof(zero));
        return 35;
    }
    (void)owner.release(); // Worker owns the shared reference after it starts.
    std::fprintf(stderr, "[Pthread] Started %s handle=0x%llx stack=%zu\n", safe_name,
        static_cast<unsigned long long>(thread->token), settings.stack_size);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI Opaque self() {
    return current_token ? current_token : (0x8000000000000000ULL | GetCurrentThreadId());
}
extern "C" GRAVITY_SYSV_ABI int join(Opaque token, void** result) {
    if (token == self()) return 11;
    const auto thread = find(token);
    if (!thread) return 3;
    if (thread->detached || (result && !guest_memory::accessible(result, sizeof(*result), true))) return 22;
    if (thread->joining.exchange(true)) return 22;
    if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0) { thread->joining = false; return 22; }
    if (result && !guest_memory::write(result, &thread->result, sizeof(thread->result))) { thread->joining = false; return 22; }
    { std::lock_guard lock(registry_mutex); threads.erase(token); }
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int getprio(Opaque token, int* output) {
    const auto thread = find(token);
    if (!thread && token != self()) return 3;
    const int priority = thread ? thread->priority.load() : current_priority;
    return guest_memory::write(output, &priority, sizeof(priority)) ? 0 : 22;
}
extern "C" GRAVITY_SYSV_ABI int setprio(Opaque token, int priority) {
    if (priority < 256 || priority > 767) return 22;
    const auto thread = find(token);
    if (!thread && token != self()) return 3;
    // Native scheduler policy: quantize the console range into Windows levels.
    const int native = priority < 384 ? THREAD_PRIORITY_ABOVE_NORMAL : priority > 640 ? THREAD_PRIORITY_BELOW_NORMAL : THREAD_PRIORITY_NORMAL;
    if (!SetThreadPriority(thread ? thread->handle : GetCurrentThread(), native)) return 1;
    if (thread) thread->priority = priority;
    else current_priority = priority;
    return 0;
}
}
