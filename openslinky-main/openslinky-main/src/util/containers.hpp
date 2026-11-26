#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace slinky::util {

// Tracked heap allocator wrappers - matches sub_18002E430..0x2E4F0 & sub_180158D70
inline void* zero_alloc(size_t count, size_t elem_size) noexcept {
    return std::calloc(count, elem_size);
}

// Fixed-capacity string builder - matches sub_18002EEF0..0x2F010
struct StringBuilder {
    char* buffer = nullptr;
    size_t capacity = 0;
    size_t length = 0;

    void init(char* buf, size_t cap) noexcept {
        buffer = buf;
        capacity = cap;
        length = 0;
    }

    bool append_str(const char* str) noexcept {
        if (capacity <= length || str == nullptr) {
            return false;
        }
        const size_t avail = capacity - length;
        const size_t len = std::strlen(str);
        if (len >= avail) {
            return false;
        }
        std::memcpy(buffer + length, str, len);
        length += len;
        return true;
    }

    bool append_fmt(const char* fmt, ...) noexcept {
        if (capacity <= length) {
            return false;
        }
        const size_t avail = capacity - length;
        va_list args;
        va_start(args, fmt);
        const int written = std::vsnprintf(buffer + length, avail, fmt, args);
        va_end(args);
        if (written < 0 || avail <= static_cast<size_t>(written)) {
            length = capacity;
            return false;
        }
        length += static_cast<size_t>(written);
        return true;
    }

    bool append_char(char ch) noexcept {
        if (length < capacity) {
            buffer[length++] = ch;
            return true;
        }
        return false;
    }

    bool terminate() noexcept {
        if (length < capacity) {
            buffer[length++] = '\0';
            return true;
        }
        return false;
    }
};

// String tokenizer view - matches sub_1800305C0..0x306A0
struct StringScanner {
    const char* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    static StringScanner from_span(const char* ptr, size_t len) noexcept {
        return {ptr, len, 0};
    }

    static StringScanner from_cstr(const char* str) noexcept {
        return {str, str ? std::strlen(str) : 0, 0};
    }

    bool has_more() const noexcept {
        return pos < size;
    }

    const char* next_token(char delim, size_t* out_len) noexcept {
        if (pos == size) {
            return nullptr;
        }
        const size_t start = pos;
        while (pos < size && data[pos] != delim) {
            ++pos;
        }
        *out_len = pos - start;
        while (pos < size && data[pos] == delim) {
            ++pos;
        }
        return data + start;
    }
};

// Growable array helper matching sub_18002F5E0 (starts at 8, doubles capacity)
inline bool ensure_array_capacity(void** data, size_t elem_size, size_t count, size_t* capacity) noexcept {
    if (count < *capacity) {
        return true;
    }
    if (*data != nullptr) {
        const size_t new_cap = (*capacity) * 2;
        void* next = std::realloc(*data, new_cap * elem_size);
        if (next == nullptr) {
            return false;
        }
        *data = next;
        *capacity = new_cap;
        return true;
    }
    void* fresh = zero_alloc(8, elem_size);
    if (fresh == nullptr) {
        return false;
    }
    *data = fresh;
    *capacity = 8;
    return true;
}

// Generic typed POD vector matching Slinky's {cap, size} + T* array conventions (sub_18002F040..0x2F4E0)
template <typename T>
struct PodVector {
    T* data = nullptr;
    int capacity = 0;
    int count = 0;

    void reserve_one() noexcept {
        if (count >= capacity) {
            const int next_cap = capacity ? capacity * 2 : 8;
            T* next = static_cast<T*>(std::realloc(data, sizeof(T) * static_cast<size_t>(next_cap)));
            if (next == nullptr) {
                std::abort();
            }
            data = next;
            capacity = next_cap;
        }
    }

    void push_back(const T& item) noexcept {
        reserve_one();
        data[count++] = item;
    }

    T* insert_at(int index, const T& item) noexcept {
        reserve_one();
        std::memmove(data + index + 1, data + index, sizeof(T) * static_cast<size_t>(count - index));
        data[index] = item;
        ++count;
        return data + index;
    }

    void erase_at(int index) noexcept {
        --count;
        std::memmove(data + index, data + index + 1, sizeof(T) * static_cast<size_t>(count - index));
    }

    void clear_and_free() noexcept {
        std::free(data);
        data = nullptr;
        capacity = 0;
        count = 0;
    }
};

// Power-of-two ring buffer matching sub_18002F540
template <typename T>
struct RingBuffer {
    T* data = nullptr;
    size_t capacity = 0;
    size_t head = 0;
    size_t tail = 0;

    size_t size() const noexcept { return tail - head; }
    bool empty() const noexcept { return head == tail; }

    void push_back(const T& val) noexcept {
        if ((tail - head) >= capacity) {
            const size_t old_cap = capacity;
            const size_t new_cap = old_cap ? old_cap * 2 : 16;
            T* next = static_cast<T*>(std::realloc(data, sizeof(T) * new_cap));
            if (next == nullptr) {
                std::abort();
            }
            data = next;
            capacity = new_cap;
            if (old_cap > 0) {
                const size_t wrapped_tail = tail & (old_cap - 1);
                const size_t wrapped_head = head & (old_cap - 1);
                if (wrapped_head >= wrapped_tail) {
                    std::memcpy(data + old_cap, data, sizeof(T) * wrapped_tail);
                }
            }
        }
        data[tail & (capacity - 1)] = val;
        ++tail;
    }

    T& front() noexcept { return data[head & (capacity - 1)]; }
    const T& front() const noexcept { return data[head & (capacity - 1)]; }
    void pop_front() noexcept { ++head; }

    void destroy() noexcept {
        std::free(data);
        data = nullptr;
        capacity = 0;
        head = 0;
        tail = 0;
    }
};

// Sorted keybind dispatch multimap (48 bytes) - matches sub_18002F750..0x2FD50
struct KeybindBucket {
    int key_code = 0;
    int pad = 0;
    size_t count = 0;
    size_t capacity = 0;
    void** targets = nullptr;
};

class KeybindMap {
public:
    using CallbackFn = bool (*)(void* target, uint32_t key_code, uint32_t pressed);
    using FreeTargetFn = void (*)(void* target);

    CallbackFn on_trigger = nullptr;
    FreeTargetFn on_free_target = nullptr;
    size_t bucket_count = 0;
    size_t bucket_capacity = 0;
    KeybindBucket* buckets = nullptr;
    bool allow_multiple = false;

    static KeybindMap* create(CallbackFn trigger_fn, FreeTargetFn free_fn, bool multi) noexcept {
        auto* map = static_cast<KeybindMap*>(zero_alloc(1, sizeof(KeybindMap)));
        if (map != nullptr) {
            map->on_trigger = trigger_fn;
            map->on_free_target = free_fn;
            map->allow_multiple = multi;
        }
        return map;
    }

    bool bind(int key_code, void* target) noexcept;
    void unbind(int key_code, void* target) noexcept;
    void* find(int key_code, void* target) const noexcept;
    bool trigger(uint32_t key_code, uint32_t pressed) noexcept;
    void release_all() noexcept;
    static void destroy(KeybindMap* map) noexcept;

private:
    KeybindBucket* find_bucket(int key_code) const noexcept;
};

} // namespace slinky::util
