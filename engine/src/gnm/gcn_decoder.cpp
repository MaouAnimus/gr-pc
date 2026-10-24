#include "hle/gnm/gcn_decoder.h"

#include <algorithm>
#include <utility>

namespace gravity::hle::gnm::gcn {
namespace {

// Source: AMD Sea Islands ISA, revision 1.3, chapter 13. Check specific
// prefixes before SOPK/SOP2/VOP2: those broader encodings overlap them.
Instruction classify(std::uint32_t word) {
    Instruction instruction;
    instruction.word_count = 1;
    instruction.raw[0] = word;
    if ((word & 0xff800000u) == 0xbe800000u) {
        instruction.encoding = Encoding::sop1;
        instruction.opcode = (word >> 8) & 0xffu;
        instruction.has_literal = (word & 0xffu) == 255;
    } else if ((word & 0xff800000u) == 0xbf000000u) {
        instruction.encoding = Encoding::sopc;
        instruction.opcode = (word >> 16) & 0x7fu;
        instruction.has_literal = (word & 0xffu) == 255 || ((word >> 8) & 0xffu) == 255;
    } else if ((word & 0xff800000u) == 0xbf800000u) {
        instruction.encoding = Encoding::sopp;
        instruction.opcode = (word >> 16) & 0x7fu;
    } else if ((word & 0xf0000000u) == 0xb0000000u) {
        instruction.encoding = Encoding::sopk;
        instruction.opcode = (word >> 23) & 0x1fu;
        // S_SETREG_IMM32_B32 has a literal irrespective of SIMM16.
        instruction.has_literal = instruction.opcode == 21;
    } else if ((word & 0xc0000000u) == 0x80000000u) {
        instruction.encoding = Encoding::sop2;
        instruction.opcode = (word >> 23) & 0x7fu;
        instruction.has_literal = (word & 0xffu) == 255 || ((word >> 8) & 0xffu) == 255;
    } else if ((word & 0xfe000000u) == 0x7e000000u) {
        instruction.encoding = Encoding::vop1;
        instruction.opcode = (word >> 9) & 0xffu;
        instruction.has_literal = (word & 0x1ffu) == 255;
    } else if ((word & 0xfe000000u) == 0x7c000000u) {
        instruction.encoding = Encoding::vopc;
        instruction.opcode = (word >> 17) & 0xffu;
        instruction.has_literal = (word & 0x1ffu) == 255;
    } else if ((word & 0x80000000u) == 0) {
        instruction.encoding = Encoding::vop2;
        instruction.opcode = (word >> 25) & 0x3fu;
        // V_MADMK_F32 and V_MADAK_F32 always carry their K operand.
        instruction.has_literal = (word & 0x1ffu) == 255 ||
                                  instruction.opcode == 32 || instruction.opcode == 33;
    } else if ((word & 0xf8000000u) == 0xc0000000u) {
        instruction.encoding = Encoding::smrd;
        instruction.opcode = (word >> 22) & 0x1fu;
    } else if ((word & 0xfc000000u) == 0xc8000000u) {
        instruction.encoding = Encoding::vintrp;
        instruction.opcode = (word >> 16) & 3u;
    } else if ((word & 0xfc000000u) == 0xd0000000u) {
        instruction.encoding = Encoding::vop3;
        instruction.opcode = (word >> 17) & 0x1ffu;
        instruction.word_count = 2;
    } else if ((word & 0xfc000000u) == 0xd8000000u) {
        instruction.encoding = Encoding::ds;
        instruction.opcode = (word >> 18) & 0xffu;
        instruction.word_count = 2;
    } else if ((word & 0xfc000000u) == 0xe0000000u) {
        instruction.encoding = Encoding::mubuf;
        instruction.opcode = (word >> 18) & 0x7fu;
        instruction.word_count = 2;
    } else if ((word & 0xfc000000u) == 0xe8000000u) {
        instruction.encoding = Encoding::mtbuf;
        instruction.opcode = (word >> 16) & 7u;
        instruction.word_count = 2;
    } else if ((word & 0xfc000000u) == 0xf0000000u) {
        instruction.encoding = Encoding::mimg;
        instruction.opcode = (word >> 18) & 0x7fu;
        instruction.word_count = 2;
    } else if ((word & 0xfc000000u) == 0xf8000000u) {
        instruction.encoding = Encoding::exp;
        instruction.opcode = 0; // EXP uses a target rather than an opcode.
        instruction.word_count = 2;
    }
    if (instruction.has_literal) ++instruction.word_count;
    return instruction;
}

bool relative_branch(const Instruction& instruction) {
    if (instruction.encoding != Encoding::sopp) return false;
    return instruction.opcode == 2 || (instruction.opcode >= 4 && instruction.opcode <= 9) ||
           (instruction.opcode >= 23 && instruction.opcode <= 26);
}

} // namespace

Program decode(std::span<const std::uint32_t> words) {
    Program program;
    auto fail = [&](std::size_t offset, std::string message) {
        program.diagnostic = {false, offset, offset < words.size() ? words[offset] : 0, std::move(message)};
        return std::move(program);
    };
    if (words.size() > max_shader_words) return fail(0, "GCN shader exceeds the 65536 DWORD input limit");
    if (words.empty()) return fail(0, "GCN shader is empty; S_ENDPGM is required");
    program.instructions.reserve((std::min)(words.size(), max_shader_instructions));
    bool terminated = false;
    while (program.word_count < words.size()) {
        const auto offset = program.word_count;
        if (program.instructions.size() == max_shader_instructions)
            return fail(offset, "GCN shader exceeds the 16384 instruction limit");
        auto instruction = classify(words[offset]);
        instruction.word_offset = offset;
        if (instruction.encoding == Encoding::unknown) return fail(offset, "Unknown Sea Islands instruction encoding");
        if (words.size() - offset < instruction.word_count)
            return fail(offset, instruction.has_literal ? "Truncated GCN literal operand" : "Truncated 64-bit GCN instruction");
        for (std::size_t i = 1; i < instruction.word_count; ++i) instruction.raw[i] = words[offset + i];
        if (instruction.has_literal) instruction.literal = instruction.raw[instruction.word_count - 1];
        program.word_count += instruction.word_count;
        program.instructions.push_back(instruction);
        if (instruction.encoding == Encoding::sopp && instruction.opcode == 1) {
            terminated = true;
            break;
        }
    }
    if (!terminated) return fail(program.word_count, "GCN shader has no S_ENDPGM terminator");

    std::vector<bool> boundary(program.word_count, false);
    for (const auto& instruction : program.instructions) boundary[instruction.word_offset] = true;
    for (const auto& instruction : program.instructions) {
        if (!relative_branch(instruction)) continue;
        // Avoid implementation-defined narrowing from uint32_t to int16_t.
        const auto immediate = instruction.raw[0] & 0xffffu;
        const auto displacement = static_cast<std::int64_t>(immediate) - ((immediate & 0x8000u) ? 65536 : 0);
        const auto target = static_cast<std::int64_t>(instruction.word_offset) + 1 + displacement;
        if (target < 0 || target >= static_cast<std::int64_t>(program.word_count))
            return fail(instruction.word_offset, "GCN branch target is outside the terminated instruction stream");
        if (!boundary[static_cast<std::size_t>(target)])
            return fail(instruction.word_offset, "GCN branch target points into a literal or the second instruction word");
    }
    return program;
}

} // namespace gravity::hle::gnm::gcn
