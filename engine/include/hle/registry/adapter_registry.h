#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>

namespace gravity::hle {

struct UnresolvedCallInfo {
    std::string nid;
    std::string library;
    uintptr_t caller_rip{0};
    uintptr_t guest_offset{0};
};

using UnresolvedHook = std::function<void(const UnresolvedCallInfo&)>;

struct AdapterSymbol {
    std::string nid;
    std::string library;
    std::string name;
    uintptr_t func_ptr{0};
    bool is_variable{false};
};

class AdapterRegistry {
public:
    static AdapterRegistry& instance() noexcept;

    // Registers a typed HLE function or variable adapter
    void register_adapter(std::string_view nid, std::string_view library,
                          std::string_view name, uintptr_t func_ptr,
                          bool is_variable = false);

    // Look up an adapter by NID and optional library
    [[nodiscard]] uintptr_t resolve(std::string_view nid, std::string_view library = {}) const noexcept;

    // Look up symbol metadata
    [[nodiscard]] const AdapterSymbol* find(std::string_view nid) const noexcept;

    // Allocates or retrieves an unresolved trap thunk for an unresolved import
    uintptr_t get_unresolved_thunk(std::string_view nid, std::string_view library);

    // Set guest base address for offset calculation
    void set_guest_base(uintptr_t base) noexcept { guest_base_ = base; }
    [[nodiscard]] uintptr_t guest_base() const noexcept { return guest_base_; }

    // Register a custom hook for unresolved calls (used by isolated runner / tests)
    void set_unresolved_hook(UnresolvedHook hook) { unresolved_hook_ = std::move(hook); }

    // Handle an unresolved call dispatched from assembly stub
    void handle_unresolved(uint64_t thunk_id, uintptr_t caller_rip);

    // Get count of registered adapters
    [[nodiscard]] std::size_t size() const noexcept;

    // Total unresolved thunks created
    [[nodiscard]] std::size_t unresolved_count() const noexcept;

    // Initializes all standard HLE adapters (libc, libkernel, libSceFiber, libScePad, libSceVideoOut, libSceAudioOut)
    void initialize_builtin_adapters();

    // Clear registry and free thunk pages (for tests/reset)
    void clear();

private:
    AdapterRegistry();
    ~AdapterRegistry();
    AdapterRegistry(const AdapterRegistry&) = delete;
    AdapterRegistry& operator=(const AdapterRegistry&) = delete;

    void allocate_thunk_page();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, AdapterSymbol> registry_;
    std::vector<UnresolvedCallInfo> unresolved_metadata_;
    std::unordered_map<std::string, uintptr_t> unresolved_thunks_;
    std::vector<void*> thunk_pages_;
    std::size_t thunks_in_current_page_{0};
    uintptr_t guest_base_{0x1000000000ULL};
    UnresolvedHook unresolved_hook_{};
    bool builtins_initialized_{false};
};

} // namespace gravity::hle
