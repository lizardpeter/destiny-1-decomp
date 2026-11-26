// Retail libc leaf import adapter: explicit allowlist, never a global symbol
// override. The host C runtime supplies the actual optimized implementations.
#include "pc_libc_leaf.h"
#include "pc_libc_wide.h"
#include "pc_libc_math.h"
#include "hle.h"
#include "nid.h"
#include <string>
#include <unordered_map>

namespace d1 {
void* native_libc_leaf(const std::string& nid, bool include_math) {
    using namespace pc_libc;
    static const std::unordered_map<std::string, void*> entries{
        {nid_of("memcpy"), guest_entry<&copy>()},
        {nid_of("memmove"), guest_entry<&move>()},
        {nid_of("memset"), guest_entry<&fill>()},
        {nid_of("memcmp"), guest_entry<&compare>()},
        {nid_of("memchr"), guest_entry<&find_byte>()},
        {nid_of("strlen"), guest_entry<&length>()},
        {nid_of("wcslen"), guest_entry<&wide_length16>()},
        {nid_of("strcmp"), guest_entry<&compare_string>()},
        {nid_of("strncmp"), guest_entry<&compare_prefix>()},
        {nid_of("strstr"), guest_entry<&find_string>()},
        {nid_of("strchr"), guest_entry<&find_char>()},
        {nid_of("strrchr"), guest_entry<&find_last_char>()},
        {nid_of("strcpy"), guest_entry<&copy_string>()},
        {nid_of("strncpy"), guest_entry<&copy_prefix>()},
        {nid_of("strcat"), guest_entry<&append>()},
        {nid_of("strncat"), guest_entry<&append_prefix>()},
        {nid_of("strspn"), guest_entry<&span>()},
        {nid_of("strcspn"), guest_entry<&reject_span>()},
        {nid_of("strpbrk"), guest_entry<&find_any>()},
        {nid_of("strlcpy"), guest_entry<&copy_bounded>()}
    };
    const auto it = entries.find(nid);
    if (it != entries.end()) return it->second;
    if (!include_math) return nullptr;
    // Math is opt-in until original-vs-host floating-point behavior and
    // the retail errno/exception environment have been compared in-game.
    static const std::unordered_map<std::string, void*> math{
        {nid_of("expf"), guest_entry<&exp_f>()},
        {nid_of("powf"), guest_entry<&pow_f>()},
        {nid_of("asinf"), guest_entry<&asin_f>()},
        {nid_of("acosf"), guest_entry<&acos_f>()},
        {nid_of("atan2f"), guest_entry<&atan2_f>()},
        {nid_of("lrintf"), guest_entry<&lrint_f>()},
        {nid_of("fmodf"), guest_entry<&fmod_f>()},
        {nid_of("tanf"), guest_entry<&tan_f>()},
        {nid_of("exp2f"), guest_entry<&exp2_f>()},
        {nid_of("modff"), guest_entry<&modf_f>()},
        {nid_of("frexpf"), guest_entry<&frexp_f>()},
        {nid_of("ldexpf"), guest_entry<&ldexp_f>()},
        {nid_of("atanf"), guest_entry<&atan_f>()},
        {nid_of("fmod"), guest_entry<&fmod_d>()},
        {nid_of("pow"), guest_entry<&pow_d>()},
        {nid_of("exp2"), guest_entry<&exp2_d>()},
        {nid_of("tan"), guest_entry<&tan_d>()},
        {nid_of("difftime"), guest_entry<&diff_time>()}
    };
    const auto mi = math.find(nid);
    return mi == math.end() ? nullptr : mi->second;
}
} // namespace d1
