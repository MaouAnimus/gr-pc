#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gravity::hle::gnm::gcn {

// AMD Sea Islands (GCN 1/2) encodings. Later GCN generations reuse opcode
// numbers with different meanings and must not be decoded using this table.
enum class Encoding {
    sop1, sop2, sopk, sopc, sopp,
    vop1, vop2, vopc, vop3,
    ds, exp, smrd, vintrp, mubuf, mtbuf, mimg, unknown
};

struct Instruction {
    Encoding encoding{Encoding::unknown};
    std::uint16_t opcode{};
    std::size_t word_offset{};
    std::uint8_t word_count{};
    std::array<std::uint32_t, 3> raw{};
    bool has_literal{};
    std::uint32_t literal{};
};

struct Diagnostic {
    bool success{true};
    std::size_t word_offset{};
    std::uint32_t raw_word{};
    std::string message;
    explicit operator bool() const { return success; }
};

struct Program {
    std::vector<Instruction> instructions;
    // Consumed words, including S_ENDPGM. Shader binary information following
    // the terminator is not an instruction stream and is deliberately excluded.
    std::size_t word_count{};
    Diagnostic diagnostic;
};

inline constexpr std::size_t max_shader_words = 65536;
inline constexpr std::size_t max_shader_instructions = 16384;

// This validates instruction boundaries, not whether an opcode can be lowered.
// Unsupported operations must be rejected by the translator, never substituted.
Program decode(std::span<const std::uint32_t> words);

} // namespace gravity::hle::gnm::gcn
