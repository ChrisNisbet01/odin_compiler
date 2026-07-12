package main
import "core:os"

#assert[true]
#assert[!false]
#assert[1 + 1 == 2]
#assert[size_of(int) == 8]
#assert[align_of(int) == 8]
#assert[typeid_of(int) == typeid_of(int)]

main :: proc() {
    os.exit(0)
}
