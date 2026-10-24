#include "hle/gnm/gcn_decoder.h"
#include "hle/gnm/gcn_translator.h"
#include "hle/gnm/spirv_builder.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

using namespace gravity::hle::gnm;
using namespace gravity::hle::gnm::gcn;
using namespace gravity::hle::gnm::spirv;

namespace {

constexpr std::uint32_t endpgm = 0xbf810000u;

// ========================= SPIR-V Builder Tests =========================

void builder_empty_module() {
    Builder b;
    b.add_capability(Capability::Shader);
    b.set_memory_model(AddressingModel::Logical, MemoryModel::GLSL450);

    auto void_t = b.type_void();
    auto func_t = b.type_function(void_t);
    auto main_fn = b.begin_function(void_t, func_t);
    b.add_entry_point(ExecutionModel::Vertex, main_fn, "main", {});
    b.begin_block();
    b.op_return();
    b.end_function();

    auto spirv = b.finalize();
    CHECK(spirv.size() >= 5);
    CHECK(spirv[0] == 0x07230203U); // Magic
    CHECK(spirv[1] == 0x00010500U); // Version 1.5
    CHECK(spirv[3] > 0);            // Bound
    CHECK(spirv[4] == 0);           // Schema
}

void builder_types() {
    Builder b;
    auto f32 = b.type_float(32);
    auto f32_2 = b.type_float(32);
    CHECK(f32 == f32_2); // Deduplication

    auto u32 = b.type_int(32, false);
    auto i32 = b.type_int(32, true);
    CHECK(u32 != i32);

    auto v4f = b.type_vector(f32, 4);
    auto v4f_2 = b.type_vector(f32, 4);
    CHECK(v4f == v4f_2);

    auto v3f = b.type_vector(f32, 3);
    CHECK(v3f != v4f);

    auto bool_t = b.type_bool();
    auto bool_t2 = b.type_bool();
    CHECK(bool_t == bool_t2);
}

void builder_constants() {
    Builder b;
    auto c1 = b.const_float32(1.0f);
    auto c1_2 = b.const_float32(1.0f);
    CHECK(c1 == c1_2);

    auto c2 = b.const_float32(2.0f);
    CHECK(c1 != c2);

    auto i0 = b.const_int32(0);
    auto i0_2 = b.const_int32(0);
    CHECK(i0 == i0_2);

    auto u42 = b.const_uint32(42);
    auto u42_2 = b.const_uint32(42);
    CHECK(u42 == u42_2);
}

void builder_instructions() {
    Builder b;
    b.add_capability(Capability::Shader);
    auto glsl = b.import_extended("GLSL.std.450");
    b.set_memory_model(AddressingModel::Logical, MemoryModel::GLSL450);

    auto f32 = b.type_float(32);
    auto v4f = b.type_vector(f32, 4);
    auto void_t = b.type_void();
    auto func_t = b.type_function(void_t);
    auto ptr_f32 = b.type_pointer(StorageClass::Function, f32);

    auto main_fn = b.begin_function(void_t, func_t);
    b.add_entry_point(ExecutionModel::Fragment, main_fn, "main", {});
    b.add_execution_mode(main_fn, ExecutionMode::OriginUpperLeft);
    b.begin_block();

    auto var = b.variable(ptr_f32, StorageClass::Function, b.const_float32(0.0f));
    auto a = b.const_float32(1.0f);
    auto b_val = b.const_float32(2.0f);
    auto sum = b.op_fadd(f32, a, b_val);
    b.op_store(var, sum);

    auto loaded = b.op_load(f32, var);
    auto product = b.op_fmul(f32, loaded, b.const_float32(3.0f));
    auto negated = b.op_fnegate(f32, product);
    (void)negated;

    auto sqrt_val = b.ext_inst(f32, glsl, GLSLstd450::Sqrt, {a});
    (void)sqrt_val;

    auto vec = b.op_composite_construct(v4f, {a, b_val, sum, product});
    auto x = b.op_composite_extract(f32, vec, 0);
    (void)x;

    b.op_return();
    b.end_function();

    auto spirv = b.finalize();
    CHECK(spirv.size() >= 20);
    CHECK(spirv[0] == 0x07230203U);
}

// ========================= Translator Tests =========================

void translate_trivial_vs() {
    // Minimal vertex shader: mov v0 to position export.
    // v_mov_b32 v0, 1.0
    // v_mov_b32 v1, 0.0
    // v_mov_b32 v2, 0.0
    // v_mov_b32 v3, 1.0
    // exp pos0, v0, v1, v2, v3 done
    // s_endpgm
    const std::array words{
        0x7e0002f2u,  // v_mov_b32 v0, 1.0 (inline constant 242 = 1.0)
        0x7e020280u,  // v_mov_b32 v1, 0 (zero)
        0x7e040280u,  // v_mov_b32 v2, 0
        0x7e0602f2u,  // v_mov_b32 v3, 1.0
        0xf80008cfu,  // exp pos0, v0,v1,v2,v3 done (target 12 = pos0)
        0x03020100u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_sgprs = 16;
    config.num_vgprs = 16;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
    CHECK(result.spirv[0] == 0x07230203U);
    CHECK(!result.exports.empty());
    CHECK(result.exports[0].target == 12); // pos0 is target 12 in GCN ISA
}

void translate_trivial_ps() {
    // Minimal fragment shader: output solid color from VGPRs.
    // v_mov_b32 v0, 1.0  (red)
    // v_mov_b32 v1, 0.0  (green)
    // v_mov_b32 v2, 0.0  (blue)
    // v_mov_b32 v3, 1.0  (alpha)
    // exp mrt0, v0,v1,v2,v3 done
    // s_endpgm
    const std::array words{
        0x7e0002f2u,  // v_mov_b32 v0, 1.0
        0x7e020280u,  // v_mov_b32 v1, 0
        0x7e040280u,  // v_mov_b32 v2, 0
        0x7e0602f2u,  // v_mov_b32 v3, 1.0
        0xf800080fu,  // exp mrt0, v0,v1,v2,v3 done (target 0 = mrt0)
        0x03020100u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Fragment;
    config.num_sgprs = 8;
    config.num_vgprs = 8;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
    CHECK(!result.exports.empty());
    // mrt0 target = 0 (MRT exports start at 0 in the target field)
    CHECK(result.exports[0].target == 0);
}

void translate_alu_ops() {
    // Shader with various ALU operations.
    const std::array words{
        0x7e0002f2u,             // v_mov_b32 v0, 1.0
        0x7e0202f4u,             // v_mov_b32 v1, 2.0
        0x06040100u,             // v_add_f32 v2, v0, v1  (VOP2 opcode 3)
        0x10060300u,             // v_mul_f32 v3, v0, v1  (VOP2 opcode 8)
        0xd2820004u, 0x040a0100u,// v_mad_f32 v4, v0, v1, v2  (VOP3 opcode 321)
        0xf800180fu,             // exp pos0
        0x03020100u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_vgprs = 16;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
}

void translate_scalar_ops() {
    // Shader with scalar ALU.
    const std::array words{
        0xbeeb03ffu, 0x00000003u,  // s_mov_b32 vcc_hi, 3 (SOP1 literal)
        0xb000ffffu,               // s_movk_i32 s0, -1
        0x80018200u,               // s_add_u32 s1, s0, 2
        0xbf068001u,               // s_cmp_eq_u32 s1, 0 (SOPC)
        0xbf8c007fu,               // s_waitcnt
        0x7e0002f2u,               // v_mov_b32 v0, 1.0
        0x7e020280u,               // v_mov_b32 v1, 0
        0x7e040280u,               // v_mov_b32 v2, 0
        0x7e0602f2u,               // v_mov_b32 v3, 1.0
        0xf800180fu, 0x03020100u,  // exp pos0
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_sgprs = 32;
    config.num_vgprs = 16;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv[0] == 0x07230203U);
}

void translate_with_texture() {
    // Fragment shader that samples a texture.
    const std::array words{
        0x7e0002f2u,             // v_mov_b32 v0, 1.0 (u coord placeholder)
        0x7e0202f3u,             // v_mov_b32 v1, 0.5 (-0.5? no, 0x2f3 = -0.5) 
        0xf0800f00u, 0x00000200u,// image_sample v[2:5], v[0:1], s[0:7], s[8:11]  (opcode 0, dmask=0xf)
        0xf800080fu,             // exp mrt0, v2,v3,v4,v5 done
        0x05040302u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Fragment;
    config.num_sgprs = 16;
    config.num_vgprs = 16;
    config.num_textures = 1;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
}

void translate_spirv_valid() {
    // Verify SPIR-V structural validity of a generated module.
    const std::array words{
        0x7e0002f2u,             // v_mov_b32 v0, 1.0
        0x7e020280u,             // v_mov_b32 v1, 0
        0x7e040280u,             // v_mov_b32 v2, 0
        0x7e0602f2u,             // v_mov_b32 v3, 1.0
        0xf80008cfu, 0x03020100u,// exp pos0 done
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_vgprs = 8;

    auto result = translate(program, config);
    CHECK(result.success);

    // Validate SPIR-V header.
    CHECK(result.spirv[0] == 0x07230203U); // Magic
    CHECK((result.spirv[1] & 0xFFFF0000U) >= 0x00010000U); // Version >= 1.0
    CHECK(result.spirv[3] > 0); // Bound
    CHECK(result.spirv[4] == 0); // Schema

    // Walk through SPIR-V instructions and verify word counts.
    for (std::size_t i = 5; i < result.spirv.size();) {
        auto word = result.spirv[i];
        auto wc = word >> 16;
        CHECK(wc > 0);
        CHECK(wc <= result.spirv.size() - i);
        i += wc;
    }
    // Must consume all words.
    std::size_t pos = 5;
    while (pos < result.spirv.size()) pos += result.spirv[pos] >> 16;
    CHECK(pos == result.spirv.size());
}

void translate_invalid_program() {
    // Empty program should fail.
    Program empty;
    TranslateConfig config;
    auto result = translate(empty, config);
    CHECK(!result.success);
    CHECK(!result.error.empty());
}

} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];

    if (suite == "builder_empty")       builder_empty_module();
    else if (suite == "builder_types")  builder_types();
    else if (suite == "builder_const")  builder_constants();
    else if (suite == "builder_instr")  builder_instructions();
    else if (suite == "translate_vs")   translate_trivial_vs();
    else if (suite == "translate_ps")   translate_trivial_ps();
    else if (suite == "translate_alu")  translate_alu_ops();
    else if (suite == "translate_scalar") translate_scalar_ops();
    else if (suite == "translate_tex")  translate_with_texture();
    else if (suite == "spirv_valid")    translate_spirv_valid();
    else if (suite == "translate_invalid") translate_invalid_program();
    else return 2;

    std::puts("PASS GCN translator");
}
