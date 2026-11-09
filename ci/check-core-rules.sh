#!/usr/bin/env bash
# Enforces docs/plan/phase-1-core-extraction.md completion rules for app/core:
#  1. npp_core links QtCore only (no QtGui / QtWidgets includes or links)
#  2. no <windows.h>, wchar_t, std::wstring, L"..." literals outside app/core/platform/win/
set -euo pipefail
cd "$(dirname "$0")/.."

fail=0
report() { echo "$1"; echo "$2"; fail=1; }

src=$(find app/core -name '*.h' -o -name '*.cpp' | grep -v '^app/core/platform/win/' || true)

if [[ -n "$src" ]]; then
    v=$(grep -nE '#include[[:space:]]*<(QtGui|QtWidgets|QWidget|QApplication|QGuiApplication|QFont|QColor|QPixmap|QIcon|QImage)' $src || true)
    [[ -n "$v" ]] && report "core must not use QtGui/QtWidgets:" "$v"

    v=$(grep -nE '#include[[:space:]]*[<"](windows\.h|Windows\.h|shlwapi\.h|shlobj\.h|tchar\.h)' $src || true)
    [[ -n "$v" ]] && report "Windows headers outside app/core/platform/win:" "$v"

    v=$(grep -nE '\bwchar_t\b|\bstd::wstring\b|\bwstring_view\b|(^|[^A-Za-z0-9_])L"' $src || true)
    [[ -n "$v" ]] && report "wide strings in core (UTF-8 std::string only):" "$v"
fi

if grep -nE 'Qt6::(Gui|Widgets)' app/core/CMakeLists.txt >/dev/null; then
    report "app/core/CMakeLists.txt links QtGui/QtWidgets:" "$(grep -nE 'Qt6::(Gui|Widgets)' app/core/CMakeLists.txt)"
fi

if [[ $fail -ne 0 ]]; then exit 1; fi
echo "Core rules check passed."
