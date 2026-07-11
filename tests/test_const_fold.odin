package test_const_fold

import "core:os"

// Compile-time constant with bitwise OR of named constants
FLAGS :: os.O_WRONLY | os.O_CREAT | os.O_TRUNC

// Compile-time constant with bitwise OR of literals
MASK :: 0x100 | 0x040 | 0x004

// when condition with named constant bitwise OR
when os.O_WRONLY | os.O_CREAT {
    WHEN_FLAG :: 1
} else {
    WHEN_FLAG :: 0
}

// when condition with literal bitwise OR
when 1 | 2 {
    WHEN_LIT :: 1
} else {
    WHEN_LIT :: 0
}

main :: proc() {
    // Runtime bitwise OR of named constants
    flags := os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    if flags != 577 {
        os.exit(1)
    }

    // Compile-time constant
    if FLAGS != 577 {
        os.exit(2)
    }

    // Literal bitwise OR constant
    if MASK != 0x144 {
        os.exit(3)
    }

    // when condition with named constants
    if WHEN_FLAG != 1 {
        os.exit(4)
    }

    // when condition with literals
    if WHEN_LIT != 1 {
        os.exit(5)
    }

    os.exit(0)
}
