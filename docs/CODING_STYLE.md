# RFScout coding style

RFScout prioritizes readability, predictable interfaces, and consistent formatting over compact source. These rules apply to all hand-maintained firmware and host-side tooling. Generated files are explicitly exempt as described below.

## C source

- Use C for firmware modules unless a dependency requires otherwise.
- Use Allman braces: opening braces are placed on their own line for functions and control blocks.
- Put spaces inside function-call parentheses and array subscripts: `foo( value )`, `items[ index ]`.
- Prefix function arguments with `a_` so arguments are visually distinct from local and module state.
- Prefer explicit comparisons for non-boolean values, including `NULL == pointer`, `ESP_OK != result`, and `0U == count`.
- Keep one statement per line. Do not compress multiple statements, control bodies, or declarations onto one line.
- Do not put an `else`, statement body, or second declaration on the same line as a preceding statement.
- Let the repository `.clang-format` policy determine wrapping for expressions and calls. Do not hand-wrap code solely to fight the canonical formatter.
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

The `Description:` line may be omitted when the short description is already complete, but the file name, copyright, and SPDX identifier are required. The formatter is configured not to reflow these comments.

## Canonical automated formatting

The repository root contains `.clang-format` and `.editorconfig`. `tools/check_format.py` is the authoritative formatter/checker for hand-maintained C and header files under `main/`.

RFScout standardizes on **clang-format 18** so formatting is deterministic across developer workstations and GitHub Actions. The checker looks for `clang-format-18` first and then accepts `clang-format` only when it reports major version 18. An explicit binary can be supplied with `RFSCOUT_CLANG_FORMAT`.

The firmware tree is fully canonical: there are no legacy blob exemptions or migration baselines. Consequently:

```sh
python3 tools/check_format.py --all
```

must pass for the complete hand-maintained C/header tree. Pull-request CI runs this full-tree audit on every PR rather than checking only changed files.

Normal `idf.py build` remains change-scoped for convenience and automatically checks locally modified, staged, or untracked C/header files. This gives fast local feedback while CI continuously proves the repository as a whole remains canonical.

Useful commands are:

```sh
python3 tools/check_format.py --fix          # format locally modified C/header files
python3 tools/check_format.py --base main    # check changes relative to a branch/ref
python3 tools/check_format.py --all          # verify the complete firmware tree
python3 tools/check_format.py --all --fix    # canonicalize the complete firmware tree
```

In an emergency, the CMake format target can be disabled with `-DRFSCOUT_FORMAT_CHECK=OFF`; this should not be used for normal development or CI.

## Layout and readability review

Before a code change is considered complete, review each touched C or header file for the following:

1. No compressed multi-statement lines remain.
2. Control-flow braces follow Allman style.
3. Public declarations have Doxygen comments.
4. Function arguments use the `a_` prefix in both declaration and definition.
5. File headers are complete.
6. The file matches canonical clang-format 18 output.
7. Comments explain intent or hardware/protocol constraints rather than restating the code.
8. Generated code has not been hand-edited.
9. `python3 tools/check_format.py` succeeds for the files being changed.

Formatting-only changes should not intentionally alter behavior. If cleanup exposes a correctness issue, keep the functional fix small and call it out separately in the commit or pull-request description.

## Generated files

Generated source such as `main/services/wireless/oui_database.inc`, `main/services/wireless/oui_database_extended.inc`, and `main/services/wireless/surveillance_signatures.inc` is exempt from hand-formatting rules. The generator must produce deterministic, readable output and the generated file should not be edited manually.

## Python and YAML

Host-side tooling follows normal Python readability conventions: descriptive names, type annotations where useful, docstrings for non-trivial functions, and deterministic output. GitHub Actions YAML should remain simple and explicit.

## Documentation maintenance

Documentation ownership is deliberate:

- `README.md` is the main current-product anchor and documentation map.
- `docs/ARCHITECTURE.md` owns implemented shared platform contracts and invariants.
- `docs/apps/*.md` own implemented app-specific behavior, controls, data interpretation, dependencies, and current limitations.
- `docs/ROADMAP.md` is the single authoritative location for future design, priorities, active release scope, and unfinished work.
- `docs/GETTING_STARTED.md` owns contributor ramp-up, and this file owns source/style conventions.
- `docs/operations/RELEASING.md` is an operational release runbook.

Changes that alter application behavior, controls, supported hardware, logging, resource lifecycle, persistence, power, user-visible terminology, or operational release behavior must update the owning documentation in the same pull request.

Do not describe planned behavior as implemented in README, Architecture, or an app page. Conversely, once behavior is implemented, move its lasting factual description out of roadmap design prose and into the appropriate current-state document; git history and pull requests remain the implementation history.


Changes that alter application behavior, controls, supported hardware, logging, power/resource lifecycle, or user-visible terminology should update the relevant README or document in `docs/` in the same pull request. `ARCHITECTURE.md` describes module ownership and lifecycle, `CAPABILITIES.md` describes user-visible functionality, and `CODING_STYLE.md` defines source conventions.
