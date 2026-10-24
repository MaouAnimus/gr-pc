#include "hle/gnm/gcn_decoder.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

using namespace gravity::hle::gnm::gcn;

namespace {
constexpr std::uint32_t endpgm = 0xbf810000u;

void scalar() {
    // Real Sea Islands encodings, including shader binary-info marker.
    const std::array words{0xbeeb03ffu, 0x00000003u, // s_mov_b32 vcc_hi, 3
                           0xb000ffffu,              // s_movk_i32 s0, -1
                           0x80018200u,              // s_add_u32 s1, s0, 2
                           0xbf068001u,              // s_cmp_eq_u32 s1, 0
                           0xbf8c007fu,              // s_waitcnt 0x7f
                           endpgm};
    const auto program = decode(words);
    CHECK(program.diagnostic && program.word_count == words.size());
    CHECK(program.instructions.size() == 6);
    CHECK(program.instructions[0].encoding == Encoding::sop1 && program.instructions[0].opcode == 3);
    CHECK(program.instructions[0].has_literal && program.instructions[0].literal == 3);
    CHECK(program.instructions[1].encoding == Encoding::sopk && program.instructions[1].opcode == 0);
    CHECK(program.instructions[2].encoding == Encoding::sop2 && program.instructions[2].opcode == 0);
    CHECK(program.instructions[3].encoding == Encoding::sopc && program.instructions[3].opcode == 6);
    CHECK(program.instructions[4].encoding == Encoding::sopp && program.instructions[4].opcode == 12);
    CHECK(program.instructions[5].word_offset == 6);
}

void literals() {
    const std::array words{0xbe8003ffu, 0x3f800000u, // SOP1 literal
                           0x8001ffffu, 0x12345678u, // both SOP2 sources share ONE literal
                           0xbf00ffffu, 0x9abcdef0u, // both SOPC sources share ONE literal
                           0x7e0002ffu, 0x3f000000u, // v_mov_b32 v0, literal
                           0x060002ffu, 0x40000000u, // v_add_f32 v0, literal, v1
                           0x7c0202ffu, 0x40400000u, // v_cmp_lt_f32 literal, v1
                           0x40000200u, 0x40800000u, // v_madmk_f32 v0, v0, 4, v1
                           0x42000200u, 0x40a00000u, // v_madak_f32 v0, v0, v1, 5
                           0xba800000u, 0x00000001u, // s_setreg_imm32_b32
                           endpgm};
    const auto program = decode(words);
    CHECK(program.diagnostic && program.instructions.size() == 10);
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(program.instructions[i].word_offset == i * 2);
        CHECK(program.instructions[i].word_count == 2 && program.instructions[i].has_literal);
        CHECK(program.instructions[i].raw[1] == words[i * 2 + 1]);
        CHECK(program.instructions[i].literal == words[i * 2 + 1]);
    }
    CHECK(program.instructions[3].opcode == 1);
    CHECK(program.instructions[4].opcode == 3);
    CHECK(program.instructions[5].opcode == 1);
    // VGPR255 is selector511, not selector255 (the literal sentinel).
    const std::array vgpr255{0x7e0003ffu, endpgm};
    const auto no_literal = decode(vgpr255);
    CHECK(no_literal.diagnostic && no_literal.instructions.size() == 2);
    CHECK(!no_literal.instructions[0].has_literal);
}

void formats() {
    const std::array words{0xd2820004u, 0x040e0500u, // VOP3 v_mad_f32
                           0xd8340007u, 0x00000302u, // ds_write_b32 offset7, addr=v2,data=v3
                           0xd8d80000u, 0x04000002u, // ds_read_b32 v4, v2
                           0xf80008cfu, 0x03020100u, // exp pos0, v0,v1,v2,v3 done
                           0xc0840100u,              // s_load_dwordx4 s[8:11], s[0:1], 0
                           0xc8040100u,              // VINTRP
                           0xe0300000u, 0x80000000u, // buffer_load_dword
                           0xebe30000u, 0x80000000u, // tbuffer_load_format_xyzw
                           0xf0800f00u, 0x00000000u, // image_sample
                           endpgm};
    const auto program = decode(words);
    CHECK(program.diagnostic && program.instructions.size() == 10);
    constexpr std::array encodings{Encoding::vop3, Encoding::ds, Encoding::ds, Encoding::exp,
                                  Encoding::smrd, Encoding::vintrp, Encoding::mubuf,
                                  Encoding::mtbuf, Encoding::mimg, Encoding::sopp};
    for (std::size_t i = 0; i < encodings.size(); ++i) CHECK(program.instructions[i].encoding == encodings[i]);
    CHECK(program.instructions[0].opcode == 321);
    CHECK(program.instructions[1].opcode == 13 && program.instructions[2].opcode == 54);
    CHECK(program.instructions[3].word_count == 2 && !program.instructions[3].has_literal);
    CHECK(program.instructions[4].opcode == 2);
    CHECK(program.instructions[6].opcode == 12);
    CHECK(program.instructions[7].opcode == 3);
    CHECK(program.instructions[8].opcode == 32);
}

void branches() {
    const std::array forward{0xbf820002u, 0xbe8003ffu, 0u, endpgm};
    CHECK(decode(forward).diagnostic);
    const std::array backward{0xbf800000u, 0xbf85fffeu, endpgm};
    CHECK(decode(backward).diagnostic);
    const std::array self{0xbf82ffffu, endpgm};
    CHECK(decode(self).diagnostic);
    for (std::uint32_t opcode : {2u, 4u, 5u, 6u, 7u, 8u, 9u, 23u, 24u, 25u, 26u}) {
        const std::array into_literal{0xbf800001u | (opcode << 16), 0xbe8003ffu, 0u, endpgm};
        const auto bad = decode(into_literal);
        CHECK(!bad.diagnostic && bad.diagnostic.word_offset == 0);
        CHECK(bad.diagnostic.message.find("literal") != std::string::npos);
    }
    const std::array into_word2{0xbf820001u, 0xd2820000u, 0u, endpgm};
    CHECK(!decode(into_word2).diagnostic);
    const std::array beyond_end{0xbf820001u, endpgm, 0xbf800000u};
    CHECK(!decode(beyond_end).diagnostic);
    const std::array before_start{0xbf82fffeu, endpgm};
    CHECK(!decode(before_start).diagnostic);
}

void invalid() {
    CHECK(!decode({}).diagnostic);
    const std::array missing_end{0xbf800000u};
    const auto missing = decode(missing_end);
    CHECK(!missing.diagnostic && missing.diagnostic.word_offset == 1);
    for (const auto word : {0xbe8003ffu, 0x8000ff80u, 0xbf0080ffu, 0x7e0002ffu,
                            0x7c0000ffu, 0x060000ffu, 0x40000080u, 0x42000080u, 0xba800000u,
                            0xd0000000u, 0xd8000000u, 0xe0000000u, 0xe8000000u,
                            0xf0000000u, 0xf8000000u}) {
        const std::array truncated{word};
        const auto result = decode(truncated);
        CHECK(!result.diagnostic && result.diagnostic.word_offset == 0 && result.diagnostic.raw_word == word);
        CHECK(result.instructions.empty());
        CHECK(result.diagnostic.message.find("Truncated") != std::string::npos);
    }
    for (const auto word : {0xcc000000u, 0xd4000000u, 0xdc000000u, 0xe4000000u,
                            0xec000000u, 0xf4000000u, 0xfc000000u, 0xffffffffu}) {
        const std::array unknown{word, endpgm};
        CHECK(!decode(unknown).diagnostic);
    }
    // Embedded binary information is data after S_ENDPGM, including unknown words.
    const std::array tail{0x7e000280u, endpgm, 0xffffffffu, 0xd8000000u};
    const auto valid_tail = decode(tail);
    CHECK(valid_tail.diagnostic && valid_tail.word_count == 2 && valid_tail.instructions.size() == 2);
}

void limits() {
    std::vector<std::uint32_t> many(max_shader_instructions, 0xbf800000u);
    many.back() = endpgm;
    CHECK(decode(many).diagnostic);
    many.back() = 0xbf800000u;
    many.push_back(endpgm);
    const auto too_many = decode(many);
    CHECK(!too_many.diagnostic && too_many.diagnostic.word_offset == max_shader_instructions);
    std::vector<std::uint32_t> huge(max_shader_words + 1, endpgm);
    CHECK(!decode(huge).diagnostic);
    // Every proper prefix of a valid stream must remain bounded and fail cleanly.
    const std::array stream{0xbe8003ffu, 0x3f800000u, 0xd2820000u, 0x040e0500u, endpgm};
    for (std::size_t size = 0; size < stream.size(); ++size) CHECK(!decode(std::span(stream).first(size)).diagnostic);
}
} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    if (suite == "scalar") scalar();
    else if (suite == "literals") literals();
    else if (suite == "formats") formats();
    else if (suite == "branches") branches();
    else if (suite == "invalid") invalid();
    else if (suite == "limits") limits();
    else return 2;
    std::puts("PASS Sea Islands decoder");
}
