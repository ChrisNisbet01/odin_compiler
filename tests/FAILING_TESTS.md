# Failing Tests List

## Currently Failing Tests (9 total):

1. test_array.odin - exit code 20
2. test_chained_member.odin - exit code 153
3. test_defer.odin - exit code 124 (TIMEOUT - hangs)
4. test_for.odin - exit code 124 (TIMEOUT - hangs)
5. test_if_assign.odin - exit code 246
6. test_scope.odin - exit code 1
7. test_struct.odin - exit code 202
8. test_switch.odin - exit code 1
9. test_using.odin - exit code 28

## Notes:
- Exit code 124 = timeout command killed the process (hung test)
- Need to fix each test individually