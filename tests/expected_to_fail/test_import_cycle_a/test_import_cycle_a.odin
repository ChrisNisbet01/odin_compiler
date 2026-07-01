package test_import_cycle_a

import "test_import_cycle_b"

a_constant :: test_import_cycle_b.b_constant + 1
