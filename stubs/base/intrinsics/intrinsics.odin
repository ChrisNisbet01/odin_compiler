package intrinsics

// Compile-time type query intrinsics. These are polymorphic procedures with
// `---` bodies: they are never codegen'd at runtime. The compiler recognizes
// them by name in `where` clause evaluation (see poly_eval_intrinsic in
// polymorphism.c) and folds them to compile-time constants.
//
// This is a subset of Odin's base/intrinsics/intrinsics.odin — only the
// "constant type tests" that carry no `where` clause on their own declaration.

type_base_type          :: proc($T: typeid) -> type ---
type_core_type          :: proc($T: typeid) -> type ---
type_elem_type          :: proc($T: typeid) -> type ---

type_is_boolean         :: proc($T: typeid) -> bool ---
type_is_integer         :: proc($T: typeid) -> bool ---
type_is_rune            :: proc($T: typeid) -> bool ---
type_is_float           :: proc($T: typeid) -> bool ---
type_is_complex         :: proc($T: typeid) -> bool ---
type_is_quaternion      :: proc($T: typeid) -> bool ---
type_is_typeid          :: proc($T: typeid) -> bool ---
type_is_any             :: proc($T: typeid) -> bool ---
type_is_string          :: proc($T: typeid) -> bool ---
type_is_unsigned        :: proc($T: typeid) -> bool ---
type_is_numeric         :: proc($T: typeid) -> bool ---
type_is_ordered         :: proc($T: typeid) -> bool ---
type_is_ordered_numeric :: proc($T: typeid) -> bool ---
type_is_indexable       :: proc($T: typeid) -> bool ---
type_is_sliceable       :: proc($T: typeid) -> bool ---
type_is_comparable      :: proc($T: typeid) -> bool ---
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
type_is_string16        :: proc($T: typeid) -> bool ---
type_is_cstring         :: proc($T: typeid) -> bool ---
type_is_cstring16       :: proc($T: typeid) -> bool ---
type_is_endian_platform :: proc($T: typeid) -> bool ---
type_is_endian_little   :: proc($T: typeid) -> bool ---
type_is_endian_big      :: proc($T: typeid) -> bool ---
type_is_valid_map_key   :: proc($T: typeid) -> bool ---
type_is_valid_matrix_elements :: proc($T: typeid) -> bool ---
type_is_named           :: proc($T: typeid) -> bool ---

type_is_matrix_row_major    :: proc($T: typeid) -> bool where type_is_matrix(T) ---
type_is_matrix_column_major :: proc($T: typeid) -> bool where type_is_matrix(T) ---

// Matrix intrinsics — declared with `---` bodies. They are never codegen'd
// directly: callers either specialise a polymorphic procedure that uses them
// (resolved through polymorphic specialisation, which emits a real body) or
// invoke the name-matched intrinsic path in the IR generator (see
// ir_gen_postfix_transpose). Aliasing them as values in user packages
// (e.g. `linalg/general.odin`: `transpose :: intrinsics.transpose`) only
// records the procedure symbol and produces no runtime reference to the
// body-less declaration, so no link-time undefined symbols result.
outer_product    :: proc "contextless" ($T: typeid, a: ^T, b: ^T) -> T #no_bounds_check ---
transpose        :: proc "contextless" ($T: typeid, m: T) -> T #no_bounds_check ---
hadamard_product :: proc "contextless" ($T: typeid, a: T, b: T) -> T #no_bounds_check ---
matrix_flatten   :: proc "contextless" ($T: typeid, m: T) -> T #no_bounds_check ---
