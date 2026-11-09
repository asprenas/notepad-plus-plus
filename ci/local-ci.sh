#!/usr/bin/env bash
# Local CI for the Qt port. GitHub is used as a repository only (Actions disabled);
# every check runs on the developer machine.
#
# Usage: ci/local-ci.sh [options]
#   --windows          Windows native builds: MinGW + MSVC        [default on Windows hosts]
#   --mingw            Windows MinGW build only
#   --msvc             Windows MSVC build only
#   --linux            Linux build in Docker on every host        [default on all hosts]
#   --backend B        ADS | QT | all (docking backend)            [default: QT]
#   --no-smoke         skip the --screenshot smoke run
#   -h, --help
#
# Environment (Windows):
#   NPP_QT_DIR        MinGW Qt kit prefix (default C:/Programs/Qt/6.12.0/mingw_64)
#   NPP_QT_MSVC_DIR   MSVC Qt kit prefix  (default C:/Programs/Qt/6.12.0/msvc2022_64)
#   NPP_QT_TOOLS      Qt Tools dir        (default C:/Programs/Qt/Tools; mingw1310_64 + Ninja)
#   Visual Studio (C++ workload) is located with vswhere.
#
# Output: build/ci/<target>-<backend>/  logs: build/ci/logs/  screenshots: build/ci/artifacts/
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CI_DIR="$ROOT/build/ci"
LOG_DIR="$CI_DIR/logs"
ART_DIR="$CI_DIR/artifacts"
LINUX_IMAGE="npp-qt-linux"
LINUX_BUILD_VOLUME="npp-qt-linux-build"   # keeps Linux builds incremental between runs

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) HOST=windows ;;
    Linux)                HOST=linux ;;
    Darwin)               HOST=macos ;;
    *)                    HOST=unknown ;;
esac

DO_MINGW=0; DO_MSVC=0; DO_LINUX=0; EXPLICIT=0; BACKENDS=(QT); SMOKE=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --windows) DO_MINGW=1; DO_MSVC=1; EXPLICIT=1 ;;
        --mingw)   DO_MINGW=1; EXPLICIT=1 ;;
        --msvc)    DO_MSVC=1; EXPLICIT=1 ;;
        --linux)   DO_LINUX=1; EXPLICIT=1 ;;
        --backend)
            shift
            case "${1:-}" in
                ADS|QT) BACKENDS=("$1") ;;
                all)    BACKENDS=(ADS QT) ;;
                *) echo "--backend must be ADS, QT or all"; exit 2 ;;
            esac ;;
        --no-smoke) SMOKE=0 ;;
        -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
    shift
done
if [[ $EXPLICIT -eq 0 ]]; then
    DO_LINUX=1
    [[ $HOST == windows ]] && { DO_MINGW=1; DO_MSVC=1; }
fi

mkdir -p "$LOG_DIR" "$ART_DIR"
RESULTS=()
FAILED=0

record() { # name status
    RESULTS+=("$(printf '%-28s %s' "$1" "$2")")
    [[ "$2" == PASS* ]] || FAILED=1
}

step() { # name logfile command...
    local name="$1" log="$2"; shift 2
    printf '  %-12s ... ' "$name"
    if "$@" >>"$log" 2>&1; then echo ok; return 0; fi
    echo "FAILED (see ${log#$ROOT/})"; return 1
}

# ---------------------------------------------------------------------------
echo "== static checks"
if bash "$ROOT/ci/check-docking-abstraction.sh" >"$LOG_DIR/static.log" 2>&1; then
    record "static: docking abstraction" PASS
else
    cat "$LOG_DIR/static.log"; record "static: docking abstraction" FAIL
fi
if bash "$ROOT/ci/check-core-rules.sh" >"$LOG_DIR/static-core.log" 2>&1; then
    record "static: core rules" PASS
else
    cat "$LOG_DIR/static-core.log"; record "static: core rules" FAIL
fi

# ---------------------------------------------------------------------------
run_windows() { # backend
    local b="$1" name="windows-mingw-$b"
    local qt="${NPP_QT_DIR:-C:/Programs/Qt/6.12.0/mingw_64}" tools="${NPP_QT_TOOLS:-C:/Programs/Qt/Tools}"
    local bdir="$CI_DIR/$name" log="$LOG_DIR/$name.log"
    : >"$log"
    echo "== $name"
    if [[ ! -d "$qt" ]]; then echo "  Qt kit not found: $qt (set NPP_QT_DIR)"; record "$name" "FAIL (no Qt)"; return; fi

    local tools_u qt_u
    tools_u="$(cygpath -u "$tools" 2>/dev/null || echo "$tools")"
    qt_u="$(cygpath -u "$qt" 2>/dev/null || echo "$qt")"
    export PATH="$tools_u/mingw1310_64/bin:$tools_u/Ninja:$qt_u/bin:$PATH"

    step configure "$log" cmake --preset windows-mingw-release -S "$ROOT" -B "$bdir" \
            -DCMAKE_PREFIX_PATH="$qt" -DNPP_DOCKING_BACKEND="$b" || { record "$name" "FAIL (configure)"; return; }
    step build "$log" cmake --build "$bdir" -j || { record "$name" "FAIL (build)"; return; }
    step test "$log" ctest --test-dir "$bdir" --output-on-failure || { record "$name" "FAIL (test)"; return; }
    if [[ $SMOKE -eq 1 ]]; then
        local shot="$ART_DIR/$name.png"
        step smoke "$log" env QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR='C:\Windows\Fonts' \
            timeout 60 "$bdir/bin/notepadpp-qt.exe" --screenshot "$shot" "$ROOT/app/ui/MainWindow.cpp" \
            || { record "$name" "FAIL (smoke)"; return; }
    fi
    record "$name" PASS
}

run_windows_msvc() { # backend
    local b="$1" name="windows-msvc-$b"
    local qt="${NPP_QT_MSVC_DIR:-C:/Programs/Qt/6.12.0/msvc2022_64}" tools="${NPP_QT_TOOLS:-C:/Programs/Qt/Tools}"
    local bdir="$CI_DIR/$name" log="$LOG_DIR/$name.log"
    : >"$log"
    echo "== $name"
    if [[ ! -d "$qt" ]]; then echo "  Qt kit not found: $qt (set NPP_QT_MSVC_DIR)"; record "$name" "FAIL (no Qt)"; return; fi

    local vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" vs
    vs="$("$vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
            -property installationPath 2>/dev/null | tr -d '\r')"
    if [[ -z "$vs" ]]; then echo "  Visual Studio C++ tools not found"; record "$name" "FAIL (no MSVC)"; return; fi

    # MSVC needs the vcvars environment: configure/build/test run from a generated batch file.
    local bat="$CI_DIR/$name.bat"
    local w_root w_bdir w_tools w_qt
    w_root="$(cygpath -w "$ROOT")"; w_bdir="$(cygpath -w "$bdir")"
    w_tools="$(cygpath -w "$tools")"; w_qt="$(cygpath -w "$qt")"
    {
        echo '@echo off'
        echo "call \"$vs\\VC\\Auxiliary\\Build\\vcvars64.bat\" >nul 2>&1 || exit /b 10"
        echo "set \"PATH=$w_tools\\Ninja;$w_qt\\bin;%PATH%\""
        echo "cd /d \"$w_root\""
        echo "cmake --preset windows-msvc-release -B \"$w_bdir\" -DCMAKE_PREFIX_PATH=$qt -DNPP_DOCKING_BACKEND=$b || exit /b 11"
        echo "cmake --build \"$w_bdir\" -j || exit /b 12"
        echo "ctest --test-dir \"$w_bdir\" --output-on-failure || exit /b 13"
    } >"$bat"

    printf '  %-12s ... ' "build+test"
    cmd //c "$(cygpath -w "$bat")" >>"$log" 2>&1
    local rc=$?
    case $rc in
        0)  echo ok ;;
        10) echo "FAILED (vcvars64.bat)"; record "$name" "FAIL (vcvars)"; return ;;
        11) echo "FAILED (see ${log#$ROOT/})"; record "$name" "FAIL (configure)"; return ;;
        12) echo "FAILED (see ${log#$ROOT/})"; record "$name" "FAIL (build)"; return ;;
        *)  echo "FAILED (see ${log#$ROOT/})"; record "$name" "FAIL (test)"; return ;;
    esac
    if [[ $SMOKE -eq 1 ]]; then
        step smoke "$log" env PATH="$(cygpath -u "$qt")/bin:$PATH" QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR='C:\Windows\Fonts' \
            timeout 60 "$bdir/bin/notepadpp-qt.exe" --screenshot "$ART_DIR/$name.png" "$ROOT/app/ui/MainWindow.cpp" \
            || { record "$name" "FAIL (smoke)"; return; }
    fi
    record "$name" PASS
}

run_linux_docker() { # backend
    local b="$1" name="linux-docker-$b"
    local log="$LOG_DIR/$name.log"
    : >"$log"
    echo "== $name"
    if ! docker info >/dev/null 2>&1; then
        echo "  Docker not available (start the daemon, check docker group permission on Linux)"; record "$name" "FAIL (no docker)"; return
    fi
    step image "$log" docker build -q -t "$LINUX_IMAGE" -f "$ROOT/ci/docker/linux.Dockerfile" "$ROOT/ci/docker" \
        || { record "$name" "FAIL (image)"; return; }

    local src="$ROOT" out="$ART_DIR"
    if [[ $HOST == windows ]]; then src="$(cygpath -m "$ROOT")"; out="$(cygpath -m "$ART_DIR")"; fi
    local smoke_cmd="true"
    [[ $SMOKE -eq 1 ]] && smoke_cmd="QT_QPA_PLATFORM=offscreen timeout 60 /build/$b/bin/notepadpp-qt --screenshot /out/$name.png /src/app/ui/MainWindow.cpp"
    # The container runs as root: on a Linux host the screenshot is handed back to the caller's user.
    local owner_cmd="true"
    [[ $HOST == linux && $SMOKE -eq 1 ]] && owner_cmd="chown $(id -u):$(id -g) /out/$name.png"

    # Source is mounted read-only; build tree lives in a named volume (incremental).
    step build+test "$log" env MSYS_NO_PATHCONV=1 docker run --rm \
        -v "$src:/src:ro" -v "$out:/out" -v "$LINUX_BUILD_VOLUME:/build" -w /tmp "$LINUX_IMAGE" bash -c "
            set -e
            cmake -S /src -B /build/$b -G Ninja -DCMAKE_BUILD_TYPE=Release -DNPP_DOCKING_BACKEND=$b
            cmake --build /build/$b -j\$(nproc)
            ctest --test-dir /build/$b --output-on-failure
            $smoke_cmd
            $owner_cmd" \
        || { record "$name" "FAIL (see log)"; return; }
    record "$name" PASS
}

# ---------------------------------------------------------------------------
for b in "${BACKENDS[@]}"; do
    if [[ $DO_MINGW -eq 1 || $DO_MSVC -eq 1 ]]; then
        if [[ $HOST != windows ]]; then
            record "windows-$b" "SKIP (not a Windows host)"
        else
            [[ $DO_MINGW -eq 1 ]] && run_windows "$b"
            [[ $DO_MSVC -eq 1 ]] && run_windows_msvc "$b"
        fi
    fi
    if [[ $DO_LINUX -eq 1 ]]; then
        run_linux_docker "$b"
    fi
done

echo
echo "================ local CI summary ================"
printf '%s\n' "${RESULTS[@]}"
echo "logs:        ${LOG_DIR#$ROOT/}/"
echo "screenshots: ${ART_DIR#$ROOT/}/"
exit $FAILED
