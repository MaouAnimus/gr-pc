#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gravity::hle::gnm::spirv {

// Minimal SPIR-V enumerations needed for shader translation.
// Values match the SPIR-V 1.5 specification, Chapter 3.
enum class Capability : std::uint32_t { Shader = 1 };
enum class AddressingModel : std::uint32_t { Logical = 0 };
enum class MemoryModel : std::uint32_t { GLSL450 = 1 };
enum class ExecutionModel : std::uint32_t { Vertex = 0, Fragment = 4 };
enum class ExecutionMode : std::uint32_t { OriginUpperLeft = 7 };
enum class StorageClass : std::uint32_t {
    UniformConstant = 0, Input = 1, Uniform = 2, Output = 3,
    Function = 7, PushConstant = 9
};
enum class Decoration : std::uint32_t {
    Block = 2, RowMajor = 4, ColMajor = 5, ArrayStride = 6,
    MatrixStride = 7, BuiltIn = 11, NoPerspective = 13,
    Flat = 14, Location = 30, Binding = 33, DescriptorSet = 34,
    Offset = 35
};
enum class BuiltIn : std::uint32_t {
    Position = 0, PointSize = 1, VertexIndex = 42, InstanceIndex = 43,
    FragCoord = 15, FrontFacing = 17, FragDepth = 22
};
enum class Dim : std::uint32_t { _1D = 0, _2D = 1, _3D = 2, Cube = 3 };
enum class ImageFormat : std::uint32_t { Unknown = 0 };

// Opcodes (subset used by the translator).
namespace Op {
    constexpr std::uint16_t Nop = 0;
    constexpr std::uint16_t ExtInstImport = 11;
    constexpr std::uint16_t ExtInst = 12;
    constexpr std::uint16_t MemoryModel_ = 14;
    constexpr std::uint16_t EntryPoint = 15;
    constexpr std::uint16_t ExecutionMode_ = 16;
    constexpr std::uint16_t Capability_ = 17;
    constexpr std::uint16_t TypeVoid = 19;
    constexpr std::uint16_t TypeBool = 20;
    constexpr std::uint16_t TypeInt = 21;
    constexpr std::uint16_t TypeFloat = 22;
    constexpr std::uint16_t TypeVector = 23;
    constexpr std::uint16_t TypeMatrix = 24;
    constexpr std::uint16_t TypeImage = 25;
    constexpr std::uint16_t TypeSampledImage = 27;
    constexpr std::uint16_t TypeArray = 28;
    constexpr std::uint16_t TypeStruct = 30;
    constexpr std::uint16_t TypePointer = 32;
    constexpr std::uint16_t TypeFunction = 33;
    constexpr std::uint16_t ConstantTrue = 41;
    constexpr std::uint16_t ConstantFalse = 42;
    constexpr std::uint16_t Constant = 43;
    constexpr std::uint16_t ConstantComposite = 44;
    constexpr std::uint16_t Function_ = 54;
    constexpr std::uint16_t FunctionParameter = 55;
    constexpr std::uint16_t FunctionEnd = 56;
    constexpr std::uint16_t Variable = 59;
    constexpr std::uint16_t Load = 61;
    constexpr std::uint16_t Store = 62;
    constexpr std::uint16_t AccessChain = 65;
    constexpr std::uint16_t Decorate = 71;
    constexpr std::uint16_t MemberDecorate = 72;
    constexpr std::uint16_t VectorShuffle = 79;
    constexpr std::uint16_t CompositeConstruct = 80;
    constexpr std::uint16_t CompositeExtract = 81;
    constexpr std::uint16_t CompositeInsert = 82;
    constexpr std::uint16_t ImageSampleImplicitLod = 87;
    constexpr std::uint16_t ImageSampleExplicitLod = 88;
    constexpr std::uint16_t FNegate = 127;
    constexpr std::uint16_t FAdd = 129;
    constexpr std::uint16_t FSub = 131;
    constexpr std::uint16_t FMul = 133;
    constexpr std::uint16_t FDiv = 136;
    constexpr std::uint16_t FMod = 141;
    constexpr std::uint16_t FOrdEqual = 180;
    constexpr std::uint16_t FUnordNotEqual = 183;
    constexpr std::uint16_t FOrdLessThan = 184;
    constexpr std::uint16_t FOrdGreaterThan = 186;
    constexpr std::uint16_t FOrdLessThanEqual = 188;
    constexpr std::uint16_t FOrdGreaterThanEqual = 190;
    constexpr std::uint16_t Phi = 245;
    constexpr std::uint16_t SelectionMerge = 247;
    constexpr std::uint16_t Label = 248;
    constexpr std::uint16_t Branch = 249;
    constexpr std::uint16_t BranchConditional = 250;
    constexpr std::uint16_t Return = 253;
    constexpr std::uint16_t ReturnValue = 254;
    constexpr std::uint16_t Select = 169;
    constexpr std::uint16_t IAdd = 128;
    constexpr std::uint16_t ISub = 130;
    constexpr std::uint16_t IMul = 132;
    constexpr std::uint16_t SDiv = 135;
    constexpr std::uint16_t UDiv = 134;
    constexpr std::uint16_t BitwiseAnd = 199;
    constexpr std::uint16_t BitwiseOr = 197;
    constexpr std::uint16_t BitwiseXor = 198;
    constexpr std::uint16_t ShiftLeftLogical = 196;
    constexpr std::uint16_t ShiftRightLogical = 194;
    constexpr std::uint16_t ShiftRightArithmetic = 195;
    constexpr std::uint16_t LogicalNot = 168;
    constexpr std::uint16_t Not = 200;
    constexpr std::uint16_t IEqual = 170;
    constexpr std::uint16_t INotEqual = 171;
    constexpr std::uint16_t SLessThan = 177;
    constexpr std::uint16_t ULessThan = 176;
    constexpr std::uint16_t SGreaterThan = 173;
    constexpr std::uint16_t SLessThanEqual = 178;
    constexpr std::uint16_t ConvertFToS = 110;
    constexpr std::uint16_t ConvertSToF = 111;
    constexpr std::uint16_t ConvertFToU = 109;
    constexpr std::uint16_t ConvertUToF = 112;
    constexpr std::uint16_t Bitcast = 124;
    constexpr std::uint16_t FClamp = 43; // GLSLstd450 FClamp = 43
    constexpr std::uint16_t Fma = 50;    // GLSLstd450 Fma = 50
} // namespace Op

// GLSL.std.450 extended instructions.
namespace GLSLstd450 {
    constexpr std::uint32_t Round = 1;
    constexpr std::uint32_t Trunc = 3;
    constexpr std::uint32_t FAbs = 4;
    constexpr std::uint32_t Floor = 8;
    constexpr std::uint32_t Ceil = 9;
    constexpr std::uint32_t Fract = 10;
    constexpr std::uint32_t Sqrt = 31;
    constexpr std::uint32_t InverseSqrt = 32;
    constexpr std::uint32_t FMin = 37;
    constexpr std::uint32_t FMax = 40;
    constexpr std::uint32_t FClamp = 43;
    constexpr std::uint32_t FMix = 46;
    constexpr std::uint32_t Fma = 50;
    constexpr std::uint32_t Exp2 = 29;
    constexpr std::uint32_t Log2 = 30;
    constexpr std::uint32_t Sin = 13;
    constexpr std::uint32_t Cos = 14;
} // namespace GLSLstd450

using Id = std::uint32_t;

class Builder {
public:
    Id next_id() { return next_id_++; }
    Id reserve_ids(std::uint32_t count) {
        auto first = next_id_; next_id_ += count; return first;
    }

    // Capabilities & extensions.
    void add_capability(Capability cap);
    Id import_extended(std::string_view name); // returns Id for the import

    // Memory model & entry point.
    void set_memory_model(AddressingModel addr, MemoryModel mem);
    void add_entry_point(ExecutionModel model, Id function, std::string_view name,
                         std::vector<Id> interface_vars);
    void add_execution_mode(Id entry, ExecutionMode mode);

    // Types (deduplicated by key).
    Id type_void();
    Id type_bool();
    Id type_int(std::uint32_t width, bool is_signed);
    Id type_float(std::uint32_t width);
    Id type_vector(Id component, std::uint32_t count);
    Id type_matrix(Id column, std::uint32_t columns);
    Id type_array(Id element, Id length_constant);
    Id type_struct(std::vector<Id> members);
    Id type_pointer(StorageClass sc, Id pointee);
    Id type_function(Id return_type, std::vector<Id> params = {});
    Id type_image(Id sampled_type, Dim dim, bool depth, bool arrayed,
                  bool multisampled, std::uint32_t sampled, ImageFormat fmt);
    Id type_sampled_image(Id image_type);

    // Constants (deduplicated).
    Id const_bool(bool value);
    Id const_int32(std::int32_t value);
    Id const_uint32(std::uint32_t value);
    Id const_float32(float value);
    Id const_composite(Id type, std::vector<Id> members);

    // Decorations.
    void decorate(Id target, Decoration dec);
    void decorate(Id target, Decoration dec, std::uint32_t operand);
    void member_decorate(Id struct_type, std::uint32_t member, Decoration dec);
    void member_decorate(Id struct_type, std::uint32_t member, Decoration dec, std::uint32_t operand);

    // Debug names (optional, for readability).
    void set_name(Id target, std::string_view name);
    void set_member_name(Id struct_type, std::uint32_t member, std::string_view name);

    // Variables.
    Id variable(Id pointer_type, StorageClass sc, Id initializer = 0);

    // Functions.
    Id begin_function(Id return_type, Id function_type);
    void end_function();

    // Blocks.
    Id begin_block();
    Id begin_block(Id label);

    // --- Instructions (emitted into the current block) ---
    Id op_load(Id result_type, Id pointer);
    void op_store(Id pointer, Id value);
    Id op_access_chain(Id result_type, Id base, std::vector<Id> indices);

    // Arithmetic.
    Id op_fadd(Id type, Id a, Id b);
    Id op_fsub(Id type, Id a, Id b);
    Id op_fmul(Id type, Id a, Id b);
    Id op_fdiv(Id type, Id a, Id b);
    Id op_fnegate(Id type, Id a);
    Id op_iadd(Id type, Id a, Id b);
    Id op_isub(Id type, Id a, Id b);
    Id op_imul(Id type, Id a, Id b);

    // Bitwise.
    Id op_bitwise_and(Id type, Id a, Id b);
    Id op_bitwise_or(Id type, Id a, Id b);
    Id op_bitwise_xor(Id type, Id a, Id b);
    Id op_shift_left(Id type, Id a, Id b);
    Id op_shift_right_logical(Id type, Id a, Id b);
    Id op_shift_right_arithmetic(Id type, Id a, Id b);
    Id op_not(Id type, Id a);

    // Comparison.
    Id op_ford_equal(Id a, Id b);
    Id op_ford_less_than(Id a, Id b);
    Id op_ford_greater_than(Id a, Id b);
    Id op_ford_less_than_equal(Id a, Id b);
    Id op_ford_greater_than_equal(Id a, Id b);
    Id op_iequal(Id type, Id a, Id b);
    Id op_inotequal(Id type, Id a, Id b);
    Id op_sless_than(Id type, Id a, Id b);

    // Conversions.
    Id op_convert_f_to_s(Id type, Id value);
    Id op_convert_s_to_f(Id type, Id value);
    Id op_convert_f_to_u(Id type, Id value);
    Id op_convert_u_to_f(Id type, Id value);
    Id op_bitcast(Id type, Id value);

    // Composite.
    Id op_composite_construct(Id type, std::vector<Id> components);
    Id op_composite_extract(Id type, Id composite, std::uint32_t index);
    Id op_composite_insert(Id type, Id object, Id composite, std::uint32_t index);
    Id op_vector_shuffle(Id type, Id v1, Id v2, std::vector<std::uint32_t> components);

    // Texture.
    Id op_image_sample_implicit(Id type, Id sampled_image, Id coord);
    Id op_image_sample_explicit_lod(Id type, Id sampled_image, Id coord, Id lod);

    // Control flow.
    void op_branch(Id label);
    void op_branch_conditional(Id condition, Id true_label, Id false_label);
    void op_selection_merge(Id merge_label);
    void op_return();
    void op_return_value(Id value);
    Id op_select(Id type, Id condition, Id true_val, Id false_val);
    Id op_phi(Id type, std::vector<std::pair<Id, Id>> incoming);

    // Extended instructions (GLSL.std.450).
    Id ext_inst(Id type, Id ext_set, std::uint32_t instruction, std::vector<Id> operands);

    // Finalize & emit.
    [[nodiscard]] std::vector<std::uint32_t> finalize();

private:
    // SPIR-V sections, built separately and concatenated in finalize().
    std::vector<std::uint32_t> capabilities_;
    std::vector<std::uint32_t> extensions_;
    std::vector<std::uint32_t> ext_imports_;
    std::vector<std::uint32_t> memory_model_;
    std::vector<std::uint32_t> entry_points_;
    std::vector<std::uint32_t> execution_modes_;
    std::vector<std::uint32_t> debug_names_;
    std::vector<std::uint32_t> annotations_;
    std::vector<std::uint32_t> type_const_;    // types, constants, global vars
    std::vector<std::uint32_t> function_body_; // all function definitions

    Id next_id_{1};

    // Deduplication caches.
    std::unordered_map<std::uint64_t, Id> type_cache_;
    std::unordered_map<std::string, Id> struct_cache_;
    std::unordered_map<std::uint64_t, Id> const_cache_;

    // Helper to emit a word-encoded instruction into a target section.
    static void emit(std::vector<std::uint32_t>& section, std::uint16_t opcode,
                     std::vector<std::uint32_t> operands);
    static void emit_string(std::vector<std::uint32_t>& section, std::string_view str);

    // Encode a string as SPIR-V literal words (NUL-padded to 4-byte boundary).
    static std::vector<std::uint32_t> encode_string(std::string_view str);
};

} // namespace gravity::hle::gnm::spirv
