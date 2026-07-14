package test_overload_ambiguous

fn1 :: proc(x: int) -> int { return 1 }
fn2 :: proc(x: int) -> int { return 2 }
ambig :: proc{fn1, fn2}

main :: proc() {
    r := ambig(42) // Both fn1 and fn2 accept int — ambiguous
}
