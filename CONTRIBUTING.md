# Contributing to RFScout

This is the single contributor guide for development setup, workflow, coding conventions, and documentation maintenance. Platform behavior belongs in [Architecture](docs/ARCHITECTURE.md); app behavior belongs in [app pages](docs/apps/); future work belongs **only** in [Roadmap](docs/ROADMAP.md).

## Getting started on Fedora Linux

This guide is the main development ramp-up for RFScout on ESP32-S3 with ESP-IDF 6.1.

**Before changing source, read [RFScout coding style](#coding-style).** It defines required source formatting, module/interface conventions, memory ownership and stack discipline, generated-file policy, and documentation expectations. Changes that affect shared ownership/lifecycle should also be checked against [Platform architecture](docs/ARCHITECTURE.md); app behavior belongs in the corresponding page under [`apps/`](docs/apps/).

## Host packages

Install the host-side tools:

```sh
sudo dnf install \
    git wget flex bison gperf python3 python3-pip \
    cmake ninja-build ccache libffi-devel openssl-devel \
    dfu-util libusb1-devel clang18-tools-extra
```

RFScout pins **clang-format 18** for deterministic formatting:

```sh
clang-format-18 --version
```

## ESP-IDF

CI tracks ESP-IDF 6.1. A typical local installation is:

```sh
mkdir -p ~/esp
cd ~/esp
git clone --recursive --branch release/v6.1 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

Activate it in each development shell:

```sh
. ~/esp/esp-idf/export.sh
```

## Clone and build RFScout

```sh
git clone https://github.com/netsurf916/RFScout.git
cd RFScout
idf.py set-target esp32s3
idf.py build
```

RFScout builds one ESP32-S3 image and validates the supported T-Embed board profile at runtime. The first build resolves managed components declared in `main/idf_component.yml`. The generated `managed_components/` directory and `dependencies.lock` are ignored; ESP-IDF may regenerate them locally as part of dependency resolution.

If major build configuration changes, regenerate the local build tree:

```sh
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

## Generated identity and surveillance data

Generated IEEE identity tables and the compiled surveillance-fact database are already committed. Refresh them only when intentionally updating their source data:

```sh
python3 tools/update_oui.py
python3 tools/update_surveillance.py
```

Generated `.inc` tables should be changed through their generators rather than hand edited.

## Formatting

Normal local builds check modified/staged/untracked hand-maintained C/header files. Useful commands are:

```sh
python3 tools/check_format.py
python3 tools/check_format.py --fix
python3 tools/check_format.py --base main
python3 tools/check_format.py --all
python3 tools/check_format.py --all --fix
```

Pull-request CI runs the complete clang-format 18 audit. An emergency local build can disable the CMake check with:

```sh
idf.py -DRFSCOUT_FORMAT_CHECK=OFF build
```

That bypass does not change CI policy.

## Flash and monitor locally

Connect a supported RFScout ESP32-S3 board over USB. ESP-IDF can usually discover the serial port automatically:

```sh
idf.py flash
idf.py monitor
```

or:

```sh
idf.py build flash monitor
```

Specify a port when needed:

```sh
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with **Ctrl+]**.

The supported hardware is the LILYGO T-Embed CC1101 Plus. If automatic flashing does not enter the ESP32-S3 ROM bootloader, use the T-Embed BOOT mechanism while connecting/resetting it.

## Browser flashing development builds and releases

RFScout publishes its browser flasher and stable OTA tree together through the **Publish RFScout Pages** workflow. The catalog is intentionally a view of the current development/release lifecycle, not just a list of published releases.

The flasher can contain:

- the newest successful candidate for the current `main` HEAD, labeled as a development build;
- successful candidates for current open pull requests;
- prepared GitHub draft releases, clearly labeled as drafts; and
- published/prerelease GitHub Releases.

Ready-for-review pull requests that pass the full ESP-IDF build produce a short-lived candidate bundle. Draft PRs intentionally skip the expensive ESP-IDF build and therefore do not create a new candidate, although a still-current candidate created before a PR was converted back to draft can remain available until the head changes, the PR closes, or the artifact expires.

The current `main` candidate is normally presented as the primary development build. If a prepared draft or published release represents the same packaged commit, the named release supersedes the duplicate `main` entry. PR candidates remain independently selectable for testing unmerged work.

Prepared draft releases are intentionally mirrored to the public Pages flasher so they can be hardware-tested before GitHub publication. A GitHub draft therefore must not be treated as private firmware once release preparation succeeds.

Use a desktop Chromium-based browser such as Edge or Chrome with Web Serial support. Connect the device over USB, open the RFScout GitHub Pages flasher, choose the desired development, PR, draft-release, or published-release entry, and use the ESP Web Tools install control.

The browser flasher is intended for quick target-hardware validation. Local `idf.py flash monitor` remains the better path when serial logs or debugger-oriented development are needed.

## Normal development loop

```sh
cd ~/path/to/RFScout
. ~/esp/esp-idf/export.sh

git pull
# edit
python3 tools/check_format.py --fix
idf.py build
idf.py flash monitor
```

Before publishing a PR for full validation:

```sh
python3 tools/check_format.py --all
idf.py build
```

The repository CI policy is designed so formatting/tests can run while a PR is draft, while the full firmware candidate build waits until the PR is Ready for Review.

## Troubleshooting

### `idf.py: command not found`

Activate ESP-IDF:

```sh
. ~/esp/esp-idf/export.sh
```

### `RFScout requires clang-format 18`

```sh
sudo dnf install clang18-tools-extra
clang-format-18 --version
```

An explicit formatter path can be supplied through `RFSCOUT_CLANG_FORMAT`.

### Managed component problems

Current RFScout uses managed `espressif/cjson`. If a checkout inherited stale component/build state:

```sh
rm -rf build managed_components sdkconfig
idf.py set-target esp32s3
idf.py build
```

If dependency resolution looks stale, remove the generated `dependencies.lock` together with the build and managed-component directories so ESP-IDF can resolve them again.

### Serial access problems

Inspect the device and permissions:

```sh
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
sudo dmesg | tail -n 30
```

Avoid running the complete ESP-IDF build/flash process as root.

## Documentation for development

- [Coding style](#coding-style) — required source/style/module conventions
- [Platform architecture](docs/ARCHITECTURE.md) — shared lifecycle, service, resource, memory, persistence, hardware, and OTA contracts
- [Application documentation](docs/apps/) — current app-specific behavior and implementation boundaries
- [Firmware releases](docs/operations/RELEASING.md) — candidate/release/browser-flasher/OTA publication runbook
- [Roadmap](docs/ROADMAP.md) — authoritative future planning and active release scope
- [README](README.md) — product/current-state anchor and full documentation map


---

## Coding style


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

## Memory ownership and stack discipline

RFScout runs on constrained task stacks, so interface design must make ownership and copy behavior explicit.

- Prefer passing non-trivial structures by reference. Use `const T *` for borrowed read-only access and `T *` for borrowed mutable access.
- Passing or returning substantial structures by value is exceptional and should have a concrete reason.
- A pointer parameter is borrowed unless the interface explicitly documents ownership transfer or retention. A callee must not retain a borrowed pointer beyond its documented lifetime.
- Caller-owned output should normally be written through an output pointer rather than returned as a substantial structure.
- Snapshot/status APIs whose structures contain arrays, strings, nested structures, or are expected to grow should write into caller-owned storage. Avoid return-by-value APIs whose future structure growth would silently enlarge every caller's stack frame.
- Avoid defensive whole-structure copies merely to simplify lifetime reasoning. Preserve ownership explicitly and copy only the fields that need independent storage.
- Whole-structure copies are appropriate when the copy itself establishes independent retained state, but that ownership decision should be obvious from the surrounding API rather than incidental convenience.
- Keep automatic objects small, especially in radio callbacks, FreeRTOS tasks, event callbacks, and other paths with constrained stacks.
- Retained or growable state belongs in the established application/session allocation model rather than large task-stack temporaries.
- Small scalar values, enums, and genuinely small value objects should still be passed by value; do not introduce pointer indirection without a memory or ownership benefit.
- When expanding a frequently instantiated structure such as `rf_device_t`, review both its retained-memory cost and every stack/local allocation site that instantiates it.

Public APIs with non-obvious lifetime behavior must document whether pointers are borrowed, retained, transferred, or caller-owned.

## Headers and module boundaries

- Every functional module has a `.h` interface and a `.c` implementation.
- Headers contain public types, constants, and function declarations; non-trivial function bodies belong in `.c` files.
- Hardware pin definitions are centralized in `board.h`; drivers should not scatter literal GPIO numbers through implementations.
- Removable SD storage is accessed only through `media_store.h` collections and handles. Do not construct `/sdcard` paths or call stdio/POSIX filesystem functions on the card outside the storage service.
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

## Platform service lifecycle naming

Shared platform services use consistent lifecycle names:

- `probe()`: retry-safe availability check; no ongoing activity.
- `init()`: framework-owned, idempotent, lightweight inactive setup.
- `start()`: app-owned, idempotent activation for persistent services.
- `begin_*(...)`: app-owned activation for parameterized transactions.
- `stop()`: idempotent quiescence/cleanup, safe for framework teardown.
- `is_active()`: authoritative active-resource predicate.

Do not add alternate lifecycle synonyms such as `prepare`, `resume`, or `recover` when the common vocabulary is sufficient. A `start()` function must not be required merely to initialize reusable infrastructure; move that work to idempotent `init()` instead.

## Optional feature failure-containment policy

Service and driver owners must define failure behavior for optional, repeatedly attempted work. The framework does **not** impose a universal retry count or automatically disable every failing service.

- Identify which operations constitute meaningful failures; do not count idle polls, unavailable prerequisites, or skipped work as repeated attempts.
- Choose and document an appropriate threshold or alternative backoff/circuit-breaker policy for the feature. A successful operation should normally reset a consecutive-failure streak.
- Respect shared media/device availability and existing recovery cooldowns to avoid log spam, busy loops, and redundant I/O retries.
- When an optional feature can safely be disabled after persistent errors, make the change through its normal persisted setting API, report the reason once in diagnostics/system events, and provide an explicit user-controlled recovery path.
- Do not silently disable critical safety, storage integrity, or system services. Surface degraded status or abort unsafe operations instead.
- Consider transient faults, device removal/reinsertion, reboot persistence, manual reenable, and internal-storage failure in tests and documentation.

For example, the breadcrumb writer uses a service-specific threshold of ten consecutive attempted write failures, resets the streak after success, and persists its own disabled setting. This threshold is **not** a platform-wide default.

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


## Release stabilization

Before preparing a release, run the [hardware acceptance checklist](docs/RELEASE_VALIDATION.md) against the exact release candidate. CI establishes build/format/packaging success only; it cannot verify device-specific radio coexistence, removable-media behavior, or OTA rollback. Proposed features and refactors belong exclusively in [ROADMAP](docs/ROADMAP.md).
