# Qt 포트 빌드 가이드

대상: `qt-port` 브랜치의 Qt 6 빌드 (Windows / Linux, macOS는 2026-11-09 주 추가 예정).
기존 Win32 빌드(`PowerEditor/visual.net`, `PowerEditor/gcc`)는 이 문서 대상이 아니다.

## 공통 준비

```bash
git clone --recurse-submodules -b qt-port https://github.com/asprenas/notepad-plus-plus.git
# 이미 clone 했다면
git submodule update --init --recursive
```

| 요구사항 | 버전 |
|---|---|
| CMake | 3.24+ |
| Ninja | 아무 버전 |
| Qt | 6.5+ (Core, Gui, Widgets, **Core5Compat**) |
| 컴파일러 | C++20 (MSVC 2022 / MinGW 13 / GCC 13+ / Clang 17+) |

`Core5Compat`는 Scintilla Qt 레이어(`PlatQt.cpp`)의 `QTextCodec` 때문에 필요하다.
Qt 온라인 설치본에서는 "Qt 5 Compatibility Module"을 선택해야 한다.

빌드 결과: `build/<preset>/bin/notepadpp-qt[.exe]` + `bin/data/` (`langs.model.xml`, `stylers.model.xml` 복사본)

## Windows — MinGW (Qt 설치본 포함 툴체인)

Qt 설치 경로가 `C:\Programs\Qt` 인 예:

```bash
export PATH="/c/Programs/Qt/Tools/mingw1310_64/bin:/c/Programs/Qt/Tools/Ninja:/c/Programs/Qt/6.12.0/mingw_64/bin:$PATH"
cmake --preset windows-mingw-debug -DCMAKE_PREFIX_PATH=C:/Programs/Qt/6.12.0/mingw_64
cmake --build --preset windows-mingw-debug
ctest --test-dir build/windows-mingw-debug --output-on-failure
./build/windows-mingw-debug/bin/notepadpp-qt.exe
```

실행 시 Qt DLL이 `PATH`에 있어야 한다 (배포용 `windeployqt`는 6단계에서 정리).

## Windows — MSVC

Qt `msvc2022_64` kit 필요. "x64 Native Tools Command Prompt"에서:

```bat
cmake --preset windows-msvc-release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset windows-msvc-release
```

## Linux — Docker (권장, 호스트에 Qt 설치 불필요)

Ubuntu 26.04 기반 (Qt 6.10). Ubuntu 24.04 패키지는 Qt 6.4라 최소 요구(6.5) 미달.

```bash
docker build -t npp-qt-linux -f ci/docker/linux.Dockerfile ci/docker
docker run --rm -v "$PWD:/src" -w /src npp-qt-linux bash -c \
  "cmake --preset linux-release && cmake --build --preset linux-release && ctest --test-dir build/linux-release"
```

## Linux — 네이티브 (Ubuntu 26.04 / Debian 계열)

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-private-dev \
                 qt6-5compat-dev libxcb1-dev libgl-dev
cmake --preset linux-release
cmake --build --preset linux-release
./build/linux-release/bin/notepadpp-qt
```

ADS는 Linux에서 `Qt6::GuiPrivate`(qt6-base-private-dev)와 `xcb`가 필요하다.

## 빌드 옵션

| 옵션 | 값 | 설명 |
|---|---|---|
| `NPP_DOCKING_BACKEND` | `ADS` (기본) / `QT` | 도킹 구현 선택. `QT`는 QDockWidget 기반 대체 구현 |
| `BUILD_TESTING` | `ON` (기본) / `OFF` | `app/tests` 빌드 |

## 스모크 테스트

```bash
# 창을 렌더링해 PNG로 저장하고 종료 (헤드리스 가능)
QT_QPA_PLATFORM=offscreen ./build/linux-release/bin/notepadpp-qt --screenshot out.png some-file.cpp
```

## 로컬 CI

GitHub은 저장소로만 사용하며 GitHub Actions는 비활성화되어 있다. 모든 검증은 로컬에서 수행한다.
**push 전에 실행한다.**

```bash
bash ci/local-ci.sh                 # 정적 검사 + Windows(네이티브) + Linux(Docker), ADS/QT 백엔드 모두
bash ci/local-ci.sh --linux         # Linux만
bash ci/local-ci.sh --windows --backend ADS
bash ci/local-ci.sh --no-smoke      # 스크린샷 스모크 실행 생략
```

| 대상 | 실행 방식 | 빌드 위치 |
|---|---|---|
| Windows | Qt 설치본 MinGW kit (`NPP_QT_DIR`, `NPP_QT_TOOLS`로 경로 변경) | `build/ci/windows-mingw-<backend>/` |
| Linux (Windows 호스트) | Docker 이미지 `npp-qt-linux`, 빌드 트리는 Docker 볼륨 `npp-qt-linux-build`(증분 빌드) | 볼륨 내부 |
| Linux (Linux 호스트) | 네이티브 | `build/ci/linux-<backend>/` |
| macOS | 2026-11-09 주 추가 예정 | |

결과: 마지막에 요약 표 출력, 실패 시 종료 코드 1. 로그 `build/ci/logs/`, 스크린샷 `build/ci/artifacts/`.
Docker Desktop이 꺼져 있으면 Linux 항목은 `FAIL (no docker)`로 표시된다.

Linux 빌드 볼륨 초기화: `docker volume rm npp-qt-linux-build`

## 정적 검사

```bash
bash ci/check-docking-abstraction.sh   # ADS 사용이 app/ui/docking/ads/ 밖으로 새지 않았는지
```

## 설정 저장 위치 (0단계 임시)

0단계 샘플은 창/도킹 상태를 `QSettings`(조직명 `NotepadPlusPlusQt`)에 저장한다.
Windows: 레지스트리 `HKCU\Software\NotepadPlusPlusQt`, Linux: `~/.config/NotepadPlusPlusQt/`.
기존 Notepad++ 설정 파일(`%APPDATA%\Notepad++`)은 아직 읽거나 쓰지 않는다 (1단계 `npp-core`에서 도입).
