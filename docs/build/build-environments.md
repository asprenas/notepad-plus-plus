# OS별 빌드 환경과 릴리스 파일

Qt 포트의 검증 빌드와 릴리스 빌드가 OS별로 어떤 환경에서 만들어지는지 정리한다.
기준 버전은 0.2.1이며, 기존 Win32 빌드(`PowerEditor/visual.net`, `PowerEditor/gcc`)는 대상이 아니다.

빌드 명령은 [build.md](build.md), 릴리스 절차는 [release.md](release.md), macOS 작업 목록은 [phase-macos.md](../plan/phase-macos.md)를 본다.

## 1. 요약

| OS | 상태 | 릴리스 컴파일러 | Qt | 현재 릴리스 파일 |
|---|---|---|---|---|
| Windows x64 | 릴리스 중 | MSVC | 6.12.0 `msvc2022_64` | 포터블 zip, 심볼 zip |
| Linux x86-64 | 릴리스 중 | GCC (g++ 12) | 6.12.0 `gcc_64` | AppImage, tar.gz |
| macOS | 미구현 (계획) | Apple Clang (계획) | 6.12 Universal kit (계획) | 없음 (`.app` + `.dmg` 계획) |

모든 대상은 같은 소스를 C++20, CMake 3.24+, Ninja로 빌드하며 Qt 6.5 이상과 Core5Compat 모듈이 필요하다.
도킹 백엔드는 기본값 `QT`를 사용한다. `ADS`는 전환 검토용 선택 옵션이다.

## 2. 검증 빌드와 릴리스 빌드

검증 빌드는 `ci/local-ci.sh`가 실행하며 빌드, `ctest` 테스트, offscreen 스크린샷 스모크 실행을 수행한다.
릴리스 빌드는 패키징 스크립트가 별도 환경에서 `BUILD_TESTING=OFF`로 만든다.

| 대상 | 검증 빌드 | 릴리스 빌드 | 비고 |
|---|---|---|---|
| Windows MSVC | 있음 | 있음 | Qt kit과 컴파일러가 같고 빌드 폴더는 다르다 |
| Windows MinGW | 있음 | 없음 | 컴파일러 호환 검증 전용 |
| Linux | Ubuntu 26.04, Qt 6.10 | Ubuntu 22.04, Qt 6.12.0 | 환경이 달라 검증 통과가 패키지 빌드 통과를 보장하지 않는다 |
| macOS | 없음 | 없음 | 프리셋만 존재 |

릴리스 파일 자체는 `ci/test-package-windows.ps1`, `ci/test-package-linux.sh`로 따로 시험한다.

GitHub Actions는 비활성화되어 있고 모든 빌드는 개발 장비에서 수행한다.

## 3. Windows

### 3.1 빌드 환경

| 항목 | MSVC (검증 + 릴리스) | MinGW (검증 전용) |
|---|---|---|
| CMake 프리셋 | `windows-msvc-release` | `windows-mingw-release` |
| 컴파일러 | Visual Studio C++ 도구 (`cl`). 로컬 검증 버전 MSVC 19.51 (VS 2026) | Qt Tools `mingw1310_64` (MinGW 13.1) |
| Qt kit | `msvc2022_64` | `mingw_64` |
| Qt 기본 경로 | `C:/Programs/Qt/6.12.0/msvc2022_64` | `C:/Programs/Qt/6.12.0/mingw_64` |
| 경로 변경 변수 | `NPP_QT_MSVC_DIR`, `NPP_QT_TOOLS` | `NPP_QT_DIR`, `NPP_QT_TOOLS` |
| 빌드 폴더 | 검증 `build/ci/windows-msvc-QT/`, 릴리스 `build/package/windows-msvc/` | `build/ci/windows-mingw-QT/` |

MSVC와 MinGW는 ABI와 런타임이 달라 Qt kit을 섞어 쓸 수 없다.

### 3.2 릴리스 파일

스크립트: `ci/package-windows.ps1`. `windeployqt`로 Qt 런타임을, 스크립트에서 MSVC 런타임 DLL을 함께 넣는다.

| 파일 | 내용 |
|---|---|
| `notepadpp-qt-<ver>-windows-x64-portable.zip` | 실행 파일과 런타임. `doLocalConf.xml`로 설정을 폴더 안에 저장 |
| `notepadpp-qt-<ver>-windows-x64-symbols.zip` | 크래시 덤프 분석용 PDB |

Inno Setup 설치본(`-setup.exe`)은 0.2.0에서 배포했고, 현재는 스크립트에서 비활성화되어 있다.
코드 서명은 하지 않으므로 SmartScreen 경고가 나올 수 있다.

## 4. Linux

### 4.1 빌드 환경

| 항목 | 검증 | 릴리스 |
|---|---|---|
| 실행 위치 | Linux 호스트는 네이티브, 그 외는 Docker | Docker (`--local`은 확인용) |
| 이미지 | `ci/docker/linux.Dockerfile` | `ci/docker/appimage.Dockerfile` |
| OS | Ubuntu 26.04 | Ubuntu 22.04 (glibc 2.35) |
| 컴파일러 | 배포판 기본 GCC | g++ 12 |
| Qt | 배포판 패키지 6.10 | aqtinstall 공식 바이너리 6.12.0 |
| CMake 구성 | `linux-release` 프리셋 또는 Docker 내 동일 옵션 | `-DCMAKE_BUILD_TYPE=Release` 직접 지정 |
| 런타임 포함 | 포함 안 함 | `linuxdeploy` + Qt 플러그인으로 번들 |

릴리스를 Ubuntu 22.04에서 빌드하는 이유는 오래된 배포판까지 실행되도록 glibc 기준을 낮추기 위해서다.
Ubuntu 24.04 패키지의 Qt는 6.4로 최소 요구 버전보다 낮다.

### 4.2 릴리스 파일

스크립트: `ci/package-linux.sh`, `ci/package-linux-formats.sh`. 대상은 x86-64이고 glibc 2.35 이상이 필요하다.

| 파일 | 내용 |
|---|---|
| `notepadpp-qt-<ver>-linux-x86_64.AppImage` | Qt 번들 단일 실행 파일 |
| `notepadpp-qt-<ver>-linux-x86_64.tar.gz` | `AppDir` 압축본. 해제 후 `AppDir/AppRun` 실행 |

deb, rpm, Flatpak 생성 코드는 있으나 현재 비활성화되어 있다. 재개하면 deb와 rpm은 별도 컴파일 없이 같은 AppImage를 `/opt/notepadpp-qt/`에 설치하는 형태다.

## 5. macOS

macOS는 아직 빌드한 적이 없다. 아래 내용은 [phase-macos.md](../plan/phase-macos.md)와 개발 계획서의 계획이며, 실제 빌드에서 바뀔 수 있다.

### 5.1 계획된 빌드 환경

| 항목 | 내용 |
|---|---|
| 장비 | Mac (Apple Silicon 권장) |
| 컴파일러 | Xcode Command Line Tools의 Apple Clang |
| Qt | Qt 6.12 macOS kit (Universal) + Qt 5 Compatibility 모듈 |
| CMake 프리셋 | `macos-release` (`arm64;x86_64` Universal 2) |
| 번들 | `MACOSX_BUNDLE`, 데이터는 `Contents/Resources/data` |
| 코드페이지 변환 | iconv (`app/core/platform/posix/CodepagePosix.cpp`) |

### 5.2 계획된 릴리스 파일

| 파일 | 내용 |
|---|---|
| `.app` | `macdeployqt`로 Qt 런타임을 포함한 Universal 2 번들 |
| `.dmg` | `.app` 배포용 디스크 이미지 |

개인 사용 단계는 ad-hoc 서명(`codesign -s -`)을 쓰고, 외부 배포에는 Apple Developer 계정의 서명과 공증이 필요하다.

### 5.3 구현 현황

| 항목 | 상태 |
|---|---|
| `macos-release` 프리셋, 번들 속성, 데이터 경로 | 작성됨, 빌드 미확인 |
| 권한 저장 (`osascript`) | 작성됨, 빌드 미확인 |
| `ci/local-ci.sh` macOS 빌드 | 없음. macOS 호스트에서 실행하면 Linux Docker 빌드만 수행 |
| `app/ui/CMakeLists.txt`의 macOS `install()` 규칙 | 없음 |
| macOS 패키징 스크립트 | 없음 |
| `ci/release.sh` 연동 | 없음. 현재 Windows 호스트 전용 |
| 서명, 공증 | 없음 |

## 6. 공통 릴리스 흐름

`bash ci/release.sh [--publish]`를 Windows 호스트에서 실행한다. Docker Desktop이 켜져 있어야 한다.

1. 작업 트리와 버전, 태그 미사용 여부를 확인한다.
2. `ci/local-ci.sh`로 정적 검사와 Windows MinGW, MSVC, Linux 빌드를 검증한다.
3. Windows와 Linux 패키지를 `dist/`에 만든다.
4. 패키지 시험을 실행한다.
5. `SHA256SUMS.txt`와 릴리스 노트를 만든다.
6. `--publish`를 지정하면 태그를 push하고 GitHub pre-release를 만든다.
