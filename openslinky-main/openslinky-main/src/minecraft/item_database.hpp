#pragma once

#include <cstddef>
#include <cstdint>

namespace slinky {

namespace minecraft {

constexpr size_t kAllItemNameCount  = 385;
constexpr size_t kPureItemNameCount = 187;
constexpr size_t kAllBlockNameCount = 198;

// Exact 16-byte item/block lookup table entry (@ 0x180302C20 & 0x180304430)
struct ItemIdEntry {
    const char* name = nullptr;
    int32_t id = 0;
    int32_t pad = 0;
};
static_assert(sizeof(ItemIdEntry) == 16, "ItemIdEntry must match exact 16-byte binary layout");

extern const char* const kAllItemNames[kAllItemNameCount];
extern const ItemIdEntry kItemsSortedByName[kAllItemNameCount];
extern const ItemIdEntry kItemsSortedById[kAllItemNameCount];

// Returns all 385 item & block display names (@ 0x180302010)
const char* const* get_all_item_names(const char* const** out_names, size_t* out_count) noexcept; // sub_18002F650

// Returns all 198 block display names (@ 0x1803025E8 = &kAllItemNames[187])
const char* const* get_all_block_names(const char* const** out_names, size_t* out_count) noexcept; // sub_18002F670

// Binary-searches kItemsSortedByName for a display name; returns numeric ID or -1 if not found
int find_item_id_by_name(const char* name) noexcept; // sub_18002F690

// bsearch comparator for kItemsSortedByName
int compare_item_entry_by_name(const void* key, const void* elem) noexcept; // sub_18002F6E0

// Binary-searches kItemsSortedById for a numeric ID; returns display name or nullptr if not found
const char* find_item_name_by_id(int id) noexcept; // sub_18002F6F0

// bsearch comparator for kItemsSortedById
int compare_item_entry_by_id(const void* key, const void* elem) noexcept; // sub_18002F740

} // namespace minecraft

namespace mc {
using namespace ::slinky::minecraft;
} // namespace mc

} // namespace slinky
