# Firmware releases

RFScout separates continuous validation from durable firmware releases. Pull requests and pushes to `main` can build short-lived candidate artifacts, but they do not create tags or GitHub Releases. A release begins only when a maintainer deliberately runs the **Prepare firmware release** workflow.

## Bundle contents

The release packager validates ESP-IDF build metadata before producing files. RFScout currently requires:

| Image | Flash offset | Purpose |
| --- | ---: | --- |
| Bootloader | `0x0` | ESP32-S3 second-stage bootloader |
| Partition table | `0x8000` | RFScout 16 MB flash layout |
| Application | `0x10000` | RFScout firmware |

The bundle contains a combined image for ordinary installation, the component images for development/recovery, SHA-256 checksums, installation instructions, structured build metadata, and an ESP Web Tools manifest. It does not contain the ESP-IDF build tree.

`manifest.web.json` is consumed by the browser flasher. The Pages deployment mirrors the manifest and referenced combined image onto the same HTTPS origin so flashing does not depend on cross-origin access to GitHub release or Actions URLs.

## Candidate artifacts

The normal **Build and formatting** workflow:

1. runs the complete clang-format 18 audit and release-packager tests;
2. builds RFScout with ESP-IDF 6.1 for eligible non-draft PR/main runs;
3. creates a combined raw image with `idf.py merge-bin`;
4. validates/packages the flash layout; and
5. retains the candidate bundle as a short-lived GitHub Actions artifact.

Draft pull requests intentionally skip the expensive ESP-IDF candidate build. Mark a PR Ready for Review before expecting a browser-flashable candidate.

Candidate artifacts are diagnostic build products, not releases, and are never marked stable automatically.

## Browser flasher

The **Publish browser flasher** workflow runs after a successful eligible **Build and formatting** workflow and can also be started manually. It assembles a GitHub Pages site from published releases plus the candidate artifact from the triggering build when available.

The static flasher shell uses the repository's `assets/logo.png` branding and RFScout orange/white visual theme. The workflow copies the logo into the Pages artifact so the deployed site remains self-contained alongside the mirrored firmware files and manifests.

For pull-request builds, the candidate is placed first in the firmware selector and labeled with the PR number. Published releases remain selectable below it. Each deployment is self-contained: firmware files and manifests are copied into the Pages artifact rather than fetched by the browser from authenticated Actions endpoints.

GitHub Pages uses **GitHub Actions** as its publishing source. The flasher requires HTTPS and Web Serial; Chromium-based desktop browsers such as Edge and Chrome are the supported path. The workflow has been validated end-to-end on target hardware.

A Pages deployment represents the most recently completed eligible build. It is a hardware-validation convenience, not permanent hosting for every historical PR candidate.

## Preparing a draft release

From the repository **Actions** page, select **Prepare firmware release**, choose **Run workflow**, and provide:

- a semantic version beginning with `v`, for example `v0.2.0` or `v0.2.1-rc.1`;
- a source ref, normally `main`; and
- whether the release should be marked prerelease.

The workflow resolves the source ref to an immutable commit and rejects requests when the version is malformed/duplicate, the source is not part of `main`, or the source contains no commits after the latest published release.

It repeats formatting, packaging tests, the ESP-IDF build, and bundle generation at that exact commit. Only after those jobs pass does it create a **draft** GitHub Release and its tag. The workflow never publishes the draft automatically.

## Publishing

Before publishing a draft:

1. review the generated release notes/version;
2. verify the archive/checksums;
3. perform applicable target-hardware installation and smoke tests;
4. record hardware validation in the release notes; and
5. publish the release deliberately through GitHub.

A subsequent browser-flasher deployment discovers the published release assets; it does not rebuild the firmware during publication/deployment.

## Installation behavior

The combined image is a clean installation/recovery image flashed at `0x0`. It can replace internal-flash state. Removable SD contents, including `/sdcard/rfscout.json` and recordings, are not part of the image and are not erased by flashing internal storage.

Separate component images are included for development/recovery. RFScout does not guarantee that application-image-only flashing is compatible across arbitrary releases.

## Reproducibility

RFScout enables ESP-IDF reproducible-build configuration and generates deterministic package metadata/checksums/ZIP output. Reproducing a firmware build still requires the same source revision, ESP-IDF/toolchain, generated source inputs, and managed-component resolution.

`dependencies.lock` is intentionally tracked in Git so managed component versions are part of the reproducibility boundary. It should change only when dependency resolution is intentionally updated.
