#pragma once

#include <cstddef>
#include <cstdint>

#include <jni.h>

namespace slinky::bytecode {

// ============================================================================
// Error codes matching libslinky.dll (0x180040F30 .. 0x18004403F)
// ============================================================================
enum RewriterError : int {
    kRewriteOk                    = 0,
    kErrOutOfMemory               = 1005,
    kErrUnexpectedEof             = 11000,
    kErrBufferOverflow            = 11001,
    kErrInvalidMagic              = 11002,
    kErrUnsupportedClassVersion   = 11003,
    kErrInvalidConstantPoolTag    = 11004,
    kErrInvalidConstantPoolIndex  = 11005,
    kErrConstantPoolTagMismatch   = 11006,
    kErrInvalidBytecodeOpcode     = 11007,
    kErrJvmtiBase                 = 12000,
};

// ============================================================================
// JVM ClassFile & Bytecode Constants (JVMS Chapter 4 & Chapter 6)
// ============================================================================
inline constexpr uint32_t kClassFileMagic           = 0xCAFEBABEu;
inline constexpr uint16_t kMaxSupportedMajorVersion = 69; // 0x45 (Java 25)

enum ConstantPoolTag : uint8_t {
    kCpUtf8               = 1,
    kCpInteger            = 3,
    kCpFloat              = 4,
    kCpLong               = 5,
    kCpDouble             = 6,
    kCpClass              = 7,
    kCpString             = 8,
    kCpFieldref           = 9,
    kCpMethodref          = 10,
    kCpInterfaceMethodref = 11,
    kCpNameAndType        = 12,
    kCpMethodHandle       = 15,
    kCpMethodType         = 16,
    kCpDynamic            = 17,
    kCpInvokeDynamic      = 18,
    kCpModule             = 19,
    kCpPackage            = 20,
};

enum JvmOpcode : uint8_t {
    kOpIinc          = 0x84,
    kOpTableswitch   = 0xAA,
    kOpLookupswitch  = 0xAB,
    kOpGetStatic     = 0xB2,
    kOpPutStatic     = 0xB3,
    kOpGetField      = 0xB4,
    kOpPutField      = 0xB5,
    kOpInvokeVirtual = 0xB6,
    kOpInvokeSpecial = 0xB7,
    kOpInvokeStatic  = 0xB8,
    kOpWide          = 0xC4,
    kMaxJvmOpcode    = 0xC9,
};

// Exact 202-byte JVM instruction length table from .rdata @ 0x180169800
extern const uint8_t kJvmOpcodeLengths[202];

// ============================================================================
// Bounded Big-Endian Stream Readers & Writers
// ============================================================================
struct ByteReader {
    const uint8_t* cur = nullptr;
    const uint8_t* end = nullptr;
};

struct ByteWriter {
    uint8_t* cur = nullptr;
    uint8_t* end = nullptr;
};

int read_bytes(ByteReader* reader, uint32_t len, void* out) noexcept; // sub_180040F30
int read_u1(ByteReader* reader, uint8_t* out) noexcept;               // sub_180040F80
int read_u2(ByteReader* reader, uint16_t* out) noexcept;              // sub_180040FB0
int read_u4(ByteReader* reader, uint32_t* out) noexcept;              // sub_180040FE0

int write_u1(ByteWriter* writer, uint8_t value) noexcept;             // sub_180041010
int write_u2(ByteWriter* writer, uint16_t value) noexcept;            // sub_180041030
int write_u4(ByteWriter* writer, uint32_t value) noexcept;
int write_bytes(ByteWriter* writer, const void* src, size_t len) noexcept;

// ============================================================================
// Constant Pool Views & Entry Resolvers
// ============================================================================
struct Utf8View {
    const char* data = nullptr;
    uint16_t length  = 0;
};

struct NameAndTypeView {
    Utf8View name;
    Utf8View descriptor;
};

struct MemberRefView {
    Utf8View owner;
    Utf8View name;
    Utf8View descriptor;
};

struct MethodSignature {
    const char* name       = nullptr;
    const char* descriptor = nullptr;
};

inline const char* wildcard_method_str() noexcept {
    return reinterpret_cast<const char*>(~uintptr_t{0});
}

struct ConstantPoolView {
    const uint8_t* const* entries = nullptr; // [constant_pool_count - 1]
    uint16_t entry_count          = 0;       // constant_pool_count - 1
    const uint8_t* pool_end       = nullptr;
};

using CpTagPredicateFn = bool (*)(uint8_t tag) noexcept;

int resolve_cp_utf8(const ConstantPoolView* cp, uint16_t cp_index, Utf8View* out) noexcept;             // sub_180041060
int resolve_cp_class(const ConstantPoolView* cp, uint16_t cp_index, Utf8View* out) noexcept;            // sub_180041100
int resolve_cp_name_and_type(const ConstantPoolView* cp, uint16_t cp_index, NameAndTypeView* out) noexcept; // sub_180041200
int resolve_cp_member_ref(
    const ConstantPoolView* cp,
    CpTagPredicateFn tag_pred,
    uint16_t cp_index,
    MemberRefView* out) noexcept;                                                                       // sub_180041320
int resolve_cp_method_ref(const ConstantPoolView* cp, uint16_t cp_index, MemberRefView* out) noexcept;  // sub_180041410
int resolve_cp_field_ref(const ConstantPoolView* cp, uint16_t cp_index, MemberRefView* out) noexcept;   // sub_180041500

int utf8_compare_cstr(const Utf8View* utf8, const char* cstr) noexcept;                                 // sub_1800415E0
bool utf8_equals_cstr(const Utf8View& utf8, const char* cstr) noexcept;
bool name_and_type_matches(const NameAndTypeView* nat, const MethodSignature* sig) noexcept;            // sub_180041630

// ============================================================================
// Bytecode Patch Descriptors & Visitor Callbacks
// ============================================================================
struct HookMethodEntry {
    const char* name           = nullptr; // +0x00
    const char* descriptor     = nullptr; // +0x08
    uint16_t resolved_cp_index = 0;       // +0x10 (populated by rewrite_class_bytecode)
};
static_assert(sizeof(HookMethodEntry) == 24, "HookMethodEntry must match 24-byte layout");

enum class HookInsnKind : uint64_t {
    InvokeInstance = 1, // invokevirtual (0xB6) / invokespecial (0xB7)
    InvokeStatic   = 2, // invokestatic  (0xB8)
    GetField       = 3, // getfield      (0xB4)
    GetStatic      = 4, // getstatic     (0xB2)
    PutField       = 5, // putfield      (0xB5)
    PutStatic      = 6, // putstatic     (0xB3)
};

struct InsnPatchTarget {
    const char* owner                = nullptr;                   // +0x00
    const char* name                 = nullptr;                   // +0x08
    const char* descriptor           = nullptr;                   // +0x10
    HookInsnKind kind                = HookInsnKind::InvokeInstance; // +0x18
    const uint16_t* hook_cp_index_ptr = nullptr;                  // +0x20 -> &HookMethodEntry::resolved_cp_index
};
static_assert(sizeof(InsnPatchTarget) == 40, "InsnPatchTarget must match 40-byte layout");

struct MethodPatchSpec {
    const char* method_name          = nullptr; // +0x00 (~0ULL = match all methods in class)
    const char* method_descriptor    = nullptr; // +0x08 (~0ULL = match all methods in class)
    const InsnPatchTarget* targets   = nullptr; // +0x10
    size_t target_count              = 0;       // +0x18
};
static_assert(sizeof(MethodPatchSpec) == 32, "MethodPatchSpec must match 32-byte layout");

struct ClassPatchSpec {
    jclass target_class                   = nullptr; // +0x00
    HookMethodEntry* hook_methods         = nullptr; // +0x08
    size_t hook_method_count              = 0;       // +0x10
    const MethodPatchSpec* method_patches = nullptr; // +0x18
    size_t method_patch_count             = 0;       // +0x20
};
static_assert(sizeof(ClassPatchSpec) == 40, "ClassPatchSpec must match 40-byte layout");

using InsnVisitorFn = int (*)(
    void* visitor_ctx,
    const ConstantPoolView* cp,
    ByteReader* in_insn,
    ByteWriter* out_insn);

using MethodSelectorFn = int (*)(
    void* selector_ctx,
    const NameAndTypeView* method_nat,
    InsnVisitorFn* out_visitor,
    void** out_visitor_ctx);

uint32_t compute_appended_cp_bytes(
    const char* hook_class_name,
    const HookMethodEntry* hook_methods,
    int hook_method_count) noexcept; // sub_1800416E0

int append_cp_class_entry(
    ByteWriter* writer,
    const char* class_name,
    uint16_t* io_next_cp_index,
    uint16_t* out_class_cp_index) noexcept; // sub_180042020

int dispatch_method_selector(
    const ConstantPoolView* cp,
    uint16_t name_index,
    uint16_t descriptor_index,
    MethodSelectorFn selector_fn,
    void* selector_ctx,
    InsnVisitorFn* out_visitor,
    void** out_visitor_ctx); // sub_180042100

int is_code_attribute_name(
    const ConstantPoolView* cp,
    uint16_t attribute_name_index,
    bool* out_is_code) noexcept; // sub_180042240

int rewrite_code_attribute(
    ByteReader* in_attr,
    ByteWriter* out_attr,
    const ConstantPoolView* cp,
    InsnVisitorFn visitor_fn,
    void* visitor_ctx); // sub_180042340

int walk_and_patch_bytecode(
    ByteReader* in_code,
    ByteWriter* out_code,
    const uint8_t* code_base,
    const ConstantPoolView* cp,
    InsnVisitorFn visitor_fn,
    void* visitor_ctx); // sub_180042420

int copy_and_patch_members(
    ByteReader* in_stream,
    ByteWriter* out_stream,
    const ConstantPoolView* cp,
    MethodSelectorFn selector_fn,
    void* selector_ctx); // sub_180041D30

int rewrite_class_bytecode(
    const uint8_t* class_bytes,
    int class_len,
    uint8_t** out_bytes,
    uint32_t* out_len,
    const char* hook_class_name,
    HookMethodEntry* hook_methods,
    int hook_method_count,
    MethodSelectorFn selector_fn,
    void* selector_ctx); // sub_180041760

int select_method_patcher(
    void* selector_ctx,
    const NameAndTypeView* method_nat,
    InsnVisitorFn* out_visitor,
    void** out_visitor_ctx) noexcept; // sub_180043DE0

int patch_member_instruction(
    void* visitor_ctx,
    const ConstantPoolView* cp,
    ByteReader* in_insn,
    ByteWriter* out_insn) noexcept; // sub_180043EA0

// ============================================================================
// Instruction-Aligned Pattern Matcher & Code Offset / StackMapTable Adjusters
// ============================================================================
struct PcEdit {
    uint32_t at_pc = 0; // original bytecode offset where insertion/deletion begins
    int32_t delta  = 0; // signed byte delta (+inserted -deleted)
};

uint32_t map_shifted_pc(
    uint32_t old_pc,
    const PcEdit* edits,
    size_t edit_count,
    bool inclusive_end = false) noexcept;

// Instruction-boundary-aware masked bytecode pattern matcher.
// Matches `pattern[i]` where `(code[pc + i] & mask[i]) == (pattern[i] & mask[i])`
// starting only at valid JVM instruction boundaries >= `start_offset`.
int match_opcode_pattern(
    const uint8_t* code,
    size_t code_len,
    const uint8_t* pattern,
    const uint8_t* mask,
    size_t pattern_len,
    size_t start_offset,
    size_t* out_match_offset) noexcept;

// Adjusts Code.exception_table[] {start_pc, end_pc, handler_pc, catch_type} in-place.
int adjust_exception_table_pcs(
    uint8_t* exception_table_bytes,
    uint16_t exception_table_length,
    const PcEdit* edits,
    size_t edit_count) noexcept;

// Adjusts LineNumberTable {start_pc, line_number} entries in-place.
int adjust_line_number_table_pcs(
    uint8_t* line_table_bytes,
    uint16_t line_table_length,
    const PcEdit* edits,
    size_t edit_count) noexcept;

// Adjusts LocalVariableTable or LocalVariableTypeTable {start_pc, length, ...} entries in-place.
int adjust_local_variable_table_pcs(
    uint8_t* local_table_bytes,
    uint16_t local_table_length,
    const PcEdit* edits,
    size_t edit_count) noexcept;

// Parses, shifts, and re-encodes a JVMS 4.7.4 StackMapTable attribute payload
// (automatically promoting compact same_frame / same_locals_1_stack_item_frame
// to extended forms 251 / 247 if offset_delta grows past 63, and updating
// ITEM_Uninitialized(8) bytecode offsets). Caller frees `*out_smt` with std::free.
int adjust_stack_map_table_frames(
    const uint8_t* in_smt,
    size_t in_smt_len,
    const PcEdit* edits,
    size_t edit_count,
    uint8_t** out_smt,
    uint32_t* out_smt_len) noexcept;

} // namespace slinky::bytecode
