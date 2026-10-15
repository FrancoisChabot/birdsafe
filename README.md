# BirdSafe & Chirp

**BirdSafe** is a modular, high-performance C11 monorepo housing the **Chirp** programming language implementation alongside its foundational runtime libraries. The project targets 64-bit architectures, delivering a mathematically grounded type-and-set substrate, an arbitrary-precision integer engine, a zero-copy language frontend, and a fiber-based bytecode virtual machine.

---

## Architecture & Components

The repository is organized into modular libraries under [`libs/`](libs/):

### Chirp Language System

- **[`libs/chirp_frontend`](libs/chirp_frontend) (`chirp_frontend`)**: Language frontend:
  - **Lexer**: Zero-copy UTF-8 scanner emitting string slices ([`chirp_str_view`](libs/chirp_frontend/include/chirp_frontend/chirp_str_view.h)), format string interpolation tokens, and Unicode set operators (`∈`, `∉`, `∪`, `∩`).
  - **Parser**: Precedence-climbing recursive-descent parser supporting statement/expression unification, pattern matching, contextual binding modifiers (`mut`, `final`, `pub`), f-strings, anonymous struct literals, and function sugar.
  - **AST & Arena**: Monotonic arena allocator ([`chirp_ast_arena`](libs/chirp_frontend/include/chirp_frontend/chirp_ast_arena.h)) enabling $O(1)$ batch memory deallocation, accompanied by an S-expression AST printer.
- **[`libs/chirp_vm`](libs/chirp_vm) (`chirp_vm`)**: Execution runtime:
  - Bytecode chunk encoding, constant pools, and disassembler.
  - Stack-based bytecode execution engine with cooperative fibers ([`chirp_fiber`](libs/chirp_vm/include/chirp_vm/chirp_vm.h)), call frames, and error trapping.

### Foundational Support Substrate (`bs_`)

- **[`libs/core`](libs/core) (`bs_core`)**: Runtime foundation:
  - Pluggable memory allocators (`bs_allocator_t`, `bs_alloc`, `bs_calloc`, `bs_realloc`, `bs_free`).
  - Two-tiered 32-bit packed status codes (`bs_status`) and three-valued Kleene logic (`bs_truth`).
  - Cross-platform threading/atomics wrappers, architecture assertions, and symbol visibility macros (`BS_API`).
- **[`libs/num`](libs/num) (`bs_num`)**: High-performance numerics runtime:
  - Arbitrary-precision integer engine featuring 63-bit immediate tagged fixnums and reference-counted bignums (`bs_int`).
  - Seamless unbounded arithmetic, bitwise manipulation, shifts, and string base conversions.
- **[`libs/sets`](libs/sets) (`bs_sets`)**: Set theory and nature system:
  - Core universe, nature system, value handles (`bs_value`), and set stores (`bs_set_store`).
  - Finite and infinite minted natures, discrete interval sets, composite sets (unions, intersections, complements), and custom predicate sets.
  - Backed by `bs_num` for unbounded integer arithmetic and `bs_core` for memory allocation.

### Documentation & Specifications

- **[`doc/`](doc/)**: Formal language specifications, including lexical structure ([`04_lexical.md`](doc/04_lexical.md)) and syntax grammar ([`05_grammar.md`](doc/05_grammar.md)).
- **[`dev.md`](dev.md)**: Authoritative architectural principles, including the zero-overhead release policy for contract checks and the set-once-at-startup allocator model.

---

## Architectural Principles

1. **Zero Runtime Cost for Programming Errors**: In release builds (`NDEBUG`), contract violations and programmer errors incur zero overhead (no extra branches, state checks, or synchronization). In debug builds, comprehensive assertions and sanitizers catch violations instantly. See [`dev.md`](dev.md).
2. **Set-Once Allocator Lifecycle**: The process-wide allocator is configured once during single-threaded startup via `bs_set_allocator()`.
3. **Monotonic Arena Memory**: Compilers and parsers use monotonic arenas (`chirp_ast_arena`) for high-speed cache-local allocation and zero-overhead AST teardown.

---

## Building and Testing

Prerequisites: A C11-compliant C compiler (GCC 9+, Clang 10+, or MSVC 2019+) and CMake 3.20+.

### Standard Build (Release)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Sanitizer Build (Debug + ASan/UBSan)

```sh
cmake -S . -B build_asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g"
cmake --build build_asan
ctest --test-dir build_asan --output-on-failure
```

---

## Modular Embedding & Usage

All libraries are modular and can be linked as needed by downstream applications:

```cmake
# Using the full Chirp environment:
target_link_libraries(my_app PRIVATE 
    birdsafe::chirp_frontend 
    birdsafe::chirp_vm
)

# Or consuming only the foundational substrate:
target_link_libraries(my_tool PRIVATE 
    birdsafe::bs_sets 
    birdsafe::bs_num 
    birdsafe::bs_core
)
```

Downstream projects using CMake FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
    birdsafe
    GIT_REPOSITORY https://github.com/FrancoisChabot/birdsafe.git
    GIT_TAG main
)
FetchContent_MakeAvailable(birdsafe)
```
