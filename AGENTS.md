# AGENTS.md

Guidelines for working in the `openrazer-win32-deathadder-elite` (`da_elite`) repository (C++23, Win32).

## Versioning

- **Project version**: Bump `VERSION` in `project(da_elite VERSION ...)` in `CMakeLists.txt` — nowhere else (`da_elite_version.h` is generated from it).
- Check published releases (GitHub tags) before bumping: do not bump if the current version has not yet been in a published release.

## Static Analysis (`clang-tidy`)

- After making C/C++ edits, verify that your changes do not introduce any `clang-tidy` warnings (rules are defined in `.clang-tidy`).
- Keep warnings to a minimum. Acceptable exceptions are:
  - False positives on Windows API structs.
  - Intentional, verified-safe performance choices (e.g., skipping zero-initialization of an array).
- When suppressing a verified-valid warning, scope it to the specific check (e.g., `// NOLINT(cppcoreguidelines-pro-type-member-init)`). Use `NOLINT` sparingly — do not scatter blanket suppressions across the codebase.

## Code Style & Performance

- Match the style, idioms, and conventions of the surrounding codebase.
- Write code with performance and allocation efficiency in mind:
  - Minimize heap allocations where practical (e.g., reuse buffers, use fixed-size stack arrays when sizes are bounded).
  - Prefer the stack-allocated string helpers in `include/da_elite/common.hpp` over allocating standard utilities — e.g., `da_elite::to_fixed_str` / `da_elite::to_fixed_wstr` instead of `std::to_string` / `std::to_wstring`, and `FixedString` / `FixedWString` for bounded stack strings.
  - Avoid `std::string + std::string` concatenation. Use `ulp::str::concat_strings` (or `usylibpp::strings::concat_strings`) when constructing a new string from multiple parts, and `da_elite::append_to` (or pre-reserved in-place appending) when appending to an existing string.
  - Continue this pattern when adding new utilities or code paths: prefer non-allocating or single-allocation helpers over chained temporaries.
- Never trade away correctness for micro-optimizations: keep **thread safety** and **avoiding undefined behavior** strictly in mind when reusing buffers, skipping initialization, or manipulating low-level memory.
