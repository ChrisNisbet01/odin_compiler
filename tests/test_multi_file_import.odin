package test_multi_file_import

import "multi_import_dep"
import "core:fmt"

main :: proc() {
    result := multi_import_dep.add(20, 22)
    fmt.println(result)

    result2 := multi_import_dep.sub(50, 8)
    fmt.println(result2)

    msg := multi_import_dep.greet()
    fmt.println(msg)

    result3 := multi_import_dep.double_len("hello")
    fmt.println(result3)
}
