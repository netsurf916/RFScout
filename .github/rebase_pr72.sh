#!/usr/bin/env bash
set -euo pipefail
git fetch origin main
git rebase origin/main
git rm .github/rebase_pr72.sh .github/workflows/rebase-pr72.yml
git commit -m "Rebase PR72 onto current main"
git push --force-with-lease origin HEAD:design/service-provider-events
