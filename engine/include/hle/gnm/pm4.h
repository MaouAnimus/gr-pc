#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <variant>
#include <vector>

namespace gravity::hle::gnm::pm4 {
enum class Error { none, empty, truncated, packet_type, opcode, flags, length,
                   register_range, address, memory, budget, recursion, state, busy, resources };
struct Diagnostic {
    Error error{Error::none};
    std::uint64_t buffer_address{};
    std::size_t word_offset{};
    std::uint32_t opcode{};
    const char* message{"ok"};
    std::size_t dcb_index{};
    explicit operator bool() const noexcept { return error == Error::none; }
};
struct RegisterWrite { std::uint32_t first; std::vector<std::uint32_t> values; };
struct IndexBase { std::uint64_t address; };
struct IndexType { bool index32; };
struct IndexLimit { std::uint32_t count; };
struct Instances { std::uint32_t count; };
struct DrawAuto { std::uint32_t count; };
struct DrawIndexed { std::uint32_t count, maximum, first; std::uint64_t address; bool replace_base; };
using Operation = std::variant<RegisterWrite, IndexBase, IndexType, IndexLimit, Instances, DrawAuto, DrawIndexed>;
struct Command {
    Operation operation;
    std::uint64_t buffer_address{};
    std::size_t word_offset{};
    std::uint32_t opcode{};
    std::size_t dcb_index{};
};
struct Limits {
    std::size_t max_words{1U << 20};
    std::size_t max_commands{1U << 16};
    std::size_t max_depth{8};
};
// Reader copies exactly the requested DWORDs from a mapped guest range.
// It must reject unmapped memory; no raw guest pointer is dereferenced here.
using MemoryReader = std::function<bool(std::uint64_t, std::span<std::uint32_t>)>;
struct Decoded {
    Diagnostic diagnostic;
    std::vector<Command> commands;
    std::size_t word_count{};
};
Decoded decode(std::span<const std::span<const std::uint32_t>> buffers,
               const MemoryReader& reader = {}, Limits limits = {});

constexpr std::uint32_t type3(std::uint8_t opcode, std::uint32_t payload_words) {
    return 0xC0000000U | ((payload_words - 1U) << 16U) | (std::uint32_t(opcode) << 8U);
}
} // namespace gravity::hle::gnm::pm4

