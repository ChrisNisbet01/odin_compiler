package test_core_import

import "core:fmt"
import "core:os"

main :: proc() {
    fmt.println("hello from core:fmt!")
    os.exit(0)
}
