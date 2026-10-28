#!/usr/bin/env bash
# Creates (or updates) the standard label set for the repository.
# Usage: OWNER=JorreFog REPO=BananaLinux ./scripts/setup-labels.sh
set -euo pipefail

OWNER="${OWNER:-JorreFog}"
REPO="${REPO:-BananaLinux}"

# Remove GitHub's default labels that are not part of our set.
# Missing labels are ignored.
for name in "good first issue" "help wanted" "invalid" "documentation"; do
  gh label delete "$name" --repo "$OWNER/$REPO" --yes 2>/dev/null || true
done

# Format: name|color|description
labels=(
  # Type
  "bug|d73a4a|Something is not working"
  "enhancement|a2eeef|New feature or improvement"
  "question|d876e3|Further information is requested"
  # Status
  "needs-triage|fbca04|Waiting to be reviewed by the team"
  "needs-info|fef2c0|More information is needed from the reporter"
  "confirmed|0e8a16|Reproduced and accepted by the team"
  "wontfix|ffffff|Will not be worked on"
  "duplicate|cfd3d7|This issue already exists"
  # Priority
  "P0-critical|b60205|Breaks installation or boot, fix immediately"
  "P1-high|d93f0b|Major problem, fix in the current release"
  "P2-normal|fbca04|Normal priority"
  "P3-low|c2e0c6|Minor problem or nice to have"
  # Area
  "installer|1d76db|Related to the installer"
  "kernel|1d76db|Related to the kernel"
  "desktop|1d76db|Related to the desktop environment"
  "packages|1d76db|Related to packages and repositories"
  "build-system|1d76db|Related to the ISO build system"
)

for entry in "${labels[@]}"; do
  IFS='|' read -r name color description <<< "$entry"
  gh label create "$name" --color "$color" --description "$description" \
    --repo "$OWNER/$REPO" --force
  echo "Label ready: $name"
done
