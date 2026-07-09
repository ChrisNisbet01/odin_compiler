package main
import "core:os"

main :: proc() {
    // complex() constructor with f64 args -> complex128
    c := complex(1.0, 2.0)
    #assert[size_of(complex128) == 16]

    // complex() constructor with f32 args -> complex64
    c2 := complex(f32(1.0), f32(2.0))
    #assert[size_of(complex64) == 8]

    // quaternion() constructor with f64 args -> quaternion256
    q := quaternion(1.0, 2.0, 3.0, 4.0)
    #assert[size_of(quaternion256) == 32]

    // quaternion() constructor with f32 args -> quaternion128
    q2 := quaternion(f32(1.0), f32(2.0), f32(3.0), f32(4.0))
    #assert[size_of(quaternion128) == 16]

    // Default zero-initialized complex64
    c3: complex64
    #assert[size_of(complex64) == 8]

    // Default zero-initialized quaternion128
    q3: quaternion128
    #assert[size_of(quaternion128) == 16]

    os.exit(0)
}
