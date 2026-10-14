# BirdSafe (`bs_`)

BirdSafe is a modular, high-performance C11 foundational runtime and substrate for Chirp and language runtime environments. The current implementation targets 64-bit architectures. Other architectures can be supported if a need arises.

## Architecture

The BirdSafe monorepo organizes foundational services in the `libs/` directory:

- **`libs/core` (`bs_core`)**: Common runtime primitives:
  - Pluggable memory allocators (`bs_allocator_t`, `bs_alloc`, `bs_calloc`, `bs_realloc`, `bs_free`).
  - Unified status codes (`bs_status`) and three-valued logic (`bs_truth`).
  - Architecture assertions and symbol visibility macros (`BS_API` in `bs_config.h`).
- **`libs/num` (`bs_num`)**: Numerics runtime:
  - Arbitrary-precision integer engine with 63-bit immediate fixnums and reference-counted bignums (`bs_int`); the fixnum representation currently requires a 64-bit pointer-sized word.
  - High-performance arithmetic, bitwise operations, shifts, and base conversions.
- **`libs/sets` (`bs_sets`)**: Set theory, natures, and universes:
  - Core universe, nature, value handles (`bs_value`), and set stores (`bs_set_store`).
  - Finite and infinite minted natures, discrete interval sets, composite and predicate sets.
  - Powered by `bs_num` for unbounded integer arithmetic and `bs_core` for memory allocation.

The process-wide allocator is selected during single-threaded startup. Call
`bs_set_allocator()` before the first BirdSafe allocation and before starting
any worker threads. `bs_set_allocator()` returns `true` on success and `false` if rejected
(e.g., if any required callback is NULL). Under the "set once at startup" contract, the
allocator should not be changed once configured or after allocations have begun; in debug builds
(when `NDEBUG` is not defined), calling `bs_set_allocator()` after allocations have started or
more than once triggers an assertion. Release builds incur zero runtime freezing checks.
See [dev.md](dev.md) for detailed architectural principles.

## Building and Testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

To run with Address and Undefined Behavior Sanitizers:

```sh
cmake -S . -B build_asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g"
cmake --build build_asan
ctest --test-dir build_asan --output-on-failure
```

## Integration in Higher-Level Projects (e.g. Chirp)

```cmake
include(FetchContent)
FetchContent_Declare(
    birdsafe
    GIT_REPOSITORY https://github.com/FrancoisChabot/birdsafe.git
    GIT_TAG main
)
FetchContent_MakeAvailable(birdsafe)

target_link_libraries(chirp_runtime PRIVATE bs_sets::bs_sets bs_num::bs_num)
```
