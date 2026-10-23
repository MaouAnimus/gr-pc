#pragma once

#include <cstdint>

namespace gravity::hle {
// Host errors. These values are deliberately NOT Orbis error numbers.
enum class Error {
    ok, invalid_argument, invalid_state, out_of_range, busy, wrong_thread,
    deadlock, permission, timeout, canceled, overflow, unsupported,
    system_error, callback_exception
};

struct Status {
    Error error{Error::ok};
    std::uint32_t native_error{};
    constexpr explicit operator bool() const noexcept { return error == Error::ok; }
};

constexpr const char* error_name(Error error) noexcept {
    switch (error) {
    case Error::ok: return "ok";
    case Error::invalid_argument: return "invalid_argument";
    case Error::invalid_state: return "invalid_state";
    case Error::out_of_range: return "out_of_range";
    case Error::busy: return "busy";
    case Error::wrong_thread: return "wrong_thread";
    case Error::deadlock: return "deadlock";
    case Error::permission: return "permission";
    case Error::timeout: return "timeout";
    case Error::canceled: return "canceled";
    case Error::overflow: return "overflow";
    case Error::unsupported: return "unsupported";
    case Error::system_error: return "system_error";
    case Error::callback_exception: return "callback_exception";
    }
    return "unknown";
}
} // namespace gravity::hle
