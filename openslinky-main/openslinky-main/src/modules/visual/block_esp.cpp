#include "modules/visual/block_esp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emmintrin.h>
#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

// Unit cube corner offsets (@ 0x180170730) & 24 GL_LINES indices (@ 0x180170790)
constexpr float kCubeVertexOffsets[8][3] = {
    {0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 1.0f, 1.0f},
    {1.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 1.0f},
    {1.0f, 1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f},
};

constexpr uint16_t kCubeLineIndices[24] = {
    0, 1, 1, 5, 5, 4, 4, 0,
    2, 3, 3, 7, 7, 6, 6, 2,
    0, 2, 1, 3, 4, 6, 5, 7,
};

// 24-byte allowed block entry (@ 0x18031F300)
struct AllowedBlockEntry {
    const char* name = nullptr; // +0x00
    math::Color3f color{};      // +0x08
    int32_t block_id = 0;       // +0x14
};
static_assert(sizeof(AllowedBlockEntry) == 0x18, "AllowedBlockEntry must be 24 bytes");

struct AllowedBlockList {
    size_t count = 0;                 // +0x00
    size_t capacity = 0;              // +0x08
    AllowedBlockEntry* data = nullptr;// +0x10
};

struct BlockRecord {
    uint8_t local_x = 0;                    // +0x00
    uint8_t y = 0;                          // +0x01
    uint8_t local_z = 0;                    // +0x02
    uint8_t pad[5]{};                       // +0x03..+0x07
    const AllowedBlockEntry* entry = nullptr;// +0x08
};
static_assert(sizeof(BlockRecord) == 0x10, "BlockRecord must be 16 bytes");

struct ColoredVertex {
    float x = 0.0f;  // +0x00
    float y = 0.0f;  // +0x04
    float z = 0.0f;  // +0x08
    uint8_t r = 0;   // +0x0C
    uint8_t g = 0;   // +0x0D
    uint8_t b = 0;   // +0x0E
    uint8_t a = 255; // +0x0F
};
static_assert(sizeof(ColoredVertex) == 0x10, "ColoredVertex must be 16 bytes");

// 80-byte per-chunk cached geometry (@ 0x18031F2E0)
struct ChunkMeshCache {
    int32_t chunk_x = 0;              // +0x00
    int32_t chunk_z = 0;              // +0x04
    size_t block_count = 0;           // +0x08
    size_t block_cap = 0;             // +0x10
    BlockRecord* blocks = nullptr;    // +0x18
    size_t vertex_count = 0;          // +0x20
    size_t vertex_cap = 0;            // +0x28
    ColoredVertex* vertices = nullptr;// +0x30
    size_t index_count = 0;           // +0x38
    size_t index_cap = 0;             // +0x40
    uint16_t* indices = nullptr;      // +0x48
};
static_assert(sizeof(ChunkMeshCache) == 0x50, "ChunkMeshCache must be 80 bytes");

using SectionScanFn = void (*)(
    int32_t chunk_x,
    int32_t section_y,
    int32_t chunk_z,
    const void* raw_storage,
    ChunkMeshCache** inout_chunk) noexcept;

size_t s_chunk_count = 0;                 // xmmword_18031F2D0.low
size_t s_chunk_cap = 0;                   // xmmword_18031F2D0.high (@ 0x31F2D8)
ChunkMeshCache* s_chunks = nullptr;       // qword_18031F2E0
bool s_rescan_all_chunks = false;         // byte_18031F2E8
AllowedBlockList s_allowed_block_list{};  // xmmword_18031F2F0..0x31F300
SectionScanFn s_section_scanner = nullptr;// qword_18031F308

void scan_section_1_7(
    int32_t chunk_x,
    int32_t section_y,
    int32_t chunk_z,
    const void* raw_storage,
    ChunkMeshCache** inout_chunk) noexcept;

void scan_section_1_8(
    int32_t chunk_x,
    int32_t section_y,
    int32_t chunk_z,
    const void* raw_storage,
    ChunkMeshCache** inout_chunk) noexcept;

// Matches sub_1800617F0
int compare_block_id_key(const void* key, const void* elem) noexcept {
    const int32_t id = *static_cast<const int32_t*>(key);
    const auto* entry = static_cast<const AllowedBlockEntry*>(elem);
    return id - entry->block_id;
}

// Matches sub_180061D40
int compare_chunk_coords(const void* a, const void* b) noexcept {
    const auto* ca = static_cast<const ChunkMeshCache*>(a);
    const auto* cb = static_cast<const ChunkMeshCache*>(b);
    if (ca->chunk_x != cb->chunk_x) {
        return ca->chunk_x - cb->chunk_x;
    }
    return ca->chunk_z - cb->chunk_z;
}

const AllowedBlockEntry* find_allowed_block(int32_t block_id) noexcept {
    if (s_allowed_block_list.data == nullptr || s_allowed_block_list.count == 0) {
        return nullptr;
    }
    return static_cast<const AllowedBlockEntry*>(std::bsearch(
        &block_id,
        s_allowed_block_list.data,
        s_allowed_block_list.count,
        sizeof(AllowedBlockEntry),
        &compare_block_id_key));
}

// Matches sub_180061060
void free_chunk_mesh(ChunkMeshCache* chunk) noexcept {
    chunk->block_count = 0;
    chunk->block_cap = 0;
    std::free(chunk->blocks);
    chunk->blocks = nullptr;

    chunk->vertex_count = 0;
    chunk->vertex_cap = 0;
    std::free(chunk->vertices);
    chunk->vertices = nullptr;

    chunk->index_count = 0;
    chunk->index_cap = 0;
    std::free(chunk->indices);
    chunk->indices = nullptr;
}

// Matches sub_180060800
void clear_all_cached_chunks() noexcept {
    for (size_t i = 0; i < s_chunk_count; ++i) {
        free_chunk_mesh(&s_chunks[i]);
    }
    s_chunk_count = 0;
    s_chunk_cap = 0;
    std::free(s_chunks);
    s_chunks = nullptr;
}

// Matches sub_1800610C0
void rebuild_chunk_mesh(ChunkMeshCache* chunk) noexcept {
    chunk->vertex_count = 0;
    chunk->index_count = 0;
    if (chunk->block_count == 0) {
        return;
    }

    for (size_t i = 0; i < chunk->block_count; ++i) {
        const BlockRecord& rec = chunk->blocks[i];
        const AllowedBlockEntry* entry = rec.entry;
        const uint8_t r = static_cast<uint8_t>(static_cast<int32_t>(entry->color.r * 255.0f));
        const uint8_t g = static_cast<uint8_t>(static_cast<int32_t>(entry->color.g * 255.0f));
        const uint8_t b = static_cast<uint8_t>(static_cast<int32_t>(entry->color.b * 255.0f));

        const uint16_t base_vtx = static_cast<uint16_t>(chunk->vertex_count);
        for (size_t v = 0; v < 8; ++v) {
            if (!util::ensure_array_capacity(
                    reinterpret_cast<void**>(&chunk->vertices),
                    sizeof(ColoredVertex),
                    chunk->vertex_count,
                    &chunk->vertex_cap)) {
                return;
            }
            ColoredVertex& dst = chunk->vertices[chunk->vertex_count++];
            dst.x = static_cast<float>(rec.local_x) + kCubeVertexOffsets[v][0];
            dst.y = static_cast<float>(rec.y) + kCubeVertexOffsets[v][1];
            dst.z = static_cast<float>(rec.local_z) + kCubeVertexOffsets[v][2];
            dst.r = r;
            dst.g = g;
            dst.b = b;
            dst.a = 0xFFu;
        }

        for (size_t idx = 0; idx < 24; ++idx) {
            if (!util::ensure_array_capacity(
                    reinterpret_cast<void**>(&chunk->indices),
                    sizeof(uint16_t),
                    chunk->index_count,
                    &chunk->index_cap)) {
                return;
            }
            chunk->indices[chunk->index_count++] = static_cast<uint16_t>(base_vtx + kCubeLineIndices[idx]);
        }
    }
}

// Matches sub_180062070
ChunkMeshCache* find_or_insert_chunk(int32_t chunk_x, int32_t chunk_z) noexcept {
    ChunkMeshCache key{};
    key.chunk_x = chunk_x;
    key.chunk_z = chunk_z;

    if (s_chunks != nullptr && s_chunk_count > 0) {
        auto* existing = static_cast<ChunkMeshCache*>(
            std::bsearch(&key, s_chunks, s_chunk_count, sizeof(ChunkMeshCache), &compare_chunk_coords));
        if (existing != nullptr) {
            return existing;
        }
    }

    size_t insert_idx = 0;
    while (insert_idx < s_chunk_count) {
        const int cmp = (chunk_x != s_chunks[insert_idx].chunk_x)
            ? (chunk_x - s_chunks[insert_idx].chunk_x)
            : (chunk_z - s_chunks[insert_idx].chunk_z);
        if (cmp <= 0) {
            break;
        }
        ++insert_idx;
    }

    if (!util::ensure_array_capacity(
            reinterpret_cast<void**>(&s_chunks),
            sizeof(ChunkMeshCache),
            s_chunk_count,
            &s_chunk_cap)) {
        return nullptr;
    }

    std::memmove(
        &s_chunks[insert_idx + 1],
        &s_chunks[insert_idx],
        (s_chunk_count - insert_idx) * sizeof(ChunkMeshCache));

    ChunkMeshCache& dst = s_chunks[insert_idx];
    dst = {};
    dst.chunk_x = chunk_x;
    dst.chunk_z = chunk_z;
    ++s_chunk_count;
    return &dst;
}

bool append_block_record(
    int32_t chunk_x,
    int32_t chunk_z,
    uint8_t local_x,
    uint8_t y,
    uint8_t local_z,
    const AllowedBlockEntry* entry,
    ChunkMeshCache** inout_chunk) noexcept {
    ChunkMeshCache* chunk = *inout_chunk;
    if (chunk == nullptr) {
        chunk = find_or_insert_chunk(chunk_x, chunk_z);
        *inout_chunk = chunk;
        if (chunk == nullptr) {
            return false;
        }
    }

    if (!util::ensure_array_capacity(
            reinterpret_cast<void**>(&chunk->blocks),
            sizeof(BlockRecord),
            chunk->block_count,
            &chunk->block_cap)) {
        return false;
    }

    BlockRecord& rec = chunk->blocks[chunk->block_count++];
    rec = {};
    rec.local_x = local_x;
    rec.y = y;
    rec.local_z = local_z;
    rec.entry = entry;
    return true;
}

// Matches sub_180061D60 (1.7.10 8-bit block ID array)
void scan_section_1_7(
    int32_t chunk_x,
    int32_t section_y,
    int32_t chunk_z,
    const void* raw_storage,
    ChunkMeshCache** inout_chunk) noexcept {
    if (s_allowed_block_list.count == 0 || raw_storage == nullptr) {
        return;
    }

    const auto* bytes = static_cast<const uint8_t*>(raw_storage);
    const int32_t base_y = section_y << 4;

    for (size_t b = 0; b < s_allowed_block_list.count; ++b) {
        const AllowedBlockEntry* entry = &s_allowed_block_list.data[b];
        const __m128i needle = _mm_set1_epi8(static_cast<char>(entry->block_id));

        for (uint32_t row = 0; row < 256u; ++row) {
            const __m128i vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes + (row << 4)));
            const int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(vec, needle));
            if (mask == 0) {
                continue;
            }

            const uint8_t y = static_cast<uint8_t>((row >> 4) + static_cast<uint32_t>(base_y));
            const uint8_t local_z = static_cast<uint8_t>(row & 0xFu);
            for (uint32_t lx = 0; lx < 16u; ++lx) {
                if ((mask & (1 << lx)) != 0) {
                    if (!append_block_record(
                            chunk_x, chunk_z, static_cast<uint8_t>(lx), y, local_z, entry, inout_chunk)) {
                        return;
                    }
                }
            }
        }
    }
}

// Matches sub_180061ED0 (1.8.9 16-bit block state array: state >> 4 == block_id)
void scan_section_1_8(
    int32_t chunk_x,
    int32_t section_y,
    int32_t chunk_z,
    const void* raw_storage,
    ChunkMeshCache** inout_chunk) noexcept {
    if (s_allowed_block_list.count == 0 || raw_storage == nullptr) {
        return;
    }

    const auto* bytes = static_cast<const uint8_t*>(raw_storage);
    const int32_t base_y = section_y << 4;

    for (size_t b = 0; b < s_allowed_block_list.count; ++b) {
        const AllowedBlockEntry* entry = &s_allowed_block_list.data[b];
        const __m128i needle = _mm_set1_epi16(static_cast<int16_t>(entry->block_id));

        for (uint32_t half_row = 0; half_row < 512u; ++half_row) {
            const __m128i raw_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes + (half_row << 4)));
            const __m128i ids_vec = _mm_srai_epi16(raw_vec, 4);
            const int mask = _mm_movemask_epi8(_mm_cmpeq_epi16(ids_vec, needle));
            if (mask == 0) {
                continue;
            }

            const uint8_t base_x = static_cast<uint8_t>((half_row & 1u) << 3);
            const uint8_t y = static_cast<uint8_t>((half_row >> 5) + static_cast<uint32_t>(base_y));
            const uint8_t local_z = static_cast<uint8_t>((half_row >> 1) & 0xFu);

            for (uint32_t lane = 0; lane < 8u; ++lane) {
                if (((mask >> (lane * 2u)) & 0x3) != 0) {
                    if (!append_block_record(
                            chunk_x,
                            chunk_z,
                            static_cast<uint8_t>(base_x + lane),
                            y,
                            local_z,
                            entry,
                            inout_chunk)) {
                        return;
                    }
                }
            }
        }
    }
}

// Matches sub_180061800
void handle_block_or_chunk_update(
    events::JniContext* jni,
    jobject world,
    int32_t update_kind,
    int32_t x,
    int32_t y_or_cz,
    int32_t z,
    int32_t block_id) noexcept {
    if (static_cast<uint32_t>(update_kind) < 2u) {
        const int32_t chunk_x = x;
        const int32_t chunk_z = y_or_cz;

        ChunkMeshCache key{};
        key.chunk_x = chunk_x;
        key.chunk_z = chunk_z;

        if (s_chunks != nullptr && s_chunk_count > 0) {
            auto* found = static_cast<ChunkMeshCache*>(
                std::bsearch(&key, s_chunks, s_chunk_count, sizeof(ChunkMeshCache), &compare_chunk_coords));
            if (found != nullptr) {
                free_chunk_mesh(found);
                const size_t idx = static_cast<size_t>(found - s_chunks);
                --s_chunk_count;
                std::memmove(found, found + 1, (s_chunk_count - idx) * sizeof(ChunkMeshCache));
            }
        }

        if (update_kind != 1) {
            return;
        }

        jobject chunk = minecraft::world_get_chunk_from_chunk_coords(jni, world, chunk_x, chunk_z);
        if (chunk == nullptr || s_section_scanner == nullptr) {
            return;
        }

        ChunkMeshCache* created_chunk = nullptr;
        JNIEnv* env = jni->env;
        jobjectArray storages = minecraft::chunk_get_storage_arrays(jni, chunk);
        for (int32_t sec_y = 0; sec_y < 16; ++sec_y) {
            jobject ebs = env->GetObjectArrayElement(storages, sec_y);
            if (ebs == nullptr) {
                continue;
            }
            auto data_arr = static_cast<jarray>(minecraft::extended_block_storage_get_data(jni, ebs));
            void* raw_ptr = env->GetPrimitiveArrayCritical(data_arr, nullptr);
            s_section_scanner(chunk_x, sec_y, chunk_z, raw_ptr, &created_chunk);
            env->ReleasePrimitiveArrayCritical(data_arr, raw_ptr, 0);
        }

        if (created_chunk != nullptr) {
            rebuild_chunk_mesh(created_chunk);
        }
        return;
    }

    if (update_kind != 2) {
        return;
    }

    const AllowedBlockEntry* matched_entry = find_allowed_block(block_id);
    const int32_t chunk_x = math::block_to_chunk_coord(x);
    const int32_t chunk_z = math::block_to_chunk_coord(z);

    ChunkMeshCache key{};
    key.chunk_x = chunk_x;
    key.chunk_z = chunk_z;

    ChunkMeshCache* chunk = nullptr;
    if (s_chunks != nullptr && s_chunk_count > 0) {
        chunk = static_cast<ChunkMeshCache*>(
            std::bsearch(&key, s_chunks, s_chunk_count, sizeof(ChunkMeshCache), &compare_chunk_coords));
    }

    const uint8_t local_x = static_cast<uint8_t>(x - math::chunk_to_block_coord(chunk_x));
    const uint8_t local_z = static_cast<uint8_t>(z - math::chunk_to_block_coord(chunk_z));
    const uint8_t y = static_cast<uint8_t>(y_or_cz);

    if (chunk != nullptr) {
        for (size_t i = 0; i < chunk->block_count; ++i) {
            BlockRecord& rec = chunk->blocks[i];
            if (rec.local_x == local_x && rec.y == y && rec.local_z == local_z) {
                if (matched_entry != nullptr) {
                    rec.entry = matched_entry;
                    rebuild_chunk_mesh(chunk);
                } else {
                    --chunk->block_count;
                    std::memmove(
                        &chunk->blocks[i],
                        &chunk->blocks[i + 1],
                        (chunk->block_count - i) * sizeof(BlockRecord));
                    if (chunk->block_count == 0) {
                        free_chunk_mesh(chunk);
                        const size_t cidx = static_cast<size_t>(chunk - s_chunks);
                        --s_chunk_count;
                        std::memmove(chunk, chunk + 1, (s_chunk_count - cidx) * sizeof(ChunkMeshCache));
                    } else {
                        rebuild_chunk_mesh(chunk);
                    }
                }
                return;
            }
        }

        if (matched_entry == nullptr) {
            return;
        }
    } else {
        if (matched_entry == nullptr) {
            return;
        }
        chunk = find_or_insert_chunk(chunk_x, chunk_z);
        if (chunk == nullptr) {
            return;
        }
    }

    if (util::ensure_array_capacity(
            reinterpret_cast<void**>(&chunk->blocks),
            sizeof(BlockRecord),
            chunk->block_count,
            &chunk->block_cap)) {
        BlockRecord& rec = chunk->blocks[chunk->block_count++];
        rec = {};
        rec.local_x = local_x;
        rec.y = y;
        rec.local_z = local_z;
        rec.entry = matched_entry;
        rebuild_chunk_mesh(chunk);
    }
}

// Matches sub_1800612B0
void scan_all_loaded_chunks(events::GameContext* ctx) noexcept {
    jobject provider = minecraft::world_get_chunk_provider_client(ctx->jni, ctx->world);
    if (provider == nullptr) {
        return;
    }

    jobject chunk_list = minecraft::chunk_provider_get_chunk_listing(ctx->jni, provider);
    const int32_t count = minecraft::java_list_size(ctx->jni, chunk_list);
    for (int32_t i = 0; i < count; ++i) {
        jobject chunk = minecraft::java_list_get(ctx->jni, chunk_list, i);
        const int32_t cx = minecraft::chunk_get_x(ctx->jni, chunk);
        const int32_t cz = minecraft::chunk_get_z(ctx->jni, chunk);
        handle_block_or_chunk_update(ctx->jni, ctx->world, 1, cx, cz, 0, 0);
    }
}

// DynamicListSetting callbacks (0x61390..0x61720)
size_t validate_block_search_char(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    if (len > 99) {
        return len;
    }
    const uint8_t upper = static_cast<uint8_t>((static_cast<uint8_t>(ch) & 0xDFu) - 'A');
    const uint8_t digit = static_cast<uint8_t>(static_cast<uint8_t>(ch) - '0');
    if (upper < 26u || digit < 10u || ch == ' ') {
        buffer[len] = ch;
    }
    return len;
}

size_t block_list_get_count(void* ctx) noexcept {
    return static_cast<const AllowedBlockList*>(ctx)->count;
}

const char* block_list_get_name(void* ctx, size_t index) noexcept {
    return static_cast<const AllowedBlockList*>(ctx)->data[index].name;
}

math::Color3f* block_list_get_color(void* ctx, size_t index) noexcept {
    return &static_cast<AllowedBlockList*>(ctx)->data[index].color;
}

bool block_list_add_item(void* ctx, const char* name, const math::Color3f* color) noexcept {
    auto* list = static_cast<AllowedBlockList*>(ctx);
    const int32_t id = minecraft::find_item_id_by_name(name);
    if (id == -1) {
        return false;
    }
    if (find_allowed_block(id) != nullptr) {
        return false;
    }

    size_t insert_idx = 0;
    while (insert_idx < list->count && id > list->data[insert_idx].block_id) {
        ++insert_idx;
    }

    const char* canonical_name = minecraft::find_item_name_by_id(id);
    const math::Color3f saved_color = *color;

    clear_all_cached_chunks();
    s_rescan_all_chunks = true;

    if (!util::ensure_array_capacity(
            reinterpret_cast<void**>(&list->data),
            sizeof(AllowedBlockEntry),
            list->count,
            &list->capacity)) {
        return false;
    }

    std::memmove(
        &list->data[insert_idx + 1],
        &list->data[insert_idx],
        (list->count - insert_idx) * sizeof(AllowedBlockEntry));

    list->data[insert_idx].name = canonical_name;
    list->data[insert_idx].color = saved_color;
    list->data[insert_idx].block_id = id;
    ++list->count;
    return true;
}

void block_list_remove_at(void* ctx, size_t index) noexcept {
    auto* list = static_cast<AllowedBlockList*>(ctx);
    --list->count;
    std::memmove(
        &list->data[index],
        &list->data[index + 1],
        (list->count - index) * sizeof(AllowedBlockEntry));

    clear_all_cached_chunks();
    s_rescan_all_chunks = true;
}

void block_list_clear_all(void* ctx) noexcept {
    auto* list = static_cast<AllowedBlockList*>(ctx);
    list->count = 0;
    list->capacity = 0;
    std::free(list->data);
    list->data = nullptr;

    clear_all_cached_chunks();
    s_rescan_all_chunks = true;
}

void block_list_get_options(const char* const** out_items, size_t* out_count) noexcept {
    minecraft::get_all_block_names(out_items, out_count);
}

config::FloatSetting s_range{
    {}, "Range", "%0.0f chunk(s)", 1.0f, 0.0f, 1.0f, 10.0f, 2.0f, 0.0f
};
config::DynamicListSetting s_allowed_blocks{
    "Block types",
    "Search block",
    &validate_block_search_char,
    config::DynamicListItemKind::ColoredEntry,
    0,
    &s_allowed_block_list,
    &block_list_get_count,
    &block_list_get_name,
    &block_list_get_color,
    &block_list_add_item,
    &block_list_remove_at,
    &block_list_clear_all,
    &block_list_get_options,
    false
};
config::BoolSetting s_should_limit_block_render{{}, "Enabled", false, false};
config::FloatSetting s_block_render_limit{
    {}, "Limit per chunk section", "%0.0f block(s)", 1.0f, 0.0f, 1.0f, 100.0f, 100.0f, 0.0f
};
config::FloatSetting s_line_width{
    {}, "Line width", "%0.0f px", 1.0f, 0.0f, 1.0f, 4.0f, 1.0f, 0.0f
};

// Matches sub_1800608C0
void on_game_tick(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    if (ctx->in_game_focus && s_allowed_blocks.modified) {
        for (size_t i = 0; i < s_chunk_count; ++i) {
            rebuild_chunk_mesh(&s_chunks[i]);
        }
        s_allowed_blocks.modified = false;
    }

    if (ctx->world != nullptr) {
        (void)minecraft::java_object_hash_code(ctx->jni, ctx->world);
        if (s_rescan_all_chunks) {
            clear_all_cached_chunks();
            scan_all_loaded_chunks(ctx);
            s_rescan_all_chunks = false;
        }
    } else {
        clear_all_cached_chunks();
    }
}

// Matches sub_180060AA0
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    if (ev->module != get_block_esp_module()) {
        return;
    }

    if (ev->enabled) {
        events::GameContext* ctx = ev->ctx;
        s_section_scanner = minecraft::is_version_1_7_10(ctx->jni)
            ? &scan_section_1_7
            : &scan_section_1_8;
        if (ctx->world != nullptr) {
            scan_all_loaded_chunks(ctx);
        }
    } else {
        clear_all_cached_chunks();
    }
}

// Matches sub_180060BB0
void on_render_world_3d(void* raw_event) noexcept {
    if (s_chunk_count == 0) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld3DEvent*>(raw_event);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glLineWidth(s_line_width.value);

    const size_t max_indices = s_should_limit_block_render.value
        ? static_cast<size_t>(s_block_render_limit.value * 24.0f)
        : 4096u;

    const double cam_cx = ev->render_pos.x * 0.0625;
    const double cam_cz = ev->render_pos.z * 0.0625;

    for (size_t i = 0; i < s_chunk_count; ++i) {
        const ChunkMeshCache& chunk = s_chunks[i];
        const double dist_chunks = std::hypot(
            cam_cx - (static_cast<double>(chunk.chunk_x) + 0.5),
            cam_cz - (static_cast<double>(chunk.chunk_z) + 0.5));
        if (dist_chunks > static_cast<double>(s_range.value)) {
            continue;
        }

        glPushMatrix();
        const math::Vec3d chunk_origin{
            static_cast<double>(chunk.chunk_x * 16),
            0.0,
            static_cast<double>(chunk.chunk_z * 16),
        };
        const math::Vec3d rel = math::vec3_sub(chunk_origin, ev->render_pos);
        glTranslatef(
            static_cast<float>(rel.x),
            static_cast<float>(rel.y),
            static_cast<float>(rel.z));

        glVertexPointer(3, GL_FLOAT, sizeof(ColoredVertex), chunk.vertices);
        glColorPointer(3, GL_UNSIGNED_BYTE, sizeof(ColoredVertex), &chunk.vertices->r);

        const GLsizei draw_count = static_cast<GLsizei>(std::min(chunk.index_count, max_indices));
        glDrawElements(GL_LINES, draw_count, GL_UNSIGNED_SHORT, chunk.indices);
        glPopMatrix();
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

// Matches sub_180060E40
void on_world_block_update(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::WorldBlockUpdateEvent*>(raw_event);
    handle_block_or_chunk_update(
        ev->jni, ev->world, ev->update_kind, ev->x, ev->y_or_cz, ev->z, ev->block_id);
}

// Matches sub_180060E50
void on_client_shutdown(void* /*raw_event*/) noexcept {
    s_allowed_block_list.count = 0;
    s_allowed_block_list.capacity = 0;
    std::free(s_allowed_block_list.data);
    s_allowed_block_list.data = nullptr;
    clear_all_cached_chunks();
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_range.render(gui, renderer);
    s_allowed_blocks.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Block render limit");
    s_should_limit_block_render.render(gui, renderer);
    if (s_should_limit_block_render.value) {
        s_block_render_limit.render(gui, renderer);
    }
    gui::widgets::render_section_header(gui, renderer, "Customization");
    s_line_width.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_range.visit(ar, "range");
    s_allowed_blocks.visit(ar, "allowed_blocks");
    s_should_limit_block_render.visit(ar, "should_limit_block_render");
    s_block_render_limit.visit(ar, "block_render_limit");
    s_line_width.visit(ar, "line_width");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Block ESP";
    mod.description = "Highlights blocks.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventGameTick] = {&on_game_tick, 0};
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventRenderWorld3D] = {&on_render_world_3d, -200};
    mod.events[events::kEventWorldBlockUpdate] = {&on_world_block_update, 0};
    mod.events[events::kEventClientShutdown] = {&on_client_shutdown, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_block_esp_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
