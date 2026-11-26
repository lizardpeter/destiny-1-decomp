// Retail libc leaf import adapter: explicit allowlist, never a global symbol
// override. The host C runtime supplies the actual optimized implementations.
#include "pc_libc_leaf.h"
#include "hle.h"
#include "nid.h"
#include <string>
#include <unordered_map>

namespace d1 {
void* native_libc_leaf(const std::string& nid) {
    using namespace pc_libc;
    static const std::unordered_map<std::string, void*> entries{
        {nid_of("memcpy"), guest_entry<&copy>()},
        {nid_of("memmove"), guest_entry<&move>()},
        {nid_of("memset"), guest_entry<&fill>()},
        {nid_of("memcmp"), guest_entry<&compare>()},
        {nid_of("memchr"), guest_entry<&find_byte>()},
        {nid_of("strlen"), guest_entry<&length>()},
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
    return it == entries.end() ? nullptr : it->second;
}
} // namespace d1
