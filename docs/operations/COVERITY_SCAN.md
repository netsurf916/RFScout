# Coverity Scan integration

**Status:** The workflow and public flasher badge are merged on `main` via PR #148. This page has not verified a completed scan, repository credentials or a successful Xtensa compiler capture; a configured workflow is not proof of a passing scan.

RFScout's Coverity Scan project: https://scan.coverity.com/projects/rfscout

The [web flasher](../../web/flasher/index.html) embeds Coverity's public build-status badge
(`https://scan.coverity.com/projects/33349/badge.svg`). It is a status indicator,
**not** an assertion that a firmware candidate is safe or free of defects. Until the
first scan finishes, the badge correctly reports a pending state. The flasher
build catalog and release process are independent of Coverity status.

## Repository setup (owner action)

In **GitHub → Settings → Secrets and variables → Actions**, add the following
**repository secrets**:

- `COVERITY_SCAN_TOKEN`: the project submission token shown on Coverity Scan's
  Project Settings page. Never commit it, embed it in the flasher, or paste it
  into CI logs. Rotate it if it is accidentally published to a public location.
- `COVERITY_SCAN_EMAIL`: an email address authorized to receive Coverity
  submission notifications (Coverity's build-upload form requires an email).

The GitHub connection used to author the PR cannot write Actions secrets. Those
must be set separately through a repository owner account. The token is used by
the build-tool download and build-submission requests only.

## First scan

1. Confirm both repository Actions secrets are set; the workflow is already on `main`.
2. Open **Actions → Coverity Scan → Run workflow**, select `main`, and run it.
3. Review the capture log and project build on Coverity Scan; confirm the
   analyzed source volume and findings are reasonable. If compilation units
   are not captured, inspect the compiler configuration rather than accepting
   an empty scan as successful.
4. After the first cleanly captured submission, consider a weekly scheduled
   scan on `main` and a process for triaging findings.

No automatic schedule or PR-triggered submission is enabled in this initial
version. This avoids sending credentials to untrusted pull requests and limits
Coverity Scan quota consumption while the first ESP32-S3 cross-compiler capture
is being validated. Scan submission is not a PR merge gate.

## Technical choices

- Reuse `espressif/idf:release-v6.1` and `esp32s3` from the existing firmware
  build workflow.
- Download the C/C++ capture tools from Coverity Scan with the project token;
  do not store the tools or capture archive in Git.
- Run `cov-configure --template` for the IDF Xtensa cross compiler, then
  `cov-build --return-emit-failures` around the firmware build so an empty
  capture fails the job. Do not confuse a successful native build with a
  successful Coverity capture.
- Submit the `cov-int` archive with the commit SHA as the version. No firmware
  binary or release is uploaded by this workflow.
- The token is provided through Actions secrets and is never placed in the
  workflow's source, build artifacts, or public web page.

The service is external and may change its tool download or upload protocol.
If the first run fails, adjust the workflow based on the actual Coverity/ESP-IDF
logs before enabling routine scans.
