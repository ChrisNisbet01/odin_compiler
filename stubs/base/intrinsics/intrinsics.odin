package intrinsics

// Compile-time type query intrinsics. These are polymorphic procedures with
// `---` bodies: they are never codegen'd at runtime. The compiler recognizes
// them by name in `where` clause evaluation (see poly_eval_intrinsic in
// polymorphism.c) and folds them to compile-time constants.
//
// This is a subset of Odin's base/intrinsics/intrinsics.odin — only the
// "constant type tests" that carry no `where` clause on their own declaration.

type_base_type :: proc($T: typeid) -> type ---
type_core_type :: proc($T: typeid) -> type ---
type_elem_type :: proc($T: typeid) -> type ---

type_is_boolean    :: proc($T: typeid) -> bool ---
type_is_integer    :: proc($T: typeid) -> bool ---
type_is_rune       :: proc($T: typeid) -> bool ---
type_is_float      :: proc($T: typeid) -> bool ---
type_is_complex    :: proc($T: typeid) -> bool ---
type_is_quaternion :: proc($T: typeid) -> bool ---
type_is_typeid     :: proc($T: typeid) -> bool ---
type_is_any        :: proc($T: typeid) -> bool ---
type_is_string     :: proc($T: typeid) -> bool ---
type_is_unsigned   :: proc($T: typeid) -> bool ---
type_is_numeric    :: proc($T: typeid) -> bool ---
type_is_ordered    :: proc($T: typeid) -> bool ---
type_is_ordered_numeric :: proc($T: typeid) -> bool ---
type_is_indexable  :: proc($T: typeid) -> bool ---
type_is_sliceable  :: proc($T: typeid) -> bool ---
type_is_comparable :: proc($T: typeid) -> bool ---
type_is_pointer         :: proc($T: typeid) -> bool ---
type_is_multi_pointer   :: proc($T: typeid) -> bool ---
type_is_array           :: proc($T: typeid) -> bool ---
type_is_slice           :: proc($T: typeid) -> bool ---
type_is_dynamic_array   :: proc($T: typeid) -> bool ---
type_is_map             :: proc($T: typeid) -> bool ---
type_is_struct          :: proc($T: typeid) -> bool ---
type_is_union           :: proc($T: typeid) -> bool ---
type_is_enum            :: proc($T: typeid) -> bool ---
type_is_proc            :: proc($T: typeid) -> bool ---
type_is_bit_set         :: proc($T: typeid) -> bool ---
type_is_bit_field       :: proc($T: typeid) -> bool ---
type_is_simd_vector     :: proc($T: typeid) -> bool ---
type_is_matrix          :: proc($T: typeid) -> bool ---
type_has_nil            :: proc($T: typeid) -> bool ---
