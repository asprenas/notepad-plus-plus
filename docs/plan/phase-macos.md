# macOS 지원 세부 계획

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 6.4절
- 상태: **착수 준비** — Phase 0~7(Windows·Linux)은 완료. macOS는 이 문서의 단계(M0~M5)로 진행한다.
- 테스트 장비: **M2 MacBook (Apple Silicon, arm64)** — 빌드·테스트·수동 확인·패키징을 모두 이 장비에서 수행한다.
- x86_64(Intel Mac): 실기기 없음. Universal 2 바이너리를 Rosetta 2로 실행해 기본 동작만 확인한다.

---

## 1. 목표

Windows·Linux와 같은 코드베이스로 macOS 빌드·실행·배포를 지원한다. 코어(`npp-core`)와 UI는 이미 Qt/표준 C++ 기반이고 macOS 분기 다수가 구현돼 있으므로, 남은 작업은 **M2 실기기 검증**과 빠진 플랫폼 통합(`QFileOpenEvent`, Info.plist, 번들/서명)이다.

## 2. 이미 구현된 것 (검증 대상)

| 항목 | 위치 | 확인 작업 |
|---|---|---|
| `macos-release` 프리셋 (`arm64;x86_64`) | `CMakePresets.json` | M1-01 |
| 설정 경로 `~/Library/Application Support/Notepad++Qt` | `app/core/config/AppPaths.cpp:34` | M1-03 |
| 코드페이지 변환 (iconv) | `app/core/platform/posix/CodepagePosix.cpp` | M1-02 |
| 정규식 UTF-8 로케일 초기화 | `npp::core::initRegexLocale()` | M1-02 |
| 번들 데이터 경로 (`Contents/Resources/data`) | `app/ui/CMakeLists.txt`, `MainWindow::loadSettings` | M4-01 |
| About/Preferences/Quit 메뉴 역할 | `app/ui/menu/ActionRegistry.cpp:223-229` | M2-01 |
| 시스템 외관(다크 모드) 추종 | `app/ui/Theme.cpp:62,192` (`colorSchemeChanged`) | M3-04 |
| 크래시 핸들러 (시그널) | `app/ui/CrashHandler.cpp:25,133` | M3-05 |
| 셸 연동 (Finder 표시 등) | `app/ui/PlatformShell.h` | M3-06 |
| 관리자 권한 저장 | `app/ui/ElevatedSave.cpp:153` | M3-07 |
| `.dylib` 플러그인 탐색·로드 | `app/ui/plugins/PluginHost.cpp:54,266,287` | M5-01 |
| `ci/local-ci.sh` Darwin 호스트 감지 | `ci/local-ci.sh:33` | M1-02 |

## 3. 작업 목록

### M0. M2 개발 환경

| ID | 작업 | 완료 조건 |
|---|---|---|
| M0-01 | Xcode Command Line Tools, Homebrew, CMake, Ninja 설치 | `cmake --version`, `ninja --version` |
| M0-02 | Qt 6 macOS kit 설치 (Qt Online Installer, **Universal** kit + Qt 5 Compatibility 모듈). Windows와 같은 Qt 6.12로 고정 | `qmake -query QT_VERSION`이 6.12.x, `lipo -info QtCore`가 `x86_64 arm64` |
| M0-03 | 저장소 클론, `CMAKE_PREFIX_PATH` 설정 방법을 `docs/build/`에 기록 | 문서에 절차 추가 |

Homebrew Qt는 arm64 전용이라 Universal 빌드에 쓸 수 없다. arm64 단독 빌드(M1-01b)에는 사용 가능.

### M1. 빌드와 코어 테스트

| ID | 작업 | 완료 조건 |
|---|---|---|
| M1-01 | `macos-release` 프리셋으로 Universal 2 빌드. `CMAKE_OSX_DEPLOYMENT_TARGET` 추가 (기본 12.0, Qt 6.12 최소 지원 버전에 맞춤) | `lipo -info notepadpp-qt`가 `x86_64 arm64` |
| M1-01b | 빠른 반복용 `macos-arm64-debug` 프리셋 추가 (`arm64` 단독, Debug) | 일상 개발은 이 프리셋 사용 |
| M1-02 | `bash ci/local-ci.sh`의 macOS 경로 완성: 빌드 → 코어 단위 테스트 → 스모크 스크린샷. iconv 인코딩 테스트(CP949, Shift_JIS, CP1252) 통과. 실패 시 `CodepagePosix.cpp` 후보 이름 보강 | M2에서 통과 |
| M1-03 | 설정 경로 생성·읽기·쓰기 확인. 테스트 실행은 항상 `-settingsDir=<기존 디렉터리>` 사용 | 실제 사용자 설정 미변경 |

### M2. 기본 편집 동작 (MVP)

| ID | 작업 | 완료 조건 |
|---|---|---|
| M2-01 | 전역 메뉴바·앱 메뉴: About/Preferences(Cmd+,)/Quit(Cmd+Q)가 앱 메뉴에 위치 | 수동 확인 |
| M2-02 | 단축키 충돌 목록 작성: 원본 단축키 중 시스템 예약(Cmd+H, Cmd+M, Cmd+Q, Cmd+Space, Cmd+Tab, Ctrl+화살표 Mission Control)과 겹치는 것 → 대체 키 결정. 실제 Control이 필요한 조합은 `Qt::MetaModifier` | 표 작성, 기본 단축키 반영 |
| M2-03 | 열기/편집/저장/다른 이름 저장/세션 복원 | 수동 + UI 테스트 통과 |
| M2-04 | 앱 수명주기: 마지막 창 닫으면 종료(Notepad++ 동작). Cmd+W는 탭 닫기 | 수동 확인 |

### M3. 플랫폼 통합

| ID | 작업 | 완료 조건 |
|---|---|---|
| M3-01 | **`QFileOpenEvent` 구현** (현재 미구현). Finder 더블클릭·Dock 드롭·`open -a`로 전달된 파일 열기. 실행 중이면 기존 인스턴스에서 열기 (단일 인스턴스 IPC와 연결) | 앱 실행 중·미실행 양쪽에서 동작 |
| M3-02 | 한국어 2벌식 IME: 조합 중 글자 표시, 후보창·커서 위치, 조합 중 Enter/방향키 | 수동 확인 |
| M3-03 | HiDPI(Retina) 렌더링·외부 모니터 이동 시 배율 변경, ADS 도킹·플로팅 창·드래그 | 수동 확인 |
| M3-04 | 시스템 외관 전환 시 테마 즉시 반영 | 수동 확인 |
| M3-05 | 크래시 리포트 생성 (강제 크래시 옵션으로) | 리포트 파일 생성 |
| M3-06 | "Finder에서 보기"·기본 앱으로 열기 | 수동 확인 |
| M3-07 | 보호된 경로 저장 방식 결정 (`osascript` 관리자 권한 요청 등) 및 확인 | 동작 또는 명확한 오류 메시지 |

### M4. 패키징·서명

| ID | 작업 | 완료 조건 |
|---|---|---|
| M4-01 | `.app` 번들 + `Info.plist` 작성: 번들 ID, 버전, `CFBundleDocumentTypes`(파일 연결), `NSHighResolutionCapable`, 최소 OS | `plutil -lint` 통과 |
| M4-02 | `macdeployqt`로 Qt 프레임워크 포함, `.dmg` 생성. `ci/release.sh`에 macOS 산출물 추가 | dmg 생성 |
| M4-03 | ad-hoc 서명(`codesign -s - --deep`). Apple Silicon은 서명 없는 arm64 바이너리를 실행하지 않으므로 필수 | `codesign -v` 통과 |
| M4-04 | 깨끗한 환경 설치 테스트: M2에 새 macOS 사용자 계정을 만들어 dmg 설치·실행·파일 연결 확인 | 수동 확인 |
| M4-05 | Rosetta 확인: `arch -x86_64 ./notepadpp-qt`로 실행, 열기/저장/플러그인 로드 | 기본 동작 확인 |

ad-hoc 서명 dmg를 다른 Mac에 내려받으면 quarantine 때문에 Gatekeeper가 차단한다. 배포 안내에 `xattr -dr com.apple.quarantine /Applications/Notepad++Qt.app` 또는 "우클릭 → 열기"를 적는다.

### M5. 플러그인

| ID | 작업 | 완료 조건 |
|---|---|---|
| M5-01 | 샘플 플러그인을 Universal `.dylib`로 빌드, `<번들>/Contents/PlugIns/`와 `~/Library/Application Support/Notepad++Qt/plugins/` 양쪽에서 로드 | `ui.plugins` 테스트 통과 |
| M5-02 | 서명 방식과 플러그인 로드 호환성 확인 (6절 리스크 참고) | ad-hoc 서명 앱이 별도 빌드 플러그인 로드 |

## 4. macOS 설계 메모

- **단축키**: Qt가 `Ctrl`을 `Cmd`로 매핑. 표시도 ⌘ 기호로 바뀌므로 문서·단축키 대화상자 표기 확인.
- **파일 열기**: Finder/Dock에서 여는 파일은 명령줄 인자가 아니라 `QFileOpenEvent`로 온다. `QApplication` 이벤트 필터에서 처리.
- **플러그인 경로**: `.dylib`, `<번들>/Contents/PlugIns/`, `~/Library/Application Support/Notepad++Qt/plugins/`.
- **외부 배포(향후)**: Apple Developer 계정 서명 + 공증. 공증은 hardened runtime이 필수이며, 이때 서드파티 플러그인 로드를 위해 `com.apple.security.cs.disable-library-validation` entitlement가 필요하다.

## 5. 완료 기준

- [ ] M2에서 `bash ci/local-ci.sh` 통과 (빌드 + 코어 테스트 + UI 테스트 + 스모크 스크린샷)
- [ ] M0~M5 작업 전부 완료
- [ ] 새 사용자 계정에서 `.dmg` 설치 후 실행·Finder 파일 연결로 열기 확인
- [ ] Rosetta(x86_64)로 실행·열기·저장 확인

## 6. 리스크

| 리스크 | 대응 |
|---|---|
| Intel 실기기 없음 | Rosetta로 기본 동작만 확인. Intel 전용 문제는 사용자 보고로 대응 |
| ADS 플로팅 창 동작 차이 | `DockManager` QT 백엔드로 비교, 필요 시 macOS만 QT 백엔드 |
| libiconv 이름 차이 (CP949 등) | `CodepagePosix.cpp` 후보 이름 보강, 인코딩 테스트로 확인 |
| 서명된 앱이 서명 다른 플러그인을 거부 | ad-hoc 단계는 M5-02로 확인. 공증 단계에서 `disable-library-validation` entitlement 추가 |
| Universal Qt kit 미설치·Homebrew Qt 혼용 | M0-02에서 `lipo -info`로 확인, `CMAKE_PREFIX_PATH`를 Universal kit로 고정 |
| 서명·공증 비용 | 개인 사용 단계는 ad-hoc 서명 |
