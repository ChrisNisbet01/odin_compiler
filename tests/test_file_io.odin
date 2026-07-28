package main

import "core:os"

main :: proc() {
    // Open file for writing (O_WRONLY | O_CREAT | O_TRUNC, mode 420 = 0o644)
    fd, err := os.open("/tmp/odin_io_test.tmp", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 420)
    if err != 0 { os.exit(1) }

    // Write "ABCD" as raw bytes
    wbuf: [4]byte
    wbuf[0] = 65
    wbuf[1] = 66
    wbuf[2] = 67
    wbuf[3] = 68
    n, err := os.write(fd, &wbuf[0], 4)
    if err != 0 || n != 4 { os.exit(2) }

    os.close(fd)

    // Open for reading
    fd, err = os.open("/tmp/odin_io_test.tmp", os.O_RDONLY, 0)
    if err != 0 { os.exit(3) }

    // Read back and verify
    rbuf: [4]byte
    n, err = os.read(fd, &rbuf[0], 4)
    if err != 0 || n != 4 { os.exit(4) }
    if rbuf[0] != 65 { os.exit(5) }
    if rbuf[1] != 66 { os.exit(6) }
    if rbuf[2] != 67 { os.exit(7) }
    if rbuf[3] != 68 { os.exit(8) }

    os.close(fd)
    os.exit(0)
}
