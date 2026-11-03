# 0단계 세부 계획: 준비 / 스파이크

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장
- 기간: 2–3주 (2026-11-02 착수)
- 상태: **대부분 완료** — 남은 항목은 4장 참조
- 다음 단계: [1단계 코어 추출](phase-1-core-extraction.md)


> 개발 범위: 현재 대상 플랫폼은 **Windows·Linux**. macOS는 차후 개발 예정이므로 macOS 관련 작업·테스트는 완료 기준에서 제외하고 별도 문서 [macOS 지원 세부 계획](phase-macos.md)에서 관리한다.

---

## 1. 목표

1. Windows·Linux에서 같은 CMake로 빌드되는 골격을 만든다 (macOS는 [별도 문서](phase-macos.md)).
2. 기존 소스 중 그대로 재사용할 부품(Scintilla Qt 레이어, Lexilla, boostregex, pugixml)이 Qt 환경에서 동작함을 증명한다.
3. 확정된 결정(UTF-8 문자열, `DockManager` + ADS)을 코드로 검증한다.
4. 이후 단계가 매일 쓰게 될 개발 환경(로컬 CI, IDE, 문서)을 갖춘다.

## 2. 범위

| 포함 | 제외 |
|---|---|
| 루트 CMake, 프리셋, 서드파티 타깃 | 기존 `PowerEditor/src` 코드 이식 (1단계) |
| 샘플 Qt 앱 (탭 편집기, 패널 2개) | 설정 파일(`config.xml` 등) 읽기·쓰기 (1단계) |
| 도킹 추상화와 두 백엔드 | 인코딩 감지 (1단계) |
| 로컬 CI, CLion 구성 | 패키징 (6단계) |
| macOS 빌드 환경 | |

## 3. 작업 목록과 결과

| ID | 작업 | 산출물 | 상태 |
|---|---|---|---|
| P0-01 | 결정 사항 확정 (문자열, 도킹, 명칭, 소스 관리, CI) | 계획서 4.2절, 11.1절 | 완료 |
| P0-02 | git 저장소 구성: fork `qt-port` 브랜치, 기준 태그 `v8.9.8.1` | `origin`/`upstream` remote | 완료 |
| P0-03 | 루트 `CMakeLists.txt`, `CMakePresets.json` | Windows MinGW/MSVC, Linux, macOS 프리셋 | 완료 (macOS는 프리셋만) |
| P0-04 | 서드파티 정적 라이브러리 타깃 | `app/thirdparty/CMakeLists.txt` (`npp_scintilla`, `npp_lexilla`, `npp_pugixml`) | 완료 |
| P0-05 | ADS 서브모듈 | `app/thirdparty/qtads` @ `v5.1.1` | 완료 |
| P0-06 | `DockManager` 인터페이스 + ADS/QT 구현 | `app/ui/docking/` | 완료 |
| P0-07 | 샘플 앱: 탭 편집기, 열기/저장, 렉서·스타일, 패널 2개 | `app/ui/MainWindow.*`, `editor/LanguageConfig.*` | 완료 |
| P0-08 | 자동 테스트 (UTF-8, Boost regex, Lexilla, 모델 XML) | `app/tests/test_editor_smoke.cpp` | 완료 |
| P0-09 | Linux 이식 문제 수정 (vendored 소스 2건) | [third-party-patches.md](third-party-patches.md) | 완료 |
| P0-10 | 로컬 CI (Windows MinGW·MSVC, Linux Docker × ADS/QT) | `ci/local-ci.sh`, `ci/docker/linux.Dockerfile` | 완료 |
| P0-11 | 도킹 추상화 누수 검사 | `ci/check-docking-abstraction.sh` | 완료 |
| P0-12 | CLion 실행·디버그 구성 (MSVC + LLDB) | `CMakeUserPresets.json`(로컬), [build.md](build.md) | 완료 |
| P0-13 | macOS 빌드 환경 | — | 차후 구현 항목으로 이동 |
| P0-14 | **한글 IME 수동 확인** (Windows MS IME, Linux ibus/fcitx5) | 확인 결과 기록 | 미완료 |
| P0-15 | **도킹 수동 확인**: 패널 플로팅 드래그, 재도킹, 재시작 후 복원 | 확인 결과 기록 | 미완료 |
| P0-16 | Linux 실제 데스크톱 확인 (X11, Wayland) | 확인 결과 기록 | 미완료 |
| P0-17 | 기능 체크리스트 작성 (메뉴 기준) | `docs/feature-checklist.md` (`tools/gen-feature-checklist.py`로 생성) | 완료 |
| P0-18 | `Parameters.cpp`, `Buffer.cpp`, `Common.cpp` Win32 API 목록 추출 | 1단계 작업 입력 자료 | 1단계 첫 주로 이동 |

## 4. 남은 작업 상세

### 4.1 macOS 빌드 (P0-13, 2026-11-09 주)

1. Mac 장비 준비: Xcode Command Line Tools, Qt 6.12 macOS kit(Universal) + Qt 5 Compatibility 모듈, CMake, Ninja.
2. `cmake --preset macos-release` 로 빌드. 예상 수정 지점:
   - `app/ui/CMakeLists.txt`의 번들 데이터 복사 경로(`Contents/Resources/data`) 동작 확인.
   - ADS가 macOS에서 추가 링크 옵션을 요구하는지 확인.
   - Scintilla Qt 레이어의 macOS 입력기 처리.
3. `ci/local-ci.sh`에 macOS 대상 추가 (`HOST=macos`일 때 네이티브 빌드, `--screenshot` 스모크).
4. 확인 항목: 전역 메뉴바에 `About`/`Quit`가 앱 메뉴로 들어가는지(`QAction::MenuRole` 지정해 둠), Cmd 단축키, 한글 입력.

### 4.2 수동 확인 (P0-14 ~ P0-16)

자동화가 불가능한 항목이다. 아래 표를 채워 [qt-cross-platform-port-plan.md](qt-cross-platform-port-plan.md) 12.1절에 결과를 남긴다.

| 항목 | Windows | Linux X11 | Linux Wayland | macOS |
|---|---|---|---|---|
| 한글 입력 (조합 중 표시, 커서 위치, 확정) | | | | |
| 한자/일본어 입력 | | | | |
| 패널 드래그 → 플로팅 | | | | |
| 플로팅 패널 재도킹, 탭 묶기 | | | | |
| 재시작 후 도킹 배치 복원 | | | | |
| HiDPI (150%, 200%) 표시 | | | | |

문제 발견 시: ADS 문제면 `NPP_DOCKING_BACKEND=QT`로 같은 시나리오를 확인해 원인이 ADS인지 앱인지 구분한다.

### 4.3 기능 체크리스트 (P0-17)

- 입력: `PowerEditor/src/menuCmdID.h`의 메뉴 명령 524개 (File 27, Edit 106, Search 66, View 94, Format 14, Language 101, Settings 15, Tools 12, ? 12 등), `Notepad_plus.rc` 메뉴 구조, 다이얼로그·패널 목록.
- 형식: 명령 ID, 메뉴 경로, 담당 단계(2–5), 상태(미착수/진행/완료/제외), 비고.
- 용도: 2–5단계 범위 관리와 완료 기준("기능 체크리스트 90% 이상")의 기준표.

## 5. 완료 기준

- [x] Windows(MinGW, MSVC)와 Linux에서 빌드·테스트·스모크 실행이 로컬 CI로 통과
- [ ] 한글 IME와 도킹 수동 확인 표 작성 완료
- [x] 기능 체크리스트 작성 완료

## 6. 0단계에서 얻은 교훈 (다음 단계 반영)

| 발견 | 반영 |
|---|---|
| Qt MinGW kit에 Core5Compat 미포함 | 빌드 문서에 명시 |
| Ubuntu 24.04의 Qt는 6.4 | Linux 기준을 Ubuntu 26.04로 |
| 키워드 클래스 → Scintilla 키워드 세트 매핑이 언어별로 다름 (upstream `setXxxLexer` 85개 함수) | 1단계에서 데이터 테이블로 이식 |
| ADS `DockManager.h`와 프로젝트 헤더 이름 충돌 | ADS는 `<...>`, 프로젝트는 `"..."` include 규칙 |
| CLion 기본 MinGW(GCC 16)와 Qt kit MinGW(13.1)는 런타임 다름 | CLion은 MSVC + LLDB 사용 |
| `include(CTest)`가 IDE 실행 구성을 28개 추가 | `enable_testing()` 사용 |
