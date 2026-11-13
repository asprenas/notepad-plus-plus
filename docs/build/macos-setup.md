# macOS 개발 환경 설치 체크리스트

대상 장비: M2 MacBook (Apple Silicon). 작업 목록은 [phase-macos.md](../plan/phase-macos.md) M0 단계, OS별 환경 요약은 [build-environments.md](build-environments.md) 5절을 본다.

## 필수

- [ ] **Xcode Command Line Tools** — Apple Clang, `git`, `lipo`, `codesign`, `plutil` 포함
  ```sh
  xcode-select --install
  ```
- [ ] **Homebrew** — CMake·Ninja 설치용 ([brew.sh](https://brew.sh) 설치 명령 사용)
- [ ] **CMake 3.24 이상** (`CMakeLists.txt`의 `cmake_minimum_required`)
  ```sh
  brew install cmake
  ```
- [ ] **Ninja**
  ```sh
  brew install ninja
  ```
- [ ] **Qt Online Installer**로 Qt 6.12 설치 ([qt.io/download-qt-installer](https://www.qt.io/download-qt-installer), Qt 계정 필요). 선택 항목:
  - [ ] Qt 6.12.x → **macOS** (Universal kit)
  - [ ] Qt 6.12.x → **Qt 5 Compatibility Module** (`Core5Compat`, Scintilla가 사용)
  - 나머지 모듈(Core, Gui, Widgets, Network, PrintSupport)은 기본 kit에 포함

  Homebrew `qt`는 arm64 전용이라 Universal 빌드(`macos-release`)에 쓸 수 없다. 섞어 쓰지 않도록 `CMAKE_PREFIX_PATH`를 Qt Installer 경로로 고정한다.

- [ ] **Rosetta 2** — x86_64 바이너리 실행 확인용 (M4-05)
  ```sh
  softwareupdate --install-rosetta --agree-to-license
  ```

## 선택

- [ ] **GitHub CLI** (`brew install gh`) — PR·릴리스 작업 (`ci/release.sh`)
- [ ] **IDE**: CLion, VS Code(+CMake Tools), Qt Creator 중 하나 (Qt Creator는 Qt Installer에서 함께 설치 가능)
- [ ] **한국어 입력 소스** — 시스템 설정 → 키보드 → 입력 소스에 "2벌식" 추가 (IME 확인, M3-02)

`macdeployqt`는 Qt kit의 `bin/`에 들어 있어 따로 설치하지 않는다. `.dmg`는 macOS 기본 `hdiutil`로 만든다.

## 설치 확인

```sh
clang --version                 # Apple clang
cmake --version                 # 3.24 이상
ninja --version
export QTDIR="$HOME/Qt/6.12.0/macos"          # 설치 경로에 맞게 수정
"$QTDIR/bin/qmake" -query QT_VERSION           # 6.12.x
lipo -info "$QTDIR/lib/QtCore.framework/QtCore" # x86_64 arm64
ls "$QTDIR/lib" | grep Core5Compat             # QtCore5Compat.framework
arch -x86_64 /usr/bin/true && echo rosetta-ok
```

모든 줄이 기대값을 출력하면 M0 완료. 빌드는 `cmake --preset macos-release -DCMAKE_PREFIX_PATH="$QTDIR"`로 시작한다.
