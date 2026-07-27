# Plan: Implement `context.temp_allocator` Support

## Goal
Enable the `context.temp_allocator` field to work properly in the Odin compiler, allowing procedures like `strings.builder_make(context.temp_allocator)` to allocate temporary memory that can be freed with `free_all(context.temp_allocator)`.

## Background from Official Odin

### Key Files Analyzed
- `~/projects/Odin/base/runtime/core.odin` — Defines `Context` struct with `temp_allocator: Allocator`
- `~/projects/Odin/base/runtime/default_temporary_allocator.odin` — Default temp allocator implementation
- `~/projects/Odin/base/runtime/default_temp_allocator_arena.odin` — Arena implementation for temp allocator

### Architecture

1. **Context Struct** (line 419 in core.odin)
   ```odin
   Context :: struct {
       allocator:              Allocator,
       temp_allocator:         Allocator,
       // ... other fields
   }
   ```

2. **Default Temp Allocator** (default_temp_allocator_arena.odin)
   - Uses an `Arena` underneath
   - Arena is a growing memory arena with linked blocks
   - Supports `arena_temp_begin` / `arena_temp_end` for scoped deallocation
   - `default_temp_allocator_proc` delegates to `arena_allocator_proc`

3. **Arena Structure** (lines 18-25)
   ```odin
   Arena :: struct {
       backing_allocator:  Allocator,   // Uses context.allocator for memory
       curr_block:         ^Memory_Block, // Current allocation block
       total_used:         uint,
       total_capacity:     uint,
       minimum_block_size: uint,
       temp_count:         uint,         // Nested temp scope counter
   }
   ```

4. **Memory_Block** (lines 8-14)
   ```odin
   Memory_Block :: struct {
       prev:      ^Memory_Block,
       allocator: Allocator,
       base:      [^]byte,
       used:      uint,
       capacity:  uint,
   }
   ```

5. **Scoped Allocation** (Arena_Temp, lines 258-262)
   ```odin
   Arena_Temp :: struct {
       arena: ^Arena,
       block: ^Memory_Block,  // Snapshot of current block
       used:  uint,           // Snapshot of used bytes
   }
   ```

### How Temp Allocator is Used

1. **Global Default Temp Allocator** (core.odin lines 825-829)
   ```odin
   c.temp_allocator.procedure = default_temp_allocator_proc
   c.temp_allocator.data = &global_default_temp_allocator_data
   ```

2. **Usage Pattern**
   ```odin
   // Allocate temporary memory
   tmp: strings.Builder
   tmp = strings.builder_make(context.temp_allocator)
   
   // Use it...
   
   // Free all temp memory at once (typically per "frame" or scope)
   free_all(context.temp_allocator)
   ```

3. **Scoped Temp (advanced)**
   ```odin
   temp := arena_temp_begin()
   // allocations...
   arena_temp_end(temp)  // Frees all allocations since temp_begin
   ```

## Implementation Plan

### Phase 1: Add Arena Infrastructure (Estimated: 2-3 days)

#### 1.1 Create Arena Type Descriptor
- [ ] Add `TD_KIND_ARENA` to `TypeDescriptor` union
- [ ] Add `get_or_create_arena_type()` function
- [ ] Arena LLVM type: `{ ptr backing_allocator, ptr curr_block, i64 total_used, i64 total_capacity, i64 minimum_block_size, i64 temp_count }`

#### 1.2 Implement Arena Allocator Type
- [ ] Create `Arena` type descriptor in `stubs/core/mem/virtual.odin` or similar
- [ ] Implement `arena_init(arena: ^Arena, size: uint, backing_allocator: Allocator)`
- [ ] Implement `arena_allocator_proc` (delegates to arena_alloc)
- [ ] Implement `arena_alloc`, `arena_free_all`, `arena_destroy`

#### 1.3 Add Arena to Type System
- [ ] Arena should be a reference type (pointer semantics)
- [ ] Arena fields: backing_allocator, curr_block (Memory_Block*), used, capacity, minimum_block_size, temp_count

### Phase 2: Implement Default Temp Allocator (Estimated: 1-2 days)

#### 2.1 Create Default Temp Allocator Structure
- [ ] Define `Default_Temp_Allocator` struct in `stubs/core/runtime/default_temporary_allocator.odin`
- [ ] Contains: `arena: Arena`

#### 2.2 Implement Allocator Procedure
- [ ] `default_temp_allocator_proc` delegates to `arena_allocator_proc`
- [ ] `default_temp_allocator_init(s: ^Default_Temp_Allocator, size: int, backing_allocator := context.allocator)`
- [ ] `default_temp_allocator_destroy(s: ^Default_Temp_Allocator)`

#### 2.3 Update Context Initialization
- [ ] In `ir_gen_top_level_decl` or entry point: initialize `context.temp_allocator`
- [ ] Set `temp_allocator.procedure = default_temp_allocator_proc`
- [ ] Set `temp_allocator.data = &global_default_temp_allocator_data`

### Phase 3: Implement Memory Block Type (Estimated: 1 day)

#### 3.1 Memory Block Structure
- [ ] Define `Memory_Block` struct
- [ ] Fields: prev (pointer), allocator, base ([^]byte), used, capacity
- [ ] Implement `memory_block_alloc`, `memory_block_dealloc`

### Phase 4: Implement Arena Temp (Scoped Allocation) (Estimated: 1 day)

#### 4.1 Arena_Temp Structure
- [ ] Define `Arena_Temp` struct for scoped allocation
- [ ] `arena_temp_begin(arena: ^Arena) -> Arena_Temp`
- [ ] `arena_temp_end(temp: Arena_Temp)`

### Phase 5: Integration with Existing Code (Estimated: 1 day)

#### 5.1 Update builder_make
- [ ] Modify `strings.builder_make(n: int)` to accept optional allocator parameter
- [ ] `strings.builder_make(n: int, allocator: Allocator)` — uses provided allocator

#### 5.2 Implement free_all
- [ ] Add `free_all(allocator: Allocator)` that calls `.Free_All` mode

### Phase 6: Testing (Estimated: 1 day)

#### 6.1 Add Tests
- [ ] Test basic temp_allocator allocation
- [ ] Test free_all clears all allocations
- [ ] Test multiple allocations in sequence
- [ ] Test builder_make with temp_allocator

## Key Implementation Details

### Memory Layout
- Arena uses linked list of Memory_Block
- Each block: Memory_Block header + allocation region
- Allocation: bump pointer within current block
- Growth: allocate new block via backing_allocator when current block exhausted

### Allocator Modes
- `Alloc`, `Alloc_Non_Zeroed` — allocate memory
- `Free` — not implemented (arena doesn't free individual allocations)
- `Free_All` — reset entire arena
- `Resize`, `Resize_Non_Zeroed` — support resizing
- `Query_Features` — report supported features

### LLVM IR Generation
- Arena is a struct type with ptr/int fields
- Memory_Block is a struct with prev ptr, allocator, base ptr, used/capacity ints
- Arena allocator proc is an intrinsic-like function
- Need to handle `free_all` as a call to the allocator proc with `.Free_All` mode

### Type System Considerations
- Arena is not a "basic" type — it's a struct-like reference type
- Memory_Block is internal, not exposed to Odin code
- Default_Temp_Allocator is a concrete struct visible in runtime package

## Dependencies
- Existing `Allocator` infrastructure (mem package)
- Existing `strings.Builder` which uses `[dynamic]byte` (needs to work with any allocator)
- `free_all` builtin or runtime function

## Estimated Total Effort
- **Total**: 7-10 days
- **Critical path**: Arena + Default_Temp_Allocator (3-5 days)
- **Testing**: 1 day

## References
- Official Odin: `~/projects/Odin/base/runtime/default_temporary_allocator.odin`
- Official Odin: `~/projects/Odin/base/runtime/default_temp_allocator_arena.odin`
- Official Odin: `~/projects/Odin/base/runtime/core.odin` (Context struct)