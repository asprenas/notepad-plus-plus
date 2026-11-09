# macOS 지원 세부 계획 (기타 문서)

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 6.4절
- 상태: **차후 개발** — 현재 개발 범위(Windows·Linux) 밖. 각 단계의 완료 기준에 포함하지 않는다.
- 착수 조건: Mac 장비(Apple Silicon 권장), Xcode Command Line Tools, Qt 6 macOS kit + Qt 5 Compatibility 모듈

---

## 1. 목표

Windows·Linux와 같은 코드베이스로 macOS 빌드·실행·배포를 지원한다. 코어(`npp-core`)와 UI는 이미 Qt/표준 C++ 기반이라 추가 작업은 플랫폼 통합(메뉴바, 단축키, 파일 열기 이벤트, 번들/서명)과 검증이 중심이다.

## 2. 이미 준비된 것

| 항목 | 위치 |
|---|---|
| `macos-release` CMake 프리셋 (Universal 2: arm64 + x86_64) | `CMakePresets.json` |
| 번들 데이터 경로 (`Contents/Resources/data`) | `app/ui/CMakeLists.txt`, `MainWindow::loadSettings` |
| 코드페이지 변환 (iconv) | `app/core/platform/posix/CodepagePosix.cpp` |
| 설정 경로 `~/Library/Application Support/Notepad++Qt` | `app/core/config/AppPaths.cpp` |
| 정규식 UTF-8 로케일 초기화 | `npp::core::initRegexLocale()` |
| About/Quit 메뉴 역할 지정 | `MainWindow::createMenus` (`QAction::MenuRole`) |

## 3. 작업 목록 (단계별)

### 0단계 준비 ([phase-0-preparation.md](phase-0-preparation.md))

| ID | 작업 |
|---|---|
| MAC-01 | macOS 빌드 환경 (Xcode CLT, Qt 6 Universal + Qt 5 Compatibility, `macos-release` 프리셋 검증) — 4.1절 절차 |
| MAC-02 | `ci/local-ci.sh`에 macOS 대상 추가 |
| MAC-03 | macOS 한글 IME, 도킹, HiDPI 수동 확인 (4.2절 표의 macOS 열) |

### 1단계 코어 추출 ([phase-1-core-extraction.md](phase-1-core-extraction.md))

| ID | 작업 |
|---|---|
| MAC-04 | macOS에서 코어 단위 테스트 실행 (`bash ci/local-ci.sh`; iconv는 macOS libiconv 이름 차이 확인) |
| MAC-05 | macOS 설정 경로(`~/Library/Application Support/Notepad++Qt`) 동작 확인 |

### 2단계 MVP ([phase-2-mvp.md](phase-2-mvp.md))

| ID | 작업 |
|---|---|
| MAC-06 | 3.7절 macOS 특이 사항 (전역 메뉴바, 앱 메뉴, `QFileOpenEvent`, 창 닫기 동작) |
| MAC-07 | 3.2절 macOS 단축키 충돌 목록과 대체 키 |
| MAC-08 | macOS에서 열기/편집/저장/세션 복원 확인 |

### 4단계 설정/테마 ([phase-4-settings-theme.md](phase-4-settings-theme.md))

| ID | 작업 |
|---|---|
| MAC-09 | macOS 시스템 외관(다크 모드) 추종 확인 |

### 6단계 패키징/안정화 ([phase-6-packaging-stabilization.md](phase-6-packaging-stabilization.md))

| ID | 작업 |
|---|---|
| MAC-10 | 3.3절 macOS 패키징 (`macdeployqt`, Universal 2, Info.plist, dmg, 서명·공증) |
| MAC-11 | macOS 크래시 리포트(시그널 핸들러) 확인 |
| MAC-12 | macOS 깨끗한 환경 설치 테스트 |

### 7단계 플러그인 API ([phase-7-plugin-api.md](phase-7-plugin-api.md))

| ID | 작업 |
|---|---|
| MAC-13 | macOS `.dylib` 플러그인 로드 (3.3절 경로) 및 샘플 플러그인 확인 |

## 4. macOS 특이 사항 (설계 메모)

- **메뉴바**: 전역 메뉴바. About/Preferences/Quit는 `QAction::MenuRole`로 앱 메뉴에 배치.
- **단축키**: Qt가 `Ctrl`을 `Cmd`로 매핑. 실제 Control이 필요한 조합은 `Qt::MetaModifier`. 시스템 예약(Cmd+H, Cmd+Q, Cmd+M)과 충돌하는 원본 단축키 목록 작성 후 대체 키 결정.
- **파일 열기**: Finder/Dock에서 여는 파일은 `QFileOpenEvent`로 전달 (명령줄 인자 아님).
- **앱 수명주기**: 마지막 창을 닫으면 종료(Notepad++ 동작)를 기본으로.
- **다크 모드**: 시스템 외관 추종(`QStyleHints::colorScheme`).
- **IME**: 한국어 2벌식 조합 표시·커서 위치 확인.
- **배포**: `macdeployqt`, `.dmg`, 개인 사용은 ad-hoc 서명(`codesign -s -`), 외부 배포는 Apple Developer 계정으로 서명·공증.
- **플러그인**: `.dylib`, 경로 `<번들>/Contents/PlugIns/`, `~/Library/Application Support/Notepad++Qt/plugins/`.

## 5. 완료 기준

- [ ] `bash ci/local-ci.sh`가 macOS(arm64, x86_64)에서 통과 (코어 테스트 + 스모크 스크린샷)
- [ ] 위 작업 목록(MAC-xx) 전부 완료
- [ ] `.dmg`로 설치 후 실행·파일 연결로 열기 확인

## 6. 리스크

| 리스크 | 대응 |
|---|---|
| ADS 플로팅 창 동작 차이 | `DockManager` QT 백엔드로 비교, 필요 시 macOS만 QT 백엔드 |
| libiconv 이름 차이 (CP949 등) | `CodepagePosix.cpp` 후보 이름 목록 보강, 인코딩 테스트로 확인 |
| 서명·공증 비용 | 개인 사용 단계에서는 ad-hoc 서명 |
