# Contributing to Dagwood

Dagwood is maintained as a personal project -- closer in spirit to
SQLite's development model than a typical collaborative GitHub
project. Bug reports, small fixes, test coverage, and documentation
improvements are very welcome. Larger features and changes to the DSL
surface, the public C structures, or the execution model are at the
maintainer's discretion and may be declined even when technically
sound; please open an issue to discuss before investing time in
anything beyond a small patch.

Dagwood is pre-1.0, so the surface is still moving. The notes below
cover what you need to know to get a change landed.

## Build and test

Dagwood uses CMake. The standard development build enables tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDAGWOOD_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For an AddressSanitizer + UndefinedBehaviorSanitizer build (catches
memory leaks and UB the regular build never sees):

```bash
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDAGWOOD_ENABLE_ASAN=ON \
    -DDAGWOOD_BUILD_TESTS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

To run both flavors end-to-end with a single command:

```bash
./test/check_all.sh
```

Run `./test/check_all.sh` before any commit that touches memory
ownership in `src/lcl_interp.c` or `src/graph.c`. ASan has caught real
bugs that the regular build missed (see `ISSUES.md` #34 for an
example).

## Code style

The codebase targets **C11** (`-std=c11`) and is formatted with the
included `.clang-format` (AlignAfterOpenBracket, `PointerAlignment:
Right`). Function naming is `snake_case`. Custom data structures
(`s_arr`, `t_arr`, `t_map`, etc.) are pure C89 implementations with no
external dependencies.

Error messages go to stderr with a `[dagwood]` prefix; Dagwood's own
chatter is on stderr, child task output is on stdout. Don't mix the
two without a reason.

The embedded DSL lives in `lib/dagwood.lcl` and is baked into the
binary at build time via `cmake/EmbedFile.cmake`. Modifying it
triggers automatic regeneration.

## Pull requests

For small fixes -- bugs, typos, missing test coverage, doc tweaks --
simply fork and make a PR.

Anything larger, please open an issue first so we can talk through
whether it's a fit before you invest time in the implementation. In
particular: changes to the DSL surface (new `task` attributes, new
top-level forms), changes to the execution model (layer scheduling,
staleness semantics), or changes to how multi-project composition works
all benefit from a design discussion up front.

## Working together

Be civil and assume good faith. The reviewer's job is to help you land
the change, not to gatekeep. The contributor's job is to make the
change small enough to review.
