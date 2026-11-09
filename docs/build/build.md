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

Qt `msvc2022_64` kit (Core5Compat 포함) + Visual Studio C++ 워크로드 필요.
VS 2026(MSVC 19.5x)에서도 `msvc2022_64` kit 사용 가능 (MSVC 2015 이후 ABI 호환). "x64 Native Tools Command Prompt"에서:

```bat
set PATH=C:\Programs\Qt\Tools\Ninja;C:\Programs\Qt\6.12.0\msvc2022_64\bin;%PATH%
cmake --preset windows-msvc-release -DCMAKE_PREFIX_PATH=C:/Programs/Qt/6.12.0/msvc2022_64
cmake --build --preset windows-msvc-release
ctest --test-dir build/windows-msvc-release --output-on-failure
```

Git Bash에서는 `bash ci/local-ci.sh --msvc`가 vswhere로 VS를 찾아 `vcvars64.bat` 환경에서 같은 과정을 수행한다.

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

선택 사항인 ADS는 Linux에서 `Qt6::GuiPrivate`(qt6-base-private-dev)와 `xcb`가 필요하다.

## 빌드 옵션

| 옵션 | 값 | 설명 |
|---|---|---|
| `NPP_DOCKING_BACKEND` | `QT` (기본) / `ADS` | 도킹 구현 선택. `QT`는 QDockWidget 기반 구현, `ADS`는 전환 검토용 선택 옵션 |
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
bash ci/local-ci.sh                 # 정적 검사 + Windows(MinGW, MSVC) + Linux(Docker), QT 백엔드
bash ci/local-ci.sh --linux         # Linux만
bash ci/local-ci.sh --msvc          # Windows MSVC만 (--mingw: MinGW만, --windows: 둘 다)
bash ci/local-ci.sh --windows       # Windows만, QT 백엔드
bash ci/local-ci.sh --linux --backend ADS  # 선택 사항: ADS 전환 검토
bash ci/local-ci.sh --linux --backend all  # 선택 사항: 두 백엔드 모두 검증
bash ci/local-ci.sh --no-smoke      # 스크린샷 스모크 실행 생략
```

| 대상 | 실행 방식 | 빌드 위치 |
|---|---|---|
| Windows MinGW | Qt 설치본 MinGW kit (`NPP_QT_DIR`, `NPP_QT_TOOLS`로 경로 변경) | `build/ci/windows-mingw-<backend>/` |
| Windows MSVC | Qt `msvc2022_64` kit (`NPP_QT_MSVC_DIR`) + vswhere로 찾은 Visual Studio | `build/ci/windows-msvc-<backend>/` |
| Linux (Windows 호스트) | Docker 이미지 `npp-qt-linux`, 빌드 트리는 Docker 볼륨 `npp-qt-linux-build`(증분 빌드) | 볼륨 내부 |
| Linux (Linux 호스트) | 네이티브 | `build/ci/linux-<backend>/` |
| macOS | 2026-11-09 주 추가 예정 | |

결과: 마지막에 요약 표 출력, 실패 시 종료 코드 1. 로그 `build/ci/logs/`, 스크린샷 `build/ci/artifacts/`.
Docker Desktop이 꺼져 있으면 Linux 항목은 `FAIL (no docker)`로 표시된다.

일반 빌드 프리셋과 Windows/Linux 패키징도 `QT`를 사용한다. 기존 빌드 폴더에 `ADS`가
캐시되어 있다면 프리셋으로 다시 configure하거나 `-DNPP_DOCKING_BACKEND=QT`를 지정한다.
Windows 패키징의 `-SkipBuild`는 기존 산출물을 재사용하므로, 전환 후 처음에는 생략한다.

Linux 빌드 볼륨 초기화: `docker volume rm npp-qt-linux-build`

## 정적 검사

```bash
bash ci/check-docking-abstraction.sh   # ADS 사용이 app/ui/docking/ads/ 밖으로 새지 않았는지
```

## 설정 저장 위치 (0단계 임시)

0단계 샘플은 창/도킹 상태를 `QSettings`(조직명 `NotepadPlusPlusQt`)에 저장한다.
Windows: 레지스트리 `HKCU\Software\NotepadPlusPlusQt`, Linux: `~/.config/NotepadPlusPlusQt/`.
설정 파일 폴더는 `%APPDATA%\Notepad++Qt` (Linux `~/.config/notepad++qt`)이며 원본 Notepad++ 폴더와 분리되어 있다.

## CLion

로컬 경로가 들어가는 설정은 저장소에 넣지 않는다 (`CMakeUserPresets.json`, `.idea/`는 gitignore 대상).

1. 툴체인: CLion의 **Visual Studio** 툴체인 + Qt `msvc2022_64` kit 사용.
   CLion 기본 MinGW 툴체인(GCC 16, win32 스레드)은 Qt kit의 MinGW 13.1(posix)과 런타임이 달라 섞어 쓰면 안 된다.
2. 저장소 루트에 `CMakeUserPresets.json` 작성 (예):

   ```json
   {
     "version": 6,
     "configurePresets": [{
       "name": "clion-msvc-debug",
       "inherits": "windows-msvc-debug",
       "binaryDir": "${sourceDir}/build/clion-msvc-debug",
       "cacheVariables": {
         "CMAKE_PREFIX_PATH": "C:/Programs/Qt/6.12.0/msvc2022_64",
         "NPP_DEPLOY_QT_RUNTIME": "ON"
       },
       "vendor": { "jetbrains.com/clion": { "toolchain": "Visual Studio" } }
     }],
     "buildPresets": [{ "name": "clion-msvc-debug", "configurePreset": "clion-msvc-debug" }]
   }
   ```

   `NPP_DEPLOY_QT_RUNTIME=ON`이면 빌드 후 `windeployqt`가 Qt DLL·플러그인을 실행 파일 옆에 복사하므로
   실행/디버그 구성에 `PATH` 설정이 필요 없다.
3. CLion에서 `CMakeLists.txt` → **Load CMake Project**, Settings → Build → CMake에서 `clion-msvc-debug` 프로필 활성화.
4. CMake 로드 후 실행 대상(`notepadpp-qt`, `test_editor_smoke`)별 실행 구성이 자동 생성된다.
5. 디버깅: Settings → Toolchains → **Visual Studio** 툴체인의 Debugger = **Bundled LLDB**.
   CLion이 자동 생성하는 기본 `Debug` 프로필(MinGW 툴체인)은 **비활성화**한다.
   켜져 있으면 MSVC 빌드에 GDB가 붙어 PDB 심볼을 못 읽고 중단점이 동작하지 않는다.
   LLDB로 붙으면 Qt 타입(`QString`, `QByteArray` 등) 값도 표시된다.

## 배포본 (6단계)

| 명령 | 결과 |
|---|---|
| `powershell -ExecutionPolicy Bypass -File ci/package-windows.ps1` | `dist/` Windows 설치본(Inno Setup 6 필요), 포터블 zip, PDB zip (MSVC Release) |
| `bash ci/package-linux.sh` | `dist/` AppImage, deb, rpm, Flatpak 번들, tar.gz (Docker: Ubuntu 22.04 + Qt 6.12, `ci/docker/appimage.Dockerfile`) |
| `bash ci/package-linux.sh --local` | Docker 없이 이 Linux에서 AppImage (로컬 확인용: `QT_DIR`, ninja, pkg-config + gio-2.0, `linuxdeploy`·`linuxdeploy-plugin-qt`·`linuxdeploy-plugin-appimage` 필요. 이 호스트의 glibc 이상에서만 실행) |
| `powershell -ExecutionPolicy Bypass -File ci/test-package-windows.ps1` | 의존 DLL, 빈 PATH 실행, 포터블, 설치·파일 연결·제거 시험 |
| `bash ci/test-package-linux.sh` | Ubuntu 22.04/24.04, Debian 12, Fedora 41 새 컨테이너에서 AppImage 시험 |
| `bash ci/release.sh [--publish]` | 전체 릴리스 ([release.md](release.md)) |

설치 레이아웃은 CMake `install()` 규칙(`app/ui/CMakeLists.txt`): Windows는 실행 파일 옆 `data/`,
Linux는 `bin/notepadpp-qt` + `share/notepadpp-qt/data/` + `.desktop`/아이콘/metainfo (앱 ID `io.github.asprenas.notepadpp_qt`).

Linux Docker 패키지는 x86-64용이다. deb와 rpm은 동일한 AppImage를 `/opt/notepadpp-qt/`에 설치하고 실행 래퍼와 데스크톱 항목을 제공한다. tar.gz는 `AppDir`을 담으며 압축 해제 후 `AppDir/AppRun`으로 실행한다. Flatpak 번들은 freedesktop 24.08 런타임이 필요하며 `flatpak install --user 파일명.flatpak`으로 설치한다. 최초 빌드에는 Docker 이미지와 Flatpak 런타임 다운로드가 필요하다. AppImage와 tar.gz 및 deb/rpm은 빌드 환경의 glibc 2.35 이상이 필요하다. `ci/test-package-linux.sh`는 현재 AppImage만 시험한다.
