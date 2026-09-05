# Getting started on Fedora Linux

This guide describes the current RFScout development workflow for ESP32-S3 with ESP-IDF 6.x.

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

The first build resolves managed components declared in `main/idf_component.yml`. The generated `managed_components/` directory is ignored. The repository intentionally tracks `dependencies.lock`; do not add it to `.gitignore`. It pins resolved component versions so local, CI, and historical release builds are less likely to drift.

If major build configuration changes, regenerate the local build tree:

```sh
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

## Generated identity data

Generated IEEE/curated prefix tables are already committed. Refresh them only when intentionally updating registry data:

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

Connect the T-Embed CC1101 Plus over USB. ESP-IDF can usually discover the serial port automatically:

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

## Browser flashing development builds and releases

RFScout also publishes a GitHub Pages browser flasher through the **Publish browser flasher** workflow. The catalog is intentionally a view of the current development/release lifecycle, not just a list of published releases.

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

Keep the tracked `dependencies.lock` file; the component manager will use it to reproduce the resolved dependency set.

### Serial access problems

Inspect the device and permissions:

```sh
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
sudo dmesg | tail -n 30
```

Avoid running the complete ESP-IDF build/flash process as root.

## Related documentation

- [Architecture](ARCHITECTURE.md)
- [Capabilities](CAPABILITIES.md)
- [Configuration and persistence](CONFIGURATION.md)
- [Board targets and porting](BOARD_PORTING.md)
- [Firmware releases](RELEASING.md)
- [Coding style](CODING_STYLE.md)
- [Roadmap](ROADMAP.md)
