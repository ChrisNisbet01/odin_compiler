package intrinsics

// Compiler intrinsics
read_cycle_counter :: proc "contextless" () -> i64 ---
read_time_stamp_counter :: proc "contextless" () -> i64 ---
cpu_relax :: proc "contextless" () ---
debug_trap :: proc "contextless" () ---
