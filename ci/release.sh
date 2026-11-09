#!/usr/bin/env bash
# Local release of the Qt port (docs/phase-6-packaging-stabilization.md 3.6, docs/release.md).
# GitHub is a repository only: everything is built, tested and signed (not yet) on this machine.
#
#   1. clean working tree, version = project VERSION in CMakeLists.txt, tag v<version> unused
#   2. local CI (ci/local-ci.sh: static checks, Windows MinGW + MSVC, Linux, both docking backends)
#   3. packages: Windows installer / portable zip / symbols (ci/package-windows.ps1),
#      Linux AppImage, deb, rpm, Flatpak and tar.gz (ci/package-linux.sh)
#   4. package tests (ci/test-package-windows.ps1, ci/test-package-linux.sh)
#   5. dist/SHA256SUMS.txt, release notes from docs/CHANGELOG.md
#   6. with --publish: git tag v<version>, push branch + tag, gh release create (pre-release
#      for 0.x) with the files of dist/
#
# Usage: ci/release.sh [--publish] [--skip-ci] [--skip-tests] [--allow-dirty]
#   --skip-ci / --skip-tests / --allow-dirty are for trying the script; a published release
#   must not use them.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PUBLISH=0; SKIP_CI=0; SKIP_TESTS=0; ALLOW_DIRTY=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --publish)     PUBLISH=1 ;;
        --skip-ci)     SKIP_CI=1 ;;
        --skip-tests)  SKIP_TESTS=1 ;;
        --allow-dirty) ALLOW_DIRTY=1 ;;
        -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
    shift
done
if [[ $PUBLISH -eq 1 && ( $SKIP_CI -eq 1 || $SKIP_TESTS -eq 1 || $ALLOW_DIRTY -eq 1 ) ]]; then
    echo "--publish cannot be combined with --skip-ci / --skip-tests / --allow-dirty"; exit 2
fi
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) echo "release.sh runs on the Windows host (it builds the Windows packages too). On Linux: ci/package-linux.sh [--local] and ci/test-package-linux.sh"; exit 2 ;; esac

step() { echo; echo "######## $*"; }
die() { echo "release: $*" >&2; exit 1; }

VERSION="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -1)"
TAG="v$VERSION"
DIST="$ROOT/dist"
step "Notepad++ Qt $VERSION ($TAG)"

# --- 1. tree and version -----------------------------------------------------------------------
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    [[ $ALLOW_DIRTY -eq 1 ]] || { git status --short --untracked-files=no; die "working tree not clean"; }
    echo "WARNING: working tree not clean (--allow-dirty)"
fi
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && die "tag $TAG exists already"
grep -q "^## \[$VERSION\]" docs/CHANGELOG.md || die "docs/CHANGELOG.md has no section ## [$VERSION]"
COMMIT="$(git rev-parse --short HEAD)"

# --- 2. local CI -------------------------------------------------------------------------------
if [[ $SKIP_CI -eq 0 ]]; then
    step "local CI"
    bash ci/local-ci.sh || die "local CI failed"
fi

# --- 3. packages -------------------------------------------------------------------------------
step "packages"
rm -rf "$DIST"; mkdir -p "$DIST"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$ROOT/ci/package-windows.ps1")" \
    -Dist "$(cygpath -w "$DIST")" || die "Windows packages failed"
bash ci/package-linux.sh --dist "$DIST" || die "Linux package failed"

# --- 4. package tests --------------------------------------------------------------------------
if [[ $SKIP_TESTS -eq 0 ]]; then
    step "package tests"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$ROOT/ci/test-package-windows.ps1")" \
        -Dist "$(cygpath -w "$DIST")" || die "Windows package test failed"
    bash ci/test-package-linux.sh --dist "$DIST" || die "Linux package test failed"
fi

# --- 5. checksums and notes ----------------------------------------------------------------------
step "checksums"
( cd "$DIST" && sha256sum -- notepadpp-qt-* > SHA256SUMS.txt && cat SHA256SUMS.txt )
NOTES="$DIST/release-notes.md"
{
    awk -v v="$VERSION" '$0 ~ "^## \\[" v "\\]" {on=1; next} on && /^## \[/ {exit} on {print}' docs/CHANGELOG.md
    echo
    echo "Built locally from commit \`$COMMIT\` with \`ci/release.sh\`; local CI and package tests passed."
    echo "Packages are not code-signed yet: Windows SmartScreen may warn. Check the files against SHA256SUMS.txt."
} > "$NOTES"
echo "notes: ${NOTES#$ROOT/}"

# --- 6. publish ----------------------------------------------------------------------------------
if [[ $PUBLISH -eq 0 ]]; then
    step "done (not published: run with --publish to tag, push and create the GitHub release)"
    ls -la "$DIST"
    exit 0
fi
step "publish $TAG"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
git tag -a "$TAG" -m "Notepad++ Qt $VERSION"
git push origin "$BRANCH"
git push origin "$TAG"
REPO="$(git remote get-url origin | sed -E 's#^(https://github.com/|git@github.com:)##; s#.git$##')"
PRE=""; [[ "$VERSION" == 0.* ]] && PRE="--prerelease"
gh release create --repo "$REPO" "$TAG" $PRE --target "$BRANCH" --title "Notepad++ Qt $VERSION (beta)" --notes-file "$NOTES" \
    "$DIST"/notepadpp-qt-* "$DIST/SHA256SUMS.txt"
gh release view --repo "$REPO" "$TAG" --json url,assets --jq '.url, (.assets[] | .name)'
