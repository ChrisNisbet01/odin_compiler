# Test Suite Restructure Plan

**Problem**: The test runner cannot distinguish between legitimate multi-file package tests and helper directories containing `.odin` files (e.g., `tests/a/`, `tests/b/` used for import cycle tests).

## Proposed Solution

Restructure the test directory layout to eliminate ambiguity without changing compiler behavior.

---

## Step 1: Audit Current Test Structure
**Goal**: Identify directories needing explicit markers.
1. Run `./run_tests.sh --dry-run | tee test_run_inspection.log`
2. Grep for misclassified directories: `grep -E "Testing (multi-file|single-file)" test_run_inspection.log`
3. List directories containing `.odin_pkg` alongside unrelated `.odin` files.

---

## Step 2: Manual Review of Misclassified Directories
**Goal**: Confirm which directories should be marked as packages.
1. Inspect contents of suspicious directories (`tests/a/`, `tests/b/`)
2. Determine if these are:
   - Legitimate packages (deserving markers)
   - Helpers (used by other tests but not standalone)

---

## Step 3: Reorganize Test Folders
**Options**:
- **Option 1**: Relocate helpers to `tests/_helpers/` (no marker needed)
- **Option 2**: Add explicit metadata file (`.test_folder`)
- **Option 3**: Use naming convention (`pkg_*` prefix for package directories)

---

## Step 4: Update Test Runner Script
**Goal**: Scope discovery to validation markers, not leaf directories.
1. Modify `run_tests.sh` to exclude helper directories
2. Add exclusion rules for `expected_to_fail/` or use marker-based discovery

---

## Step 5: Final Verification
1. Confirm no false positives using `grep`/`read` tools

---

## Risks and Mitigations
| Risk | Mitigation |
|------|------------|
| Accidental deletion of markers | Use git to track changes |
| Objection to metadata files | Adopt semantic naming (`pkg_*`) |

---

**Next Steps** (pending approval):
- Proceed with structure audit?
- Choose Option 1, 2, or 3?
- Review mock draft of revised test runner logic?