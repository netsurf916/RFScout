# Firmware releases

RFScout separates continuous validation from durable firmware releases. Pull requests and pushes to `main` can build short-lived candidate artifacts, but they do not create tags or GitHub Releases. A release begins only when a maintainer deliberately runs the **Prepare firmware release** workflow.

## Bundle contents

The release packager validates ESP-IDF build metadata before producing files. RFScout currently requires:

| Image | Flash offset | Purpose |
| --- | ---: | --- |
| Bootloader | `0x0` | ESP32-S3 second-stage bootloader |
| Partition table | `0x8000` | RFScout 16 MB flash layout |
| Application | `0x10000` | Universal RFScout firmware |

The bundle contains a combined image for ordinary installation, the component images for development/recovery, SHA-256 checksums, installation instructions, structured build metadata, and an ESP Web Tools manifest. It does not contain the ESP-IDF build tree. The same ESP32-S3 firmware image selects a supported RFScout board profile at runtime.

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

The browser flasher is intentionally a continuous view of RFScout's current development and release lifecycle. A stable catalog can contain all of the following at the same time:

- the newest successful candidate for the current `main` HEAD;
- successful candidates for current open pull requests;
- prepared GitHub draft releases;
- prerelease releases; and
- published releases.

The **Publish browser flasher** workflow reevaluates the catalog when a **Build and formatting** workflow completes, when a pull request closes, when a release is published, when supported release deletion events are delivered, when started manually, and on an hourly reconciliation schedule. Successful **Prepare firmware release** completion also dispatches a flasher refresh so a newly prepared draft can become available for hardware validation without waiting for an unrelated CI event.

Release events are relay-only. GitHub executes a `release`-triggered workflow in the release tag context, while the `github-pages` environment is intentionally restricted to `main`. The release-triggered job therefore dispatches **Publish browser flasher** on `main`; only that main-context run assembles and deploys Pages. Do not move Pages deployment back into the tag-context release run or weaken the environment restriction to make it pass.

Each completed Pages deployment is rebuilt from the current repository state rather than incrementally editing the previous deployment. Current `main`, current PR heads, and currently visible GitHub Releases are rediscovered each time. This means expired candidate artifacts, closed PR candidates, and withdrawn releases disappear from the next successful reconciliation rather than remaining as permanent Pages content.

Candidate publication is intentionally atomic with respect to in-flight builds. Before replacing the deployed Pages snapshot, the workflow verifies that the current `main` HEAD has a Build and formatting run and is no longer building, and it performs equivalent checks for every open non-draft PR current head. If a required build is missing or still queued/in progress, the refresh is deferred and the previously deployed flasher remains untouched.

The workflow captures the exact `main` and open-PR head/draft snapshot used for that stability decision and revalidates it immediately before Pages deployment. If `main`, any PR head, draft state, or open/closed membership changes while the site is being assembled, publication is deferred rather than deploying a mixed snapshot.

Candidate discovery searches successful workflow runs for the requested source head until it finds a live `rfscout-candidate-*` artifact instead of assuming the newest successful run necessarily contains firmware. Pull-request lookup additionally filters runs by PR number so two PRs sharing a head SHA cannot cross-select each other's merge-ref firmware.

The current successful `main` push candidate is exposed as a development entry. Open PR candidates are exposed independently. Candidate entries are presented ahead of ordinary release history for development/testing convenience.

If a named draft, prerelease, or published release represents the same packaged commit as the current `main` candidate, the named release supersedes that duplicate `main` entry. This preserves a single meaningful identity for identical firmware while leaving unrelated PR candidates visible.

Release entries are included only when the required browser-flasher assets are present. Draft releases are not subject to the published-release history limit; published release history is capped independently so a newly prepared draft cannot be pushed out of the catalog by older releases.

Each deployment is self-contained: firmware files and manifests are copied into the Pages artifact rather than fetched by the browser from authenticated Actions endpoints.

**Important:** GitHub draft status is not a privacy boundary for RFScout firmware once a draft has been successfully prepared. The release-preparation workflow triggers a browser-flasher refresh, and valid draft release assets are mirrored into the public GitHub Pages site so they can be installed for pre-publication hardware validation. Anyone who knows or discovers the Pages site can download those mirrored files before the GitHub Release is formally published. Do not place confidential, embargoed, security-sensitive, or otherwise non-public firmware/assets into a prepared draft. Use a separate private validation path if the firmware must remain private until publication.

GitHub does not emit `release: deleted` workflow activity for draft releases. As a result, manually deleting a prepared draft cannot trigger an immediate event-driven cleanup. The hourly flasher reconciliation is the fallback that removes such withdrawn drafts from the public Pages mirror; a maintainer can also run **Publish browser flasher** manually for immediate cleanup. Treat a deleted draft as potentially still downloadable from the previous Pages deployment until one of those reconciliations succeeds.

Publishing a draft triggers an immediate release event. That tag-context event relays a fresh flasher run onto `main`, and the main-context run removes the draft label once candidate state is stable. Deleting a published/non-draft release can use the same relay where GitHub supplies that event.

The static flasher shell uses the repository's `assets/logo.png` branding and RFScout orange/white visual theme. GitHub Pages uses **GitHub Actions** as its publishing source. The flasher requires HTTPS and Web Serial; Chromium-based desktop browsers such as Edge and Chrome are the supported path.

The candidate portion of a Pages deployment represents current testable development builds rather than permanent hosting for historical CI artifacts. Published GitHub Releases provide the durable firmware history.

## Preparing a draft release

From the repository **Actions** page, select **Prepare firmware release**, choose **Run workflow**, and provide:

- a semantic version beginning with `v`, for example `v0.2.0` or `v0.2.1-rc.1`;
- a source ref, normally `main`; and
- whether the release should be marked prerelease.

The workflow resolves the source ref to an immutable commit and rejects requests when the version is malformed/duplicate, the source is not part of `main`, or the source contains no commits after the latest published release.

It repeats formatting, packaging tests, the ESP-IDF 6.1 build, and bundle generation at that exact commit. Only after those jobs pass does it create the semantic version tag at the validated commit and then create the **draft** GitHub Release against that existing tag. This explicit ordering prevents GitHub from assigning a temporary `untagged-*` release identity while a release is under review.

If draft creation itself fails after the workflow has pushed the tag, the workflow removes that newly-created tag before exiting. A successfully prepared draft intentionally retains its semantic tag while release notes and hardware validation are completed. The workflow never publishes the GitHub Release automatically.

A successfully prepared draft is nevertheless **publicly exposed through the browser flasher**: its valid web-flasher assets are mirrored to GitHub Pages as a draft-labeled catalog entry. Treat draft preparation as the point at which the firmware becomes publicly downloadable for validation purposes, not as a private staging step.

## Publishing

Before publishing a draft:

1. confirm that the firmware and release assets are suitable for public exposure;
2. review the generated release notes/version;
3. verify the archive/checksums;
4. perform applicable hardware installation and smoke tests using the same universal image on each supported profile;
5. record hardware validation in the release notes; and
6. publish the release deliberately through GitHub.

Publishing preserves the semantic tag created during draft preparation. The release publication event automatically starts the tag-context relay, which dispatches **Publish browser flasher** on `main`. The main-context run discovers the now-published release state and republishes the firmware catalog so the draft label is removed once candidate state is stable. The flasher does not rebuild firmware during publication/deployment.

## Withdrawing a prepared draft

If a prepared draft should no longer be available, delete the GitHub draft release and then either:

- run **Publish browser flasher** manually for immediate reconciliation; or
- allow the hourly scheduled reconciliation to remove it.

Because GitHub does not generate draft-release deletion workflow events, deleting the GitHub draft alone does not instantly remove files from an already deployed Pages snapshot.

## Compatibility before v1.0.0

RFScout versions before `v1.0.0` are development releases and do not promise backward compatibility between releases. Internal persistence layouts, removable-storage formats, log schemas, and other implementation contracts may change without migration shims when doing so keeps the implementation simpler or materially improves the design. Compatibility work may still be added when it has a specific operational need, but preserving superseded pre-1.0 formats is not a default requirement.

The project will revisit and define its long-term compatibility guarantees for `v1.0.0` and later. Until then, development builds should be treated as potentially format-breaking upgrades, and important removable or persistent data should be exported before upgrading when its continued readability matters.

## Installation behavior

The combined image is a clean installation/recovery image flashed at `0x0`. It can replace internal NVS and LittleFS state, including RFScout configuration, calibration, and other persistent system/app records. Removable SD contents such as logs and recordings are not part of the image and are not erased by flashing internal storage.

Separate component images are included for development/recovery. RFScout does not guarantee that application-image-only flashing is compatible across arbitrary releases.

## Reproducibility

RFScout enables ESP-IDF reproducible-build configuration and generates deterministic package metadata/checksums/ZIP output. Reproducing a firmware build still requires the same source revision, ESP-IDF/toolchain, generated source inputs, and managed-component resolution.

`dependencies.lock` is intentionally tracked in Git so managed component versions are part of the reproducibility boundary. It should change only when dependency resolution is intentionally updated.
