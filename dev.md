# BirdSafe Developer Guide & Architectural Principles

This document outlines the core architectural principles, design guidelines, and code review standards for BirdSafe (`bs_`). It serves as an authoritative reference for contributors and code reviewers.

---

## 1. Zero Runtime Cost for Programming Errors

### The Principle

> **In release builds, we incur zero runtime cost (CPU cycles, branches, memory, or synchronization) to guard against programming errors.**

BirdSafe is a foundational C11 runtime designed for high-performance language environments and substrate systems. Every cycle and branch in core paths matters.

### Programming Errors vs. Operational Failures

To apply this principle consistently, BirdSafe strictly distinguishes between two categories of failures:

| Category | Definition | Handling in BirdSafe | Build Configuration |
| :--- | :--- | :--- | :--- |
| **Operational / Environmental Failures** | Conditions that can arise at runtime in correct code due to resource limits, external inputs, or system state (e.g., out-of-memory, invalid serialized data, arithmetic overflow in unbounded inputs, callback errors). | Explicit status codes (`bs_status`), return `false`, or return `NULL`. Must be handled gracefully by callers. | **All builds** (Debug and Release). |
| **Programming Errors (Contract Violations)** | Violations of documented API contracts, invariants, or sequencing rules caused by bugs in caller code (e.g., passing invalid handles, violating lifecycle constraints, bad initialization order, null pointers where forbidden). | Assertions (`assert()`) and validation checks. Fast abort / fatal trap. | **Debug builds only** (`#ifndef NDEBUG`). Stripped in Release (`NDEBUG`). |

### Why Release Guards Are Forbidden for Contract Violations

1. **Meaningless Recovery**: A program that violates basic sequencing or memory ownership contracts (such as swapping an allocator mid-flight) has corrupted internal state. Returning a status code provides a false sense of security; callers cannot safely recover from a broken invariant.
2. **Taxing Correct Code**: Defensive checks in release builds penalize well-behaved, verified code by forcing it to pay a permanent tax of branches, cache footprint, and pipeline stalls.
3. **Debug Catches Bugs, Release Goes Fast**: Thorough verification, debug builds, address/undefined-behavior sanitizers (`-fsanitize=address,undefined`), and exhaustive test suites are where contract violations are discovered and fixed.

### Case Study: Allocator Freezing (`bs_set_allocator`)

Reviewers frequently ask:
> *"Why doesn't `bs_set_allocator()` check an `allocator_frozen` flag or return an error in release builds if someone calls it after allocations have started?"*

**The Rationale:**
- **The Contract**: `bs_set_allocator()` must be called during single-threaded startup before any BirdSafe allocations occur.
- **The Bug**: Calling `bs_set_allocator()` after allocations have already begun is an irrecoverable programming error. If allowed, subsequent deallocations would pass pointers allocated by the old allocator to the new allocator's `free()` callback, corrupting the heap.
- **The Implementation**:
  - The `allocator_frozen` flag and `freeze_allocator()` tracking exist **exclusively** under `#ifndef NDEBUG`.
  - In debug builds, attempting to call `bs_set_allocator()` after allocations begin triggers `assert(!allocator_frozen)`.
  - In release builds (`NDEBUG`), the tracking flag and checks are compiled out completely. The runtime does not check a boolean, execute a branch, or access global state on allocation paths.

---

### Operational Error Design: Two-Tiered Packed Status (`bs_status`)

Operational errors that callers can handle or recover from are communicated via `bs_status`, a 32-bit packed scalar layout:
`[ 16-bit Domain ID | 16-bit Subsystem Code ]`

- **`BS_OK == 0`**: Checking for success is a single-instruction test (`if (status)` or `bs_status_is_ok(status)`).
- **Subsystem Decoupling**: Each library defines its own status codes within its assigned `bs_domain` (`BS_DOMAIN_CORE`, `BS_DOMAIN_NUM`, `BS_DOMAIN_SETS`, `BS_DOMAIN_VM`).
- **Enums for Visibility**: Codes are defined as enum constants using `BS_STATUS_MAKE(domain, code)` rather than `#define` macros, ensuring GDB/LLDB and IDE watch windows display symbolic names (e.g., `BS_SETS_EXHAUSTED`) rather than magic integers.
- **Introspection**: Helper macros `bs_status_domain(s)` and `bs_status_code(s)` extract components, and `bs_status_domain_name(s)` returns the domain name string.

---

## 2. Public Header Comment Terseness

Public headers in `include/bs_*/*.h` are the primary interface contracts for developers and automated tooling. They should be clean, precise, and immediately scannable.

### Guidelines for Header Comments

1. **Document the Contract, Not the Implementation**:
   - Focus on: pre-conditions, post-conditions, ownership transfer, lifetime expectations, and return values (especially failure modes like `NULL` or error codes).
   - Omit internal algorithms, private helpers, and storage layout details. Implementation details belong in `.c` files or private headers.

2. **Prefer Crisp One-Liners over Narrative Blocks**:
   - Keep doc comments concise. If a function's purpose and contract can be expressed in a single line, use a single line.
   ```c
   /** Allocates uninitialized memory. Returns NULL on OOM. */
   BS_API void *bs_alloc(size_t size);

   /** Resizes an allocated block. Returns NULL on failure (original block untouched). */
   BS_API void *bs_realloc(void *ptr, size_t new_size);

   /** Frees memory allocated via bs_alloc/bs_calloc/bs_realloc. Safe to pass NULL. */
   BS_API void bs_free(void *ptr);
   ```

3. **Avoid Obvious Parameter Echoes**:
   - Do not repeat parameter names and types with empty descriptions:
     ```c
     /* BAD: Verbose noise that adds no insight */
     /**
      * @brief Frees memory.
      * @param ptr Pointer to the memory block.
      * @return void
      */
     void bs_free(void *ptr);

     /* GOOD: Concise contract */
     /** Frees memory allocated via bs_alloc. Safe to pass NULL. */
     void bs_free(void *ptr);
     ```

4. **Group Function Declarations Logically**:
   - Organize public headers into thematic sections using clean banner comments:
     ```c
     /* ========================================================================= */
     /* Consumer Allocation API                                                   */
     /* ========================================================================= */
     ```

5. **If it Takes Three Paragraphs, Question the API**:
   - Needing an essay in a public header usually signals that an API has too many edge cases, coupled side-effects, or ambiguous ownership. Simplify the interface design first.

---

## 3. Tiered Concurrency Architecture

BirdSafe employs a **Tiered Concurrency Model** to achieve maximum multi-threaded performance while maintaining a clean, zero-overhead single-threaded operational path.

### Concurrency Tiers

1. **Tier 1: Value Handles (`bs_int`, `bs_value`) — Lock-Free & Thread-Safe**
   - Numeric and value handles can cross thread boundaries freely without synchronization.
   - Bignums use lock-free atomic reference counting (`bs_atomic_uint32_t ref_count` with acquire/release semantics).
   - Value handles (`bs_value`) are immutable value types passed by value.

2. **Tier 2: Substrate Container (`bs_universe`) — Thread-Safe Concurrent Substrate**
   - A single `bs_universe` can be safely shared across multiple threads simultaneously (`const bs_universe *`).
   - Dynamic registration of natures (`bs_nature_mint_finite`, `bs_nature_mint_nonfinite`) is serialized across threads via an internal universe mutex (`bs_mutex_t natures_mutex`).
   - Value construction and bignum payload anchoring during interval evaluation are lock-free, utilizing atomic CAS stack insertion (`bs_atomic_ptr_t payload_objects`).
   - Non-finite nature value minting (`bs_nature_mint_value`) uses atomic fetch-add / compare-exchange on `nature->next_payload` to guarantee monotonically unique IDs without locking.
   - In debug builds, active store tracking (`universe->active_stores`) uses atomic operations to avoid races.

3. **Tier 3: Working Arenas (`bs_set_store`) — Thread-Confined Arenas**
   - Set stores are evaluation workspaces and are **thread-confined**.
   - Multiple threads must not access the same `bs_set_store` concurrently without external synchronization.
   - By confining stores to individual threads (or worker arenas), all internal set allocations, DAG graph manipulations, and algebraic simplifications execute with **zero mutex or atomic overhead**.

---

## 4. Checklist for Code Reviewers

When reviewing pull requests in BirdSafe, verify:

- [ ] **No Release Cost for Contract Checks**: Are parameter or invariant checks guarded with `#ifndef NDEBUG` or `assert()`? Ensure no release-build branches were added to catch programmer bugs.
- [ ] **Proper Error Codes for Runtime Failures**: Are operational errors (OOM, external parse failures, callback failures) reported via `bs_status` or explicit return values?
- [ ] **Minimal and Terse Headers**: Are public header comments concise, focused on contracts, and free of implementation details?
- [ ] **Tiered Concurrency Contract Respected**: Are arenas thread-confined without mutex overhead? Are shared substrates thread-safe and value handles lock-free?
- [ ] **No Speculative or Dead Code**: Are all newly introduced types, functions, and macros actually utilized? No placeholders for hypothetical future features.
- [ ] **Zero Overhead Intrinsics**: Are hardware intrinsics and private utilities isolated in private headers rather than leaked into public headers?
- [ ] **Sanitizer and Leak Cleanliness**: Do all tests pass cleanly under AddressSanitizer and UndefinedBehaviorSanitizer without leaks?
