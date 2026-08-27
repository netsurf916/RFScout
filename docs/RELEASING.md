# Firmware releases

RFScout separates continuous validation from durable firmware releases. Pull requests and pushes to `main` build and package short-lived candidate artifacts, but they never create a tag or GitHub Release. A release begins only when a maintainer deliberately runs the **Prepare firmware release** workflow.

## Bundle contents

The release packager validates the ESP-IDF build metadata before producing files. RFScout currently requires:

| Image | Flash offset | Purpose |
| --- | ---: | --- |
| Bootloader | `0x0` | ESP32-S3 second-stage bootloader |
| Partition table | `0x8000` | RFScout's 16 MB flash layout |
| Application | `0x10000` | RFScout firmware |

The bundle contains a combined image for ordinary installation, the three component images for development/recovery, SHA-256 checksums, installation instructions, structured build metadata, and an ESP Web Tools manifest. It does not contain the ESP-IDF build tree.

`manifest.web.json` is staged for a future browser installer. Publishing the manifest and its referenced combined image from an HTTPS origin is deliberately outside the initial release-packaging work.

## Candidate artifacts

The normal **Build and formatting** workflow:

1. runs the complete clang-format 18 audit and release-packager tests;
2. builds RFScout with ESP-IDF 6.1;
3. creates a combined raw image with `idf.py merge-bin`;
4. validates and packages the resulting flash layout; and
5. retains the candidate bundle as a GitHub Actions artifact for 14 days.

Candidate artifacts are identified by commit SHA. They are diagnostic build products, not releases, and are never marked stable automatically.

## Preparing a draft release

From the repository's **Actions** page, select **Prepare firmware release**, choose **Run workflow**, and provide:

- a semantic version beginning with `v`, such as `v0.1.0` or `v0.2.0-rc.1`;
- a source ref, normally `main`; and
- whether the eventual release is a prerelease.

The workflow resolves the source ref to an immutable commit and rejects the request when:

- the version does not follow the expected syntax;
- the tag or release already exists;
- the source commit is not part of `main`; or
- the source contains no commits after the latest published release.

It then repeats formatting, packaging tests, the complete ESP-IDF build, and bundle generation at that exact commit. Only after those jobs pass does it create a **draft** GitHub Release and its tag. The workflow never publishes the draft.

## Publishing

Before publishing a draft:

1. review the generated release notes and version;
2. download the ZIP and verify it against `SHA256SUMS`;
3. perform the applicable target-hardware installation and smoke tests;
4. record hardware validation in the release notes; and
5. publish the release deliberately through GitHub.

Publishing a release is the future trigger point for updating the browser installer's stable manifest. That deployment should consume the already released files rather than rebuilding different firmware.

## Installation behavior

The combined image is a clean installation/recovery image flashed at `0x0`. It can replace internal NVS and other internal-flash state. Removable SD contents, including `/sdcard/rfscout.json`, are not part of the image and are not erased by flashing internal storage.

Separate component images are included for development and recovery. RFScout does not yet guarantee that flashing only the application image is a compatible update path across arbitrary releases.

## Reproducibility

RFScout enables ESP-IDF's reproducible-build option, which removes build time and local path information from firmware output. The packager also writes JSON, checksums, and ZIP archives deterministically. Reproduction still requires the same source revision, ESP-IDF/toolchain version, managed-component versions, and generated source inputs used by CI.
