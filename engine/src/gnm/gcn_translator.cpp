#include "hle/gnm/gcn_translator.h"
#include "hle/gnm/spirv_validation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <sstream>
#include <stdexcept>
#include <locale>

namespace gravity::hle::gnm::gcn {
namespace {

bool one_of(std::uint32_t value, std::initializer_list<std::uint32_t> values) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

// Refuse a program before emitting any SPIR-V if it needs semantics we do not
// implement. Register initialization in emitted code is storage initialization,
// not permission to replace missing guest inputs by zeros.
bool validate_subset(const Program& program, const TranslateConfig& config, TranslateResult& result) {
    auto fail = [&](const Instruction& instruction, const std::string& reason) {
        result.error_word_offset = instruction.word_offset;
        result.error_opcode = instruction.opcode;
        result.error = "GCN DWORD " + std::to_string(instruction.word_offset) + " opcode " +
            std::to_string(instruction.opcode) + ": " + reason;
        return false;
    };
    const auto& first = program.instructions.front();
    std::vector<std::uint32_t> raw;
    for (const auto& instruction : program.instructions) {
        if (!instruction.word_count || instruction.word_count > 3 || raw.size() > max_shader_words - instruction.word_count)
            return fail(instruction, "malformed decoded program");
        raw.insert(raw.end(), instruction.raw.begin(), instruction.raw.begin() + instruction.word_count);
    }
    const auto verified = decode(raw);
    if (!verified.diagnostic || verified.instructions.size() != program.instructions.size() || verified.word_count != program.word_count)
        return fail(first, "decoded program is not a complete canonical instruction stream");
    for (std::size_t i = 0; i < program.instructions.size(); ++i) {
        const auto& supplied = program.instructions[i]; const auto& actual = verified.instructions[i];
        if (supplied.encoding != actual.encoding || supplied.opcode != actual.opcode || supplied.word_offset != actual.word_offset ||
            supplied.word_count != actual.word_count || supplied.has_literal != actual.has_literal || supplied.literal != actual.literal)
            return fail(supplied, "decoded opcode or operand metadata does not match raw ISA");
    }
    if ((config.stage != Stage::Vertex && config.stage != Stage::Fragment) || !config.num_sgprs || config.num_sgprs > 104 ||
        !config.num_vgprs || config.num_vgprs > 256 || config.num_user_sgprs > config.num_sgprs ||
        config.user_sgpr_values.size() > config.num_user_sgprs || config.num_vertex_attribs > 16 ||
        config.num_textures > 1 || config.num_samplers > 1)
        return fail(first, "invalid or unsupported shader interface configuration");
    if (config.num_vertex_attribs != config.vertex_components.size() ||
        config.num_vertex_attribs != config.vertex_vgpr_bases.size())
        return fail(first, "vertex attribute VGPR destinations/components must be explicit");
    if (config.sampled_texture) {
        const auto& binding = *config.sampled_texture;
        if (binding.resource_sgpr_base % 4 || binding.sampler_sgpr_base % 4 ||
            binding.resource_sgpr_base > config.num_sgprs || 8 > config.num_sgprs - binding.resource_sgpr_base ||
            binding.sampler_sgpr_base > config.num_sgprs || 4 > config.num_sgprs - binding.sampler_sgpr_base)
            return fail(first, "sampled texture descriptor SGPR ranges exceed allocation or alignment");
    }
    std::array<bool, 128> scalar{};
    std::array<bool, 256> vector{};
    for (std::size_t i = 0; i < config.user_sgpr_values.size(); ++i) scalar[i] = true;
    if (config.stage == Stage::Vertex) {
        vector[0] = true;
        for (std::size_t i = 0; i < config.vertex_components.size(); ++i) {
            const auto base = config.vertex_vgpr_bases[i], count = config.vertex_components[i];
            if (!base || !count || count > 4 || base >= config.num_vgprs || count > config.num_vgprs - base)
                return fail(first, "vertex input is outside allocated VGPRs");
            for (std::uint32_t component = 0; component < count; ++component) {
                if (vector[base + component]) return fail(first, "overlapping vertex attribute VGPR destinations");
                vector[base + component] = true;
            }
        }
    }
    bool position{}, color{};
    for (const auto& instruction : program.instructions) {
        const auto word = instruction.raw[0];
        const auto source = [&](std::uint32_t selector) {
            if (selector >= 256) {
                const auto index = selector - 256;
                return index < config.num_vgprs && vector[index];
            }
            if (selector < 104) return selector < config.num_sgprs && scalar[selector];
            if (selector == 106 || selector == 107) return scalar[selector];
            return (selector >= 128 && selector <= 208) || (selector >= 240 && selector <= 247) ||
                (selector == 255 && instruction.has_literal);
        };
        const auto read = [&](std::uint32_t selector) {
            return source(selector) || fail(instruction, "undefined or unsupported source selector " + std::to_string(selector));
        };
        const auto write_scalar = [&](std::uint32_t destination) {
            if ((destination < config.num_sgprs && destination < 104) || destination == 106 || destination == 107) {
                scalar[destination] = true; return true;
            }
            return fail(instruction, "unsupported scalar destination (EXEC/wave state is not emulated)");
        };
        const auto write_vector = [&](std::uint32_t destination) {
            if (destination >= config.num_vgprs) return fail(instruction, "destination VGPR is outside allocation");
            vector[destination] = true; return true;
        };
        switch (instruction.encoding) {
        case Encoding::sop1: {
            const auto src = word & 255, dst = (word >> 16) & 127;
            if (instruction.opcode == 3) {
                if (!read(src) || !write_scalar(dst)) return false;
            } else if (instruction.opcode == 4) {
                if ((src & 1) || (dst & 1) || src >= 103 || dst >= config.num_sgprs - 1 ||
                    !read(src) || !read(src + 1) || !write_scalar(dst) || !write_scalar(dst + 1))
                    return fail(instruction, "S_MOV_B64 requires defined, aligned SGPR pairs");
            } else return fail(instruction, "unsupported SOP1, including fetch/call and EXEC operations");
            break;
        }
        case Encoding::sop2:
            if (!one_of(instruction.opcode, {0,1,2,3,14,16,18,20,30,32,34,38}))
                return fail(instruction, "unsupported SOP2");
            if (!read(word & 255) || !read((word >> 8) & 255) || !write_scalar((word >> 16) & 127)) return false;
            break;
        case Encoding::sopk:
            if (!one_of(instruction.opcode, {0,15})) return fail(instruction, "unsupported SOPK");
            if (instruction.opcode == 15 && !read((word >> 16) & 127)) return false;
            if (!write_scalar((word >> 16) & 127)) return false;
            break;
        case Encoding::sopc:
            if (instruction.opcode > 11) return fail(instruction, "unsupported SOPC");
            if (!read(word & 255) || !read((word >> 8) & 255)) return false;
            break;
        case Encoding::sopp:
            if (!one_of(instruction.opcode, {0,1,12})) return fail(instruction, "control flow/barrier instruction is not lowered");
            break;
        case Encoding::vop1:
            if (instruction.opcode == 0) break;
            if (!one_of(instruction.opcode, {1,5,6,32,33,34,35,36,37,39,42,46,51,55}))
                return fail(instruction, "unsupported VOP1");
            if (!read(word & 511) || !write_vector((word >> 17) & 255)) return false;
            break;
        case Encoding::vop2:
            if (!one_of(instruction.opcode, {3,4,5,8,15,16,21,22,23,24,25,26,27,28,29,31,32,33}))
                return fail(instruction, "unsupported VOP2");
            if (!read(word & 511) || !read(256 + ((word >> 9) & 255))) return false;
            if (instruction.opcode == 31 && !read(256 + ((word >> 17) & 255))) return false;
            if (!write_vector((word >> 17) & 255)) return false;
            break;
        case Encoding::vop3: {
            if ((word & 0x1f000u) || !one_of(instruction.opcode, {259,260,261,264,271,272,321,331}))
                return fail(instruction, "unsupported VOP3 opcode or reserved modifier bits");
            const auto operands = instruction.raw[1];
            if (!read(operands & 511) || !read((operands >> 9) & 511)) return false;
            if ((instruction.opcode == 321 || instruction.opcode == 331) && !read((operands >> 18) & 511)) return false;
            if (!write_vector(word & 255)) return false;
            break;
        }
        case Encoding::mimg: {
            if (config.stage != Stage::Fragment || instruction.opcode != 32 || word != 0xf0800f00u ||
                !config.sampled_texture || config.num_textures != 1 || config.num_samplers != 1 ||
                (instruction.raw[1] & 0xfc000000u)) return fail(instruction, "only explicitly bound 2D IMAGE_SAMPLE with full DMASK is supported");
            const auto& binding = *config.sampled_texture;
            if (((instruction.raw[1] >> 16) & 31) * 4 != binding.resource_sgpr_base ||
                ((instruction.raw[1] >> 21) & 31) * 4 != binding.sampler_sgpr_base)
                return fail(instruction, "IMAGE_SAMPLE resource/sampler SGPR bases do not match explicit binding");
            const auto address = instruction.raw[1] & 255, destination = (instruction.raw[1] >> 8) & 255;
            if (address >= 255 || destination > 252) return fail(instruction, "IMAGE_SAMPLE VGPR range overflows");
            if (!read(256 + address) || !read(257 + address)) return false;
            for (unsigned component = 0; component < 4; ++component) if (!write_vector(destination + component)) return false;
            break;
        }
        case Encoding::exp: {
            const auto target = (word >> 4) & 63;
            if ((word & 15) != 15 || (word & 0x400u) || (word & 0x03ffe000u))
                return fail(instruction, "partial/compressed or reserved EXP encoding is unsupported");
            if (config.stage == Stage::Vertex && (target == 12 || (target >= 32 && target < 48))) {
                if (target == 12) position = true;
            } else if (config.stage == Stage::Fragment && target == 0) color = true;
            else return fail(instruction, "unsupported EXP target");
            for (unsigned component = 0; component < 4; ++component)
                if (!read(256 + ((instruction.raw[1] >> (component * 8)) & 255))) return false;
            break;
        }
        default: return fail(instruction, "instruction family requires unimplemented wave, LDS, interpolation or memory semantics");
        }
    }
    if ((config.stage == Stage::Vertex && !position) || (config.stage == Stage::Fragment && !color))
        return fail(program.instructions.back(), "shader does not write its required position/color output");
    return true;
}

// Sea Islands (GCN 1.x) special register selectors in SRC0/SRC1 fields.
constexpr std::uint32_t sel_vcc_lo       = 106;
constexpr std::uint32_t sel_vcc_hi       = 107;
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
        throw std::logic_error("unsupported scalar selector escaped validation");
    }

    // Unified VALU selector: 0..255 scalar/constants, 256..511 VGPR.
    spirv::Id load_vsrc(std::uint32_t selector, std::uint32_t literal_value) {
        if (selector >= sel_vgpr_start) {
            auto vgpr_idx = selector - sel_vgpr_start;
            if (vgpr_idx < vgpr_vars.size() && vgpr_vars[vgpr_idx])
                return b.op_load(f32, vgpr_vars[vgpr_idx]);
            throw std::logic_error("unallocated VGPR escaped validation");
        }
        // Scalar source → bitcast to float.
        auto val = load_scalar(selector, literal_value);
        return b.op_bitcast(f32, val);
    }

    // Store to SGPR.
    void store_sgpr(std::uint32_t idx, spirv::Id value) {
        if (idx < sgpr_vars.size() && sgpr_vars[idx])
            b.op_store(sgpr_vars[idx], value);
        else if (idx == sel_vcc_lo) b.op_store(vcc_lo_var, value);
        else if (idx == sel_vcc_hi) b.op_store(vcc_hi_var, value);
        else throw std::logic_error("unsupported scalar destination escaped validation");
    }

    // Store to VGPR (value must be f32).
    void store_vgpr(std::uint32_t idx, spirv::Id value) {
        if (idx < vgpr_vars.size() && vgpr_vars[idx])
            b.op_store(vgpr_vars[idx], value);
        else throw std::logic_error("unallocated VGPR destination escaped validation");
    }

    // Load VGPR as float.
    spirv::Id load_vgpr(std::uint32_t idx) {
        if (idx < vgpr_vars.size() && vgpr_vars[idx])
            return b.op_load(f32, vgpr_vars[idx]);
        throw std::logic_error("unallocated VGPR source escaped validation");
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

std::string cache_identity(const TranslateConfig& config) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "sea-islands-strict-v3;spirv=1.5;vulkan=1.2;" << static_cast<int>(config.stage) << ';'
        << config.num_sgprs << ';' << config.num_vgprs << ';' << config.num_user_sgprs << ';'
        << config.num_vertex_attribs << ';' << config.num_textures << ';' << config.num_samplers << ';';
    const auto vector = [&](const auto& values) {
        out << values.size() << ':';
        for (const auto value : values) out << value << ',';
        out << ';';
    };
    vector(config.vertex_strides); vector(config.vertex_components); vector(config.vertex_vgpr_bases); vector(config.user_sgpr_values);
    out << bool(config.sampled_texture) << ';';
    if (config.sampled_texture) {
        const auto& binding = *config.sampled_texture;
        out << binding.resource_sgpr_base << ';' << binding.sampler_sgpr_base << ';' << binding.set << ';' << binding.binding << ';';
    }
    return out.str();
}

TranslateResult translate(const Program& program, const TranslateConfig& config) {
    TranslateResult result;
    if (program.instructions.empty() || !program.diagnostic) {
        result.error = "Invalid decoded GCN program";
        return result;
    }
    if (!validate_subset(program, config, result)) return result;
    try {

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
    spirv::Id sampled_image_type{};

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
        auto num_attribs = config.num_vertex_attribs;
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
        std::uint32_t parameter_count{};
        for (const auto& instruction : program.instructions) if (instruction.encoding == Encoding::exp) {
            const auto target = (instruction.raw[0] >> 4) & 63;
            if (target >= 32 && target < 48) parameter_count = (std::max)(parameter_count, target - 31);
        }
        for (std::uint32_t i = 0; i < parameter_count; ++i) {
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

        // Texture samplers.
        if (std::any_of(program.instructions.begin(), program.instructions.end(), [](const auto& instruction) {
                return instruction.encoding == Encoding::mimg;
            })) {
            auto img_type = b.type_image(regs.f32, spirv::Dim::_2D, false, false, false, 1, spirv::ImageFormat::Unknown);
            auto sampled_type = b.type_sampled_image(img_type);
            sampled_image_type = sampled_type;
            auto ptr_sampled = b.type_pointer(spirv::StorageClass::UniformConstant, sampled_type);
            tex_sampler_var = b.variable(ptr_sampled, spirv::StorageClass::UniformConstant);
            const auto binding = config.sampled_texture.value_or(SampledTextureBinding{});
            b.decorate(tex_sampler_var, spirv::Decoration::DescriptorSet, binding.set);
            b.decorate(tex_sampler_var, spirv::Decoration::Binding, binding.binding);
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
    for (std::uint32_t i = 0; i < config.user_sgpr_values.size(); ++i)
        regs.store_sgpr(i, b.const_uint32(config.user_sgpr_values[i]));

    // Load vertex attributes into VGPRs (hardware convention: v0=vertexId, v1..vN from fetch).
    if (config.stage == Stage::Vertex) {
        // v0 = vertex index (as float)
        auto vid = b.op_load(regs.u32, vertex_id_var);
        auto vid_f = b.op_bitcast(regs.f32, vid);
        regs.store_vgpr(0, vid_f);

        // Load vertex attributes into consecutive VGPRs.
        for (std::uint32_t attr = 0; attr < vertex_input_vars.size(); ++attr) {
            auto vgpr = config.vertex_vgpr_bases[attr];
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
    }

    const auto bool_t = b.type_bool();
    const auto no_contraction = static_cast<spirv::Decoration>(42);
    auto not_bool = [&](spirv::Id value) { return b.op_select(bool_t, value, b.const_bool(false), b.const_bool(true)); };
    auto unfused_mad = [&](spirv::Id a, spirv::Id value_b, spirv::Id c) {
        const auto product = b.op_fmul(regs.f32, a, value_b);
        const auto sum = b.op_fadd(regs.f32, product, c);
        b.decorate(product, no_contraction); b.decorate(sum, no_contraction);
        return sum;
    };
    auto float_binary = [&](std::uint32_t opcode, spirv::Id a, spirv::Id value_b) -> spirv::Id {
        switch (opcode) {
        case 3: return b.op_fadd(regs.f32, a, value_b);
        case 4: return b.op_fsub(regs.f32, a, value_b);
        case 5: return b.op_fsub(regs.f32, value_b, a);
        case 8: return b.op_fmul(regs.f32, a, value_b);
        case 15: return b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMin, {a, value_b});
        case 16: return b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FMax, {a, value_b});
        default: throw std::logic_error("unsupported float binary operation escaped validation");
        }
    };
    for (const auto& inst : program.instructions) {
        const auto word = inst.raw[0];
        switch (inst.encoding) {
        case Encoding::sop1: {
            const auto src = word & 255, dst = (word >> 16) & 127;
            const auto value = regs.load_scalar(src, inst.literal);
            if (inst.opcode == 4) {
                const auto upper = regs.load_scalar(src + 1, 0);
                regs.store_sgpr(dst, value); regs.store_sgpr(dst + 1, upper);
            } else regs.store_sgpr(dst, value);
            break;
        }
        case Encoding::sop2: {
            const auto a = regs.load_scalar(word & 255, inst.literal);
            const auto value_b = regs.load_scalar((word >> 8) & 255, inst.literal);
            const auto shift = b.op_bitwise_and(regs.u32, value_b, b.const_uint32(31));
            spirv::Id value{};
            switch (inst.opcode) {
            case 0: case 2: value = b.op_iadd(regs.u32, a, value_b); break;
            case 1: case 3: value = b.op_isub(regs.u32, a, value_b); break;
            case 14: value = b.op_bitwise_and(regs.u32, a, value_b); break;
            case 16: value = b.op_bitwise_or(regs.u32, a, value_b); break;
            case 18: value = b.op_bitwise_xor(regs.u32, a, value_b); break;
            case 20: value = b.op_bitwise_and(regs.u32, a, b.op_not(regs.u32, value_b)); break;
            case 30: value = b.op_shift_left(regs.u32, a, shift); break;
            case 32: value = b.op_shift_right_logical(regs.u32, a, shift); break;
            case 34: value = b.op_bitcast(regs.u32, b.op_shift_right_arithmetic(regs.i32, b.op_bitcast(regs.i32, a), shift)); break;
            case 38: value = b.op_imul(regs.u32, a, value_b); break;
            default: throw std::logic_error("unsupported SOP2 escaped validation");
            }
            regs.store_sgpr((word >> 16) & 127, value); break;
        }
        case Encoding::sopk: {
            const auto dst = (word >> 16) & 127;
            const auto immediate = (word & 65535) - ((word & 32768) ? 65536u : 0u);
            auto value = b.const_uint32(immediate);
            if (inst.opcode == 15) value = b.op_iadd(regs.u32, regs.load_scalar(dst, 0), value);
            regs.store_sgpr(dst, value); break;
        }
        case Encoding::sopc: {
            auto a = regs.load_scalar(word & 255, inst.literal);
            auto value_b = regs.load_scalar((word >> 8) & 255, inst.literal);
            const auto condition = inst.opcode % 6;
            spirv::Id cmp{};
            if (condition < 2) cmp = condition ? b.op_inotequal(bool_t, a, value_b) : b.op_iequal(bool_t, a, value_b);
            else {
                if (inst.opcode >= 6) {
                    a = b.op_bitwise_xor(regs.u32, a, b.const_uint32(0x80000000u));
                    value_b = b.op_bitwise_xor(regs.u32, value_b, b.const_uint32(0x80000000u));
                }
                a = b.op_bitcast(regs.i32, a); value_b = b.op_bitcast(regs.i32, value_b);
                if (condition == 2) cmp = b.op_sless_than(bool_t, value_b, a);
                else if (condition == 3) cmp = not_bool(b.op_sless_than(bool_t, a, value_b));
                else if (condition == 4) cmp = b.op_sless_than(bool_t, a, value_b);
                else cmp = not_bool(b.op_sless_than(bool_t, value_b, a));
            }
            b.op_store(regs.scc_var, cmp); break;
        }
        case Encoding::sopp: break; // validated NOP, WAITCNT or final END only
        case Encoding::vop1: {
            if (!inst.opcode) break;
            const auto value = regs.load_vsrc(word & 511, inst.literal);
            const auto dst = (word >> 17) & 255;
            spirv::Id output{};
            switch (inst.opcode) {
            case 1: output = value; break;
            case 5: output = b.op_convert_s_to_f(regs.f32, b.op_bitcast(regs.i32, value)); break;
            case 6: output = b.op_convert_u_to_f(regs.f32, b.op_bitcast(regs.u32, value)); break;
            case 32: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Fract, {value}); break;
            case 33: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Trunc, {value}); break;
            case 34: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Ceil, {value}); break;
            case 35: output = b.ext_inst(regs.f32, glsl_ext, 2 /* RoundEven */, {value}); break;
            case 36: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Floor, {value}); break;
            case 37: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Exp2, {value}); break;
            case 39: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Log2, {value}); break;
            case 42: output = b.op_fdiv(regs.f32, b.const_float32(1.0f), value); break;
            case 46: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::InverseSqrt, {value}); break;
            case 51: output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Sqrt, {value}); break;
            case 55: output = b.op_bitcast(regs.f32, b.op_not(regs.u32, b.op_bitcast(regs.u32, value))); break;
            default: throw std::logic_error("unsupported VOP1 escaped validation");
            }
            regs.store_vgpr(dst, output); break;
        }
        case Encoding::vop2: {
            const auto dst = (word >> 17) & 255;
            const auto a = regs.load_vsrc(word & 511, inst.literal);
            const auto value_b = regs.load_vgpr((word >> 9) & 255);
            spirv::Id output{};
            if (one_of(inst.opcode, {3,4,5,8,15,16})) output = float_binary(inst.opcode, a, value_b);
            else if (one_of(inst.opcode, {31,32,33})) {
                const auto literal = b.op_bitcast(regs.f32, b.const_uint32(inst.literal));
                if (inst.opcode == 31) output = unfused_mad(a, value_b, regs.load_vgpr(dst));
                else if (inst.opcode == 32) output = unfused_mad(a, literal, value_b);
                else output = unfused_mad(a, value_b, literal);
            } else {
                auto integer_a = b.op_bitcast(regs.u32, a), integer_b = b.op_bitcast(regs.u32, value_b);
                spirv::Id integer{};
                if (inst.opcode >= 27) {
                    if (inst.opcode == 27) integer = b.op_bitwise_and(regs.u32, integer_a, integer_b);
                    else if (inst.opcode == 28) integer = b.op_bitwise_or(regs.u32, integer_a, integer_b);
                    else integer = b.op_bitwise_xor(regs.u32, integer_a, integer_b);
                } else {
                    if ((inst.opcode & 1) == 0) std::swap(integer_a, integer_b);
                    const auto shift = b.op_bitwise_and(regs.u32, integer_b, b.const_uint32(31));
                    if (inst.opcode <= 22) integer = b.op_shift_right_logical(regs.u32, integer_a, shift);
                    else if (inst.opcode <= 24) integer = b.op_bitcast(regs.u32,
                        b.op_shift_right_arithmetic(regs.i32, b.op_bitcast(regs.i32, integer_a), shift));
                    else integer = b.op_shift_left(regs.u32, integer_a, shift);
                }
                output = b.op_bitcast(regs.f32, integer);
            }
            regs.store_vgpr(dst, output); break;
        }
        case Encoding::vop3: {
            const auto operands = inst.raw[1];
            const auto load_operand = [&](unsigned index) {
                auto value = regs.load_vsrc((operands >> (index * 9)) & 511, 0);
                if (word & (1u << (8 + index))) value = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FAbs, {value});
                if (operands & (1u << (29 + index))) value = b.op_fnegate(regs.f32, value);
                return value;
            };
            const auto a = load_operand(0), value_b = load_operand(1);
            spirv::Id output{};
            if (inst.opcode == 321) output = unfused_mad(a, value_b, load_operand(2));
            else if (inst.opcode == 331) output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::Fma, {a, value_b, load_operand(2)});
            else output = float_binary(inst.opcode - 256, a, value_b);
            const auto modifier = (operands >> 27) & 3;
            if (modifier) output = b.op_fmul(regs.f32, output, b.const_float32(modifier == 1 ? 2.0f : modifier == 2 ? 4.0f : 0.5f));
            if (word & 0x800u) output = b.ext_inst(regs.f32, glsl_ext, spirv::GLSLstd450::FClamp,
                {output, b.const_float32(0.0f), b.const_float32(1.0f)});
            regs.store_vgpr(word & 255, output); break;
        }
        case Encoding::mimg: {
            const auto destination = (inst.raw[1] >> 8) & 255, address = inst.raw[1] & 255;
            const auto texture = b.op_load(sampled_image_type, tex_sampler_var);
            const auto coordinate = b.op_composite_construct(regs.v2f, {regs.load_vgpr(address), regs.load_vgpr(address + 1)});
            const auto sampled = b.op_image_sample_implicit(regs.v4f, texture, coordinate);
            for (unsigned component = 0; component < 4; ++component)
                regs.store_vgpr(destination + component, b.op_composite_extract(regs.f32, sampled, component));
            const auto binding = *config.sampled_texture;
            result.resources.push_back({ResourceSlot::Texture, binding.resource_sgpr_base, binding.binding, binding.set});
            result.resources.push_back({ResourceSlot::Sampler, binding.sampler_sgpr_base, binding.binding, binding.set});
            break;
        }
        case Encoding::exp: {
            const auto info = decode_exp(inst); result.exports.push_back(info);
            std::vector<spirv::Id> components;
            for (unsigned component = 0; component < 4; ++component)
                components.push_back(regs.load_vgpr((inst.raw[1] >> (component * 8)) & 255));
            const auto value = b.op_composite_construct(regs.v4f, components);
            if (config.stage == Stage::Fragment) b.op_store(frag_color_var, value);
            else if (info.target == 12) b.op_store(position_var, value);
            else b.op_store(param_output_vars[info.target - 32], value);
            break;
        }
        default: throw std::logic_error("unimplemented instruction family escaped validation");
        }
    }

    // Return.
    b.op_return();
    b.end_function();

    result.spirv = b.finalize();
    if (!validate_spirv(result.spirv, result.error)) {
        result.spirv.clear(); return result;
    }
    result.success = true;
    return result;
    } catch (const std::exception& error) {
        result.spirv.clear(); result.exports.clear(); result.resources.clear();
        result.error = std::string("GCN lowering failed: ") + error.what();
        return result;
    }
}

} // namespace gravity::hle::gnm::gcn
