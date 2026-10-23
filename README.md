# CatPjt-Fablewood-Game

동화풍 협동 액션 RPG Fablewood의 Unreal Engine 공통 게임 프로젝트입니다. 플레이어 클라이언트와 전투를 판정하는 Unreal 서버가 같은 `Source`, `Config`, `Content`를 사용합니다. 계정·접속 권한·중앙 저장을 처리하는 .NET API와 PostgreSQL 구성은 형제 디렉터리 `CatPjt-Fablewood-Backend`에 있습니다.

외부 디렉터리 이름만 분리했습니다. 엔진 내부 프로젝트·모듈·Target·에셋 경로의 `Fablewood` 이름은 유지하며, 진입 파일은 `Fablewood.uproject`입니다.

```text
GameProjects/
├─ CatPjt-Fablewood-Game/       # Unreal 서버·클라이언트 공통 소스와 에셋
└─ CatPjt-Fablewood-Backend/    # .NET API, 통합 테스트, SQL, Docker 구성
```

현재 이 디렉터리는 소스 분리 작업본입니다. 원본 `D:\GameProjects\Fablewood`에서 수행한 빌드·플레이·저장 검증 기록은 아래 개발 이력에 보존했으며, **새 디렉터리에서는 아직 빌드·플레이를 재검증하지 않았습니다.**

## 처음 준비하고 실행하기

Windows, Unreal Engine 5.8.4, MSVC 14.50 C++ 도구, Windows SDK 10.0.26100.0이 필요합니다. 온라인 실행기와 검증 도구에는 PowerShell 7.4 이상(`pwsh`)을 사용합니다. .NET SDK와 Docker 준비는 형제 폴더의 `CatPjt-Fablewood-Backend/README.md`를 따릅니다.

아래 명령은 이 디렉터리에서 실행합니다. 소스 복사본에는 `Binaries`가 없으므로 **먼저 `Setup-Project.cmd`를 실행해야 합니다.** 이 단계는 `FablewoodEditor`를 빌드하고 포함된 맵을 확인합니다. 전체 아트를 재생성하는 작업은 아니며, 기존 `Content`를 입력으로 사용합니다.

```powershell
.\Setup-Project.cmd
.\Play.cmd -Editor
```

엔진을 자동으로 찾지 못하면 `Setup-Project.cmd -EngineRoot "C:\Program Files\Epic Games\UE_5.8"`처럼 지정합니다. 에디터 작업은 `Open-Editor.cmd`, 기존 로컬 listen-server 협동 두 창은 `Start-Coop.cmd`로 실행합니다.

Windows 패키지가 필요하면 `Package-Prototype.cmd`로 새 경로에서 빌드합니다. 이후 `Play.cmd`는 이 작업본의 완료된 패키징 보고서에서 실행 파일을 찾습니다. 소스 복사본에는 과거 패키지나 보고서가 없으므로 최초 실행에 패키지 모드를 바로 사용할 수는 없습니다.

## 백엔드와 연결하는 온라인 실행

먼저 형제 백엔드의 안내에 따라 NuGet 잠금 복원과 Release 빌드를 마칩니다. 백엔드 및 게임 실행기는 API 소스를 자동으로 빌드하지 않습니다. `Start-OnlineServer.cmd`는 연결된 백엔드 실행기를 통해 API·DB를 준비하거나 기존 소유 서비스를 재사용한 뒤, 화면 없는 Unreal 전용 서버를 시작합니다. 기본 서버 주소는 `127.0.0.1:7789`, API 주소는 `http://127.0.0.1:5281`입니다.

```powershell
.\Start-OnlineServer.cmd
.\Play-Online.cmd -Register -Username mage_player -InitialClass Mage
.\Play-Online.cmd -Register -Username archer_player -InitialClass Archer -WindowX 950
```

서버에는 로컬 플레이어가 없고, 서로 다른 계정의 게임 창 두 개가 접속합니다. 비밀번호는 숨긴 입력으로 받습니다. 기존 계정으로 다시 접속할 때는 `-Register`를 뺍니다. `-InitialClass`는 처음 계정을 만들 때만 적용됩니다. 중앙 저장은 새 계정으로 시작하며 기존 로컬 `.sav`를 자동 업로드하지 않습니다.

기본 백엔드 위치는 형제 디렉터리입니다. 다른 작업본은 `-BackendRoot`로 지정하거나 현재 PowerShell 프로세스의 `FABLE_BACKEND_ROOT` 환경 변수로 지정합니다. 연결 경로 해석은 `Tools/Backend-Link.ps1`에서 공유합니다. 원본의 기존 백엔드 환경을 명시적으로 사용하는 예는 다음과 같습니다.

```powershell
pwsh -NoProfile -File .\Tools\Start-OnlineServer.ps1 -BackendRoot D:\GameProjects\Fablewood
```

`Tools/Test-Dedicated.ps1`도 `-BackendRoot`를 받습니다. 백엔드 실행기·비공개 설정·프로세스 상태는 지정한 백엔드 루트의 것을 사용하며, 게임 저장이나 원본의 키 파일을 이 저장소로 복사하지 않습니다. 새 백엔드 작업본을 처음 실행하기 전에는 기존 서비스·DB와 구분하는 해당 README의 안내를 확인합니다.

클라이언트의 주소와 API는 `Play-Online.cmd -Address 127.0.0.1:7789 -BackendUrl http://127.0.0.1:5281`로 지정할 수 있습니다. 기본은 현재 Editor 모듈의 게임 창입니다. `-PackagedExecutable <내부 Fablewood.exe>` 또는 `-LatestPackaged`는 클라이언트에만 사용하며, 호환되는 현재 게임 소스의 패키지가 필요합니다. 온라인 클라이언트 실행기는 DX11과 창 비활성 시에도 진행하는 설정을 실행별로 적용합니다.

일회용 입장권은 180초 동안 유효합니다. 비밀번호와 세션 토큰은 게임 명령줄이나 상태 파일에 넣지 않습니다. 첫 실행 초기화가 오래 걸려 입장권이 만료되면 실행기를 다시 실행해 새 입장권을 받습니다. 서버·클라이언트 상태와 로그는 이 게임 작업본의 `Saved/OnlineSessions`에 기록합니다.

## 전용 서버 빌드의 현재 범위

현재 지원하는 서버 실행 경로는 **`UnrealEditor-Cmd.exe -server -NullRHI`**입니다. 원본에서 실제 `NM_DedicatedServer` 1개와 Windows 패키지 클라이언트 2개의 접속·저장을 검증했습니다. 일반 Launcher 엔진의 Game Target `Fablewood.exe`에 `-server`를 붙이는 방식은 전용 서버로 동작하지 않아 사용하지 않습니다.

`Source/FablewoodServer.Target.cs`와 `FablewoodClient.Target.cs`는 공통 게임 모듈을 사용하는 별도 Target 정의입니다. 최적화된 독립 `FablewoodServer.exe` 빌드는 Server Target을 지원하는 Unreal 소스 엔진이 필요하며 아직 빌드·검증하지 않았습니다. 원본 검증은 한 PC의 loopback 범위이며 인터넷 공개 서버나 물리적으로 분리된 PC의 결과가 아닙니다.

## 소스에 포함하는 것과 생성 결과

- 포함: `Source`, `Config`, `Tools`, 루트 실행기, 문서, `Content`의 `.uasset`·`.umap`, `ArtSource` 원본, 폰트 라이선스와 참고 원화. `Build/Windows`의 직접 작성한 아이콘·리소스도 소스입니다.
- 제외: `Binaries`, `Intermediate`, `DerivedDataCache`, `Saved`, `Builds`, `.vs`, Unreal이 만든 루트 솔루션, 미리보기·검증 캡처, 실제 비밀 설정과 DB 데이터.

에셋은 바이너리 형식이어도 게임을 편집하고 실행하는 필수 입력입니다. 특히 `ArtSource/Meshes/*.obj`는 메시 원본이므로 빌드 중간 파일과 혼동하여 제외하지 않습니다. 현재 프로젝트에는 원본 OBJ 68개와 대응 런타임 메시가 포함됩니다. `.gitignore`는 제외 규칙이며 기존 파일을 삭제하지 않습니다.

```powershell
pwsh -NoProfile -File .\Tools\Get-SourceManifest.ps1
```

게임 manifest는 이 루트의 파일·크기·SHA-256, 필수 맵·폰트·아트·전체 `Source` 포함 여부와 비밀 설정 패턴을 검사하여 `Saved/SourceManifest`에 기록합니다. 형제 백엔드의 소스나 `global.json`은 해당 백엔드 manifest가 관리합니다. 이 검사는 빌드·플레이 검증이나 Git 커밋이 아니며, 알려진 비밀 값이 없는 작업본의 검사만으로 모든 비밀의 부재를 보장하지 않습니다.

## 개발 이력과 과거 검증 자료

[DevelopmentHistory.md](Docs/DevelopmentHistory.md)는 분리 전 README를 바이트 그대로 옮긴 개발 이력입니다. 기능·조작법, 단계별 검증과 제한 사항을 보존했습니다. 문서 안의 `Saved/...`, `Builds/...`, `ArtDirection/Previews/...` 상대 경로는 **원래 `D:\GameProjects\Fablewood` 루트를 기준으로 작성된 과거 위치**이며, 현재 `Docs` 하위나 새 작업본에 자료가 있다는 뜻이 아닙니다. 기존 절대 경로도 역사 기록으로 유지했습니다.

분리 기준은 원본의 `Saved/SourceManifest/20261023-005200-source-only.json`입니다. 원본 폴더의 코드·저장·패키지·검증 자료는 별도로 보존합니다. 과거 마이그레이션 회귀에는 summary와 역할별 `.sav` fixture가 필요하므로 새 소스 작업본만으로 모든 과거 회귀를 실행할 수는 없습니다. 필요한 경우 별도 보존 자료를 명시적으로 지정하고 원본 SHA를 대조합니다.
