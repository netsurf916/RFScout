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

New draft pull requests intentionally skip the expensive ESP-IDF candidate build. Mark a PR Ready for Review when a fresh browser-flashable candidate is needed. If a PR already has a successful candidate for its current head and is later converted back to draft without changing that head, the existing candidate remains eligible for the browser flasher until the PR changes, closes, or the artifact expires.

Candidate artifacts are diagnostic build products, not releases, and are never marked stable automatically.

## Browser flasher

The **Publish browser flasher** workflow reevaluates the catalog whenever a **Build and formatting** workflow completes, whenever a GitHub Release is published, when a pull request is closed, and when started manually. Each completed Pages deployment is rebuilt from published releases plus the current successful candidate build for every open pull request whose artifact is still available.

Candidate publication is intentionally atomic with respect to in-flight pull-request builds. Before replacing the deployed Pages snapshot, the workflow checks every open non-draft PR current head. If a current head does not yet have a recorded Build and formatting run, or any matching run is still queued/in progress, that refresh is deferred and the previously deployed flasher remains untouched. The workflow also captures the exact open-PR head/draft snapshot used for that stability decision and revalidates it immediately before the Pages deployment step; if any PR head, draft state, or open/closed membership changes while the site is being assembled, publication is deferred rather than deploying a mixed snapshot. When the outstanding build or PR-state change settles, its normal workflow event reevaluates the catalog. A successful current-head build replaces/adds the candidate, while a failed/cancelled current-head build causes that PR to be omitted on the next stable deployment. This prevents unrelated build completions or mid-refresh PR synchronization from temporarily publishing a partial/stale candidate catalog.

The static flasher shell uses the repository's `assets/logo.png` branding and RFScout orange/white visual theme. The workflow copies the logo into the Pages artifact so the deployed site remains self-contained alongside the mirrored firmware files and manifests.

Open pull-request candidates are kept independently under `firmware/candidates/pr-N/` and are labeled with their PR numbers in the selector. A newer build from another PR therefore does not overwrite an existing candidate. Only a successful candidate matching the PR's current head commit is included in a newly published catalog, so a stale build from an earlier revision is not deliberately republished after the new head settles. While a replacement build is still in flight, the prior Pages deployment may continue to show the previous candidate because the site is left unchanged rather than publishing a partial catalog. Draft status by itself does not hide an otherwise current, successful candidate; PRs without a current successful artifact, or whose candidate artifacts have expired, are omitted once the catalog is stable enough to republish.

Candidate discovery checks successful workflow runs for the PR's current head until it finds a live `rfscout-candidate-*` artifact instead of assuming the newest successful run necessarily contains firmware. This avoids formatting-only or rerun records masking a still-valid candidate artifact for the same source head.

Ordinary `main` push artifacts are not exposed as browser-flasher candidates. Once a PR is merged or closed, the next stable flasher refresh removes its candidate from the catalog; the explicit pull-request-close trigger handles closes that do not otherwise cause a `main` build. Published releases remain durable entries below the current PR candidates.

Each deployment is self-contained: firmware files and manifests are copied into the Pages artifact rather than fetched by the browser from authenticated Actions endpoints. Publishing a release therefore refreshes the browser flasher automatically. A manual **Publish browser flasher** dispatch remains available for recovery or maintenance, but normal release publication should not require it.

GitHub Pages uses **GitHub Actions** as its publishing source. The flasher requires HTTPS and Web Serial; Chromium-based desktop browsers such as Edge and Chrome are the supported path. The workflow has been validated end-to-end on target hardware.

The candidate portion of a Pages deployment represents currently open, testable pull requests rather than permanent hosting for historical PR builds. Published GitHub Releases provide the durable firmware history.

## Preparing a draft release

From the repository **Actions** page, select **Prepare firmware release**, choose **Run workflow**, and provide:

- a semantic version beginning with `v`, for example `v0.2.0` or `v0.2.1-rc.1`;
- a source ref, normally `main`; and
- whether the release should be marked prerelease.

The workflow resolves the source ref to an immutable commit and rejects requests when the version is malformed/duplicate, the source is not part of `main`, or the source contains no commits after the latest published release.

It repeats formatting, packaging tests, the ESP-IDF build, and bundle generation at that exact commit. Only after those jobs pass does it create the semantic version tag at the validated commit and then create the **draft** GitHub Release against that existing tag. This explicit ordering prevents GitHub from assigning a temporary `untagged-*` release identity while a release is under review.

If draft creation itself fails after the workflow has pushed the tag, the workflow removes that newly-created tag before exiting. A successfully prepared draft intentionally retains its semantic tag while release notes and hardware validation are completed. The workflow never publishes the draft automatically.

## Publishing

Before publishing a draft:

1. review the generated release notes/version;
2. verify the archive/checksums;
3. perform applicable target-hardware installation and smoke tests;
4. record hardware validation in the release notes; and
5. publish the release deliberately through GitHub.

Publishing preserves the semantic tag created during draft preparation. The release publication event automatically starts **Publish browser flasher**, which discovers the now-published release assets and republishes the firmware catalog once candidate state is stable. The flasher does not rebuild firmware during publication/deployment.

## Installation behavior

The combined image is a clean installation/recovery image flashed at `0x0`. It can replace internal-flash state. Removable SD contents, including `/sdcard/rfscout.json` and recordings, are not part of the image and are not erased by flashing internal storage.

Separate component images are included for development/recovery. RFScout does not guarantee that application-image-only flashing is compatible across arbitrary releases.

## Reproducibility

RFScout enables ESP-IDF reproducible-build configuration and generates deterministic package metadata/checksums/ZIP output. Reproducing a firmware build still requires the same source revision, ESP-IDF/toolchain, generated source inputs, and managed-component resolution.

`dependencies.lock` is intentionally tracked in Git so managed component versions are part of the reproducibility boundary. It should change only when dependency resolution is intentionally updated.
