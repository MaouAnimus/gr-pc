#include "hle/gnm/spirv_validation.h"
#include <spirv-tools/libspirv.h>

namespace gravity::hle::gnm {
bool validate_spirv(std::span<const std::uint32_t> words, std::string& error) {
    error.clear();
    if (words.size() < 5) { error = "Truncated SPIR-V module"; return false; }
    const auto context = spvContextCreate(SPV_ENV_VULKAN_1_2);
    if (!context) { error = "SPIRV-Tools context unavailable"; return false; }
    spv_diagnostic diagnostic{};
    const auto result = spvValidateBinary(context, words.data(), words.size(), &diagnostic);
    if (result != SPV_SUCCESS) {
        error = diagnostic && diagnostic->error ? diagnostic->error : "Invalid SPIR-V for Vulkan 1.2";
    }
    spvDiagnosticDestroy(diagnostic);
    spvContextDestroy(context);
    return result == SPV_SUCCESS;
}
}
