#!/usr/bin/env bash
# Prints the release notes of a version: its docs/CHANGELOG.md section, how it was built and
# the signing notice. Used by ci/release.sh and .github/workflows/release.yml.
#
# Usage: ci/release-notes.sh <version> <build description>
set -euo pipefail

VERSION="$1"
BUILD_LINE="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

awk -v v="$VERSION" '$0 ~ "^## \\[" v "\\]" {on=1; next} on && /^## \[/ {exit} on {print}' "$ROOT/docs/CHANGELOG.md"
echo
echo "$BUILD_LINE"
echo "Packages are not code-signed yet: Windows SmartScreen may warn. Check the files against SHA256SUMS.txt."
