#pragma once

#include "hle/gnm/gcn_decoder.h"
#include "hle/gnm/spirv_builder.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gravity::hle::gnm::gcn {

// Shader stage as determined by the PM4 context registers that configure
// the shader program address.
enum class Stage { Vertex, Fragment };

// Describes a single export from the shader (EXP instruction).
struct ExportInfo {
    std::uint8_t target{};    // 0..63 (0..7=mrt0..7, 12..15=pos0..3, 32..63=param0..31)
    std::uint8_t compr{};
    std::uint8_t en{0xf};     // component enable mask
    std::uint8_t num_components{4};
};

// Resource binding inferred from S_LOAD / BUFFER_LOAD / IMAGE_SAMPLE.
struct ResourceSlot {
    enum Kind { ConstantBuffer, VertexBuffer, Texture, Sampler };
    Kind kind{};
    std::uint32_t sgpr_base{};     // SGPR pair holding the descriptor
    std::uint32_t binding{};       // assigned Vulkan binding
    std::uint32_t set{};           // assigned Vulkan descriptor set
};

struct SampledTextureBinding {
    std::uint32_t resource_sgpr_base{};
    std::uint32_t sampler_sgpr_base{};
    std::uint32_t set{};
    std::uint32_t binding{};
};

// Configuration that the translator needs beyond the raw instruction stream.
struct TranslateConfig {
    Stage stage{Stage::Vertex};
    // Number of SGPRs and VGPRs allocated by the hardware for this shader.
    std::uint32_t num_sgprs{16};
    std::uint32_t num_vgprs{16};
    // Number of user data SGPRs (set by SPI_SHADER_USER_DATA_*).
    std::uint32_t num_user_sgprs{0};
    // Vertex attribute count (for VS).
    std::uint32_t num_vertex_attribs{0};
    // Texture/sampler count (for PS).
    std::uint32_t num_textures{0};
    std::uint32_t num_samplers{0};
    // Fetch shader vertex buffer strides (index = attribute slot).
    std::vector<std::uint32_t> vertex_strides;
    // Fetch shader vertex component counts per attribute.
    std::vector<std::uint32_t> vertex_components; // 1..4 floats
    // Explicit interface contracts, never inferred by replacing memory reads.
    // Each vertex attribute is copied to this first VGPR (v0 is VertexIndex).
    std::vector<std::uint32_t> vertex_vgpr_bases;
    // Optional compile-time scalar inputs. Unprovided registers are undefined.
    std::vector<std::uint32_t> user_sgpr_values;
    // Exactly one combined 2D sampler is supported, only when its guest
    // resource/sampler SGPR bases have been verified by the caller.
    std::optional<SampledTextureBinding> sampled_texture;
};

// Result of translation.
struct TranslateResult {
    bool success{false};
    std::string error;
    std::vector<std::uint32_t> spirv; // Valid SPIR-V module if success == true.
    std::vector<ExportInfo> exports;
    std::vector<ResourceSlot> resources;
    std::size_t error_word_offset{};
    std::uint32_t error_opcode{};
};

// Includes translator version and every semantic configuration field.
std::string cache_identity(const TranslateConfig& config);

// Translate a decoded GCN program into SPIR-V.
// The config must match what the PM4 context registers configured.
// The current subset is straight-line VS/PS arithmetic and full uncompressed
// POS0/PARAM0..15/MRT0 exports. Unsupported fetch/control flow, EXEC/VCC wave
// operations, LDS, memory/UBO access, interpolation, saturating float-to-integer
// conversions and compressed/partial exports fail with an ISA offset/opcode.
// Success additionally requires semantic SPIRV-Tools Vulkan 1.2 validation.
TranslateResult translate(const Program& program, const TranslateConfig& config);

} // namespace gravity::hle::gnm::gcn
