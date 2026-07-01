package test_import_cycle_b

import "test_import_cycle_a"

b_constant :: test_import_cycle_a.a_constant + 1
