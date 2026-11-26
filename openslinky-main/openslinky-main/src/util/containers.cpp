#include "util/containers.hpp"

#include <cstdlib>
#include <cstring>

namespace slinky::util {

namespace {

int compare_bucket_key(const void* key_ptr, const void* elem_ptr) noexcept {
    const auto* a = static_cast<const KeybindBucket*>(key_ptr);
    const auto* b = static_cast<const KeybindBucket*>(elem_ptr);
    return a->key_code - b->key_code;
}

} // namespace

KeybindBucket* KeybindMap::find_bucket(int key_code) const noexcept {
    if (key_code == 0 || buckets == nullptr || bucket_count == 0) {
        return nullptr;
    }
    KeybindBucket key{};
    key.key_code = key_code;
    return static_cast<KeybindBucket*>(
        std::bsearch(&key, buckets, bucket_count, sizeof(KeybindBucket), compare_bucket_key));
}

bool KeybindMap::bind(int key_code, void* target) noexcept {
    if (key_code == 0) {
        return true;
    }

    KeybindBucket* bucket = find_bucket(key_code);
    if (bucket == nullptr) {
        size_t insert_idx = 0;
        while (insert_idx < bucket_count && key_code > buckets[insert_idx].key_code) {
            ++insert_idx;
        }
        if (!ensure_array_capacity(reinterpret_cast<void**>(&buckets), sizeof(KeybindBucket), bucket_count, &bucket_capacity)) {
            return false;
        }
        std::memmove(
            buckets + insert_idx + 1,
            buckets + insert_idx,
            sizeof(KeybindBucket) * (bucket_count - insert_idx));
        buckets[insert_idx] = KeybindBucket{};
        buckets[insert_idx].key_code = key_code;
        ++bucket_count;
        bucket = &buckets[insert_idx];
    }

    if (!allow_multiple) {
        if (bucket->count > 0) {
            if (on_free_target != nullptr && bucket->targets != nullptr) {
                on_free_target(bucket->targets[0]);
            }
            std::free(bucket->targets);
            bucket->targets = nullptr;
            bucket->count = 0;
            bucket->capacity = 0;
        }
    } else {
        for (size_t i = 0; i < bucket->count; ++i) {
            if (bucket->targets[i] == target) {
                return true;
            }
        }
    }

    if (!ensure_array_capacity(reinterpret_cast<void**>(&bucket->targets), sizeof(void*), bucket->count, &bucket->capacity)) {
        return false;
    }
    bucket->targets[bucket->count++] = target;
    return true;
}

void KeybindMap::unbind(int key_code, void* target) noexcept {
    KeybindBucket* bucket = find_bucket(key_code);
    if (bucket == nullptr || bucket->count == 0) {
        return;
    }

    if (!allow_multiple) {
        if (on_free_target != nullptr && bucket->targets != nullptr) {
            on_free_target(bucket->targets[0]);
        }
    } else {
        size_t found_idx = bucket->count;
        for (size_t i = 0; i < bucket->count; ++i) {
            if (bucket->targets[i] == target) {
                found_idx = i;
                break;
            }
        }
        if (found_idx == bucket->count) {
            return;
        }
        if (on_free_target != nullptr) {
            on_free_target(bucket->targets[found_idx]);
        }
        --bucket->count;
        std::memmove(
            bucket->targets + found_idx,
            bucket->targets + found_idx + 1,
            sizeof(void*) * (bucket->count - found_idx));
        if (bucket->count > 0) {
            return;
        }
    }

    std::free(bucket->targets);
    bucket->targets = nullptr;
    bucket->count = 0;
    bucket->capacity = 0;

    const size_t bucket_idx = static_cast<size_t>(bucket - buckets);
    --bucket_count;
    std::memmove(
        buckets + bucket_idx,
        buckets + bucket_idx + 1,
        sizeof(KeybindBucket) * (bucket_count - bucket_idx));
}

void* KeybindMap::find(int key_code, void* target) const noexcept {
    KeybindBucket* bucket = find_bucket(key_code);
    if (bucket == nullptr || bucket->count == 0) {
        return nullptr;
    }
    if (!allow_multiple) {
        return bucket->targets[0];
    }
    for (size_t i = 0; i < bucket->count; ++i) {
        if (bucket->targets[i] == target) {
            return target;
        }
    }
    return nullptr;
}

bool KeybindMap::trigger(uint32_t key_code, uint32_t pressed) noexcept {
    KeybindBucket* bucket = find_bucket(static_cast<int>(key_code));
    if (bucket == nullptr || bucket->count == 0 || on_trigger == nullptr) {
        return false;
    }
    bool handled = false;
    for (size_t i = 0; i < bucket->count; ++i) {
        handled = on_trigger(bucket->targets[i], key_code, pressed) || handled;
    }
    return handled;
}

void KeybindMap::release_all() noexcept {
    if (on_trigger == nullptr) {
        return;
    }
    for (size_t b = 0; b < bucket_count; ++b) {
        KeybindBucket& bucket = buckets[b];
        for (size_t i = 0; i < bucket.count; ++i) {
            on_trigger(bucket.targets[i], static_cast<uint32_t>(bucket.key_code), 0);
        }
    }
}

void KeybindMap::destroy(KeybindMap* map) noexcept {
    if (map == nullptr) {
        return;
    }
    for (size_t b = 0; b < map->bucket_count; ++b) {
        KeybindBucket& bucket = map->buckets[b];
        if (map->on_free_target != nullptr) {
            for (size_t i = 0; i < bucket.count; ++i) {
                map->on_free_target(bucket.targets[i]);
            }
        }
        std::free(bucket.targets);
        bucket.targets = nullptr;
        bucket.count = 0;
        bucket.capacity = 0;
    }
    std::free(map->buckets);
    map->buckets = nullptr;
    map->bucket_count = 0;
    map->bucket_capacity = 0;
    std::free(map);
}

} // namespace slinky::util
