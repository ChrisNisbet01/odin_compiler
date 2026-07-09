package test_multi_return
import "core:os"
// Procedure returning multiple anonymous values
foo :: proc() -> (int, bool)
{
    return 42, true
}

// Procedure with single return (still works)
bar :: proc(x: int) -> int
{
    return x * 2
}

// Void procedure (still works)
baz :: proc()
{
    return
}

main :: proc()
{
    a, b := foo()
    // a = 42, b = true

    c := bar(5)
    // c = 10

    // Check values
    if a != 42 {
        os.exit(1)
    }
    if b != true {
        os.exit(2)
    }
    if c != 10 {
        os.exit(3)
    }
    
    baz()

    os.exit(0)
}
