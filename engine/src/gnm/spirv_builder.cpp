#include "hle/gnm/spirv_builder.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace gravity::hle::gnm::spirv {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void Builder::emit(std::vector<std::uint32_t>& section, std::uint16_t opcode,
                   std::vector<std::uint32_t> operands) {
    const auto word_count = static_cast<std::uint32_t>(operands.size() + 1);
    section.push_back((word_count << 16) | opcode);
    section.insert(section.end(), operands.begin(), operands.end());
}

std::vector<std::uint32_t> Builder::encode_string(std::string_view str) {
    const auto byte_count = str.size() + 1; // include NUL
    const auto word_count = (byte_count + 3) / 4;
    std::vector<std::uint32_t> words(word_count, 0);
    std::memcpy(words.data(), str.data(), str.size());
    return words;
}

void Builder::emit_string(std::vector<std::uint32_t>& section, std::string_view str) {
    auto encoded = encode_string(str);
    section.insert(section.end(), encoded.begin(), encoded.end());
}

// ---------------------------------------------------------------------------
// Capabilities & Extensions
// ---------------------------------------------------------------------------

void Builder::add_capability(Capability cap) {
    emit(capabilities_, Op::Capability_, {static_cast<std::uint32_t>(cap)});
}

Id Builder::import_extended(std::string_view name) {
    auto id = next_id();
    auto encoded = encode_string(name);
    std::vector<std::uint32_t> ops;
    ops.push_back(id);
    ops.insert(ops.end(), encoded.begin(), encoded.end());
    emit(ext_imports_, Op::ExtInstImport, ops);
    return id;
}

// ---------------------------------------------------------------------------
// Memory Model & Entry Point
// ---------------------------------------------------------------------------

void Builder::set_memory_model(AddressingModel addr, MemoryModel mem) {
    emit(memory_model_, Op::MemoryModel_,
         {static_cast<std::uint32_t>(addr), static_cast<std::uint32_t>(mem)});
}

void Builder::add_entry_point(ExecutionModel model, Id function, std::string_view name,
                              std::vector<Id> interface_vars) {
    std::vector<std::uint32_t> ops;
    ops.push_back(static_cast<std::uint32_t>(model));
    ops.push_back(function);
    auto encoded = encode_string(name);
    ops.insert(ops.end(), encoded.begin(), encoded.end());
    ops.insert(ops.end(), interface_vars.begin(), interface_vars.end());
    emit(entry_points_, Op::EntryPoint, ops);
}

void Builder::add_execution_mode(Id entry, ExecutionMode mode) {
    emit(execution_modes_, Op::ExecutionMode_, {entry, static_cast<std::uint32_t>(mode)});
}

// ---------------------------------------------------------------------------
// Types (deduplicated)
// ---------------------------------------------------------------------------

Id Builder::type_void() {
    const std::uint64_t key = 0x100;
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeVoid, {id});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_bool() {
    const std::uint64_t key = 0x200;
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeBool, {id});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_int(std::uint32_t width, bool is_signed) {
    const std::uint64_t key = 0x300 | (static_cast<std::uint64_t>(width) << 16) |
                              (is_signed ? 1u : 0u);
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeInt, {id, width, is_signed ? 1u : 0u});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_float(std::uint32_t width) {
    const std::uint64_t key = 0x400 | (static_cast<std::uint64_t>(width) << 16);
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeFloat, {id, width});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_vector(Id component, std::uint32_t count) {
    const std::uint64_t key = 0x500 | (static_cast<std::uint64_t>(component) << 16) |
                              (static_cast<std::uint64_t>(count) << 48);
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeVector, {id, component, count});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_matrix(Id column, std::uint32_t columns) {
    const std::uint64_t key = 0x600 | (static_cast<std::uint64_t>(column) << 16) |
                              (static_cast<std::uint64_t>(columns) << 48);
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeMatrix, {id, column, columns});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_array(Id element, Id length_constant) {
    const std::uint64_t key = 0x700 | (static_cast<std::uint64_t>(element) << 16) |
                              (static_cast<std::uint64_t>(length_constant) << 32);
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeArray, {id, element, length_constant});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_struct(std::vector<Id> members) {
    // Use sorted string key for deduplication.
    std::string key = "S";
    for (auto m : members) key += ":" + std::to_string(m);
    auto it = struct_cache_.find(key);
    if (it != struct_cache_.end()) return it->second;
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(id);
    ops.insert(ops.end(), members.begin(), members.end());
    emit(type_const_, Op::TypeStruct, ops);
    struct_cache_[key] = id;
    return id;
}

Id Builder::type_pointer(StorageClass sc, Id pointee) {
    const std::uint64_t key = 0x900 | (static_cast<std::uint64_t>(sc) << 16) |
                              (static_cast<std::uint64_t>(pointee) << 32);
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypePointer, {id, static_cast<std::uint32_t>(sc), pointee});
    type_cache_[key] = id;
    return id;
}

Id Builder::type_function(Id return_type, std::vector<Id> params) {
    std::string key = "F:" + std::to_string(return_type);
    for (auto p : params) key += ":" + std::to_string(p);
    auto it = struct_cache_.find(key);
    if (it != struct_cache_.end()) return it->second;
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(id);
    ops.push_back(return_type);
    ops.insert(ops.end(), params.begin(), params.end());
    emit(type_const_, Op::TypeFunction, ops);
    struct_cache_[key] = id;
    return id;
}

Id Builder::type_image(Id sampled_type, Dim dim, bool depth, bool arrayed,
                       bool multisampled, std::uint32_t sampled, ImageFormat fmt) {
    auto id = next_id();
    emit(type_const_, Op::TypeImage, {
        id, sampled_type, static_cast<std::uint32_t>(dim),
        depth ? 1u : 0u, arrayed ? 1u : 0u, multisampled ? 1u : 0u,
        sampled, static_cast<std::uint32_t>(fmt)
    });
    return id;
}

Id Builder::type_sampled_image(Id image_type) {
    const std::uint64_t key = 0xB00 | (static_cast<std::uint64_t>(image_type) << 16);
    auto it = type_cache_.find(key);
    if (it != type_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::TypeSampledImage, {id, image_type});
    type_cache_[key] = id;
    return id;
}

// ---------------------------------------------------------------------------
// Constants (deduplicated)
// ---------------------------------------------------------------------------

Id Builder::const_bool(bool value) {
    const std::uint64_t key = 0x10000 | (value ? 1u : 0u);
    auto it = const_cache_.find(key);
    if (it != const_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, value ? Op::ConstantTrue : Op::ConstantFalse, {type_bool(), id});
    const_cache_[key] = id;
    return id;
}

Id Builder::const_int32(std::int32_t value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    const std::uint64_t key = 0x20000 | (static_cast<std::uint64_t>(bits) << 32);
    auto it = const_cache_.find(key);
    if (it != const_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::Constant, {type_int(32, true), id, bits});
    const_cache_[key] = id;
    return id;
}

Id Builder::const_uint32(std::uint32_t value) {
    const std::uint64_t key = 0x30000 | (static_cast<std::uint64_t>(value) << 32);
    auto it = const_cache_.find(key);
    if (it != const_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::Constant, {type_int(32, false), id, value});
    const_cache_[key] = id;
    return id;
}

Id Builder::const_float32(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    const std::uint64_t key = 0x40000 | (static_cast<std::uint64_t>(bits) << 32);
    auto it = const_cache_.find(key);
    if (it != const_cache_.end()) return it->second;
    auto id = next_id();
    emit(type_const_, Op::Constant, {type_float(32), id, bits});
    const_cache_[key] = id;
    return id;
}

Id Builder::const_composite(Id type, std::vector<Id> members) {
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(type);
    ops.push_back(id);
    ops.insert(ops.end(), members.begin(), members.end());
    emit(type_const_, Op::ConstantComposite, ops);
    return id;
}

// ---------------------------------------------------------------------------
// Decorations
// ---------------------------------------------------------------------------

void Builder::decorate(Id target, Decoration dec) {
    emit(annotations_, Op::Decorate, {target, static_cast<std::uint32_t>(dec)});
}

void Builder::decorate(Id target, Decoration dec, std::uint32_t operand) {
    emit(annotations_, Op::Decorate, {target, static_cast<std::uint32_t>(dec), operand});
}

void Builder::member_decorate(Id struct_type, std::uint32_t member, Decoration dec) {
    emit(annotations_, Op::MemberDecorate, {struct_type, member, static_cast<std::uint32_t>(dec)});
}

void Builder::member_decorate(Id struct_type, std::uint32_t member, Decoration dec, std::uint32_t operand) {
    emit(annotations_, Op::MemberDecorate, {struct_type, member, static_cast<std::uint32_t>(dec), operand});
}

// ---------------------------------------------------------------------------
// Debug Names
// ---------------------------------------------------------------------------

void Builder::set_name(Id target, std::string_view name) {
    std::vector<std::uint32_t> ops;
    ops.push_back(target);
    auto encoded = encode_string(name);
    ops.insert(ops.end(), encoded.begin(), encoded.end());
    // OpName = 5
    emit(debug_names_, 5, ops);
}

void Builder::set_member_name(Id struct_type, std::uint32_t member, std::string_view name) {
    std::vector<std::uint32_t> ops;
    ops.push_back(struct_type);
    ops.push_back(member);
    auto encoded = encode_string(name);
    ops.insert(ops.end(), encoded.begin(), encoded.end());
    // OpMemberName = 6
    emit(debug_names_, 6, ops);
}

// ---------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------

Id Builder::variable(Id pointer_type, StorageClass sc, Id initializer) {
    auto id = next_id();
    if (sc == StorageClass::Function) {
        // Function-scope variables go into the current function body.
        if (initializer)
            emit(function_body_, Op::Variable, {pointer_type, id, static_cast<std::uint32_t>(sc), initializer});
        else
            emit(function_body_, Op::Variable, {pointer_type, id, static_cast<std::uint32_t>(sc)});
    } else {
        if (initializer)
            emit(type_const_, Op::Variable, {pointer_type, id, static_cast<std::uint32_t>(sc), initializer});
        else
            emit(type_const_, Op::Variable, {pointer_type, id, static_cast<std::uint32_t>(sc)});
    }
    return id;
}

// ---------------------------------------------------------------------------
// Functions & Blocks
// ---------------------------------------------------------------------------

Id Builder::begin_function(Id return_type, Id function_type) {
    auto id = next_id();
    // OpFunction: result_type result function_control function_type
    emit(function_body_, Op::Function_, {return_type, id, 0u /* None */, function_type});
    return id;
}

void Builder::end_function() {
    emit(function_body_, Op::FunctionEnd, {});
}

Id Builder::begin_block() {
    auto id = next_id();
    emit(function_body_, Op::Label, {id});
    return id;
}

Id Builder::begin_block(Id label) {
    emit(function_body_, Op::Label, {label});
    return label;
}

// ---------------------------------------------------------------------------
// Instructions
// ---------------------------------------------------------------------------

Id Builder::op_load(Id result_type, Id pointer) {
    auto id = next_id();
    emit(function_body_, Op::Load, {result_type, id, pointer});
    return id;
}

void Builder::op_store(Id pointer, Id value) {
    emit(function_body_, Op::Store, {pointer, value});
}

Id Builder::op_access_chain(Id result_type, Id base, std::vector<Id> indices) {
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(result_type);
    ops.push_back(id);
    ops.push_back(base);
    ops.insert(ops.end(), indices.begin(), indices.end());
    emit(function_body_, Op::AccessChain, ops);
    return id;
}

// Binary arithmetic.
#define BINARY_OP(name, opcode) \
    Id Builder::name(Id type, Id a, Id b) { \
        auto id = next_id(); \
        emit(function_body_, opcode, {type, id, a, b}); \
        return id; \
    }
BINARY_OP(op_fadd, Op::FAdd)
BINARY_OP(op_fsub, Op::FSub)
BINARY_OP(op_fmul, Op::FMul)
BINARY_OP(op_fdiv, Op::FDiv)
BINARY_OP(op_iadd, Op::IAdd)
BINARY_OP(op_isub, Op::ISub)
BINARY_OP(op_imul, Op::IMul)
BINARY_OP(op_bitwise_and, Op::BitwiseAnd)
BINARY_OP(op_bitwise_or, Op::BitwiseOr)
BINARY_OP(op_bitwise_xor, Op::BitwiseXor)
BINARY_OP(op_shift_left, Op::ShiftLeftLogical)
BINARY_OP(op_shift_right_logical, Op::ShiftRightLogical)
BINARY_OP(op_shift_right_arithmetic, Op::ShiftRightArithmetic)
#undef BINARY_OP

Id Builder::op_fnegate(Id type, Id a) {
    auto id = next_id();
    emit(function_body_, Op::FNegate, {type, id, a});
    return id;
}

Id Builder::op_not(Id type, Id a) {
    auto id = next_id();
    emit(function_body_, Op::Not, {type, id, a});
    return id;
}

// Comparison (float results are bool).
#define CMP_OP(name, opcode) \
    Id Builder::name(Id a, Id b) { \
        auto id = next_id(); \
        emit(function_body_, opcode, {type_bool(), id, a, b}); \
        return id; \
    }
CMP_OP(op_ford_equal, Op::FOrdEqual)
CMP_OP(op_ford_less_than, Op::FOrdLessThan)
CMP_OP(op_ford_greater_than, Op::FOrdGreaterThan)
CMP_OP(op_ford_less_than_equal, Op::FOrdLessThanEqual)
CMP_OP(op_ford_greater_than_equal, Op::FOrdGreaterThanEqual)
#undef CMP_OP

Id Builder::op_iequal(Id type, Id a, Id b) {
    auto id = next_id(); emit(function_body_, Op::IEqual, {type, id, a, b}); return id;
}
Id Builder::op_inotequal(Id type, Id a, Id b) {
    auto id = next_id(); emit(function_body_, Op::INotEqual, {type, id, a, b}); return id;
}
Id Builder::op_sless_than(Id type, Id a, Id b) {
    auto id = next_id(); emit(function_body_, Op::SLessThan, {type, id, a, b}); return id;
}

// Conversions.
#define CONV_OP(name, opcode) \
    Id Builder::name(Id type, Id value) { \
        auto id = next_id(); \
        emit(function_body_, opcode, {type, id, value}); \
        return id; \
    }
CONV_OP(op_convert_f_to_s, Op::ConvertFToS)
CONV_OP(op_convert_s_to_f, Op::ConvertSToF)
CONV_OP(op_convert_f_to_u, Op::ConvertFToU)
CONV_OP(op_convert_u_to_f, Op::ConvertUToF)
CONV_OP(op_bitcast, Op::Bitcast)
#undef CONV_OP

// Composite.
Id Builder::op_composite_construct(Id type, std::vector<Id> components) {
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(type);
    ops.push_back(id);
    ops.insert(ops.end(), components.begin(), components.end());
    emit(function_body_, Op::CompositeConstruct, ops);
    return id;
}

Id Builder::op_composite_extract(Id type, Id composite, std::uint32_t index) {
    auto id = next_id();
    emit(function_body_, Op::CompositeExtract, {type, id, composite, index});
    return id;
}

Id Builder::op_composite_insert(Id type, Id object, Id composite, std::uint32_t index) {
    auto id = next_id();
    emit(function_body_, Op::CompositeInsert, {type, id, object, composite, index});
    return id;
}

Id Builder::op_vector_shuffle(Id type, Id v1, Id v2, std::vector<std::uint32_t> components) {
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(type);
    ops.push_back(id);
    ops.push_back(v1);
    ops.push_back(v2);
    ops.insert(ops.end(), components.begin(), components.end());
    emit(function_body_, Op::VectorShuffle, ops);
    return id;
}

// Texture sampling.
Id Builder::op_image_sample_implicit(Id type, Id sampled_image, Id coord) {
    auto id = next_id();
    emit(function_body_, Op::ImageSampleImplicitLod, {type, id, sampled_image, coord});
    return id;
}

Id Builder::op_image_sample_explicit_lod(Id type, Id sampled_image, Id coord, Id lod) {
    auto id = next_id();
    // The Lod image operand mask is 0x2, followed by the lod value.
    emit(function_body_, Op::ImageSampleExplicitLod, {type, id, sampled_image, coord, 0x2u, lod});
    return id;
}

// Control flow.
void Builder::op_branch(Id label) {
    emit(function_body_, Op::Branch, {label});
}

void Builder::op_branch_conditional(Id condition, Id true_label, Id false_label) {
    emit(function_body_, Op::BranchConditional, {condition, true_label, false_label});
}

void Builder::op_selection_merge(Id merge_label) {
    // SelectionControl = None (0)
    emit(function_body_, Op::SelectionMerge, {merge_label, 0u});
}

void Builder::op_return() {
    emit(function_body_, Op::Return, {});
}

void Builder::op_return_value(Id value) {
    emit(function_body_, Op::ReturnValue, {value});
}

Id Builder::op_select(Id type, Id condition, Id true_val, Id false_val) {
    auto id = next_id();
    emit(function_body_, Op::Select, {type, id, condition, true_val, false_val});
    return id;
}

Id Builder::op_phi(Id type, std::vector<std::pair<Id, Id>> incoming) {
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(type);
    ops.push_back(id);
    for (auto& [val, block] : incoming) { ops.push_back(val); ops.push_back(block); }
    emit(function_body_, Op::Phi, ops);
    return id;
}

// Extended instructions.
Id Builder::ext_inst(Id type, Id ext_set, std::uint32_t instruction, std::vector<Id> operands) {
    auto id = next_id();
    std::vector<std::uint32_t> ops;
    ops.push_back(type);
    ops.push_back(id);
    ops.push_back(ext_set);
    ops.push_back(instruction);
    ops.insert(ops.end(), operands.begin(), operands.end());
    emit(function_body_, Op::ExtInst, ops);
    return id;
}

// ---------------------------------------------------------------------------
// Finalize
// ---------------------------------------------------------------------------

std::vector<std::uint32_t> Builder::finalize() {
    std::vector<std::uint32_t> module;
    // Reserve header (5 words).
    module.resize(5);

    // Section order per SPIR-V specification:
    // 1. Capabilities
    // 2. Extensions
    // 3. ExtInstImport
    // 4. MemoryModel
    // 5. EntryPoint
    // 6. ExecutionMode
    // 7. Debug (names)
    // 8. Annotations (decorations)
    // 9. Types, constants, global variables
    // 10. Function definitions
    auto append = [&](const std::vector<std::uint32_t>& section) {
        module.insert(module.end(), section.begin(), section.end());
    };
    append(capabilities_);
    append(extensions_);
    append(ext_imports_);
    append(memory_model_);
    append(entry_points_);
    append(execution_modes_);
    append(debug_names_);
    append(annotations_);
    append(type_const_);
    append(function_body_);

    // Header:
    // [0] Magic number
    // [1] Version (1.5 = 0x00010500)
    // [2] Generator magic (0 for custom)
    // [3] Bound (max ID + 1)
    // [4] Schema (0)
    module[0] = 0x07230203U;
    module[1] = 0x00010500U;
    module[2] = 0x00000000U;
    module[3] = next_id_;
    module[4] = 0;

    return module;
}

} // namespace gravity::hle::gnm::spirv
