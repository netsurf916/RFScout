# RFScout coding style

RFScout prioritizes readability, predictable interfaces, and consistent formatting over compact source.

## C source

- Use C for firmware modules unless a dependency requires otherwise.
- Use Allman braces: opening braces are placed on their own line for functions and control blocks.
- Put spaces inside function-call parentheses and array subscripts: `foo( value )`, `items[ index ]`.
- Prefix function arguments with `a_` so arguments are visually distinct from local and module state.
- Prefer explicit comparisons for non-boolean values, including `NULL == pointer`, `ESP_OK != result`, and `0U == count`.
- Keep one logical operation per line. Do not compress conditionals, loops, declarations, or multiple statements onto one line.
- Use descriptive names instead of abbreviations unless the abbreviation is an established protocol or hardware term.
- Keep module-private state and helper functions `static`.
- Use fixed-width integer types for protocol, hardware, storage, and externally meaningful data.
- Use `const` wherever data is not modified.

## Headers and module boundaries

- Every functional module has a `.h` interface and a `.c` implementation.
- Headers contain public types, constants, and function declarations; non-trivial function bodies belong in `.c` files.
- Hardware pin definitions are centralized in `board.h`; drivers should not scatter literal GPIO numbers through implementations.
- Include guards are used in addition to `#pragma once`.
- Public functions and public data types require Doxygen documentation.
- Internal helpers should have a concise Doxygen `@brief` when their purpose is not self-evident.

## File headers

Source and header files begin with a short block identifying the file, purpose, copyright, and SPDX license identifier.

```c
/**
    module.c : Short module description
    Description: Longer description when useful.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
```

## Generated files

Generated source such as `main/oui_database.inc` is exempt from hand-formatting rules. The generator must produce deterministic, readable output and the generated file should not be edited manually.

## Python and YAML

Host-side tooling follows normal Python readability conventions: descriptive names, type annotations where useful, docstrings for non-trivial functions, and deterministic output. GitHub Actions YAML should remain simple and explicit.
