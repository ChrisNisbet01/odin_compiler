package main
import "core:os"

main :: proc() {
    // Basic complex type declarations (zero-initialized)
    z32: complex32
    z64: complex64
    z128: complex128

    // Quaternion types
    q64: quaternion64
    q128: quaternion128
    q256: quaternion256

    // size_of checks
    s32 := size_of(complex32)
    s64 := size_of(complex64)
    s128 := size_of(complex128)

    result: int = 0
    if s32 == 4 { result = result + 1 }
    if s64 == 8 { result = result + 1 }
    if s128 == 16 { result = result + 1 }

    // Quaternion sizes
    qs64 := size_of(quaternion64)
    qs128 := size_of(quaternion128)
    qs256 := size_of(quaternion256)

    if qs64 == 8 { result = result + 1 }
    if qs128 == 16 { result = result + 1 }
    if qs256 == 32 { result = result + 1 }

    os.exit(result - 6)
}
