#include "hle/gnm/gcn_translator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <utility>

namespace gravity::hle::gnm::gcn {
namespace {

// Sea Islands (GCN 1.x) special register selectors in SRC0/SRC1 fields.
constexpr std::uint32_t sel_vcc_lo       = 106;
constexpr std::uint32_t sel_vcc_hi       = 107;
constexpr std::uint32_t sel_exec_lo      = 126;
constexpr std::uint32_t sel_exec_hi      = 127;
constexpr std::uint32_t sel_zero         = 128;
constexpr std::uint32_t sel_float_start  = 240;
constexpr std::uint32_t sel_literal      = 255;
constexpr std::uint32_t sel_vgpr_start   = 256;

// Map a GCN scalar source selector (0..255) to a SPIR-V Id.
// Returns 0 if the selector cannot be mapped.
struct RegFile {
    spirv::Builder& b;
    spirv::Id f32{};
    spirv::Id u32{};
    spirv::Id i32{};
    spirv::Id v2f{};
    spirv::Id v3f{};
    spirv::Id v4f{};
    spirv::Id glsl{};

    // Scalar register file: s0..s103 stored as u32 Function variables.
    std::array<spirv::Id, 104> sgpr_vars{};
    // Vector register file: v0..v255 stored as f32 Function variables.
    std::array<spirv::Id, 256> vgpr_vars{};
    // SCC (scalar condition code), VCC (vector carry), EXEC.
    spirv::Id scc_var{};
    spirv::Id vcc_lo_var{};
    spirv::Id vcc_hi_var{};

    spirv::Id ptr_f32_func{};
    spirv::Id ptr_u32_func{};

    void init_types() {
        f32 = b.type_float(32);
        u32 = b.type_int(32, false);
        i32 = b.type_int(32, true);
        v2f = b.type_vector(f32, 2);
        v3f = b.type_vector(f32, 3);
        v4f = b.type_vector(f32, 4);
        ptr_f32_func = b.type_pointer(spirv::StorageClass::Function, f32);
        ptr_u32_func = b.type_pointer(spirv::StorageClass::Function, u32);
    }

    void alloc_regs(std::uint32_t num_s, std::uint32_t num_v) {
        auto bool_t = b.type_bool();
        auto ptr_bool = b.type_pointer(spirv::StorageClass::Function, bool_t);
        scc_var = b.variable(ptr_bool, spirv::StorageClass::Function, b.const_bool(false));
        vcc_lo_var = b.variable(ptr_u32_func, spirv::StorageClass::Function, b.const_uint32(0));
        vcc_hi_var = b.variable(ptr_u32_func, spirv::StorageClass::Function, b.const_uint32(0));

        for (std::uint32_t i = 0; i < num_s && i < sgpr_vars.size(); ++i) {
            sgpr_vars[i] = b.variable(ptr_u32_func, spirv::StorageClass::Function, b.const_uint32(0));
        }
        for (std::uint32_t i = 0; i < num_v && i < vgpr_vars.size(); ++i) {
            vgpr_vars[i] = b.variable(ptr_f32_func, spirv::StorageClass::Function, b.const_float32(0.0f));
        }
    }

    // Load a scalar source (returns u32 id).
    spirv::Id load_scalar(std::uint32_t selector, std::uint32_t literal_value) {
        if (selector < 104 && sgpr_vars[selector]) return b.op_load(u32, sgpr_vars[selector]);
        if (selector == sel_vcc_lo) return b.op_load(u32, vcc_lo_var);
        if (selector == sel_vcc_hi) return b.op_load(u32, vcc_hi_var);
        if (selector == sel_exec_lo || selector == sel_exec_hi) return b.const_uint32(0xffffffffu);
        if (selector == sel_zero || selector == 128) return b.const_uint32(0);
        if (selector >= 129 && selector <= 192) return b.const_uint32(selector - 128); // 1..64
        if (selector >= 193 && selector <= 208) return b.const_uint32(static_cast<std::uint32_t>(-(static_cast<int>(selector) - 192))); // -1..-16
        if (selector == sel_literal) return b.const_uint32(literal_value);
        // Inline float constants: 0.5, -0.5, 1.0, -1.0, 2.0, -2.0, 4.0, -4.0
        if (selector >= sel_float_start && selector <= 247) {
            static constexpr float inline_floats[] = {0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f, 4.0f, -4.0f};
            float val = inline_floats[selector - sel_float_start];
            std::uint32_t bits;
            std::memcpy(&bits, &val, 4);
            return b.const_uint32(bits); // Return as uint32 bitpattern for scalar regs
        }
        return b.const_uint32(0); // fallback
    }

    // Load a vector source (returns f32 id). Selector 0..255 = VGPR, 256+ = scalar.
    spirv::Id load_vsrc(std::uint32_t selector, std::uint32_t literal_value) {
        if (selector >= sel_vgpr_start) {
            auto vgpr_idx = selector - sel_vgpr_start;
            if (vgpr_idx < vgpr_vars.size() && vgpr_vars[vgpr_idx])
                return b.op_load(f32, vgpr_vars[vgpr_idx]);
            return b.const_float32(0.0f);
        }
        // Scalar source → bitcast to float.
        auto val = load_scalar(selector, literal_value);
        return b.op_bitcast(f32, val);
    }

    // Store to SGPR.
    void store_sgpr(std::uint32_t idx, spirv::Id value) {
        if (idx < sgpr_vars.size() && sgpr_vars[idx])
            b.op_store(sgpr_vars[idx], value);
    }

    // Store to VGPR (value must be f32).
    void store_vgpr(std::uint32_t idx, spirv::Id value) {
        if (idx < vgpr_vars.size() && vgpr_vars[idx])
            b.op_store(vgpr_vars[idx], value);
    }

    // Load VGPR as float.
    spirv::Id load_vgpr(std::uint32_t idx) {
        if (idx < vgpr_vars.size() && vgpr_vars[idx])
            return b.op_load(f32, vgpr_vars[idx]);
        return b.const_float32(0.0f);
    }
};

// Extract EXP target info according to AMD Sea Islands ISA (bits 3:0=EN, bits 9:4=TARGET).
ExportInfo decode_exp(const Instruction& inst) {
    ExportInfo info{};
    info.en = inst.raw[0] & 0xfu;
    info.target = static_cast<std::uint8_t>((inst.raw[0] >> 4) & 0x3fu);
    info.compr = static_cast<std::uint8_t>((inst.raw[0] >> 10) & 1u);
    // Count enabled components.
    info.num_components = 0;
    for (int i = 0; i < 4; ++i) if (info.en & (1 << i)) ++info.num_components;
    return info;
}

} // namespace

TranslateResult translate(const Program& program, const TranslateConfig& config) {
    TranslateResult result;
    if (program.instructions.empty() || !program.diagnostic) {
        result.error = "Invalid decoded GCN program";
        return result;
    }

    spirv::Builder b;
    b.add_capability(spirv::Capability::Shader);
    auto glsl_ext = b.import_extended("GLSL.std.450");
    b.set_memory_model(spirv::AddressingModel::Logical, spirv::MemoryModel::GLSL450);

    RegFile regs{b};
    regs.init_types();
    regs.glsl = glsl_ext;

    // Determine interface variables based on stage.
    std::vector<spirv::Id> interface_vars;

    // Vertex inputs and fragment outputs.
    spirv::Id position_var{};   // VS output: gl_Position
    spirv::Id frag_color_var{}; // PS output: outColor
    std::vector<spirv::Id> vertex_input_vars; // VS: vertex attributes
    std::vector<spirv::Id> param_output_vars; // VS: param exports → PS inputs
    std::vector<spirv::Id> param_input_vars;  // PS: interpolated inputs from VS
    spirv::Id tex_sampler_var{};              // PS: sampler2D

    spirv::Id vertex_id_var{}; // VS built-in

    auto ptr_v4f_out = b.type_pointer(spirv::StorageClass::Output, regs.v4f);
    auto ptr_v4f_in  = b.type_pointer(spirv::StorageClass::Input, regs.v4f);
    auto ptr_v2f_in  = b.type_pointer(spirv::StorageClass::Input, regs.v2f);
    auto ptr_v3f_in  = b.type_pointer(spirv::StorageClass::Input, regs.v3f);
    auto ptr_f32_in  = b.type_pointer(spirv::StorageClass::Input, regs.f32);
    auto ptr_u32_in  = b.type_pointer(spirv::StorageClass::Input, regs.u32);

    if (config.stage == Stage::Vertex) {
        // gl_Position output
        position_var = b.variable(ptr_v4f_out, spirv::StorageClass::Output);
        b.decorate(position_var, spirv::Decoration::BuiltIn, static_cast<std::uint32_t>(spirv::BuiltIn::Position));
        interface_vars.push_back(position_var);

        // Vertex index built-in
        vertex_id_var = b.variable(ptr_u32_in, spirv::StorageClass::Input);
        b.decorate(vertex_id_var, spirv::Decoration::BuiltIn, static_cast<std::uint32_t>(spirv::BuiltIn::VertexIndex));
        interface_vars.push_back(vertex_id_var);

        // Vertex attribute inputs.
        auto num_attribs = std::max(config.num_vertex_attribs, 1u);
        for (std::uint32_t i = 0; i < num_attribs; ++i) {
            auto components = (i < config.vertex_components.size()) ? config.vertex_components[i] : 4u;
            spirv::Id attrib_type;
            spirv::Id ptr_type;
            if (components == 1) { attrib_type = regs.f32; ptr_type = ptr_f32_in; }
            else if (components == 2) { attrib_type = regs.v2f; ptr_type = ptr_v2f_in; }
            else if (components == 3) { attrib_type = regs.v3f; ptr_type = ptr_v3f_in; }
            else { attrib_type = regs.v4f; ptr_type = ptr_v4f_in; }
            (void)attrib_type;
            auto var = b.variable(ptr_type, spirv::StorageClass::Input);
            b.decorate(var, spirv::Decoration::Location, i);
            vertex_input_vars.push_back(var);
            interface_vars.push_back(var);
        }

        // Parameter exports (generic VS→PS outputs). Pre-allocate up to 16.
        for (std::uint32_t i = 0; i < 16u; ++i) {
            auto var = b.variable(ptr_v4f_out, spirv::StorageClass::Output);
            b.decorate(var, spirv::Decoration::Location, i);
            param_output_vars.push_back(var);
            interface_vars.push_back(var);
        }
    } else {
        // Fragment shader.
        // Fragment color output.
        frag_color_var = b.variable(ptr_v4f_out, spirv::StorageClass::Output);
        b.decorate(frag_color_var, spirv::Decoration::Location, 0);
        interface_vars.push_back(frag_color_var);

        // Interpolated inputs from VS parameters.
        for (std::uint32_t i = 0; i < 16u; ++i) {
            auto var = b.variable(ptr_v4f_in, spirv::StorageClass::Input);
            b.decorate(var, spirv::Decoration::Location, i);
            param_input_vars.push_back(var);
            interface_vars.push_back(var);
        }

        // Texture samplers.
        if (config.num_textures > 0) {
            auto img_type = b.type_image(regs.f32, spirv::Dim::_2D, false, false, false, 1, spirv::ImageFormat::Unknown);
            auto sampled_type = b.type_sampled_image(img_type);
            auto ptr_sampled = b.type_pointer(spirv::StorageClass::UniformConstant, sampled_type);
            tex_sampler_var = b.variable(ptr_sampled, spirv::StorageClass::UniformConstant);
            b.decorate(tex_sampler_var, spirv::Decoration::DescriptorSet, 0);
            b.decorate(tex_sampler_var, spirv::Decoration::Binding, 0);
            interface_vars.push_back(tex_sampler_var);
        }
    }

    // Entry point.
    auto void_type = b.type_void();
    auto main_type = b.type_function(void_type);
    auto exec_model = config.stage == Stage::Vertex ? spirv::ExecutionModel::Vertex
                                                    : spirv::ExecutionModel::Fragment;

    auto main_func = b.begin_function(void_type, main_type);
    b.add_entry_point(exec_model, main_func, "main", interface_vars);
    if (config.stage == Stage::Fragment) {
        b.add_execution_mode(main_func, spirv::ExecutionMode::OriginUpperLeft);
    }

    b.begin_block();

    // Allocate register file as function-scope variables.
    regs.alloc_regs(config.num_sgprs, config.num_vgprs);

    // Load vertex attributes into VGPRs (hardware convention: v0=vertexId, v1..vN from fetch).
    if (config.stage == Stage::Vertex) {
        // v0 = vertex index (as float)
        auto vid = b.op_load(regs.u32, vertex_id_var);
        auto vid_f = b.op_convert_u_to_f(regs.f32, vid);
        regs.store_vgpr(0, vid_f);

        // Load vertex attributes into consecutive VGPRs.
        std::uint32_t vgpr = 1;
        for (std::uint32_t attr = 0; attr < vertex_input_vars.size(); ++attr) {
            auto components = (attr < config.vertex_components.size()) ? config.vertex_components[attr] : 4u;
            if (components == 1) {
                auto val = b.op_load(regs.f32, vertex_input_vars[attr]);
                regs.store_vgpr(vgpr++, val);
            } else if (components == 2) {
                auto val = b.op_load(regs.v2f, vertex_input_vars[attr]);
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 0));
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 1));
            } else if (components == 3) {
                auto val = b.op_load(regs.v3f, vertex_input_vars[attr]);
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 0));
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 1));
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 2));
            } else {
                auto val = b.op_load(regs.v4f, vertex_input_vars[attr]);
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 0));
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 1));
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 2));
                regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 3));
            }
        }
    } else {
        // Fragment: load interpolated parameters into VGPRs.
        // v0,v1 = FragCoord.xy (not loaded here, would require BuiltIn FragCoord).
        // v2+ = interpolated attributes.
        std::uint32_t vgpr = 2;
        for (std::uint32_t i = 0; i < param_input_vars.size() && vgpr < 252; ++i) {
            auto val = b.op_load(regs.v4f, param_input_vars[i]);
            regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 0));
            regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 1));
            regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 2));
            regs.store_vgpr(vgpr++, b.op_composite_extract(regs.f32, val, 3));
        }
    }

    // --- Translate instructions ---
    for (const auto& inst : program.instructions) {
        switch (inst.encoding) {

        // ====================== SCALAR ALU ======================
        case Encoding::sop1: {
            auto src0 = inst.raw[0] & 0xffu;
            auto dst  = (inst.raw[0] >> 16) & 0x7fu;
            auto val  = regs.load_scalar(src0, inst.literal);

            switch (inst.opcode) {
            case 3: // S_MOV_B32
                regs.store_sgpr(dst, val);
                break;
            case 33: // S_MOV_B64 → store as pair
                regs.store_sgpr(dst, val);
                if (dst + 1 < 104) regs.store_sgpr(dst + 1, b.const_uint32(0));
                break;
            default: // Unsupported SOP1 → pass through as identity
                regs.store_sgpr(dst, val);
                break;
            }
            break;
        }

        case Encoding::sop2: {
            auto src0 = inst.raw[0] & 0xffu;
            auto src1 = (inst.raw[0] >> 8) & 0xffu;
            auto dst  = (inst.raw[0] >> 16) & 0x7fu;
            auto a = regs.load_scalar(src0, inst.literal);
            auto a_s = b.op_bitcast(regs.i32, a);
            auto b_val = regs.load_scalar(src1, inst.literal);
            auto b_s = b.op_bitcast(regs.i32, b_val);

            switch (inst.opcode) {
            case 0: // S_ADD_U32
                regs.store_sgpr(dst, b.op_iadd(regs.u32, a, b_val));
                break;
            case 1: // S_SUB_U32
                regs.store_sgpr(dst, b.op_isub(regs.u32, a, b_val));
                break;
            case 2: // S_ADD_I32
                regs.store_sgpr(dst, b.op_bitcast(regs.u32, b.op_iadd(regs.i32, a_s, b_s)));
                break;
            case 3: // S_SUB_I32
                regs.store_sgpr(dst, b.op_bitcast(regs.u32, b.op_isub(regs.i32, a_s, b_s)));
                break;
            case 8:  // S_AND_B32
                regs.store_sgpr(dst, b.op_bitwise_and(regs.u32, a, b_val));
                break;
            case 10: // S_OR_B32
                regs.store_sgpr(dst, b.op_bitwise_or(regs.u32, a, b_val));
                break;
            case 12: // S_XOR_B32
                regs.store_sgpr(dst, b.op_bitwise_xor(regs.u32, a, b_val));
                break;
            case 14: // S_ANDN2_B32
                regs.store_sgpr(dst, b.op_bitwise_and(regs.u32, a, b.op_not(regs.u32, b_val)));
                break;
            case 22: // S_LSHL_B32
                regs.store_sgpr(dst, b.op_shift_left(regs.u32, a, b_val));
                break;
            case 24: // S_LSHR_B32
                regs.store_sgpr(dst, b.op_shift_right_logical(regs.u32, a, b_val));
                break;
            case 26: // S_ASHR_I32
                regs.store_sgpr(dst, b.op_bitcast(regs.u32, b.op_shift_right_arithmetic(regs.i32, a_s, b_s)));
                break;
            case 36: // S_MUL_I32
                regs.store_sgpr(dst, b.op_bitcast(regs.u32, b.op_imul(regs.i32, a_s, b_s)));
                break;
            default:
                // Unsupported SOP2: store src0 as fallback.
                regs.store_sgpr(dst, a);
                break;
            }
            break;
        }

        case Encoding::sopk: {
            auto dst = (inst.raw[0] >> 16) & 0x7fu;
            auto simm16 = static_cast<std::int16_t>(inst.raw[0] & 0xffffu);
            std::uint32_t bits;
            std::int32_t val32 = simm16;
            std::memcpy(&bits, &val32, 4);

            switch (inst.opcode) {
            case 0: // S_MOVK_I32
                regs.store_sgpr(dst, b.const_uint32(bits));
                break;
            case 15: // S_ADDK_I32
            {
                auto old = regs.load_scalar(dst, 0);
                auto old_s = b.op_bitcast(regs.i32, old);
                regs.store_sgpr(dst, b.op_bitcast(regs.u32, b.op_iadd(regs.i32, old_s, b.const_int32(simm16))));
                break;
            }
            default: break;
            }
            break;
        }

        case Encoding::sopc: {
            // Scalar comparison: sets SCC.
            auto src0 = inst.raw[0] & 0xffu;
            auto src1 = (inst.raw[0] >> 8) & 0xffu;
            auto a = regs.load_scalar(src0, inst.literal);
            auto b_val = regs.load_scalar(src1, inst.literal);
            auto bool_t = b.type_bool();
            spirv::Id cmp_result{};

            switch (inst.opcode) {
            case 0: cmp_result = b.op_iequal(bool_t, a, b_val); break;  // S_CMP_EQ_I32
            case 1: // S_CMP_LG_I32
                cmp_result = b.op_inotequal(bool_t, a, b_val); break;
            case 2: // S_CMP_GT_I32
                cmp_result = b.op_sless_than(bool_t, b_val, a); break;
            case 3: // S_CMP_GE_I32
            {   auto lt = b.op_sless_than(bool_t, a, b_val);
                auto logical_not_id = b.next_id();
                // Note: using LogicalNot
                std::vector<std::uint32_t> ops = {bool_t, logical_not_id, lt};
                // We can use op_select or add op_logical_not
                cmp_result = b.op_select(bool_t, lt, b.const_bool(false), b.const_bool(true));
                break;
            }
            case 4: // S_CMP_LT_I32
                cmp_result = b.op_sless_than(bool_t, a, b_val); break;
            case 6: // S_CMP_EQ_U32
                cmp_result = b.op_iequal(bool_t, a, b_val); break;
            case 7: // S_CMP_LG_U32
                cmp_result = b.op_inotequal(bool_t, a, b_val); break;
            default:
                cmp_result = b.const_bool(false); break;
            }

            auto ptr_bool = b.type_pointer(spirv::StorageClass::Function, bool_t);
            (void)ptr_bool;
            b.op_store(regs.scc_var, cmp_result);
            break;
        }

        case Encoding::sopp: {
            // SOPP opcodes: most are control flow we handle as linear for now.
            switch (inst.opcode) {
            case 1:  break; // S_ENDPGM - handled below
            case 12: break; // S_WAITCNT - no-op for translation
            default: break; // S_BRANCH and conditional branches: linearize for now
            }
            break;
        }

        // ====================== VECTOR ALU ======================
        case Encoding::vop1: {
            auto src0 = inst.raw[0] & 0x1ffu;
            auto dst  = (inst.raw[0] >> 17) & 0xffu;
            auto val  = regs.load_vsrc(src0, inst.literal);

            switch (inst.opcode) {
            case 1:  // V_MOV_B32
                regs.store_vgpr(dst, val);
                break;
            case 5:  // V_CVT_F32_I32
            {
                auto u = b.op_bitcast(regs.u32, val);
                auto s = b.op_bitcast(regs.i32, u);
                regs.store_vgpr(dst, b.op_convert_s_to_f(regs.f32, s));
                break;
            }
            case 6:  // V_CVT_F32_U32
            {
                auto u = b.op_bitcast(regs.u32, val);
                regs.store_vgpr(dst, b.op_convert_u_to_f(regs.f32, u));
                break;
            }
            case 7:  // V_CVT_U32_F32
            {
                auto u = b.op_convert_f_to_u(regs.u32, val);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, u));
                break;
            }
            case 8:  // V_CVT_I32_F32
            {
                auto s = b.op_convert_f_to_s(regs.i32, val);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, s));
                break;
            }
            case 33: // V_RCP_F32 → 1.0/x
            {
                auto one = b.const_float32(1.0f);
                regs.store_vgpr(dst, b.op_fdiv(regs.f32, one, val));
                break;
            }
            case 34: // V_RCP_IFLAG_F32
            {
                auto one = b.const_float32(1.0f);
                regs.store_vgpr(dst, b.op_fdiv(regs.f32, one, val));
                break;
            }
            case 35: // V_RSQ_F32 → 1/sqrt(x)
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::InverseSqrt, {val}));
                break;
            case 37: // V_SQRT_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Sqrt, {val}));
                break;
            case 41: // V_EXP_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Exp2, {val}));
                break;
            case 42: // V_LOG_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Log2, {val}));
                break;
            case 51: // V_FRACT_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Fract, {val}));
                break;
            case 52: // V_TRUNC_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Trunc, {val}));
                break;
            case 53: // V_CEIL_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Ceil, {val}));
                break;
            case 54: // V_RNDNE_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Round, {val}));
                break;
            case 55: // V_FLOOR_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Floor, {val}));
                break;
            default:
                regs.store_vgpr(dst, val);
                break;
            }
            break;
        }

        case Encoding::vop2: {
            auto src0 = inst.raw[0] & 0x1ffu;
            auto src1_vgpr = (inst.raw[0] >> 9) & 0xffu;
            auto dst = (inst.raw[0] >> 17) & 0xffu;
            auto a = regs.load_vsrc(src0, inst.literal);
            auto b_val = regs.load_vgpr(src1_vgpr);

            switch (inst.opcode) {
            case 1:  // V_MOV_B32 (VOP2 encoding variant)
                regs.store_vgpr(dst, a); break;
            case 3:  // V_ADD_F32
                regs.store_vgpr(dst, b.op_fadd(regs.f32, a, b_val)); break;
            case 4:  // V_SUB_F32
                regs.store_vgpr(dst, b.op_fsub(regs.f32, a, b_val)); break;
            case 5:  // V_SUBREV_F32
                regs.store_vgpr(dst, b.op_fsub(regs.f32, b_val, a)); break;
            case 8:  // V_MUL_F32
                regs.store_vgpr(dst, b.op_fmul(regs.f32, a, b_val)); break;
            case 9:  // V_MUL_I32_I24 (low 24 bits multiply)
            {
                auto au = b.op_bitcast(regs.u32, a);
                auto bu = b.op_bitcast(regs.u32, b_val);
                auto res = b.op_imul(regs.u32, au, bu);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, res));
                break;
            }
            case 16: // V_MAX_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMax, {a, b_val}));
                break;
            case 17: // V_MIN_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMin, {a, b_val}));
                break;
            case 25: // V_AND_B32
            {
                auto au = b.op_bitcast(regs.u32, a);
                auto bu = b.op_bitcast(regs.u32, b_val);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, b.op_bitwise_and(regs.u32, au, bu)));
                break;
            }
            case 26: // V_OR_B32
            {
                auto au = b.op_bitcast(regs.u32, a);
                auto bu = b.op_bitcast(regs.u32, b_val);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, b.op_bitwise_or(regs.u32, au, bu)));
                break;
            }
            case 27: // V_XOR_B32
            {
                auto au = b.op_bitcast(regs.u32, a);
                auto bu = b.op_bitcast(regs.u32, b_val);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, b.op_bitwise_xor(regs.u32, au, bu)));
                break;
            }
            case 32: // V_MADMK_F32 → a * K + b (K is literal)
            {
                float k;
                std::memcpy(&k, &inst.literal, 4);
                auto k_id = b.const_float32(k);
                auto mul = b.op_fmul(regs.f32, a, k_id);
                regs.store_vgpr(dst, b.op_fadd(regs.f32, mul, b_val));
                break;
            }
            case 33: // V_MADAK_F32 → a * b + K
            {
                float k;
                std::memcpy(&k, &inst.literal, 4);
                auto k_id = b.const_float32(k);
                auto mul = b.op_fmul(regs.f32, a, b_val);
                regs.store_vgpr(dst, b.op_fadd(regs.f32, mul, k_id));
                break;
            }
            case 46: // V_MAC_F32 → dst = a * b + dst
            {
                auto prev = regs.load_vgpr(dst);
                auto mul = b.op_fmul(regs.f32, a, b_val);
                regs.store_vgpr(dst, b.op_fadd(regs.f32, mul, prev));
                break;
            }
            default:
                regs.store_vgpr(dst, a);
                break;
            }
            break;
        }

        case Encoding::vop3: {
            // VOP3 has 64-bit encoding: raw[0] and raw[1].
            auto src0 = inst.raw[1] & 0x1ffu;
            auto src1 = (inst.raw[1] >> 9) & 0x1ffu;
            auto src2 = (inst.raw[1] >> 18) & 0x1ffu;
            auto dst  = (inst.raw[0] >> 17) & 0xffu;
            auto neg  = (inst.raw[1] >> 29) & 0x7u;

            auto load_and_neg = [&](std::uint32_t sel, std::uint32_t neg_bit) {
                auto val = regs.load_vsrc(sel, inst.literal);
                if (neg_bit) val = b.op_fnegate(regs.f32, val);
                return val;
            };

            auto a = load_and_neg(src0, neg & 1);
            auto b_val = load_and_neg(src1, neg & 2);
            auto c = load_and_neg(src2, neg & 4);

            switch (inst.opcode) {
            case 256 + 3: // V_ADD_F32 (VOP3)
                regs.store_vgpr(dst, b.op_fadd(regs.f32, a, b_val)); break;
            case 256 + 4: // V_SUB_F32
                regs.store_vgpr(dst, b.op_fsub(regs.f32, a, b_val)); break;
            case 256 + 8: // V_MUL_F32
                regs.store_vgpr(dst, b.op_fmul(regs.f32, a, b_val)); break;
            case 256 + 16: // V_MAX_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMax, {a, b_val}));
                break;
            case 256 + 17: // V_MIN_F32
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMin, {a, b_val}));
                break;
            case 321: // V_MAD_F32 → a * b + c
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Fma, {a, b_val, c}));
                break;
            case 322: // V_MAD_I32_I24
            {
                auto au = b.op_bitcast(regs.u32, a);
                auto bu = b.op_bitcast(regs.u32, b_val);
                auto cu = b.op_bitcast(regs.u32, c);
                auto mul = b.op_imul(regs.u32, au, bu);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, b.op_iadd(regs.u32, mul, cu)));
                break;
            }
            case 323: // V_MAD_U32_U24
            {
                auto au = b.op_bitcast(regs.u32, a);
                auto bu = b.op_bitcast(regs.u32, b_val);
                auto cu = b.op_bitcast(regs.u32, c);
                auto mul = b.op_imul(regs.u32, au, bu);
                regs.store_vgpr(dst, b.op_bitcast(regs.f32, b.op_iadd(regs.u32, mul, cu)));
                break;
            }
            case 449: // V_MED3_F32 → clamp(a, min(b,c), max(b,c))
            {
                auto mn = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMin, {b_val, c});
                auto mx = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMax, {b_val, c});
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FClamp, {a, mn, mx}));
                break;
            }
            default:
                // Fallback: treat as MAD-like (a * b + c) for unknown VOP3.
                regs.store_vgpr(dst, b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Fma, {a, b_val, c}));
                break;
            }
            break;
        }

        case Encoding::vopc: {
            auto src0 = inst.raw[0] & 0x1ffu;
            auto src1_vgpr = (inst.raw[0] >> 9) & 0xffu;
            auto a = regs.load_vsrc(src0, inst.literal);
            auto b_val = regs.load_vgpr(src1_vgpr);
            spirv::Id cmp{};
            switch (inst.opcode) {
            case 0:  cmp = b.const_bool(false); break; // V_CMP_F_F32
            case 1:  cmp = b.op_ford_less_than(a, b_val); break;
            case 2:  cmp = b.op_ford_equal(a, b_val); break;
            case 3:  cmp = b.op_ford_less_than_equal(a, b_val); break;
            case 4:  cmp = b.op_ford_greater_than(a, b_val); break;
            case 6:  cmp = b.op_ford_greater_than_equal(a, b_val); break;
            default: cmp = b.const_bool(false); break;
            }

            // Write result to VCC_LO (as uint32 0 or 1).
            auto result_u32 = b.op_select(regs.u32, cmp, b.const_uint32(1), b.const_uint32(0));
            b.op_store(regs.vcc_lo_var, result_u32);
            break;
        }

        // ====================== MEMORY ======================
        case Encoding::smrd: {
            // S_LOAD_DWORD etc. → load from constant buffer.
            // For now, store 0 as placeholder since we don't have UBO bindings yet.
            auto dst = (inst.raw[0] >> 15) & 0x7fu;
            regs.store_sgpr(dst, b.const_uint32(0));
            break;
        }

        case Encoding::mubuf: {
            // BUFFER_LOAD_FORMAT_* → load from vertex/storage buffer.
            // We handle vertex fetch via the Input variables above, so these
            // become identity operations on VGPRs that were pre-loaded.
            break;
        }

        case Encoding::mtbuf: {
            // TBUFFER_LOAD_FORMAT → typed buffer load.
            // Same as mubuf for now.
            break;
        }

        case Encoding::mimg: {
            // IMAGE_SAMPLE → texture sampling.
            if (inst.opcode == 0 && tex_sampler_var && config.stage == Stage::Fragment) {
                auto vdata = (inst.raw[1] >> 8) & 0xffu;
                auto vaddr = inst.raw[1] & 0xffu;

                auto img_type = b.type_image(regs.f32, spirv::Dim::_2D, false, false, false, 1, spirv::ImageFormat::Unknown);
                auto sampled_type = b.type_sampled_image(img_type);

                auto tex = b.op_load(sampled_type, tex_sampler_var);
                auto u = regs.load_vgpr(vaddr);
                auto v = regs.load_vgpr(vaddr + 1);
                auto coord = b.op_composite_construct(regs.v2f, {u, v});
                auto sampled = b.op_image_sample_implicit(regs.v4f, tex, coord);

                regs.store_vgpr(vdata,     b.op_composite_extract(regs.f32, sampled, 0));
                regs.store_vgpr(vdata + 1, b.op_composite_extract(regs.f32, sampled, 1));
                regs.store_vgpr(vdata + 2, b.op_composite_extract(regs.f32, sampled, 2));
                regs.store_vgpr(vdata + 3, b.op_composite_extract(regs.f32, sampled, 3));
            }
            break;
        }

        // ====================== EXPORT ======================
        case Encoding::exp: {
            auto info = decode_exp(inst);
            result.exports.push_back(info);

            // Source VGPRs from the second instruction word.
            auto vsrc0 = inst.raw[1] & 0xffu;
            auto vsrc1 = (inst.raw[1] >> 8) & 0xffu;
            auto vsrc2 = (inst.raw[1] >> 16) & 0xffu;
            auto vsrc3 = (inst.raw[1] >> 24) & 0xffu;

            auto v0 = regs.load_vgpr(vsrc0);
            auto v1 = regs.load_vgpr(vsrc1);
            auto v2 = regs.load_vgpr(vsrc2);
            auto v3 = regs.load_vgpr(vsrc3);
            auto vec4 = b.op_composite_construct(regs.v4f, {v0, v1, v2, v3});

            if (info.target >= 12 && info.target <= 15 && config.stage == Stage::Vertex) {
                // Position export (pos0..pos3: 12..15 in GCN ISA).
                b.op_store(position_var, vec4);
            } else if (info.target >= 32 && info.target <= 63 && config.stage == Stage::Vertex) {
                // Parameter export (param0..param31: 32..63 in GCN ISA).
                auto param_idx = static_cast<std::size_t>(info.target - 32);
                if (param_idx < param_output_vars.size())
                    b.op_store(param_output_vars[param_idx], vec4);
            } else if (info.target <= 7 && config.stage == Stage::Fragment) {
                // MRT export (mrt0..mrt7: 0..7 in GCN ISA).
                b.op_store(frag_color_var, vec4);
            }
            break;
        }

        // ====================== INTERPOLATION ======================
        case Encoding::vintrp: {
            // V_INTERP_P1_F32 / V_INTERP_P2_F32 / V_INTERP_MOV_F32
            // The PS inputs are already loaded from interpolated varyings.
            // These instructions adjust interpolation at pixel centers.
            // For now, the VGPRs are pre-loaded; these become identity ops.
            auto dst = (inst.raw[0] >> 18) & 0xffu;
            auto attr = (inst.raw[0] >> 10) & 0x3fu;
            auto chan = (inst.raw[0] >> 8) & 0x3u;

            // The input param for this attribute should already be in vgpr 2 + attr*4 + chan.
            auto src_vgpr = 2 + attr * 4 + chan;
            auto val = regs.load_vgpr(src_vgpr);
            regs.store_vgpr(dst, val);
            break;
        }

        case Encoding::ds: break; // LDS: not needed for basic rendering
        default: break;
        }
    }

    // Return.
    b.op_return();
    b.end_function();

    result.spirv = b.finalize();
    result.success = !result.spirv.empty() && result.spirv.size() >= 5;
    return result;
}

} // namespace gravity::hle::gnm::gcn
