#include "bytecode/classfile_rewriter.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace slinky::bytecode {

// ============================================================================
// Exact 202-byte JVM instruction length table (.rdata @ 0x180169800)
// ============================================================================
const uint8_t kJvmOpcodeLengths[202] = {
    /* 0x00 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x10 */ 2, 3, 2, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1,
    /* 0x20 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x30 */ 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1,
    /* 0x40 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x50 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x60 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x70 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x80 */ 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x90 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3,
    /* 0xA0 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 99, 99, 1, 1, 1, 1,
    /* 0xB0 */ 1, 1, 3, 3, 3, 3, 3, 3, 3, 5, 5, 3, 2, 3, 1, 1,
    /* 0xC0 */ 3, 3, 1, 1, 0, 4, 3, 3, 5, 5,
};

namespace {

constexpr uint16_t load_be_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

constexpr uint32_t load_be_u32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

constexpr void store_be_u16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFFu);
}

constexpr void store_be_u32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<uint8_t>(v & 0xFFu);
}

int get_cp_entry_slice(
    const ConstantPoolView* cp,
    uint16_t cp_index,
    ByteReader* out_slice) noexcept {
    if (!cp || cp_index == 0) {
        return kErrInvalidConstantPoolIndex;
    }
    const uint16_t slot = static_cast<uint16_t>(cp_index - 1u);
    if (slot >= cp->entry_count) {
        return kErrInvalidConstantPoolIndex;
    }
    const uint8_t* start = cp->entries[slot];
    if (!start) {
        return kErrInvalidConstantPoolIndex;
    }
    const uint8_t* end = cp->pool_end;
    for (uint16_t next_slot = cp_index; next_slot < cp->entry_count; ++next_slot) {
        if (cp->entries[next_slot] != nullptr) {
            end = cp->entries[next_slot];
            break;
        }
    }
    out_slice->cur = start;
    out_slice->end = end;
    return kRewriteOk;
}

bool is_method_or_interface_method_tag(uint8_t tag) noexcept {
    return (tag & 0xFEu) == kCpMethodref; // 10 (Methodref) or 11 (InterfaceMethodref)
}

bool is_field_tag(uint8_t tag) noexcept {
    return tag == kCpFieldref; // 9 (Fieldref)
}

int compute_instruction_length(
    const uint8_t* insn_start,
    const uint8_t* code_end,
    const uint8_t* code_base,
    size_t* out_len) noexcept {
    if (insn_start >= code_end) {
        return kErrUnexpectedEof;
    }
    const uint8_t opcode = *insn_start;
    const uint8_t* p = insn_start + 1;

    if (opcode == kOpTableswitch) {
        const size_t rel = static_cast<size_t>(p - code_base);
        if ((rel & 3u) != 0) {
            const size_t pad = 4u - (rel & 3u);
            if (static_cast<size_t>(code_end - p) < pad) {
                return kErrUnexpectedEof;
            }
            p += pad;
        }
        if (static_cast<size_t>(code_end - p) < 12u) {
            return kErrUnexpectedEof;
        }
        const int32_t low = static_cast<int32_t>(load_be_u32(p + 4));
        const int32_t high = static_cast<int32_t>(load_be_u32(p + 8));
        const int64_t count = static_cast<int64_t>(high) - static_cast<int64_t>(low) + 1;
        if (count < 0) {
            return kErrUnexpectedEof;
        }
        const uint64_t jump_bytes = static_cast<uint64_t>(count) * 4u;
        p += 12;
        if (static_cast<uint64_t>(code_end - p) < jump_bytes) {
            return kErrUnexpectedEof;
        }
        *out_len = static_cast<size_t>((p + jump_bytes) - insn_start);
        return kRewriteOk;
    }

    if (opcode == kOpLookupswitch) {
        const size_t rel = static_cast<size_t>(p - code_base);
        if ((rel & 3u) != 0) {
            const size_t pad = 4u - (rel & 3u);
            if (static_cast<size_t>(code_end - p) < pad) {
                return kErrUnexpectedEof;
            }
            p += pad;
        }
        if (static_cast<size_t>(code_end - p) < 8u) {
            return kErrUnexpectedEof;
        }
        const int32_t npairs = static_cast<int32_t>(load_be_u32(p + 4));
        if (npairs < 0) {
            return kErrUnexpectedEof;
        }
        const uint64_t pair_bytes = static_cast<uint64_t>(npairs) * 8u;
        p += 8;
        if (static_cast<uint64_t>(code_end - p) < pair_bytes) {
            return kErrUnexpectedEof;
        }
        *out_len = static_cast<size_t>((p + pair_bytes) - insn_start);
        return kRewriteOk;
    }

    if (opcode == kOpWide) {
        if (static_cast<size_t>(code_end - insn_start) < 2u) {
            return kErrUnexpectedEof;
        }
        const size_t total = (*p == kOpIinc) ? 6u : 4u;
        if (static_cast<size_t>(code_end - insn_start) < total) {
            return kErrUnexpectedEof;
        }
        *out_len = total;
        return kRewriteOk;
    }

    if (opcode <= kMaxJvmOpcode) {
        const size_t len = kJvmOpcodeLengths[opcode];
        if (static_cast<size_t>(code_end - insn_start) < len) {
            return kErrUnexpectedEof;
        }
        *out_len = len;
        return kRewriteOk;
    }

    return kErrInvalidBytecodeOpcode;
}

int append_cp_utf8_raw(
    ByteWriter* writer,
    const char* str,
    uint16_t* io_next_cp_index,
    uint16_t* out_utf8_cp_index) noexcept {
    const size_t raw_len = std::strlen(str);
    if (raw_len > 0xFFFFu) {
        return kErrBufferOverflow;
    }
    const uint16_t utf_len = static_cast<uint16_t>(raw_len);
    int err = write_u1(writer, kCpUtf8);
    if (err != kRewriteOk) {
        return err;
    }
    err = write_u2(writer, utf_len);
    if (err != kRewriteOk) {
        return err;
    }
    err = write_bytes(writer, str, utf_len);
    if (err != kRewriteOk) {
        return err;
    }
    const uint16_t assigned = (*io_next_cp_index)++;
    if (out_utf8_cp_index) {
        *out_utf8_cp_index = assigned;
    }
    return kRewriteOk;
}

} // namespace

// ============================================================================
// Stream Reader & Writer Helpers (sub_180040F30 .. sub_180041030)
// ============================================================================

// sub_180040F30
int read_bytes(ByteReader* reader, uint32_t len, void* out) noexcept {
    if (!reader || !reader->cur || !reader->end || reader->cur > reader->end) {
        return kErrUnexpectedEof;
    }
    if (static_cast<size_t>(reader->end - reader->cur) < len) {
        return kErrUnexpectedEof;
    }
    if (out && len > 0) {
        std::memcpy(out, reader->cur, len);
    }
    reader->cur += len;
    return kRewriteOk;
}

// sub_180040F80
int read_u1(ByteReader* reader, uint8_t* out) noexcept {
    if (!reader || !reader->cur || !reader->end || reader->cur >= reader->end) {
        return kErrUnexpectedEof;
    }
    if (out) {
        *out = *reader->cur;
    }
    reader->cur += 1;
    return kRewriteOk;
}

// sub_180040FB0
int read_u2(ByteReader* reader, uint16_t* out) noexcept {
    if (!reader || !reader->cur || !reader->end ||
        reader->cur > reader->end || static_cast<size_t>(reader->end - reader->cur) < 2u) {
        return kErrUnexpectedEof;
    }
    if (out) {
        *out = load_be_u16(reader->cur);
    }
    reader->cur += 2;
    return kRewriteOk;
}

// sub_180040FE0
int read_u4(ByteReader* reader, uint32_t* out) noexcept {
    if (!reader || !reader->cur || !reader->end ||
        reader->cur > reader->end || static_cast<size_t>(reader->end - reader->cur) < 4u) {
        return kErrUnexpectedEof;
    }
    if (out) {
        *out = load_be_u32(reader->cur);
    }
    reader->cur += 4;
    return kRewriteOk;
}

// sub_180041010
int write_u1(ByteWriter* writer, uint8_t value) noexcept {
    if (!writer || !writer->cur || !writer->end || writer->cur >= writer->end) {
        return kErrBufferOverflow;
    }
    *writer->cur = value;
    writer->cur += 1;
    return kRewriteOk;
}

// sub_180041030
int write_u2(ByteWriter* writer, uint16_t value) noexcept {
    if (!writer || !writer->cur || !writer->end ||
        writer->cur > writer->end || static_cast<size_t>(writer->end - writer->cur) < 2u) {
        return kErrBufferOverflow;
    }
    store_be_u16(writer->cur, value);
    writer->cur += 2;
    return kRewriteOk;
}

int write_u4(ByteWriter* writer, uint32_t value) noexcept {
    if (!writer || !writer->cur || !writer->end ||
        writer->cur > writer->end || static_cast<size_t>(writer->end - writer->cur) < 4u) {
        return kErrBufferOverflow;
    }
    store_be_u32(writer->cur, value);
    writer->cur += 4;
    return kRewriteOk;
}

int write_bytes(ByteWriter* writer, const void* src, size_t len) noexcept {
    if (!writer || !writer->cur || !writer->end || writer->cur > writer->end) {
        return kErrBufferOverflow;
    }
    if (static_cast<size_t>(writer->end - writer->cur) < len) {
        return kErrBufferOverflow;
    }
    if (len > 0 && src) {
        std::memcpy(writer->cur, src, len);
    }
    writer->cur += len;
    return kRewriteOk;
}

// ============================================================================
// Constant Pool Entry Resolvers & String Matchers (sub_180041060 .. sub_180041630)
// ============================================================================

// sub_180041060
int resolve_cp_utf8(const ConstantPoolView* cp, uint16_t cp_index, Utf8View* out) noexcept {
    ByteReader slice{};
    int err = get_cp_entry_slice(cp, cp_index, &slice);
    if (err != kRewriteOk) {
        return err;
    }
    uint8_t tag = 0;
    err = read_u1(&slice, &tag);
    if (err != kRewriteOk) {
        return err;
    }
    if (tag != kCpUtf8) {
        return kErrConstantPoolTagMismatch;
    }
    uint16_t utf_len = 0;
    err = read_u2(&slice, &utf_len);
    if (err != kRewriteOk) {
        return err;
    }
    const char* str_ptr = reinterpret_cast<const char*>(slice.cur);
    err = read_bytes(&slice, utf_len, nullptr);
    if (err != kRewriteOk) {
        return err;
    }
    if (out) {
        out->data = str_ptr;
        out->length = utf_len;
    }
    return kRewriteOk;
}

// sub_180041100
int resolve_cp_class(const ConstantPoolView* cp, uint16_t cp_index, Utf8View* out) noexcept {
    ByteReader slice{};
    int err = get_cp_entry_slice(cp, cp_index, &slice);
    if (err != kRewriteOk) {
        return err;
    }
    uint8_t tag = 0;
    err = read_u1(&slice, &tag);
    if (err != kRewriteOk) {
        return err;
    }
    if (tag != kCpClass) {
        return kErrConstantPoolTagMismatch;
    }
    uint16_t name_index = 0;
    err = read_u2(&slice, &name_index);
    if (err != kRewriteOk) {
        return err;
    }
    return resolve_cp_utf8(cp, name_index, out);
}

// sub_180041200
int resolve_cp_name_and_type(
    const ConstantPoolView* cp,
    uint16_t cp_index,
    NameAndTypeView* out) noexcept {
    ByteReader slice{};
    int err = get_cp_entry_slice(cp, cp_index, &slice);
    if (err != kRewriteOk) {
        return err;
    }
    uint8_t tag = 0;
    err = read_u1(&slice, &tag);
    if (err != kRewriteOk) {
        return err;
    }
    if (tag != kCpNameAndType) {
        return kErrConstantPoolTagMismatch;
    }
    uint16_t name_index = 0;
    uint16_t desc_index = 0;
    err = read_u2(&slice, &name_index);
    if (err != kRewriteOk) {
        return err;
    }
    err = read_u2(&slice, &desc_index);
    if (err != kRewriteOk) {
        return err;
    }
    Utf8View name_view{};
    Utf8View desc_view{};
    err = resolve_cp_utf8(cp, name_index, &name_view);
    if (err != kRewriteOk) {
        return err;
    }
    err = resolve_cp_utf8(cp, desc_index, &desc_view);
    if (err != kRewriteOk) {
        return err;
    }
    if (out) {
        out->name = name_view;
        out->descriptor = desc_view;
    }
    return kRewriteOk;
}

// sub_180041320
int resolve_cp_member_ref(
    const ConstantPoolView* cp,
    CpTagPredicateFn tag_pred,
    uint16_t cp_index,
    MemberRefView* out) noexcept {
    ByteReader slice{};
    int err = get_cp_entry_slice(cp, cp_index, &slice);
    if (err != kRewriteOk) {
        return err;
    }
    uint8_t tag = 0;
    err = read_u1(&slice, &tag);
    if (err != kRewriteOk) {
        return err;
    }
    if (!tag_pred || !tag_pred(tag)) {
        return kErrConstantPoolTagMismatch;
    }
    uint16_t class_index = 0;
    uint16_t nat_index = 0;
    err = read_u2(&slice, &class_index);
    if (err != kRewriteOk) {
        return err;
    }
    err = read_u2(&slice, &nat_index);
    if (err != kRewriteOk) {
        return err;
    }
    Utf8View owner_view{};
    NameAndTypeView nat_view{};
    err = resolve_cp_class(cp, class_index, &owner_view);
    if (err != kRewriteOk) {
        return err;
    }
    err = resolve_cp_name_and_type(cp, nat_index, &nat_view);
    if (err != kRewriteOk) {
        return err;
    }
    if (out) {
        out->owner = owner_view;
        out->name = nat_view.name;
        out->descriptor = nat_view.descriptor;
    }
    return kRewriteOk;
}

// sub_180041410
int resolve_cp_method_ref(const ConstantPoolView* cp, uint16_t cp_index, MemberRefView* out) noexcept {
    return resolve_cp_member_ref(cp, &is_method_or_interface_method_tag, cp_index, out);
}

// sub_180041500
int resolve_cp_field_ref(const ConstantPoolView* cp, uint16_t cp_index, MemberRefView* out) noexcept {
    return resolve_cp_member_ref(cp, &is_field_tag, cp_index, out);
}

// sub_1800415E0
int utf8_compare_cstr(const Utf8View* utf8, const char* cstr) noexcept {
    if (!utf8 || !cstr) {
        return -1;
    }
    const size_t cstr_len = std::strlen(cstr);
    const size_t min_len = std::min<size_t>(utf8->length, cstr_len);
    if (min_len > 0) {
        const int cmp = std::memcmp(utf8->data, cstr, min_len);
        if (cmp != 0) {
            return cmp;
        }
    }
    if (utf8->length == cstr_len) {
        return 0;
    }
    return (utf8->length < cstr_len) ? -1 : 1;
}

bool utf8_equals_cstr(const Utf8View& utf8, const char* cstr) noexcept {
    if (!cstr) {
        return false;
    }
    const size_t cstr_len = std::strlen(cstr);
    if (cstr_len != utf8.length) {
        return false;
    }
    return cstr_len == 0 || std::memcmp(utf8.data, cstr, cstr_len) == 0;
}

// sub_180041630
bool name_and_type_matches(const NameAndTypeView* nat, const MethodSignature* sig) noexcept {
    if (!nat || !sig || !sig->name || !sig->descriptor) {
        return false;
    }
    return utf8_equals_cstr(nat->name, sig->name) &&
           utf8_equals_cstr(nat->descriptor, sig->descriptor);
}

// ============================================================================
// Constant Pool Growth & Classfile Rewriter (sub_1800416E0 .. sub_180042420)
// ============================================================================

// sub_1800416E0
uint32_t compute_appended_cp_bytes(
    const char* hook_class_name,
    const HookMethodEntry* hook_methods,
    int hook_method_count) noexcept {
    uint32_t total = static_cast<uint32_t>(std::strlen(hook_class_name)) + 6u;
    if (hook_methods && hook_method_count > 0) {
        for (int i = 0; i < hook_method_count; ++i) {
            const uint32_t name_len = static_cast<uint32_t>(std::strlen(hook_methods[i].name));
            const uint32_t desc_len = static_cast<uint32_t>(std::strlen(hook_methods[i].descriptor));
            total += name_len + desc_len + 16u;
        }
    }
    return total;
}

// sub_180042020
int append_cp_class_entry(
    ByteWriter* writer,
    const char* class_name,
    uint16_t* io_next_cp_index,
    uint16_t* out_class_cp_index) noexcept {
    if (!writer || !class_name || !io_next_cp_index || !out_class_cp_index) {
        return kErrBufferOverflow;
    }
    uint16_t utf8_index = 0;
    int err = append_cp_utf8_raw(writer, class_name, io_next_cp_index, &utf8_index);
    if (err != kRewriteOk) {
        return err;
    }
    const uint16_t class_index = (*io_next_cp_index)++;
    *out_class_cp_index = class_index;

    err = write_u1(writer, kCpClass);
    if (err != kRewriteOk) {
        return err;
    }
    return write_u2(writer, utf8_index);
}

// sub_180042100
int dispatch_method_selector(
    const ConstantPoolView* cp,
    uint16_t name_index,
    uint16_t descriptor_index,
    MethodSelectorFn selector_fn,
    void* selector_ctx,
    InsnVisitorFn* out_visitor,
    void** out_visitor_ctx) {
    NameAndTypeView method_nat{};
    int err = resolve_cp_utf8(cp, name_index, &method_nat.name);
    if (err != kRewriteOk) {
        return err;
    }
    err = resolve_cp_utf8(cp, descriptor_index, &method_nat.descriptor);
    if (err != kRewriteOk) {
        return err;
    }
    if (!selector_fn) {
        return kRewriteOk;
    }
    return selector_fn(selector_ctx, &method_nat, out_visitor, out_visitor_ctx);
}

// sub_180042240
int is_code_attribute_name(
    const ConstantPoolView* cp,
    uint16_t attribute_name_index,
    bool* out_is_code) noexcept {
    Utf8View attr_name{};
    const int err = resolve_cp_utf8(cp, attribute_name_index, &attr_name);
    if (err != kRewriteOk) {
        return err;
    }
    if (out_is_code) {
        *out_is_code = utf8_equals_cstr(attr_name, "Code");
    }
    return kRewriteOk;
}

// sub_180042420
int walk_and_patch_bytecode(
    ByteReader* in_code,
    ByteWriter* out_code,
    const uint8_t* code_base,
    const ConstantPoolView* cp,
    InsnVisitorFn visitor_fn,
    void* visitor_ctx) {
    while (in_code->cur < in_code->end) {
        const uint8_t* insn_start = in_code->cur;
        size_t insn_len = 0;
        int err = compute_instruction_length(insn_start, in_code->end, code_base, &insn_len);
        if (err != kRewriteOk) {
            return err;
        }
        const uint8_t* insn_end = insn_start + insn_len;
        in_code->cur = insn_end;

        if (static_cast<size_t>(out_code->end - out_code->cur) < insn_len) {
            return kErrBufferOverflow;
        }
        uint8_t* out_insn_start = out_code->cur;
        uint8_t* out_insn_end = out_insn_start + insn_len;
        out_code->cur = out_insn_end;

        ByteReader in_slice{insn_start, insn_end};
        ByteWriter out_slice{out_insn_start, out_insn_end};
        err = visitor_fn(visitor_ctx, cp, &in_slice, &out_slice);
        if (err != kRewriteOk) {
            return err;
        }
    }
    return kRewriteOk;
}

// sub_180042340
int rewrite_code_attribute(
    ByteReader* in_attr,
    ByteWriter* out_attr,
    const ConstantPoolView* cp,
    InsnVisitorFn visitor_fn,
    void* visitor_ctx) {
    // Skip max_stack (u2) + max_locals (u2) = 4 bytes, then read code_length (u4)
    int err = read_bytes(in_attr, 4u, nullptr);
    if (err != kRewriteOk) {
        return err;
    }
    uint32_t code_length = 0;
    err = read_u4(in_attr, &code_length);
    if (err != kRewriteOk) {
        return err;
    }
    if (static_cast<size_t>(out_attr->end - out_attr->cur) < 8u) {
        return kErrBufferOverflow;
    }
    out_attr->cur += 8;

    if (static_cast<size_t>(in_attr->end - in_attr->cur) < code_length) {
        return kErrUnexpectedEof;
    }
    if (static_cast<size_t>(out_attr->end - out_attr->cur) < code_length) {
        return kErrBufferOverflow;
    }

    const uint8_t* code_base = in_attr->cur;
    ByteReader in_code{code_base, code_base + code_length};
    ByteWriter out_code{out_attr->cur, out_attr->cur + code_length};

    in_attr->cur += code_length;
    out_attr->cur += code_length;

    return walk_and_patch_bytecode(&in_code, &out_code, code_base, cp, visitor_fn, visitor_ctx);
}

// sub_180041D30
int copy_and_patch_members(
    ByteReader* in_stream,
    ByteWriter* out_stream,
    const ConstantPoolView* cp,
    MethodSelectorFn selector_fn,
    void* selector_ctx) {
    uint16_t member_count = 0;
    int err = read_u2(in_stream, &member_count);
    if (err != kRewriteOk) {
        return err;
    }
    err = write_u2(out_stream, member_count);
    if (err != kRewriteOk) {
        return err;
    }

    for (uint16_t i = 0; i < member_count; ++i) {
        if (static_cast<size_t>(in_stream->end - in_stream->cur) < 8u) {
            return kErrUnexpectedEof;
        }
        const uint8_t* member_hdr = in_stream->cur;
        const uint16_t name_index = load_be_u16(member_hdr + 2);
        const uint16_t desc_index = load_be_u16(member_hdr + 4);
        const uint16_t attributes_count = load_be_u16(member_hdr + 6);
        in_stream->cur += 8;

        err = write_bytes(out_stream, member_hdr, 8u);
        if (err != kRewriteOk) {
            return err;
        }

        InsnVisitorFn visitor_fn = nullptr;
        void* visitor_ctx = nullptr;
        if (cp != nullptr) {
            err = dispatch_method_selector(
                cp, name_index, desc_index, selector_fn, selector_ctx, &visitor_fn, &visitor_ctx);
            if (err != kRewriteOk) {
                return err;
            }
        }

        for (uint16_t attr_i = 0; attr_i < attributes_count; ++attr_i) {
            if (static_cast<size_t>(in_stream->end - in_stream->cur) < 6u) {
                return kErrUnexpectedEof;
            }
            const uint8_t* attr_hdr = in_stream->cur;
            const uint16_t attr_name_index = load_be_u16(attr_hdr);
            const uint32_t attr_length = load_be_u32(attr_hdr + 2);
            in_stream->cur += 6;

            err = write_bytes(out_stream, attr_hdr, 6u);
            if (err != kRewriteOk) {
                return err;
            }

            if (static_cast<size_t>(in_stream->end - in_stream->cur) < attr_length) {
                return kErrUnexpectedEof;
            }
            const uint8_t* in_attr_body = in_stream->cur;
            in_stream->cur += attr_length;

            if (static_cast<size_t>(out_stream->end - out_stream->cur) < attr_length) {
                return kErrBufferOverflow;
            }
            uint8_t* out_attr_body = out_stream->cur;
            if (attr_length > 0) {
                std::memcpy(out_attr_body, in_attr_body, attr_length);
            }
            out_stream->cur += attr_length;

            if (visitor_fn != nullptr && cp != nullptr) {
                bool is_code = false;
                err = is_code_attribute_name(cp, attr_name_index, &is_code);
                if (err == kRewriteOk && is_code) {
                    ByteReader in_attr_slice{in_attr_body, in_attr_body + attr_length};
                    ByteWriter out_attr_slice{out_attr_body, out_attr_body + attr_length};
                    err = rewrite_code_attribute(
                        &in_attr_slice, &out_attr_slice, cp, visitor_fn, visitor_ctx);
                }
                if (err != kRewriteOk) {
                    return err;
                }
            }
        }
    }

    return kRewriteOk;
}

// sub_180041760
int rewrite_class_bytecode(
    const uint8_t* class_bytes,
    int class_len,
    uint8_t** out_bytes,
    uint32_t* out_len,
    const char* hook_class_name,
    HookMethodEntry* hook_methods,
    int hook_method_count,
    MethodSelectorFn selector_fn,
    void* selector_ctx) {
    if (!class_bytes || class_len < 0 || !out_bytes || !out_len || !hook_class_name || hook_method_count < 0) {
        return kErrUnexpectedEof;
    }

    const size_t in_size = static_cast<size_t>(class_len);
    const size_t extra_cp = compute_appended_cp_bytes(hook_class_name, hook_methods, hook_method_count);
    const size_t alloc_cap = in_size + std::max<size_t>(4096u, extra_cp + 64u);

    uint8_t* out_buf = static_cast<uint8_t*>(std::malloc(alloc_cap));
    if (!out_buf) {
        return kErrOutOfMemory;
    }

    const uint8_t* const in_end = class_bytes + in_size;

    if (in_size < 4u) {
        std::free(out_buf);
        return kErrUnexpectedEof;
    }
    if (load_be_u32(class_bytes) != kClassFileMagic) {
        std::free(out_buf);
        return kErrInvalidMagic;
    }
    if (in_size < 8u) {
        std::free(out_buf);
        return kErrUnexpectedEof;
    }
    const uint16_t major_version = load_be_u16(class_bytes + 6);
    if (major_version > kMaxSupportedMajorVersion) {
        std::free(out_buf);
        return kErrUnsupportedClassVersion;
    }
    std::memcpy(out_buf, class_bytes, 8u);

    if (in_size < 10u) {
        std::free(out_buf);
        return kErrUnexpectedEof;
    }
    const uint16_t orig_cp_count = load_be_u16(class_bytes + 8);
    if (orig_cp_count == 0) {
        std::free(out_buf);
        return kErrInvalidConstantPoolIndex;
    }

    const uint32_t added_cp_entries = 2u + 4u * static_cast<uint32_t>(hook_method_count);
    if (static_cast<uint32_t>(orig_cp_count) + added_cp_entries > 0xFFFFu) {
        std::free(out_buf);
        return kErrBufferOverflow;
    }
    const uint16_t new_cp_count = static_cast<uint16_t>(orig_cp_count + added_cp_entries);
    store_be_u16(out_buf + 8, new_cp_count);

    const uint16_t orig_entry_slots = static_cast<uint16_t>(orig_cp_count - 1u);
    const uint8_t** cp_entries = static_cast<const uint8_t**>(
        std::calloc(orig_entry_slots > 0 ? orig_entry_slots : 1u, sizeof(const uint8_t*)));
    if (!cp_entries) {
        std::free(out_buf);
        return kErrOutOfMemory;
    }

    const uint8_t* const cp_start = class_bytes + 10;
    const uint8_t* p = cp_start;
    uint16_t slot = 0;
    while (slot < orig_entry_slots) {
        cp_entries[slot] = p;
        if (p >= in_end) {
            std::free(cp_entries);
            std::free(out_buf);
            return kErrUnexpectedEof;
        }
        const uint8_t tag = *p++;
        size_t payload_bytes = 0;
        bool wide_entry = false;
        switch (tag) {
            case kCpUtf8: {
                if (static_cast<size_t>(in_end - p) < 2u) {
                    std::free(cp_entries);
                    std::free(out_buf);
                    return kErrUnexpectedEof;
                }
                const uint16_t utf_len = load_be_u16(p);
                payload_bytes = 2u + static_cast<size_t>(utf_len);
                break;
            }
            case kCpInteger:
            case kCpFloat:
            case kCpFieldref:
            case kCpMethodref:
            case kCpInterfaceMethodref:
            case kCpNameAndType:
            case kCpDynamic:
            case kCpInvokeDynamic:
                payload_bytes = 4u;
                break;
            case kCpLong:
            case kCpDouble:
                payload_bytes = 8u;
                wide_entry = true;
                break;
            case kCpClass:
            case kCpString:
            case kCpMethodType:
            case kCpModule:
            case kCpPackage:
                payload_bytes = 2u;
                break;
            case kCpMethodHandle:
                payload_bytes = 3u;
                break;
            default:
                std::free(cp_entries);
                std::free(out_buf);
                return kErrInvalidConstantPoolTag;
        }
        if (static_cast<size_t>(in_end - p) < payload_bytes) {
            std::free(cp_entries);
            std::free(out_buf);
            return kErrUnexpectedEof;
        }
        p += payload_bytes;
        slot = static_cast<uint16_t>(slot + (wide_entry ? 2u : 1u));
    }

    const uint8_t* const cp_end = p;
    const size_t orig_cp_bytes = static_cast<size_t>(cp_end - cp_start);

    ByteWriter writer{out_buf + 10, out_buf + alloc_cap};
    int err = write_bytes(&writer, cp_start, orig_cp_bytes);
    if (err != kRewriteOk) {
        std::free(cp_entries);
        std::free(out_buf);
        return err;
    }

    uint16_t next_cp_index = orig_cp_count;
    uint16_t hook_class_cp_index = 0;
    err = append_cp_class_entry(&writer, hook_class_name, &next_cp_index, &hook_class_cp_index);
    if (err != kRewriteOk) {
        std::free(cp_entries);
        std::free(out_buf);
        return err;
    }

    for (int i = 0; i < hook_method_count; ++i) {
        uint16_t name_cp_idx = 0;
        uint16_t desc_cp_idx = 0;
        err = append_cp_utf8_raw(&writer, hook_methods[i].name, &next_cp_index, &name_cp_idx);
        if (err != kRewriteOk) {
            std::free(cp_entries);
            std::free(out_buf);
            return err;
        }
        err = append_cp_utf8_raw(&writer, hook_methods[i].descriptor, &next_cp_index, &desc_cp_idx);
        if (err != kRewriteOk) {
            std::free(cp_entries);
            std::free(out_buf);
            return err;
        }

        // CONSTANT_NameAndType (tag 12)
        const uint16_t nat_cp_idx = next_cp_index++;
        err = write_u1(&writer, kCpNameAndType);
        if (err == kRewriteOk) err = write_u2(&writer, name_cp_idx);
        if (err == kRewriteOk) err = write_u2(&writer, desc_cp_idx);
        if (err != kRewriteOk) {
            std::free(cp_entries);
            std::free(out_buf);
            return err;
        }

        // CONSTANT_Methodref (tag 10)
        const uint16_t methodref_cp_idx = next_cp_index++;
        hook_methods[i].resolved_cp_index = methodref_cp_idx;
        err = write_u1(&writer, kCpMethodref);
        if (err == kRewriteOk) err = write_u2(&writer, hook_class_cp_index);
        if (err == kRewriteOk) err = write_u2(&writer, nat_cp_idx);
        if (err != kRewriteOk) {
            std::free(cp_entries);
            std::free(out_buf);
            return err;
        }
    }

    // Copy access_flags (2) + this_class (2) + super_class (2) + interfaces_count (2) + interfaces[2 * count]
    if (static_cast<size_t>(in_end - cp_end) < 8u) {
        std::free(cp_entries);
        std::free(out_buf);
        return kErrUnexpectedEof;
    }
    const uint16_t interfaces_count = load_be_u16(cp_end + 6);
    const size_t header_and_ifaces_len = 8u + 2u * static_cast<size_t>(interfaces_count);
    if (static_cast<size_t>(in_end - cp_end) < header_and_ifaces_len) {
        std::free(cp_entries);
        std::free(out_buf);
        return kErrUnexpectedEof;
    }
    err = write_bytes(&writer, cp_end, header_and_ifaces_len);
    if (err != kRewriteOk) {
        std::free(cp_entries);
        std::free(out_buf);
        return err;
    }

    ByteReader reader{cp_end + header_and_ifaces_len, in_end};
    const ConstantPoolView cp_view{cp_entries, orig_entry_slots, cp_end};

    // Copy fields verbatim (no bytecode visitor)
    err = copy_and_patch_members(&reader, &writer, nullptr, nullptr, nullptr);
    if (err == kRewriteOk) {
        // Copy methods and patch matching Code attributes
        err = copy_and_patch_members(&reader, &writer, &cp_view, selector_fn, selector_ctx);
    }
    if (err == kRewriteOk) {
        // Copy trailing classfile attributes verbatim
        const size_t trailing_bytes = static_cast<size_t>(reader.end - reader.cur);
        err = write_bytes(&writer, reader.cur, trailing_bytes);
    }

    std::free(cp_entries);
    if (err != kRewriteOk) {
        std::free(out_buf);
        return err;
    }

    *out_bytes = out_buf;
    *out_len = static_cast<uint32_t>(writer.cur - out_buf);
    return kRewriteOk;
}

// ============================================================================
// Method Selector & Instruction Patcher Callbacks (sub_180043DE0 & sub_180043EA0)
// ============================================================================

// sub_180043DE0
int select_method_patcher(
    void* selector_ctx,
    const NameAndTypeView* method_nat,
    InsnVisitorFn* out_visitor,
    void** out_visitor_ctx) noexcept {
    const auto* class_spec = static_cast<const ClassPatchSpec*>(selector_ctx);
    if (!class_spec || class_spec->method_patch_count == 0 || !class_spec->method_patches) {
        return kRewriteOk;
    }

    const char* const wildcard = wildcard_method_str();
    for (size_t i = 0; i < class_spec->method_patch_count; ++i) {
        const MethodPatchSpec& mp = class_spec->method_patches[i];
        const bool is_wildcard = (mp.method_name == wildcard && mp.method_descriptor == wildcard);
        if (!is_wildcard) {
            const MethodSignature sig{mp.method_name, mp.method_descriptor};
            if (!name_and_type_matches(method_nat, &sig)) {
                continue;
            }
        }
        *out_visitor = &patch_member_instruction;
        *out_visitor_ctx = const_cast<MethodPatchSpec*>(&mp);
        break;
    }

    return kRewriteOk;
}

// sub_180043EA0
int patch_member_instruction(
    void* visitor_ctx,
    const ConstantPoolView* cp,
    ByteReader* in_insn,
    ByteWriter* out_insn) noexcept {
    uint8_t opcode = 0;
    int err = read_u1(in_insn, &opcode);
    if (err != kRewriteOk) {
        return err;
    }

    HookInsnKind insn_kind{};
    bool is_field_op = false;
    switch (opcode) {
        case kOpGetStatic:
            insn_kind = HookInsnKind::GetStatic;
            is_field_op = true;
            break;
        case kOpPutStatic:
            insn_kind = HookInsnKind::PutStatic;
            is_field_op = true;
            break;
        case kOpGetField:
            insn_kind = HookInsnKind::GetField;
            is_field_op = true;
            break;
        case kOpPutField:
            insn_kind = HookInsnKind::PutField;
            is_field_op = true;
            break;
        case kOpInvokeVirtual:
        case kOpInvokeSpecial:
            insn_kind = HookInsnKind::InvokeInstance;
            is_field_op = false;
            break;
        case kOpInvokeStatic:
            insn_kind = HookInsnKind::InvokeStatic;
            is_field_op = false;
            break;
        default:
            return kRewriteOk;
    }

    uint16_t operand_cp_idx = 0;
    err = read_u2(in_insn, &operand_cp_idx);
    if (err != kRewriteOk) {
        return err;
    }

    MemberRefView ref_view{};
    err = is_field_op
              ? resolve_cp_field_ref(cp, operand_cp_idx, &ref_view)
              : resolve_cp_method_ref(cp, operand_cp_idx, &ref_view);
    if (err != kRewriteOk) {
        return err;
    }

    const auto* method_spec = static_cast<const MethodPatchSpec*>(visitor_ctx);
    if (!method_spec || method_spec->target_count == 0 || !method_spec->targets) {
        return kRewriteOk;
    }

    for (size_t i = 0; i < method_spec->target_count; ++i) {
        const InsnPatchTarget& target = method_spec->targets[i];
        if (target.kind != insn_kind) {
            continue;
        }
        if (!utf8_equals_cstr(ref_view.owner, target.owner)) {
            continue;
        }
        const NameAndTypeView nat_view{ref_view.name, ref_view.descriptor};
        const MethodSignature target_sig{target.name, target.descriptor};
        if (name_and_type_matches(&nat_view, &target_sig)) {
            err = write_u1(out_insn, kOpInvokeStatic);
            if (err != kRewriteOk) {
                return err;
            }
            return write_u2(out_insn, *target.hook_cp_index_ptr);
        }
    }

    return kRewriteOk;
}

// ============================================================================
// Instruction-Aligned Pattern Matcher & Code Offset / StackMapTable Adjusters
// ============================================================================

uint32_t map_shifted_pc(
    uint32_t old_pc,
    const PcEdit* edits,
    size_t edit_count,
    bool inclusive_end) noexcept {
    int64_t shifted = static_cast<int64_t>(old_pc);
    if (edits && edit_count > 0) {
        for (size_t i = 0; i < edit_count; ++i) {
            const bool applies = inclusive_end ? (edits[i].at_pc < old_pc) : (edits[i].at_pc <= old_pc);
            if (applies) {
                shifted += edits[i].delta;
            }
        }
    }
    if (shifted < 0) {
        return 0u;
    }
    if (shifted > 0xFFFF) {
        return 0xFFFFu;
    }
    return static_cast<uint32_t>(shifted);
}

int match_opcode_pattern(
    const uint8_t* code,
    size_t code_len,
    const uint8_t* pattern,
    const uint8_t* mask,
    size_t pattern_len,
    size_t start_offset,
    size_t* out_match_offset) noexcept {
    if (!code || !pattern || pattern_len == 0 || !out_match_offset) {
        return kErrUnexpectedEof;
    }
    const uint8_t* const code_end = code + code_len;
    const uint8_t* cur = code;

    while (cur < code_end) {
        const size_t pc = static_cast<size_t>(cur - code);
        size_t insn_len = 0;
        const int err = compute_instruction_length(cur, code_end, code, &insn_len);
        if (err != kRewriteOk) {
            return err;
        }
        if (pc >= start_offset && pc + pattern_len <= code_len) {
            bool matched = true;
            for (size_t i = 0; i < pattern_len; ++i) {
                const uint8_t m = mask ? mask[i] : 0xFFu;
                if ((cur[i] & m) != (pattern[i] & m)) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                *out_match_offset = pc;
                return kRewriteOk;
            }
        }
        cur += insn_len;
    }

    *out_match_offset = static_cast<size_t>(-1);
    return kRewriteOk;
}

int adjust_exception_table_pcs(
    uint8_t* exception_table_bytes,
    uint16_t exception_table_length,
    const PcEdit* edits,
    size_t edit_count) noexcept {
    if (exception_table_length == 0) {
        return kRewriteOk;
    }
    if (!exception_table_bytes) {
        return kErrUnexpectedEof;
    }
    for (uint16_t i = 0; i < exception_table_length; ++i) {
        uint8_t* entry = exception_table_bytes + static_cast<size_t>(i) * 8u;
        const uint16_t start_pc = load_be_u16(entry + 0);
        const uint16_t end_pc = load_be_u16(entry + 2);
        const uint16_t handler_pc = load_be_u16(entry + 4);

        store_be_u16(entry + 0, static_cast<uint16_t>(map_shifted_pc(start_pc, edits, edit_count, false)));
        store_be_u16(entry + 2, static_cast<uint16_t>(map_shifted_pc(end_pc, edits, edit_count, true)));
        store_be_u16(entry + 4, static_cast<uint16_t>(map_shifted_pc(handler_pc, edits, edit_count, false)));
    }
    return kRewriteOk;
}

int adjust_line_number_table_pcs(
    uint8_t* line_table_bytes,
    uint16_t line_table_length,
    const PcEdit* edits,
    size_t edit_count) noexcept {
    if (line_table_length == 0) {
        return kRewriteOk;
    }
    if (!line_table_bytes) {
        return kErrUnexpectedEof;
    }
    for (uint16_t i = 0; i < line_table_length; ++i) {
        uint8_t* entry = line_table_bytes + static_cast<size_t>(i) * 4u;
        const uint16_t start_pc = load_be_u16(entry);
        store_be_u16(entry, static_cast<uint16_t>(map_shifted_pc(start_pc, edits, edit_count, false)));
    }
    return kRewriteOk;
}

int adjust_local_variable_table_pcs(
    uint8_t* local_table_bytes,
    uint16_t local_table_length,
    const PcEdit* edits,
    size_t edit_count) noexcept {
    if (local_table_length == 0) {
        return kRewriteOk;
    }
    if (!local_table_bytes) {
        return kErrUnexpectedEof;
    }
    for (uint16_t i = 0; i < local_table_length; ++i) {
        uint8_t* entry = local_table_bytes + static_cast<size_t>(i) * 10u;
        const uint16_t start_pc = load_be_u16(entry + 0);
        const uint16_t length = load_be_u16(entry + 2);
        const uint32_t new_start = map_shifted_pc(start_pc, edits, edit_count, false);
        const uint32_t new_end = map_shifted_pc(
            static_cast<uint32_t>(start_pc) + static_cast<uint32_t>(length), edits, edit_count, true);
        const uint16_t new_len = (new_end >= new_start) ? static_cast<uint16_t>(new_end - new_start) : 0u;
        store_be_u16(entry + 0, static_cast<uint16_t>(new_start));
        store_be_u16(entry + 2, new_len);
    }
    return kRewriteOk;
}

namespace {

int copy_and_adjust_verification_types(
    ByteReader* reader,
    ByteWriter* writer,
    uint16_t count,
    const PcEdit* edits,
    size_t edit_count) noexcept {
    // JVMS 4.7.4 verification_type_info:
    // 0..6: 1-byte tags (Top, Integer, Float, Double, Long, Null, UninitializedThis)
    // 7 (ITEM_Object): u1 tag + u2 cpool_index
    // 8 (ITEM_Uninitialized): u1 tag + u2 offset (bytecode offset of `new` instruction!)
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t vtag = 0;
        int err = read_u1(reader, &vtag);
        if (err != kRewriteOk) {
            return err;
        }
        err = write_u1(writer, vtag);
        if (err != kRewriteOk) {
            return err;
        }
        if (vtag == 7u) {
            uint16_t cp_idx = 0;
            err = read_u2(reader, &cp_idx);
            if (err != kRewriteOk) {
                return err;
            }
            err = write_u2(writer, cp_idx);
            if (err != kRewriteOk) {
                return err;
            }
        } else if (vtag == 8u) {
            uint16_t new_insn_pc = 0;
            err = read_u2(reader, &new_insn_pc);
            if (err != kRewriteOk) {
                return err;
            }
            const uint16_t adjusted_pc = static_cast<uint16_t>(
                map_shifted_pc(new_insn_pc, edits, edit_count, false));
            err = write_u2(writer, adjusted_pc);
            if (err != kRewriteOk) {
                return err;
            }
        } else if (vtag > 8u) {
            return kErrConstantPoolTagMismatch;
        }
    }
    return kRewriteOk;
}

} // namespace

int adjust_stack_map_table_frames(
    const uint8_t* in_smt,
    size_t in_smt_len,
    const PcEdit* edits,
    size_t edit_count,
    uint8_t** out_smt,
    uint32_t* out_smt_len) noexcept {
    if (!in_smt || in_smt_len < 2u || !out_smt || !out_smt_len) {
        return kErrUnexpectedEof;
    }

    ByteReader reader{in_smt, in_smt + in_smt_len};
    uint16_t num_entries = 0;
    int err = read_u2(&reader, &num_entries);
    if (err != kRewriteOk) {
        return err;
    }

    // Worst-case growth: every 1-byte compact frame promoted to 3-byte extended frame (+2 bytes/entry)
    const size_t max_out_cap = in_smt_len + static_cast<size_t>(num_entries) * 2u + 16u;
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(max_out_cap));
    if (!buf) {
        return kErrOutOfMemory;
    }

    ByteWriter writer{buf, buf + max_out_cap};
    err = write_u2(&writer, num_entries);
    if (err != kRewriteOk) {
        std::free(buf);
        return err;
    }

    uint32_t prev_old_pc = 0;
    uint32_t prev_new_pc = 0;

    for (uint16_t frame_i = 0; frame_i < num_entries; ++frame_i) {
        uint8_t frame_type = 0;
        err = read_u1(&reader, &frame_type);
        if (err != kRewriteOk) {
            std::free(buf);
            return err;
        }

        uint16_t old_delta = 0;
        if (frame_type <= 63u) {
            old_delta = frame_type;
        } else if (frame_type <= 127u) {
            old_delta = static_cast<uint16_t>(frame_type - 64u);
        } else if (frame_type >= 247u) {
            err = read_u2(&reader, &old_delta);
            if (err != kRewriteOk) {
                std::free(buf);
                return err;
            }
        } else {
            std::free(buf);
            return kErrConstantPoolTagMismatch;
        }

        const uint32_t cur_old_pc = (frame_i == 0)
                                        ? static_cast<uint32_t>(old_delta)
                                        : (prev_old_pc + 1u + static_cast<uint32_t>(old_delta));
        const uint32_t cur_new_pc = map_shifted_pc(cur_old_pc, edits, edit_count, false);

        uint32_t new_delta_u32 = 0;
        if (frame_i == 0) {
            new_delta_u32 = cur_new_pc;
        } else {
            new_delta_u32 = (cur_new_pc > prev_new_pc) ? (cur_new_pc - prev_new_pc - 1u) : 0u;
        }
        if (new_delta_u32 > 0xFFFFu) {
            std::free(buf);
            return kErrBufferOverflow;
        }
        const uint16_t new_delta = static_cast<uint16_t>(new_delta_u32);

        if (frame_type <= 63u || frame_type == 251u) {
            // same_frame (0..63) or same_frame_extended (251)
            if (new_delta <= 63u && frame_type <= 63u) {
                err = write_u1(&writer, static_cast<uint8_t>(new_delta));
            } else {
                err = write_u1(&writer, 251u);
                if (err == kRewriteOk) err = write_u2(&writer, new_delta);
            }
        } else if (frame_type <= 127u || frame_type == 247u) {
            // same_locals_1_stack_item_frame (64..127) or extended (247)
            if (new_delta <= 63u && frame_type <= 127u) {
                err = write_u1(&writer, static_cast<uint8_t>(64u + new_delta));
            } else {
                err = write_u1(&writer, 247u);
                if (err == kRewriteOk) err = write_u2(&writer, new_delta);
            }
            if (err == kRewriteOk) {
                err = copy_and_adjust_verification_types(&reader, &writer, 1u, edits, edit_count);
            }
        } else if (frame_type >= 248u && frame_type <= 250u) {
            // chop_frame (248..250)
            err = write_u1(&writer, frame_type);
            if (err == kRewriteOk) err = write_u2(&writer, new_delta);
        } else if (frame_type >= 252u && frame_type <= 254u) {
            // append_frame (252..254): k = frame_type - 251 locals
            const uint16_t k_locals = static_cast<uint16_t>(frame_type - 251u);
            err = write_u1(&writer, frame_type);
            if (err == kRewriteOk) err = write_u2(&writer, new_delta);
            if (err == kRewriteOk) {
                err = copy_and_adjust_verification_types(&reader, &writer, k_locals, edits, edit_count);
            }
        } else {
            // full_frame (255): u2 offset_delta, u2 num_locals, locals[], u2 num_stack, stack[]
            err = write_u1(&writer, 255u);
            if (err == kRewriteOk) err = write_u2(&writer, new_delta);
            uint16_t num_locals = 0;
            if (err == kRewriteOk) err = read_u2(&reader, &num_locals);
            if (err == kRewriteOk) err = write_u2(&writer, num_locals);
            if (err == kRewriteOk) {
                err = copy_and_adjust_verification_types(&reader, &writer, num_locals, edits, edit_count);
            }
            uint16_t num_stack = 0;
            if (err == kRewriteOk) err = read_u2(&reader, &num_stack);
            if (err == kRewriteOk) err = write_u2(&writer, num_stack);
            if (err == kRewriteOk) {
                err = copy_and_adjust_verification_types(&reader, &writer, num_stack, edits, edit_count);
            }
        }

        if (err != kRewriteOk) {
            std::free(buf);
            return err;
        }

        prev_old_pc = cur_old_pc;
        prev_new_pc = cur_new_pc;
    }

    *out_smt = buf;
    *out_smt_len = static_cast<uint32_t>(writer.cur - buf);
    return kRewriteOk;
}

} // namespace slinky::bytecode
