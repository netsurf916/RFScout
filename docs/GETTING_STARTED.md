# Getting started on Fedora Linux

This guide describes a straightforward RFScout development environment on a current Fedora Linux workstation. RFScout targets the **ESP32-S3** and uses **ESP-IDF** for configuration, compilation, flashing, and serial monitoring.

The repository also enforces its C formatting policy with **clang-format 18**. Normal `idf.py build` runs the RFScout format checker for locally modified or staged C/header files, so the versioned formatter is a required local development dependency unless the format check is explicitly disabled for an emergency build.

## 1. Install Fedora packages

Install the host-side tools needed by ESP-IDF and RFScout:

```sh
sudo dnf install \
    git \
    wget \
    flex \
    bison \
    gperf \
    python3 \
    python3-pip \
    cmake \
    ninja-build \
    ccache \
    libffi-devel \
    openssl-devel \
    dfu-util \
    libusb1-devel \
    clang18-tools-extra
```

`clang18-tools-extra` is Fedora's compatibility package for LLVM/Clang 18 and provides `clang-format-18`. RFScout pins the formatter major because newer clang-format releases can make different wrapping decisions even with the same configuration.

Confirm the required formatter is available:

```sh
clang-format-18 --version
```

The output must report major version 18. `tools/check_format.py` automatically prefers `clang-format-18`; it will also accept a `clang-format` executable when that executable is version 18. To use an explicit binary path, set `RFSCOUT_CLANG_FORMAT`.

ESP-IDF downloads and manages its own versioned ESP cross-compilers and related target-specific tools, so those do not need to be installed manually through `dnf`.

## 2. Install ESP-IDF

RFScout currently builds against the ESP-IDF 5.x toolchain used by the project CI. A convenient local layout is to keep Espressif software under `~/esp`:

```sh
mkdir -p ~/esp
cd ~/esp
git clone --recursive --branch release/v5.5 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

The install script creates the ESP-IDF Python environment and downloads the tool versions required for ESP32-S3 development.

ESP-IDF does not permanently add those tools to your shell environment. Before working on RFScout in a new terminal, activate ESP-IDF with:

```sh
. ~/esp/esp-idf/export.sh
```

Verify the environment:

```sh
idf.py --version
python3 --version
cmake --version
ninja --version
clang-format-18 --version
```

If desired, create a shell alias instead of typing the full export command each time:

```sh
alias get_idf='. ~/esp/esp-idf/export.sh'
```

Add that alias to `~/.bashrc`, start a new shell, and run `get_idf` before building RFScout.

## 3. Clone RFScout

```sh
git clone https://github.com/netsurf916/RFScout.git
cd RFScout
```

When developing a pull request or feature branch, switch to that branch before configuring the build:

```sh
git switch <branch-name>
```

## 4. Prepare generated data

RFScout includes a generated IEEE OUI/vendor table. Refresh it before the first build and whenever intentionally updating the registry data:

```sh
python3 tools/update_oui.py
```

The generated `.inc` files are exempt from hand formatting. Update them through their generator scripts rather than editing them directly.

## 5. Configure and build

Activate ESP-IDF first if the current shell has not already been configured:

```sh
. ~/esp/esp-idf/export.sh
```

Then configure RFScout for ESP32-S3 and build it:

```sh
idf.py set-target esp32s3
idf.py build
```

The first build creates `sdkconfig` and the `build/` directory. Subsequent builds normally require only:

```sh
idf.py build
```

If `sdkconfig.defaults`, the partition table, or major build configuration changes, start from a fresh configuration:

```sh
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

## 6. Formatting checks

RFScout's formatting configuration lives in `.clang-format` and `.editorconfig`.

Normal `idf.py build` invokes `tools/check_format.py` through CMake and checks locally modified, staged, or untracked hand-maintained C/header files under `main/`. If a source file does not match canonical clang-format 18 output, the build fails before it is considered complete.

Format the currently modified firmware files with:

```sh
python3 tools/check_format.py --fix
```

Check the current local changes without modifying them:

```sh
python3 tools/check_format.py
```

Check changes relative to another branch or ref:

```sh
python3 tools/check_format.py --base main
```

Verify every hand-maintained C/header file in the repository:

```sh
python3 tools/check_format.py --all
```

The complete tree is expected to pass this command. GitHub Actions runs the same full-tree audit on every pull request, so there are no legacy formatting exemptions. Generated `.inc` tables are intentionally excluded.

To explicitly canonicalize the entire firmware tree:

```sh
python3 tools/check_format.py --all --fix
```

### Emergency format-check bypass

A developer can explicitly disable the CMake formatting target if a build is needed before clang-format 18 can be installed:

```sh
idf.py -DRFSCOUT_FORMAT_CHECK=OFF build
```

This is an escape hatch rather than a normal development mode. Pull-request CI still enforces the repository formatting policy.

## 7. Connect and flash the device

Connect the LILYGO T-Embed CC1101 Plus over USB and identify its serial device. On Fedora, useful commands include:

```sh
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

and, immediately after connecting the board:

```sh
sudo dmesg | tail -n 30
```

ESP-IDF can usually discover the port automatically. Flash the current build with:

```sh
idf.py flash
```

To specify the serial port explicitly:

```sh
idf.py -p /dev/ttyACM0 flash
```

Open the serial monitor with:

```sh
idf.py monitor
```

or combine build, flash, and monitoring:

```sh
idf.py build flash monitor
```

Exit the ESP-IDF monitor with **Ctrl+]**.

### Serial-port permissions

If flashing fails with a permission error, inspect the device ownership:

```sh
ls -l /dev/ttyACM0
```

Fedora normally manages local device access through udev/logind. If your local configuration instead assigns serial devices to a dedicated group, add your user to the group shown by `ls -l`, then log out and back in. Avoid running the complete ESP-IDF build/flash process as root.

## 8. Typical development loop

A normal RFScout edit/build cycle is:

```sh
cd ~/path/to/RFScout
. ~/esp/esp-idf/export.sh

git pull
# edit source
python3 tools/check_format.py --fix
idf.py build
idf.py flash monitor
```

Before opening or updating a pull request, run:

```sh
python3 tools/check_format.py --all
idf.py build
```

GitHub Actions repeats the full-tree canonical formatting audit and performs a complete ESP32-S3 build.

## 9. Troubleshooting

### `idf.py: command not found`

The ESP-IDF environment is not active in the current shell:

```sh
. ~/esp/esp-idf/export.sh
```

### `RFScout requires clang-format 18`

Install the Fedora compatibility package and retry:

```sh
sudo dnf install clang18-tools-extra
clang-format-18 --version
```

If clang-format 18 is installed at a nonstandard path:

```sh
export RFSCOUT_CLANG_FORMAT=/path/to/clang-format-18
python3 tools/check_format.py --all
```

### CMake or configuration behaves unexpectedly

Regenerate the build directory and configuration:

```sh
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

### Flashing cannot open the serial device

Confirm the correct device path and permissions, disconnect/reconnect the board, and verify that another terminal is not already holding the port open.

## Related documentation

- [Coding style](CODING_STYLE.md) — source formatting and documentation requirements
- [Architecture](ARCHITECTURE.md) — application lifecycle and module ownership
- [Capabilities](CAPABILITIES.md) — user-visible RFScout features and limitations
- [SD card behavior](SD_CARD.md) — storage compatibility and logging behavior
