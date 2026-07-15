package test_bounds_check_short_circuit

import "core:os"

main :: proc() {
    arr: [5]int
    arr[0] = 10
    arr[1] = 20
    arr[2] = 30
    arr[3] = 40
    arr[4] = 50

    slices: [3][]int
    slices[0] = arr[..]
    slices[1] = arr[1..]
    slices[2] = arr[..3]

    str := "hello"

    // ================================================================
    // SECTION A: Logical || with bounds-checked RHS only
    // (RHS gets split via ir_gen_emit_bounds_check, PHI's rhs_bb stale)
    // ================================================================

    // A1: || with RHS containing array subscript, RHS not evaluated path
    if arr[0] != 10 || arr[1] != 20 {
        os.exit(1)
    }

    // A2: || with RHS containing array subscript, LHS already true
    // (so RHS branch should be skipped — relevant for PHI entry_bb predecessor)
    if arr[0] == 10 || arr[2] != 30 {
        // OK, LHS was true so RHS path evaluated too — actually ||
        // evaluates RHS only when LHS is false, so when LHS is true
        // the RHS gets the "default true" from the entry branch.
    } else {
        os.exit(2)
    }

    // A3: || with RHS containing two subscripts (multiple bounds-check splits)
    if arr[3] != 40 || (arr[4] != 50 && arr[2] != 30) {
        os.exit(3)
    }

    // A4: || with RHS a slice subscript (different bounds-check call site)
    s := arr[..]
    if s[0] != 10 || s[2] != 30 {
        os.exit(4)
    }

    // A5: || with RHS a string subscript
    b := str[0]
    if b != 'h' || str[1] != 'e' {
        os.exit(5)
    }

    // A6: Multiple || in one expression (chained logical short-circuit)
    if arr[0] != 10 || arr[1] != 20 || arr[2] != 30 || arr[3] != 40 {
        os.exit(6)
    }

    // ================================================================
    // SECTION B: Logical && with bounds-checked RHS only
    // ================================================================

    // B1: && with RHS containing array subscript
    // (LHS true → RHS evaluated → bounds-check splits rhs_bb)
    if arr[0] == 10 && arr[1] == 20 {
        // OK
    } else {
        os.exit(7)
    }

    // B2: && with RHS false (short-circuits, RHS not evaluated)
    if arr[0] == 10 && arr[1] != 99 {
        // OK — LHS true and RHS true (arr[1] is 20 != 99)
    } else {
        os.exit(8)
    }

    // B3: && with RHS containing multiple subscripts
    if arr[0] == 10 && (arr[2] == 30 && arr[3] == 40) {
        // OK
    } else {
        os.exit(9)
    }

    // B4: && with RHS slice subscript
    if s[0] == 10 && s[4] == 50 {
        // OK
    } else {
        os.exit(10)
    }

    // B5: Chained &&
    if arr[0] == 10 && arr[1] == 20 && arr[2] == 30 && arr[3] == 40 && arr[4] == 50 {
        // OK
    } else {
        os.exit(11)
    }

    // ================================================================
    // SECTION C: Mixed || and && with subscripts (real-world pattern)
    // ================================================================

    // C1: || in if-condition with && chained inside
    if arr[0] == 10 || (arr[1] == 20 && arr[2] == 30) {
        // OK — short-circuit should make RHS lazy
    } else {
        os.exit(12)
    }

    // C2: && combined with || — both operands involve subscripts
    if (arr[0] == 10 && arr[1] == 20) || (arr[2] == 30 && arr[3] == 40) {
        // OK — entire chain covers all bounds-check paths
    } else {
        os.exit(13)
    }

    // C3: Subscript on RHS of || in a nested sub-expression
    sum := arr[0] + arr[1]
    if sum != 30 || arr[3] == 40 {
        // sum is 30, so LHS false, RHS evaluates
    } else {
        os.exit(14)
    }

    // C4: Subscript in BOTH LHS and RHS of ||
    if arr[0] == 10 || arr[4] == 50 {
        // both true, RHS branch evaluated when LHS false
    } else {
        os.exit(15)
    }

    // ================================================================
    // SECTION D: or_else integer/pointer variant (line 2402)
    // ================================================================

    // D1: 0 or_else arr[idx] — integer path with array subscript RHS
    n0 := 0 or_else arr[2]
    if n0 != 30 {
        os.exit(16)
    }

    // D2: nonzero or_else arr[idx] — LHS true, RHS not evaluated
    n1 := 42 or_else arr[3]
    if n1 != 42 {
        os.exit(17)
    }

    // D3: 0 or_else s[idx] — integer path with slice subscript RHS
    n2 := 0 or_else s[3]
    if n2 != 40 {
        os.exit(18)
    }

    // D4: 0 or_else with subscript in RHS of larger expression
    base := 5
    n3 := 0 or_else (arr[1] + base)
    if n3 != 25 {
        os.exit(19)
    }

    // D5: nil pointer or_else ptr_from_array[idx] — pointer path with bounds check
    // Use array of ^int so subscript result type matches LHS pointer type
    ptrs: [2]^int
    pv_int: int = 7
    ptrs[0] = &pv_int
    pv_int2: int = 999
    ptrs[1] = &pv_int2
    p: ^int = nil
    n4 := p or_else ptrs[0]
    // n4 is ^int — dereference to read the pointed-to integer (7)
    if n4^ != 7 {
        os.exit(20)
    }

    // D6: non-nil pointer or_else — LHS wins (no RHS bounds-check exercised)
    pp: ^int = &pv_int
    n5 := pp or_else ptrs[1]
    if n5^ != 7 {
        os.exit(21)
    }

    // ================================================================
    // SECTION E: or_else Maybe variant (line 2373)
    // ================================================================

    // E1: Maybe(int) = none, or_else arr[idx]
    m1: Maybe(int) = none
    e1 := m1 or_else arr[0]
    if e1 != 10 {
        os.exit(22)
    }

    // E2: Maybe(int) = some(value), or_else arr[idx] — expected to use payload
    m2: Maybe(int) = 999
    e2 := m2 or_else arr[1]
    if e2 != 999 {
        os.exit(23)
    }

    // E3: Maybe(int) uninitialized (zero = some(0)), or_else arr[idx]
    // A Maybe(int) with zero-init has tag=0 (some) and payload=0, so this
    // path uses the value 0, not the RHS.
    m3: Maybe(int)
    e3 := m3 or_else arr[4]
    if e3 != 0 {
        os.exit(24)
    }

    // E4: Maybe(int) = none, or_else s[idx] (slice subscript RHS)
    m4: Maybe(int) = none
    e4 := m4 or_else s[2]
    if e4 != 30 {
        os.exit(25)
    }

    // ================================================================
    // SECTION F: Stress — subscript in ternary-style position (existing
    // code already correct, but verify no regression)
    // ================================================================

    // F1: Ternary with subscript in branches (separate from this bug
    // but uses similar PHI pattern — verify still correct)
    cond := arr[0] == 10
    tv := cond ? arr[1] : arr[2]
    if tv != 20 {
        os.exit(26)
    }

    // ================================================================
    // SECTION G: Compound scenarios — || chained with or_else inside
    // ================================================================

    // G1: Bounds-checked subscript directly in condition, then or_else
    // The or_else path is exercised: nil ptr triggers RHS evaluation
    // which is a bounds-checked subscript.
    ok := (p == nil) || (arr[2] == 30)
    if !ok {
        os.exit(27)
    }

    // G2: subscript-derived value used in || with another subscript
    v0 := arr[0]
    v1 := arr[1]
    if (v0 != 10) || (v1 != 20) {
        os.exit(28)
    }

    // G3: Slice subscript in chained || (each operand triggers bounds check)
    if s[0] != 10 || s[1] != 20 || s[2] != 30 || s[3] != 40 || s[4] != 50 {
        os.exit(29)
    }

    // ================================================================
    // SECTION H: Out-of-path safety — verify subscript values correct
    // when bounds check would NOT trigger a split (still produce right value)
    // ================================================================

    // H1: Simple subscript read after all the above chaos
    last := arr[4]
    if last != 50 {
        os.exit(30)
    }

    // H2: Slice subscript read after all
    slast := s[4]
    if slast != 50 {
        os.exit(31)
    }

    os.exit(0)
}
