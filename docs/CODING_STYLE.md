# RFScout coding style

RFScout prioritizes readability, predictable interfaces, and consistent formatting over compact source. These rules apply to all hand-maintained firmware and host-side tooling. Generated files are explicitly exempt as described below.

## C source

- Use C for firmware modules unless a dependency requires otherwise.
- Use Allman braces: opening braces are placed on their own line for functions and control blocks.
- Put spaces inside function-call parentheses and array subscripts: `foo( value )`, `items[ index ]`.
- Prefix function arguments with `a_` so arguments are visually distinct from local and module state.
- Prefer explicit comparisons for non-boolean values, including `NULL == pointer`, `ESP_OK != result`, and `0U == count`.
- Keep one logical operation per line. Do not compress conditionals, loops, declarations, or multiple statements onto one line.
- Do not put an `else`, statement body, or second declaration on the same line as a preceding statement.
- Break long calls and conditions across lines at logical argument or operator boundaries. Continuation lines should make ownership of the expression obvious.
- Use descriptive names instead of abbreviations unless the abbreviation is an established protocol or hardware term.
- Keep module-private state and helper functions `static`.
- Use fixed-width integer types for protocol, hardware, storage, and externally meaningful data.
- Use `const` wherever data is not modified.
- Prefer named local variables for geometry, timing, limits, and other values whose purpose would otherwise be unclear at the call site.
- Empty functions are permitted only when they intentionally satisfy a stable module interface; document that intent in the public header.

## Headers and module boundaries

- Every functional module has a `.h` interface and a `.c` implementation.
- Headers contain public types, constants, and function declarations; non-trivial function bodies belong in `.c` files.
- Hardware pin definitions are centralized in `board.h`; drivers should not scatter literal GPIO numbers through implementations.
- Include guards are used in addition to `#pragma once`.
- Keep project headers, SDK headers, and C library headers in visually separate include groups when practical.
- Public functions, callback typedefs, and public data types require Doxygen documentation.
- Public functions with non-obvious arguments should document `@param` values and meaningful return values with `@return`.
- Public enums and structures require a concise `@brief` describing what they represent.
- Internal helpers should have a concise Doxygen `@brief` when their purpose is not self-evident from the function name and signature.
- Interfaces should use the same argument names as their implementations.

## File headers

Every hand-maintained `.c` and `.h` file begins with a block identifying the file, purpose, copyright, and SPDX license identifier:

```c
/**
    module.c : Short module description
    Description: Longer description when useful.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
```

The `Description:` line may be omitted when the short description is already complete, but the file name, copyright, and SPDX identifier are required.

## Automated formatting checks

The repository root contains `.clang-format` and `.editorconfig`. The clang-format policy is enforced by `tools/check_format.py`.

Normal `idf.py build` runs the format checker automatically for locally modified or staged `main/*.c` and `main/*.h` files. Pull-request CI runs the same checker against C/header files changed relative to the PR base. This prevents a legacy untouched file from blocking an unrelated build while ensuring new and modified firmware code follows the repository policy.

Useful commands are:

```sh
python3 tools/check_format.py --fix          # format locally modified C/header files
python3 tools/check_format.py --base main    # check changes relative to a branch/ref
python3 tools/check_format.py --all          # audit all hand-maintained C/header files
```

`clang-format` must be installed and available on `PATH`. In an emergency, the CMake format target can be disabled with `-DRFSCOUT_FORMAT_CHECK=OFF`; this should not be used for normal development or CI.

## Layout and readability review

Before a code change is considered complete, review each touched C or header file for the following:

1. No compressed multi-statement lines remain.
2. Control-flow braces follow Allman style.
3. Public declarations have Doxygen comments.
4. Function arguments use the `a_` prefix in both declaration and definition.
5. File headers are complete.
6. Long expressions are split at readable boundaries rather than allowed to run across the screen.
7. Comments explain intent or hardware/protocol constraints rather than restating the code.
8. Generated code has not been hand-edited.
9. `python3 tools/check_format.py` succeeds for the files being changed.

Formatting-only changes should not intentionally alter behavior. If cleanup exposes a correctness issue, keep the functional fix small and call it out separately in the commit or pull-request description.

## Generated files

Generated source such as `main/oui_database.inc` and `main/surveillance_signatures.inc` is exempt from hand-formatting rules. The generator must produce deterministic, readable output and the generated file should not be edited manually.

## Python and YAML

Host-side tooling follows normal Python readability conventions: descriptive names, type annotations where useful, docstrings for non-trivial functions, and deterministic output. GitHub Actions YAML should remain simple and explicit.

## Documentation maintenance

Changes that alter application behavior, controls, supported hardware, logging, power/resource lifecycle, or user-visible terminology should update the relevant README or document in `docs/` in the same pull request. `ARCHITECTURE.md` describes module ownership and lifecycle, `CAPABILITIES.md` describes user-visible functionality, and `CODING_STYLE.md` defines source conventions.
