## 최신 안내 · 2026-10-24 / 最新案内 · 2026-10-24

**최신 Foundation 사용·계약 설명은 [아래 Foundation 개발 기록](#foundation-development-record-20261023)에 있다.** 그 앞의 기존 한일 공개 본문과 아래 Foundation 원문의 상태 문장은 각 단계의 이력으로 보존했다. 23일 원문의 ‘패키지 플레이 미검증’은 당시 상태이며, 아래 1단계 결과가 그 한 항목을 갱신한다. 기존 단계의 사실이나 실패 기록은 삭제하지 않았다.

**最新の Foundation の使い方・契約は[下の Foundation 開発記録](#foundation-development-record-20261023)にあります。** その前の既存韓日公開本文と Foundation 原文の状態記述は、各段階の履歴として保存しています。23 日の「package play 未検証」は当時の状態で、以下の第 1 段階結果がその項目を更新します。旧段階の事実・失敗記録は削除していません。

- 기능·소스 변경일은 **2026-10-23 KST**, 이 일본어 번역·공개 문서 병합과 최종 안내 작성일은 **2026-10-24 KST**다. 1단계 실제 검증은 23일 23:56경 시작해 **24일 00:00:15 KST**에 끝났고, 최종 정적 감사·화면 검토는 24일에 수행됐다. 날짜를 파일 수정 시각으로 추정하지 않았다.
- 실제 작업 **`20261023T145628442Z-97b1128f` · Exit 0**에서 Windows 패키지 클라이언트 두 개와 **Editor 전용 서버**의 가입·로그인, 같은 DB를 유지한 API 재시작 뒤 캐릭터 지갑·전체 아이템·옵션·장착·요약의 영속 복원을 확인했다. 초기·복원 실행의 실제 패키지 클라이언트 프로세스는 합계 4개이며, 동시 클라이언트 4명 검증이라는 뜻이 아니다.
- 이 작업은 한 Windows PC의 loopback 검증이다. **2~4단계의 구현·검증, 전체 던전·장비 성장·던전 중단 복구, Linux, 서로 다른 물리 PC 두 대, 외부망, 독립 Server Target 실행 파일은 완료로 표시하지 않는다.** 서버는 `UnrealEditor-Cmd.exe -server -NullRHI`였으며 `FablewoodServer.exe`를 빌드·시험한 결과가 아니다.
- Foundation의 HTTP/PostgreSQL **556개**, 실제 DB 재시작 지속성 **42개**, native **22개**, Editor 빌드·Windows Game 패키징은 각각 앞선 별도 작업의 결과다. 이번 패키지 클라이언트 1단계에서 전부 재실행했다고 하거나 통과 수를 합산하지 않는다.

- 機能・source の変更日は **2026-10-23 KST**、今回の日本語翻訳・公開文書の統合・最新案内の作成日は **2026-10-24 KST** です。第 1 段階の実検証は 23 日 23:56 頃に始まり、**24 日 00:00:15 KST** に終了しました。最終静的監査・画面レビューは 24 日です。file 更新時刻から日付を推測していません。
- 実 job **`20261023T145628442Z-97b1128f`・Exit 0** で Windows package client 2 個と **Editor 専用 server** による登録・login、同じ DB を保つ API 再起動後の character wallet・全 item・affix・equipment・summary の永続復元を確認しました。初回/復元実行の実 package client process は合計 4 個であり、4 人同時 client 検証ではありません。
- 1 台の Windows PC の loopback 検証です。**第 2～4 段階の実装・検証、全 dungeon・装備成長・dungeon 中断復旧、Linux、別の物理 PC 2 台、外部 network、独立 Server Target 実行ファイルを完了扱いしません。** server は `UnrealEditor-Cmd.exe -server -NullRHI` であり、`FablewoodServer.exe` を build・test した結果ではありません。
- Foundation の HTTP/PostgreSQL **556 件**、実 DB 再起動永続性 **42 件**、native **22 件**、Editor build・Windows Game packaging は、それぞれ先行する別 job の結果です。今回の package client 第 1 段階ですべて再実行したとは記録せず、件数も合算しません。

소스 대조의 한계도 보존한다. 원시 보고서의 `reusedBuild.compiledAndCookedInputsMatched=0`은 역슬래시/경로 선택 버그이며 성공 근거로 사용하지 않았다. 별도 정규화 보완 대조는 고정 입력 324개 중 320개 동일·승인 차이 4개와 컴파일/cook 입력 260개 일치를 확인했다. 실제 runtime은 재사용 Editor 바이너리 3개와 패키지 사본 2개를 별도로 확인했다. 보완 정적 대조는 옛 데스크톱 소스 파일을 다시 읽거나 게임 플레이를 재실행한 검사가 아니다. 원시 보고서를 덮어쓰지 않았다.

source 照合の限界も保存します。原 report の `reusedBuild.compiledAndCookedInputsMatched=0` は backslash/path selector の bug で、成功根拠には使っていません。別の正規化補足照合で固定入力 324 件中 320 件一致・承認差分 4 件、compiled/cooked 入力 260 件の一致を確認しました。実 runtime は再利用 Editor binary 3 個と package copy 2 個を別途確認しました。補足静的照合は旧 desktop source file の再読や gameplay 再実行ではなく、原 report は上書きしていません。

근거는 비공개 로컬 검증 기록 `stage1-git-handoff.json`, `packaged-two-client-validation.json`, `stage1-source-provenance.json`, `stage1-visual-review.json`, `reused-source-supplement-static.json`이다. 이 문서 반영은 새 게임 실행·DB 변경·서비스 배포를 수행하거나 증빙·로그·캡처를 공개 저장소에 추가한 작업이 아니다.

根拠は非公開のローカル検証記録 `stage1-git-handoff.json`、`packaged-two-client-validation.json`、`stage1-source-provenance.json`、`stage1-visual-review.json`、`reused-source-supplement-static.json` です。この文書反映で新たな game 実行・DB 変更・service deploy は行わず、根拠・log・capture を公開 repository に追加していません。

---

# CatPjt-Fablewood-Game

동화풍 협동 액션 RPG Fablewood의 Unreal Engine 공통 게임 프로젝트입니다. 플레이어 클라이언트와 전투를 판정하는 Unreal 서버가 같은 `Source`, `Config`, `Content`를 사용합니다. 계정·접속 권한·중앙 저장을 처리하는 .NET API와 PostgreSQL 구성은 형제 디렉터리 `CatPjt-Fablewood-Backend`에 있습니다.

おとぎ話風の協力アクション RPG「Fablewood」の Unreal Engine 共通ゲームプロジェクトです。プレイヤークライアントと戦闘を判定する Unreal サーバーは、同じ `Source`、`Config`、`Content` を使用します。アカウント・接続権限・中央保存を扱う .NET API と PostgreSQL の構成は、兄弟ディレクトリ `CatPjt-Fablewood-Backend` にあります。

이 개발 사본은 장비·인벤토리 중앙 저장 확장용입니다. 엔진 내부 프로젝트·모듈·Target·에셋 경로의 `Fablewood` 이름은 유지하며, 진입 파일은 `Fablewood.uproject`입니다. 원본과 Git 등록 중인 분리 저장소를 보존한 별도 경로 `D:\GameProjects\Fablewood-Inventory-20261023` 아래에서 작업합니다.

この開発用コピーは、装備・インベントリの中央保存拡張用です。エンジン内部のプロジェクト・モジュール・Target・アセットパスの `Fablewood` という名前を維持し、起点ファイルは `Fablewood.uproject` です。原本と Git 登録中の分離リポジトリを保全し、別のパス `D:\GameProjects\Fablewood-Inventory-20261023` 以下で作業します。

```text
GameProjects/Fablewood-Inventory-20261023/
├─ CatPjt-Fablewood-Game/       # Unreal 서버·클라이언트 공통 소스와 에셋
└─ CatPjt-Fablewood-Backend/    # .NET API, 통합 테스트, SQL, Docker 구성
```

上の構成では、`CatPjt-Fablewood-Game/` が Unreal サーバー・クライアント共通のソースとアセット、`CatPjt-Fablewood-Backend/` が .NET API、統合テスト、SQL、Docker 構成です。

현재 개발 사본에서 Unreal Editor 빌드와 native 검사 18개가 통과했습니다. .NET API·통합 검사 프로젝트의 잠금 복원 빌드도 경고 0개·오류 0개로 성공했고, HTTP/PostgreSQL 통합 검사 316개(기존 65 + 장비 확장 251), 별도 PostgreSQL 재시작 후 지속성 검사 23개가 통과했습니다.

現在の開発用コピーで Unreal Editor のビルドと native 検査 18 件が通過しました。.NET API・統合検査プロジェクトのロックファイルに従う復元とビルドも警告 0 件・エラー 0 件で成功し、HTTP/PostgreSQL 統合検査 316 件（既存 65 + 装備拡張 251）、別途 PostgreSQL を再起動した後の永続性検査 23 件が通過しました。

새 장비 시나리오는 서버 1개와 원격 클라이언트 2개로 fresh·reconnect·resume을 통과했습니다. 판매·실제 마모 수리·강화·창고 입출고와 재화/아이템의 DB 일치를 확인했고, 두 캐릭터 모두 최종 14골드·4광석, 가방 4개·창고 1개(+1 무기)를 유지했습니다. 별도 계정에는 원본의 실제 중앙 저장 v8 두 개를 바이트 해시 그대로 복원한 뒤 v0→v1 백필과 v9 이전·재접속·API/서버 재시작 복원을 확인했습니다. 최초 수리 단일 요청 미적용 기록은 보존했고, 접근·정지·상태 관측을 보완한 재실행에서 두 명의 6개 동작은 각각 1회 요청으로 완료됐습니다. 이전 원본 검증과 소스 분리 이력은 [개발 이력](Docs/DevelopmentHistory.md)에 보존합니다.

新しい装備シナリオは、サーバー 1 つとリモートクライアント 2 つで fresh・reconnect・resume を通過しました。売却・実際に摩耗した装備の修理・強化・倉庫への預け入れと取り出し、および通貨・素材とアイテムの DB 一致を確認し、両キャラクターとも最終的に 14 ゴールド・4 鉱石、バッグ 4 個・倉庫 1 個（+1 武器）を維持しました。別アカウントには原本の実際の中央保存 v8 を 2 件、バイトハッシュを保持して復元し、v0→v1 のバックフィルと v9 移行・再接続・API/サーバー再起動後の復元を確認しました。最初の修理の単一リクエストが適用されなかった記録は保全し、接近・停止・状態観測を補強した再実行では、2 人の 6 動作がそれぞれ 1 回のリクエストで完了しました。以前の原本検証とソース分離の履歴は[開発履歴](Docs/DevelopmentHistory.md)に保全します。

## 장비·인벤토리 중앙 저장 / 装備・インベントリの中央保存

UE SaveGame은 v9, DB 스키마는 2, 정형 장비 snapshot 버전은 1입니다. 아이템 UUID와 기존 로컬 ID를 함께 유지하고 소유자, 가방·창고 위치와 순서, 장착 UUID, 옵션·품질·등급·강화·내구도를 중앙 테이블에 저장합니다. 재화 7종, SaveGame blob, 정형 장비, 거래 receipt와 변경 이력은 같은 DB 트랜잭션에서 확정됩니다. 삭제된 아이템의 UUID·로컬 ID 이력은 남겨 타 계정 이동이나 재생성을 거절합니다.

UE SaveGame は v9、DB スキーマは 2、構造化された装備 snapshot のバージョンは 1 です。アイテム UUID と既存のローカル ID を併存させ、所有者、バッグ・倉庫内の位置と順序、装着 UUID、オプション・品質・レアリティ・強化・耐久度を中央テーブルに保存します。7 種類の通貨・素材・消耗品、SaveGame blob、構造化装備、取引 receipt と変更履歴は同じ DB トランザクションで確定します。削除済みアイテムの UUID・ローカル ID の履歴を残し、別アカウントへの移動や再生成を拒否します。

전투·이동·가격·보상·아이템 생성 판정은 계속 Unreal 전용 서버가 담당합니다. .NET API는 소유권, 고유 ID, 형식·범위, 접속 리스·리비전과 원자 저장을 확인하며 게임 가격을 재계산하지 않습니다. API가 Unreal blob을 해독하는 구조도 아닙니다.

戦闘・移動・価格・報酬・アイテム生成の判定は、引き続き Unreal 専用サーバーが担当します。.NET API は所有権、一意 ID、形式・範囲、接続リース・リビジョンと原子的な保存を確認し、ゲーム内価格を再計算しません。API が Unreal blob を解読する構成でもありません。

기존 중앙 저장 v8 이하는 UE가 먼저 검증한 뒤 기존 값·장비 성능·옵션·강화·내구도·로컬 ID를 유지하며 누락 UUID를 부여합니다. 첫 정형 백필 ACK 전에는 플레이를 차단합니다. 이후 claim에서는 원본 v9 blob과 정형 장비를 모든 필드·순서·장착 참조까지 대조하고 불일치하면 덮어쓰지 않습니다. 기존 로컬 `.sav`의 자동 업로드는 하지 않습니다.

既存の中央保存 v8 以下は UE が先に検証し、既存の値・装備性能・オプション・強化・耐久度・ローカル ID を維持したまま、欠けている UUID を付与します。最初の構造化バックフィル ACK まではプレイを止めます。以降の claim では原本の v9 blob と構造化装備を全フィールド・順序・装着参照まで照合し、不一致の場合は上書きしません。既存のローカル `.sav` を自動アップロードすることはありません。

## 처음 준비하고 실행하기 / 初期準備と起動

Windows, Unreal Engine 5.8.4, MSVC 14.50 C++ 도구, Windows SDK 10.0.26100.0이 필요합니다. 온라인 실행기와 검증 도구에는 PowerShell 7.4 이상(`pwsh`)을 사용합니다. .NET SDK와 Docker 준비는 형제 폴더의 `CatPjt-Fablewood-Backend/README.md`를 따릅니다.

Windows、Unreal Engine 5.8.4、MSVC 14.50 C++ ツール、Windows SDK 10.0.26100.0 が必要です。オンラインランチャーと検証ツールには PowerShell 7.4 以上（`pwsh`）を使用します。.NET SDK と Docker の準備は、兄弟フォルダーの `CatPjt-Fablewood-Backend/README.md` に従ってください。

아래 명령은 이 디렉터리에서 실행합니다. 소스만 새로 받은 환경에는 생성된 `Binaries`가 포함되지 않으므로 **먼저 `Setup-Project.cmd`를 실행합니다.** 현재 개발 사본의 Editor 빌드는 이미 성공했으며, 다른 환경의 실행에는 그 환경에서 만든 호환 바이너리가 필요합니다. 이 단계는 `FablewoodEditor`를 빌드하고 포함된 맵을 확인합니다. 전체 아트를 재생성하는 작업은 아니며, 기존 `Content`를 입력으로 사용합니다.

以下のコマンドはこのディレクトリで実行します。ソースだけを新しく取得した環境には生成済みの `Binaries` が含まれないため、**先に `Setup-Project.cmd` を実行します。** 現在の開発用コピーでは Editor ビルドがすでに成功していますが、別環境での起動には、その環境で作成した互換バイナリが必要です。この手順は `FablewoodEditor` をビルドし、同梱マップを確認します。アート全体の再生成ではなく、既存の `Content` を入力として使用します。

```powershell
.\Setup-Project.cmd
.\Play.cmd -Editor
```

엔진을 자동으로 찾지 못하면 `Setup-Project.cmd -EngineRoot "C:\Program Files\Epic Games\UE_5.8"`처럼 지정합니다. 에디터 작업은 `Open-Editor.cmd`, 기존 로컬 listen-server 협동 두 창은 `Start-Coop.cmd`로 실행합니다.

エンジンが自動検出されない場合は、`Setup-Project.cmd -EngineRoot "C:\Program Files\Epic Games\UE_5.8"` のように指定します。エディター作業には `Open-Editor.cmd`、従来のローカル listen-server による協力プレイの 2 ウィンドウには `Start-Coop.cmd` を使用します。

Windows 패키지가 필요하면 `Package-Prototype.cmd`로 새 경로에서 빌드합니다. 이후 `Play.cmd`는 이 작업본의 완료된 패키징 보고서에서 실행 파일을 찾습니다. 소스 배포에는 과거 패키지나 보고서를 포함하지 않습니다. 해당 작업본에서 성공한 패키징 보고서와 호환 실행 파일이 있어야 패키지 모드를 사용할 수 있습니다.

Windows パッケージが必要な場合は `Package-Prototype.cmd` で新しい出力先にビルドします。その後、`Play.cmd` はこの作業コピーの完了済みパッケージング報告書から実行ファイルを探します。ソース配布には過去のパッケージや報告書を含めません。パッケージモードには、その作業コピーで成功したパッケージング報告書と互換実行ファイルが必要です。

## 백엔드와 연결하는 온라인 실행 / バックエンドに接続するオンライン起動

먼저 형제 [백엔드 README](https://github.com/krumichan/CatPjt-Fablewood-Backend/blob/main/README.md)의 잠금 복원·Release 빌드와 **별도 인스턴스 실행 예시**를 따릅니다. 현재 개발 검증은 API `127.0.0.1:5291`, PostgreSQL `127.0.0.1:55439`, schema `fable_inventory_20261023`을 사용하며 원본의 `5281/55438` 서비스는 그대로 유지합니다. 이 v9 게임은 장비 저장 API가 있는 스키마 2 서비스에 연결합니다.

まず兄弟リポジトリの[バックエンド README](https://github.com/krumichan/CatPjt-Fablewood-Backend/blob/main/README.md)に従い、ロックファイルによる復元・Release ビルドと**別インスタンスの起動例**を確認します。現在の開発検証は API `127.0.0.1:5291`、PostgreSQL `127.0.0.1:55439`、schema `fable_inventory_20261023` を使用し、原本の `5281/55438` サービスは維持します。この v9 ゲームは、装備保存 API を備えたスキーマ 2 のサービスに接続します。

백엔드 및 게임 실행기는 API 소스를 자동으로 빌드하지 않습니다. `Start-OnlineServer.cmd`는 지정한 백엔드 루트의 저장된 인스턴스 설정(포트·schema·컨테이너·볼륨)을 재사용하고 화면 없는 Unreal 전용 서버를 시작합니다. UE 포트 기본값은 7789이며 아래 예시는 7802를 명시합니다.

バックエンドとゲームのランチャーは API ソースを自動ビルドしません。`Start-OnlineServer.cmd` は、指定したバックエンドルートに保存されたインスタンス設定（ポート・schema・コンテナー・ボリューム）を再利用し、画面のない Unreal 専用サーバーを起動します。UE ポートの既定値は 7789 で、以下の例では 7802 を明示しています。

```powershell
.\Start-OnlineServer.cmd -BackendRoot ..\CatPjt-Fablewood-Backend -Port 7802
.\Play-Online.cmd -Register -Username mage_player -InitialClass Mage -Address 127.0.0.1:7802 -BackendUrl http://127.0.0.1:5291
.\Play-Online.cmd -Register -Username archer_player -InitialClass Archer -WindowX 950 -Address 127.0.0.1:7802 -BackendUrl http://127.0.0.1:5291
```

서버에는 로컬 플레이어가 없고, 서로 다른 계정의 게임 창 두 개가 접속합니다. 비밀번호는 숨긴 입력으로 받습니다. 기존 계정으로 다시 접속할 때는 `-Register`를 뺍니다. `-InitialClass`는 처음 계정을 만들 때만 적용됩니다. 중앙 저장은 새 계정으로 시작하며 기존 로컬 `.sav`를 자동 업로드하지 않습니다.

サーバーにはローカルプレイヤーが存在せず、別々のアカウントのゲームウィンドウ 2 つが接続します。パスワードは非表示入力で受け取ります。既存アカウントで再接続する場合は `-Register` を外します。`-InitialClass` はアカウント初回作成時にのみ適用されます。中央保存は新規アカウントから開始し、既存のローカル `.sav` を自動アップロードしません。

기본 백엔드 위치는 형제 디렉터리입니다. 다른 호환 작업본은 `-BackendRoot` 또는 현재 PowerShell 프로세스의 `FABLE_BACKEND_ROOT`로 지정합니다. 연결 경로는 `Tools/Backend-Link.ps1`에서 공유합니다. `Start-OnlineServer.ps1`은 그 백엔드의 `Saved/Backend/server.json`과 상태를 자동 선택하며 `BackendConfig` 인자를 받지 않습니다. 기존 원본 서비스의 설정·키·볼륨을 이 개발 사본으로 자동 이전하지 않습니다.

バックエンドの既定位置は兄弟ディレクトリです。他の互換作業コピーは `-BackendRoot` または現在の PowerShell プロセスの `FABLE_BACKEND_ROOT` で指定します。接続先パスは `Tools/Backend-Link.ps1` で共有します。`Start-OnlineServer.ps1` はそのバックエンドの `Saved/Backend/server.json` と状態を自動選択し、`BackendConfig` 引数は受け取りません。既存の原本サービスの設定・キー・ボリュームをこの開発用コピーへ自動移行することはありません。

`Tools/Test-Dedicated.ps1`은 `-BackendRoot`와 명시적 `-BackendConfig`를 받습니다. 서버 실행 전 API/DB 설정 일치를 확인하며, `-RestoreAccountsPath` 이전 검증은 두 계정의 실제 장비 버전 0·null 목록·원본 blob 해시·소유자 일치를 요구합니다. 이미 변환된 계정을 이전 검증에 재사용하지 않습니다. 백엔드 실행기·비공개 설정·프로세스 상태는 지정한 백엔드 루트의 것을 사용하며, 게임 저장이나 원본의 키 파일을 이 저장소로 복사하지 않습니다. 새 백엔드 작업본을 처음 실행하기 전에는 기존 서비스·DB와 구분하는 해당 README의 안내를 확인합니다.

`Tools/Test-Dedicated.ps1` は `-BackendRoot` と明示的な `-BackendConfig` を受け取ります。サーバー起動前に API/DB 設定の一致を確認し、`-RestoreAccountsPath` による移行検証では、2 アカウントの実際の装備バージョン 0・null の一覧・原本 blob ハッシュ・所有者の一致を要求します。移行済みアカウントは移行検証に再利用しません。バックエンドランチャー・非公開設定・プロセス状態は指定バックエンドルートのものを使用し、ゲーム保存や原本のキーファイルをこのリポジトリへコピーしません。新しいバックエンド作業コピーを初めて起動する前に、その README の既存サービス・DB と区別する案内を確認してください。

클라이언트의 주소와 API는 `Play-Online.cmd -Address 127.0.0.1:7802 -BackendUrl http://127.0.0.1:5291`로 지정할 수 있습니다. 기본은 현재 Editor 모듈의 게임 창입니다. `-PackagedExecutable <내부 Fablewood.exe>` 또는 `-LatestPackaged`는 클라이언트에만 사용하며, 호환되는 현재 게임 소스의 패키지가 필요합니다. 온라인 클라이언트 실행기는 DX11과 창 비활성 시에도 진행하는 설정을 실행별로 적용합니다.

クライアントのアドレスと API は `Play-Online.cmd -Address 127.0.0.1:7802 -BackendUrl http://127.0.0.1:5291` で指定できます。既定では現在の Editor モジュールのゲームウィンドウを使用します。`-PackagedExecutable <내부 Fablewood.exe>`（内部の Fablewood.exe）または `-LatestPackaged` はクライアント専用で、現在のゲームソースと互換性のあるパッケージが必要です。オンラインクライアントランチャーは、DX11 とウィンドウ非アクティブ時も処理を継続する設定を起動ごとに適用します。

일회용 입장권은 180초 동안 유효합니다. 비밀번호와 세션 토큰은 게임 명령줄이나 상태 파일에 넣지 않습니다. 첫 실행 초기화가 오래 걸려 입장권이 만료되면 실행기를 다시 실행해 새 입장권을 받습니다. 서버·클라이언트 상태와 로그는 이 게임 작업본의 `Saved/OnlineSessions`에 기록합니다.

使い捨て入場券の有効期間は 180 秒です。パスワードとセッショントークンはゲームのコマンドラインや状態ファイルに含めません。初回起動の初期化が長引いて入場券が失効した場合は、ランチャーを再実行して新しい入場券を取得します。サーバー・クライアントの状態とログは、このゲーム作業コピーの `Saved/OnlineSessions` に記録します。

## 전용 서버 빌드의 현재 범위 / 専用サーバービルドの現在の範囲

현재 지원하는 서버 실행 경로는 **`UnrealEditor-Cmd.exe -server -NullRHI`**입니다. 이전 원본 단계에서 실제 `NM_DedicatedServer` 1개와 Windows 패키지 클라이언트 2개의 접속·저장을 검증했습니다. 이번 장비 중앙 저장 확장은 Editor-Cmd 전용 서버와 Editor 게임 클라이언트 두 개로 검증했습니다. 이번 변경의 새 Windows 패키지·독립 Server Target·Linux·외부망은 검증하지 않았습니다. 일반 Launcher 엔진의 Game Target `Fablewood.exe`에 `-server`를 붙이는 방식은 전용 서버로 동작하지 않아 사용하지 않습니다.

現在対応しているサーバー起動方法は **`UnrealEditor-Cmd.exe -server -NullRHI`** です。以前の原本段階では、実際の `NM_DedicatedServer` 1 つと Windows パッケージクライアント 2 つの接続・保存を検証しました。今回の装備中央保存拡張は、Editor-Cmd 専用サーバーと Editor ゲームクライアント 2 つで検証しました。今回の変更に対応する新しい Windows パッケージ・独立 Server Target・Linux・外部ネットワークは未検証です。一般的な Launcher エンジンの Game Target `Fablewood.exe` に `-server` を付ける方法は専用サーバーとして動作しないため、使用しません。

`Source/FablewoodServer.Target.cs`와 `FablewoodClient.Target.cs`는 공통 게임 모듈을 사용하는 별도 Target 정의입니다. 최적화된 독립 `FablewoodServer.exe` 빌드는 Server Target을 지원하는 Unreal 소스 엔진이 필요하며 아직 빌드·검증하지 않았습니다. 원본 검증은 한 PC의 loopback 범위이며 인터넷 공개 서버나 물리적으로 분리된 PC의 결과가 아닙니다.

`Source/FablewoodServer.Target.cs` と `FablewoodClient.Target.cs` は、共通ゲームモジュールを使用する別々の Target 定義です。最適化された独立 `FablewoodServer.exe` のビルドには Server Target をサポートする Unreal ソースエンジンが必要で、まだビルド・検証していません。原本の検証は 1 台の PC の loopback 内であり、インターネット公開サーバーや物理的に別の PC での結果ではありません。

## 소스에 포함하는 것과 생성 결과 / ソースに含めるものと生成物

- 포함: `Source`, `Config`, `Tools`, 루트 실행기, 문서, `Content`의 `.uasset`·`.umap`, `ArtSource` 원본, 폰트 라이선스와 참고 원화. `Build/Windows`의 직접 작성한 아이콘·리소스도 소스입니다.
- 제외: `Binaries`, `Intermediate`, `DerivedDataCache`, `Saved`, `Builds`, `bin/obj`, `.vs`, Unreal이 만든 루트 솔루션, 미리보기·검증 캡처, 실제 비밀 설정과 DB 데이터.

- 含めるもの: `Source`、`Config`、`Tools`、ルートのランチャー、文書、`Content` の `.uasset`・`.umap`、`ArtSource` 原本、フォントライセンスと参考原画。`Build/Windows` に直接作成したアイコン・リソースもソースです。
- 除外するもの: `Binaries`、`Intermediate`、`DerivedDataCache`、`Saved`、`Builds`、`bin/obj`、`.vs`、Unreal が生成したルートのソリューション、プレビュー・検証キャプチャ、実際の秘密設定と DB データ。

에셋은 바이너리 형식이어도 게임을 편집하고 실행하는 필수 입력입니다. 특히 `ArtSource/Meshes/*.obj`는 메시 원본이므로 빌드 중간 파일과 혼동하여 제외하지 않습니다. 현재 프로젝트에는 원본 OBJ 68개와 대응 런타임 메시가 포함됩니다. `.gitignore`는 제외 규칙이며 기존 파일을 삭제하지 않습니다.

アセットはバイナリ形式でも、ゲームの編集と起動に必要な入力です。特に `ArtSource/Meshes/*.obj` はメッシュ原本であり、ビルド中間ファイルと混同して除外しません。現在のプロジェクトには OBJ 原本 68 個と対応するランタイムメッシュが含まれています。`.gitignore` は除外規則であり、既存ファイルを削除しません。

```powershell
pwsh -NoProfile -File .\Tools\Get-SourceManifest.ps1
```

게임 manifest는 이 루트의 파일·크기·SHA-256, 필수 맵·폰트·아트·전체 `Source`와 신규 장비 구현/검사 파일 포함 여부, 비밀 설정 패턴을 검사하여 `Saved/SourceManifest`에 기록합니다. 출력 직전 전체 해시를 다시 확인하고 기존 출력 파일은 덮어쓰지 않습니다. 형제 백엔드의 소스나 `global.json`은 해당 백엔드 manifest가 관리합니다. 이 검사는 빌드·플레이 검증이나 Git 커밋이 아니며, 알려진 비밀 값이 없는 작업본의 검사만으로 모든 비밀의 부재를 보장하지 않습니다.

ゲームの manifest は、このルートのファイル・サイズ・SHA-256、必須マップ・フォント・アート・全 `Source` と新しい装備実装/検査ファイルの包含、および秘密設定パターンを検査し、`Saved/SourceManifest` に記録します。出力直前に全ハッシュを再確認し、既存の出力ファイルは上書きしません。兄弟バックエンドのソースや `global.json` は、そのバックエンドの manifest が管理します。この検査はビルド・プレイ検証や Git コミットではなく、既知の秘密値がない作業コピーを検査するだけで、あらゆる秘密が存在しないことを保証するものでもありません。

## 개발 이력과 과거 검증 자료 / 開発履歴と過去の検証資料

[DevelopmentHistory.md](Docs/DevelopmentHistory.md)는 분리 전 README를 바이트 그대로 옮긴 개발 이력입니다. 기능·조작법, 단계별 검증과 제한 사항을 보존했습니다. 문서 안의 `Saved/...`, `Builds/...`, `ArtDirection/Previews/...` 상대 경로는 **원래 `D:\GameProjects\Fablewood` 루트를 기준으로 작성된 과거 위치**이며, 현재 `Docs` 하위나 새 작업본에 자료가 있다는 뜻이 아닙니다. 기존 절대 경로도 역사 기록으로 유지했습니다.

[DevelopmentHistory.md](Docs/DevelopmentHistory.md) は、分離前の README をバイト単位でそのまま移した開発履歴です。機能・操作方法、段階ごとの検証と制限事項を保全しました。文書内の `Saved/...`、`Builds/...`、`ArtDirection/Previews/...` の相対パスは、**元の `D:\GameProjects\Fablewood` ルートを基準に記載した過去の場所**であり、現在の `Docs` 以下や新しい作業コピーに資料があることを意味しません。既存の絶対パスも歴史記録として維持しています。

분리 기준은 원본의 `Saved/SourceManifest/20261023-005200-source-only.json`입니다. 원본 폴더의 코드·저장·패키지·검증 자료는 별도로 보존합니다. 과거 마이그레이션 회귀에는 summary와 역할별 `.sav` fixture가 필요하므로 새 소스 작업본만으로 모든 과거 회귀를 실행할 수는 없습니다. 필요한 경우 별도 보존 자료를 명시적으로 지정하고 원본 SHA를 대조합니다.

分離の基準は原本の `Saved/SourceManifest/20261023-005200-source-only.json` です。原本フォルダーのコード・保存データ・パッケージ・検証資料は別途保全します。過去の移行回帰には summary と役割別の `.sav` fixture が必要なため、新しいソース作業コピーだけで過去の全回帰を実行することはできません。必要に応じて別途保全した資料を明示的に指定し、原本 SHA を照合します。


<a id="foundation-development-record-20261023"></a>

## Foundation 개발 원문 · 2026-10-23 / Foundation 開発原文 · 2026-10-23

이 구역은 23일 Foundation 문서를 원문 단위로 보존한 한일 대응이다. 당시 ‘현재’와 미검증 상태는 이력이며, 위 24일 안내가 최신 1단계 결과를 구분한다.

本節は 23 日の Foundation 文書を原文単位で保存した韓日対応です。当時の「現在」・未検証状態は履歴で、上の 24 日案内が最新の第 1 段階結果を区別します。

# CatPjt-Fablewood-Game

# CatPjt-Fablewood-Game

동화풍 협동 액션 RPG Fablewood의 Unreal 공통 프로젝트입니다. 클라이언트는 화면·입력을, Unreal 전용 서버는 이동·전투·보상·아이템 사용을 담당합니다. 형제 `CatPjt-Fablewood-Backend`의 .NET API와 PostgreSQL이 계정·접속 권한·영구 저장을 담당합니다.

おとぎ話風の協力アクション RPG Fablewood の Unreal 共通プロジェクトです。client は画面・入力、Unreal 専用 server は移動・戦闘・報酬・item 使用を担当します。兄弟 `CatPjt-Fablewood-Backend` の .NET API と PostgreSQL が account・接続権限・永続保存を担当します。

현재 작업본은 `D:\GameProjects\Fablewood-OnlineFlow-20261023` 아래에 있습니다. 기존 원본, Git 등록 중인 분리 저장소와 완료된 Inventory 작업본은 보존합니다. 내부 프로젝트·모듈·Target·에셋 이름은 `Fablewood`, 진입 파일은 `Fablewood.uproject`입니다.

当時の作業コピーは `D:\GameProjects\Fablewood-OnlineFlow-20261023` 以下です。既存原本、Git 登録中の分離 repository、完了済み Inventory 作業コピーは保存します。内部 project・module・Target・asset 名は `Fablewood`、入口は `Fablewood.uproject` です。

```text
Fablewood-OnlineFlow-20261023/
├─ CatPjt-Fablewood-Game/
└─ CatPjt-Fablewood-Backend/
```

## 이번 변경과 검증 상태

## 今回の変更と検証状態

게임 안에서 가입·로그인, 계정별 단일 캐릭터 확인, 서버 선택·입장, 연결 실패 재시도, 저장 확인 후 퇴장과 로그아웃을 구현했습니다. 캐릭터 요약은 서버가 SaveGame·장비·재화와 같은 트랜잭션으로 저장합니다. 아직 요약이 없는 기존 캐릭터는 접속 전 레벨을 추측해서 표시하지 않습니다. 서버 목록은 설정된 접속 주소이며 서버의 실시간 가동 상태는 아닙니다.

ゲーム内での登録・login、account ごとの単一 character 確認、server 選択・入場、接続失敗 retry、保存確認後の退場・logout を実装しました。character summary は server が SaveGame・装備・通貨等と同じ transaction で保存します。summary のない既存 character の接続前 level を推測表示しません。server list は設定済み接続先であり、リアルタイムの稼働状態ではありません。

2026-10-23 공용 데스크톱에서 Editor C++ 빌드·맵 준비, native 22개(실패·경고 0), Windows Development Game 패키징이 통과했습니다. 최종 온라인 작업 `20261023T131703552Z-fa59464f`는 서버 1개와 렌더링 클라이언트 2개로 가입·로그인·이동·스킬·물약·저장·정상 퇴장·의도한 강제 종료를 확인하고, 같은 DB를 유지한 API 재시작 뒤 두 캐릭터의 ID·재화·전체 아이템·장비·요약이 보존된 재로그인까지 통과했습니다. 한 PC의 loopback 검증이며 패키지 실행 파일 플레이는 아직 검증하지 않았습니다. 빌드·native·패키지는 이전 성공 작업 `20261023T115009777Z-aee36c0b`의 동일한 C++·에셋·설정과 검증한 바이너리를 사용했습니다. 두 Tools 파일의 진단·ACL 수정은 별도 실제 검사와 최종 온라인 작업으로 검증했습니다.

2026-10-23 に共用 desktop で Editor C++ build・map 準備、native 22 件（失敗・警告 0）、Windows Development Game packaging が通過しました。当時の最終 online job `20261023T131703552Z-fa59464f` は server 1 個と描画 client 2 個で登録・login・移動・skill・potion・保存・正常退場・意図した強制終了を確認し、同じ DB を維持した API 再起動後に 2 character の ID・通貨等・全 item・装備・summary が保存された再 login まで通過しました。1 PC の loopback 検証であり、この記録時点で package 実行ファイルの play は未検証です。build・native・package は先行成功 job `20261023T115009777Z-aee36c0b` と同じ C++・asset・設定および検証済み binary を使用しました。Tools 2 file の診断・ACL 修正は別の実検査と当該最終 online job で検証しました。

이전 Inventory 작업본에서 통과한 native 18개, HTTP/DB 316개, 재시작 지속성 23개와 fresh/reconnect/resume 2인 검사는 이전 소스의 결과입니다. 이번 작업본의 검증 결과로 재사용하지 않습니다. 초기 개발 이력은 [DevelopmentHistory.md](https://github.com/krumichan/CatPjt-Fablewood-Game/blob/main/Docs/DevelopmentHistory.md)에 보존합니다.

以前の Inventory 作業コピーで通過した native 18 件、HTTP/DB 316 件、再起動永続性 23 件、fresh/reconnect/resume の 2 人検査は以前の source の結果です。今回コピーの検証結果として再利用しません。初期開発履歴は [DevelopmentHistory.md](https://github.com/krumichan/CatPjt-Fablewood-Game/blob/main/Docs/DevelopmentHistory.md) に保存します。

## 빌드와 계정 화면

## Build と account 画面

Windows, Unreal Engine **5.8.4**, MSVC **14.50**, Windows SDK **10.0.26100.0**, PowerShell **7.4 이상**이 필요합니다. 실행·빌드·테스트는 공용 데스크톱 실행 절차를 우선 사용합니다. 소스 배포에는 호환 바이너리가 없으므로 준비된 환경에서 먼저 Editor를 빌드해야 합니다.

Windows、Unreal Engine **5.8.4**、MSVC **14.50**、Windows SDK **10.0.26100.0**、PowerShell **7.4 以上**が必要です。実行・build・test は共用 desktop 手順を優先します。source 配布には対応 binary がないため、準備済み環境で最初に Editor を build します。

```powershell
.\Setup-Project.cmd -EngineRoot 'C:\Program Files\Epic Games\UE_5.8'
.\Play-Account.cmd -BackendUrl http://127.0.0.1:5292
```

`Play-Account.cmd`는 로컬 모험을 생성하지 않고 게임 내 계정 화면을 엽니다. 계정 이름·비밀번호는 화면에 입력합니다. 비밀번호와 재사용 가능한 세션 토큰은 명령줄·상태 파일에 기록하지 않습니다. 시작 직업은 가입 시 전사·마법사·궁수 중 선택하며, 현재는 계정당 캐릭터 하나입니다. 기존 로컬 플레이는 계정 화면의 버튼으로 들어갑니다. `Open-Editor.cmd`, 기존 `Play.cmd -Editor`, 협동·검증 실행기도 유지합니다.

`Play-Account.cmd` は local adventure を作らず、ゲーム内 account 画面を開きます。account 名・password は画面に入力します。password と再利用可能 session token を command line・state file に記録しません。開始 class は登録時に戦士・魔法使い・弓使いから選び、現在は account ごとに character 1 人です。既存 local play は account 画面のボタンから入ります。`Open-Editor.cmd`、既存 `Play.cmd -Editor`、協力・検証 launcher も維持します。

## 격리된 온라인 실행

## 隔離した online 実行

형제 [백엔드 README](https://github.com/krumichan/CatPjt-Fablewood-Backend/blob/main/README.md)의 Release 빌드와 **이번 별도 인스턴스 명령**을 먼저 수행합니다. 이번 작업에 예약한 주소는 API `127.0.0.1:5292`, DB `127.0.0.1:55440`, Unreal `127.0.0.1:7821`입니다. 검증에서는 작업별 고유 컨테이너·volume·schema와 새 테스트 계정으로 이 주소를 사용했고, 완료 후 소유 API·게임·컨테이너를 종료했습니다. 기존 `5281/55438`, Inventory `5291/55439`와 저장 데이터를 재사용하지 않습니다.

兄弟の [backend README](https://github.com/krumichan/CatPjt-Fablewood-Backend/blob/main/README.md) の Release build と、**今回の別 instance コマンド**を先に行います。今回予約した接続先は API `127.0.0.1:5292`、DB `127.0.0.1:55440`、Unreal `127.0.0.1:7821` です。検証では job 固有 container・volume・schema と新 test account で使い、完了後に所有 API・game・container を終了しました。既存 `5281/55438`、Inventory `5291/55439` と保存データは再利用しません。

```powershell
.\Start-OnlineServer.cmd -BackendRoot ..\CatPjt-Fablewood-Backend -Port 7821
.\Play-Account.cmd -BackendUrl http://127.0.0.1:5292
.\Play-Account.cmd -BackendUrl http://127.0.0.1:5292 -WindowX 1040
```

게임 서버 목록의 기본 주소도 `127.0.0.1:7821`입니다. 다른 포트를 쓰면 API의 `FABLE_GAME_ADDRESS`를 일치시켜야 합니다. 위 두 창은 서로 다른 계정으로 로그인합니다. 플레이 중 Esc로 온라인 메뉴를 열고 안전 퇴장을 선택하면 마지막 저장과 writer 리스 반환 응답을 받은 뒤 캐릭터 화면으로 돌아갑니다. 실패·시간 초과를 성공으로 처리하지 않습니다. 강제 종료 뒤에는 서버의 연결 종료 저장과 리스 만료/반환을 기다린 뒤 새 일회용 접속권으로 재시도합니다.

game server list の既定接続先も `127.0.0.1:7821` です。別 port を使う場合は API の `FABLE_GAME_ADDRESS` を合わせます。上記 2 window は異なる account で login します。play 中に Esc で online menu を開いて安全退場を選ぶと、最終保存と writer lease 返却の応答を受けてから character 画面に戻ります。失敗・timeout を成功扱いしません。強制終了後は server の切断保存と lease 失効/返却を待ち、新しい一回限りの接続 ticket で retry します。

기본 백엔드 위치는 형제 디렉터리이며 `-BackendRoot` 또는 프로세스의 `FABLE_BACKEND_ROOT`로 바꿉니다. `Start-OnlineServer.ps1`은 해당 백엔드의 소유 상태와 `Saved/Backend/server.json`을 사용하고 API를 자동 빌드하지 않습니다. 비공개 설정을 게임 소스에 복사하지 않습니다.

backend の既定位置は兄弟 directory で、`-BackendRoot` または process の `FABLE_BACKEND_ROOT` で変更できます。`Start-OnlineServer.ps1` は対象 backend の所有状態と `Saved/Backend/server.json` を使い、API を自動 build しません。非公開設定を game source にコピーしません。

기존 `Play-Online.cmd`는 직접 접속 검증용으로 유지합니다. 새 가입·로그인 UI 검증은 아래 명령입니다. 이미 준비된 별도 API를 검사한 후 자체 소유의 Unreal 서버와 클라이언트만 시작·종료합니다.

既存 `Play-Online.cmd` は直接接続検証用として維持します。新しい登録/login UI の検証は次のコマンドです。準備済みの別 API を確認し、自ら所有する Unreal server/client だけを開始・終了します。

```powershell
pwsh -NoProfile -File .\Tools\Test-OnlineFlow.ps1 -BackendRoot ..\CatPjt-Fablewood-Backend -BackendUrl http://127.0.0.1:5292 -Port 7821
```

검증기는 서버 부재 오류→새 접속권 재시도, 두 계정의 이동·스킬·물약, 한 명의 안전 퇴장과 다른 한 명의 강제 종료, API/서버 재시작 후 기존 계정 재접속·보존값 비교를 검사하도록 작성했습니다. 실행 전 준비 조건과 실제 결과를 확인해야 합니다. 임시 테스트 계정·비밀번호·보고서·캡처는 `Saved/OnlineValidation`에만 두며 Git에서 제외합니다.

検証器は server 不在 error→新 ticket による retry、2 account の移動・skill・potion、1 人の安全退場と他方の強制終了、API/server 再起動後の既存 account 再接続・保存値比較を検査するよう作成しました。実行前の前提と実結果を確認してください。一時 test account・password・report・capture は `Saved/OnlineValidation` だけに置き、Git から除外します。

## 저장 계약과 서버 범위

## 保存契約と server 範囲

UE SaveGame은 v9, DB 스키마는 **3**, 정형 장비와 캐릭터 요약은 각각 버전 1입니다. 아이템 UUID·소유자·가방/창고 순서·장착·옵션·품질·등급·강화·내구도, 재화 7종과 프로필 blob을 원자 저장합니다. API는 소유권·형식·접속 리스·리비전을 검증하고 전투나 가격을 재계산하지 않습니다. 기존 중앙 저장은 값과 장비를 유지하며 필요한 정보를 첫 접속 때 백필하고 ACK 전 플레이를 차단합니다. 로컬 `.sav` 자동 업로드는 없습니다.

UE SaveGame は v9、DB schema は **3**、構造化装備と character summary は各 version 1 です。item UUID・owner・bag/storage 順序・装備・affix・quality・rarity・upgrade・durability、通貨等 7 種と profile blob を原子保存します。API は ownership・形式・接続 lease・revision を検証し、戦闘や価格を再計算しません。既存中央保存の値・装備を維持し、初回接続時に必要情報を backfill して ACK 前の play を止めます。local `.sav` の自動 upload はありません。

현재 지원하는 전용 서버 실행 경로는 **`UnrealEditor-Cmd.exe -server -NullRHI`**입니다. `FablewoodServer.Target.cs`와 Client Target 정의는 있지만 독립 `FablewoodServer.exe`에는 Server Target 지원 소스 엔진이 필요합니다. Windows Development Game 패키징은 통과했지만 패키지 자체 플레이·독립 Server Target·Linux·외부망은 검증하지 않았습니다. Game Target 실행 파일에 `-server`만 붙여 전용 서버라고 취급하지 않습니다.

対応する専用 server の実行経路は **`UnrealEditor-Cmd.exe -server -NullRHI`** です。`FablewoodServer.Target.cs` と Client Target 定義はありますが、独立した `FablewoodServer.exe` には Server Target 対応 source engine が必要です。Windows Development Game packaging は通過しましたが、この記録時点の package 自体の play・独立 Server Target・Linux・外部 network は未検証です。Game Target 実行ファイルに `-server` を付けるだけで専用 server と扱いません。

## 소스와 산출물

## Source と生成物

포함 대상은 `Source`, `Config`, `Tools`, 루트 실행기·문서, `Content`의 필수 `.uasset`·`.umap`, `ArtSource` 원본과 라이선스, 직접 작성한 `Build/Windows` 리소스입니다. 원본 메시 OBJ 68개는 필수 소스입니다.

含めるのは `Source`、`Config`、`Tools`、root launcher・文書、`Content` の必須 `.uasset`・`.umap`、`ArtSource` 原本・license、自作 `Build/Windows` resource です。原本 mesh OBJ 68 個は必須 source です。

`Binaries`, `Intermediate`, `DerivedDataCache`, `Saved`, `Builds`, `bin/obj`, `.vs`, 생성 솔루션·검증 캡처·실제 비밀 설정·DB 데이터는 제외합니다. `.gitignore`와 소스 매니페스트는 기존 파일을 삭제하지 않습니다.

`Binaries`、`Intermediate`、`DerivedDataCache`、`Saved`、`Builds`、`bin/obj`、`.vs`、生成 solution・検証 capture・実秘密設定・DB data は除外します。`.gitignore` と source manifest は既存 file を削除しません。

```powershell
pwsh -NoProfile -File .\Tools\Get-SourceManifest.ps1
```

매니페스트는 허용 목록·필수 소스·자산 쌍·파일 SHA-256과 알려진 비밀 설정 패턴을 확인합니다. 이는 빌드·플레이 통과나 모든 비밀의 부재를 보증하지 않습니다. 형제 백엔드는 별도 매니페스트를 사용합니다. 과거 이력 문서의 원본 경로·산출물 참조는 역사 기록이며 새 소스 배포에 과거 실행 결과가 포함된다는 의미가 아닙니다.

manifest は allowlist・必須 source・asset pair・file SHA-256・既知の秘密設定 pattern を確認します。build/play 通過やあらゆる秘密情報の不在を保証しません。兄弟 backend は別 manifest を使います。過去履歴文書の原本 path・成果物参照は歴史記録で、新 source 配布に過去実行結果を含む意味ではありません。
