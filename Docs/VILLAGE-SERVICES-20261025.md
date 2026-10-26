# 마을 서비스 / 村サービス

기능 개발일: 2026-10-25 · QA 수정·검증·문서 정리일: 2026-10-26 (KST)

機能開発日: 2026-10-25 · QA修正・検証・文書整理日: 2026-10-26 (KST)

## 한국어

### 1. 구현 범위

기존 `LegacyVillage`의 퀘스트 안내인·공방·상인·원정 포털을 논리 서비스 ID로 구분하고, 대화와 변경 동작에 서버 권한 검사를 연결한다. 새 NPC 모델·서비스 좌표·지형을 추가하지 않는다. 이 문서는 현재 API와 검증 계약을 설명한다. 2026-10-26에 완료한 실제 실행과 독립 감사 결과는 8절에 기록한다.

| 논리 서비스 ID | 기존 카탈로그 앵커 | 연결된 기능 |
| --- | --- | --- |
| `QuestGuide` | `Service.Board` | 의뢰 수락·보고, 직업·스킬·특성·육성 초기화, 이야기 보고 |
| `Workshop` | `Service.Forge` | 장비 강화·제작·분해·옵션 재설정·수리·창고 |
| `Merchant` | `Service.Merchant` | 구매·판매 |
| `ExpeditionPortal` | `Portal.Expedition` | 마을의 원정 입장·마을에서 시작하는 재도전 |

장비 착용은 `QuestGuide` 또는 `Workshop`에서 허용한다. 공개 숲의 참여·재도전도 `QuestGuide`를 변경 시점에 검사한다. 일반 이동·전투·물약, 봉인과 지역 귀환은 각자의 기존 서버 규칙을 유지한다. 기존 좌표형 서비스 helper와 사설 원정 내부의 재도전 경로도 필요한 호출자에 남아 있다.

### 2. 논리 참조와 읽기 전용 UI

`FFableTownServiceRef`는 마을 ID·서비스 ID·카탈로그 버전으로 구성된다. `UFableTownCatalog::ResolveService`는 해당 서비스의 기존 앵커 첫 슬롯과 층에서 `FFableTownServiceDescriptor`를 만든다. 알 수 없는 ID, 버전 불일치, 누락 앵커·지원 태그, 준비되지 않은 지형은 상호작용에 사용할 수 없다. 서비스 참조는 이동 권한이나 안전 저장 위치가 아니며, durable 안전 앵커 허용목록은 기존 `Entry` 전용 정책을 유지한다.

`ResolveNearbyService`는 클라이언트에서도 사용할 수 있는 표시·선택용 공간 조회다. E 키 후보와 특성 UI가 이를 사용하지만, 화면의 거리 표시·미리보기·열린 패널은 변경 권한을 부여하지 않는다. E 키로 네 종류의 마을 서비스 대화를 여는 경로는 서버 검사를 통과해야 한다. 원정 준비 패널을 로컬에서 열어도 실제 입장·변경 요청은 별도의 서버 검사를 통과해야 한다. 공방의 비용·보유량·작업 사유 조회도 상태를 바꾸지 않는다.

### 3. 실제 변경 시점의 서버 검사

서비스 RPC의 공통 준비 검사와 `AFableGameMode::CanUseTownService`, `AFablePlayerState::AtTownService`는 실제 변경 직전에 다음 조건을 확인한다.

- 현재 서버 월드의 controller·profile·pawn이 서로 정확한 소유 관계이고, 현재 연결의 nonce·세대가 등록 명부와 일치한다.
- 살아 있고 프로필·저장 상태가 사용 가능하며, 복원 차단·진행 중 마을 이동·파티 복구 등 해당 작업을 막는 상태가 없다.
- 저장된 presence나 클라이언트가 지정한 XYZ 대신 서버의 현재 pawn에서 마을·층·카탈로그 버전·지형 준비 상태를 다시 읽는다. 서비스와 같은 마을·층이어야 하며 `Connector` 안에서는 허용하지 않는다.
- 좌표와 반경은 유한해야 한다. 서비스 반경은 수평 330cm 이하이며, 실제 높이 차이는 300cm 미만이어야 한다. 호출자가 이 범위를 확장할 수 없다.
- `IsGroundedAtCurrentTownAnchor`가 새 floor/capsule query로 실제 walking 상태, walkable 지지면, 카탈로그의 지원 component tag·높이, 바닥 간격과 capsule 비충돌을 검사한다. 지지면은 현재 persistent level의 `WorldStatic`이어야 한다.

서비스 관련 실제 의뢰·직업·스킬·특성·육성 초기화·이야기·착용·공방·상점 변경은 위 검사와 각 기능의 기존 소유·비용·재사용 대기시간·아이템 규칙을 함께 적용한다. 대화를 먼저 열었더라도 변경 시점의 검사를 생략하지 않는다. 조회 자체는 안전 귀환지·체크포인트·골드·아이템·저장 상태를 갱신하지 않는다.

기존 원정 참가 증서를 보유한 플레이어가 실제 마을 포털로 재입장하는 경우에는, 현재 소유 원정의 참가자이며 귀환 중이 아님을 확인하는 제한된 예외가 있다. 이 예외도 현재 연결·소유권·실제 마을·층·범위·지지면 검사를 생략하지 않는다. 가짜 원정 GUID, 다른 서비스, 다른 참가자, 사설 공간의 같은 XY는 허용 근거가 아니다. 완료된 사설 원정 내부 재도전의 기존 교체 규칙은 별도로 유지한다.

### 4. 후보 마을의 활성화 경계

`Sunbrook`, `Starbough`, `Snowbloom`, `Eldergrove`는 계속 `geometryReady=false`, `adoptedCandidate=null`이다. 서비스 API를 추가했다고 네 마을의 배치·층 연결·스트리밍·지지면·아트가 승인되는 것은 아니다. 후보의 기존 Entry/Return/Recovery 앵커, bootstrap 정책과 Content 채택 경계는 유지한다. 새 마을 서비스는 해당 지형·앵커·component tag·cook·실제 플레이 승인이 확보되기 전 활성화하지 않는다.

### 5. Native 검사와 기존 회귀 계약

Native 목록은 기존 70개와 다음 서비스 검사 2개를 합한 총 72개다.

- `Fablewood.Native.TownService.CatalogScopeReadinessAndFiniteRange`
- `Fablewood.Native.TownService.CurrentOwnerAndSupportedFloor`

서비스 검사 2개는 메모리 월드와 합성 지지면에서 카탈로그·알 수 없는 ID·버전·미준비 지형·다른 마을/층·연결부·유한 거리, 현재 소유자·연결 교체·부유·낙하·잘못된 태그/높이를 다룬다. 실제 구매·제작·강화·판매의 비용과 상태 변화, 거절 시 보존도 검사한다. 이 fixture는 실제 DB 저장, UI 배치, 새 마을 지형 승인 증거를 대신하지 않는다.

기존 `party-results` 메인의 582개 ordered typed 조건은 의미를 유지한다. 기존 메인은 실제 파티 전투·결과·재도전·재접속·안전 이탈·저장 보존을 검사하며, 상점·공방의 실제 동작 증거를 대신하지 않는다. 서비스 실제 플레이는 별도 opt-in 시나리오 `unit4-legacy-services`로 검사한다.

### 6. 실제 서비스 플레이의 검증 계약

별도 시나리오는 정상 이동 입력과 제품 UI/RPC를 사용한다. 검증 전용 순간이동이나 아이템 지급 RPC로 조건을 만들지 않는다. 각 플레이어의 다음 13개 라벨을 클라이언트 기록과 서버 관찰에 연결해 총 26개 서버 관찰을 대조한다.

| 라벨 | 확인할 동작 |
| --- | --- |
| `guide-out-of-range-reject` | 안내인 범위 밖의 요청 거절 |
| `guide-open` | 안내인 대화 열기 |
| `forest-accept` | 숲 의뢰 수락과 저장 확인 |
| `forest-three-kills` | 정상 공유 숲 전투의 세 번 처치·보상 집계 |
| `guide-turnin-open` | 보고를 위한 안내인 대화 열기 |
| `forest-turnin` | 숲 의뢰 보고·보상과 저장 확인 |
| `shop-wrong-service-reject` | 다른 서비스 위치에서 구매 요청 거절 |
| `shop-open` | 상점 대화 열기 |
| `shop-buy-one` | 체력 물약 한 개 구매와 저장 확인 |
| `forge-wrong-service-reject` | 다른 서비스 위치에서 강화 요청 거절 |
| `forge-open` | 공방 대화 열기 |
| `forge-upgrade-one` | 획득한 숲 무기 한 개의 1단계 강화와 저장 확인 |
| `portal-open` | 실제 마을 포털에서 원정 준비 UI 열기 |

거절은 실제 핸들러 도달·서버 거절 통지·상태 보존으로 확인하며, 타임아웃을 성공으로 처리하지 않는다. 단일 서비스 변경은 실제 전후 상태와 정확한 저장 ACK로 확인한다. 다른 플레이어의 상태 보존을 함께 대조하되, 공유 숲 전투의 정당한 공동 보상은 별도 집계 계약을 적용한다. 실제 UI 캡처와 현재 실행·프로세스·패키지의 연결도 검증 대상이다.

추가 서비스 실행은 등록 클라이언트 2회와 재로그인 클라이언트 2회로 총 4회 시작한다. 원래 메인의 5회 시작과 합하면 9회이며, 동시 클라이언트는 최대 2개다. 각 실행은 한 Windows PC의 loopback에서 Editor 전용 서버 1개와 실제 패키징된 클라이언트를 사용한다. 원래 메인과 추가 서비스 실행의 register/login은 순차 서버 프로세스를 사용하므로 전체 서버 프로세스 시작은 4회다.

### 7. 저장·재시작 증거의 한계

등록 후 정상 이탈하고, 같은 DB를 사용하는 API만 재시작한 뒤 새 서버·클라이언트 프로세스에서 두 계정으로 다시 로그인한다. 재시작 전후의 저장 프로필·지갑·인벤토리·장비·캐릭터 요약과 재로그인 소유권을 대조한다. 이 흐름은 DB 프로세스 재시작이나 장애 복구를 수행했다는 뜻이 아니다.

원정 결과·재도전 영수증의 재열람과 복원은 검사된 동일 게임 서버 생명주기 범위에서만 해석한다. API 재시작 뒤 프로필이 보존되어도 원정 결과 ledger가 게임 서버 재시작을 넘어 영구 보존된다고 주장하지 않는다. 독립 standalone ServerTarget, Linux, 외부 네트워크, 물리 PC 두 대, 네 후보 마을의 아트 품질도 이 검증 범위에 포함되지 않는다. 공중·낙하 상태의 실제 UI 거절 시나리오는 실행하지 않았다.

### 8. 실제 실행과 독립 감사 결과

2026-10-26(KST)에 작업 `20261026T023627036Z-07dfbbc9`의 실제 실행이 `Passed / exit 0`으로 완료됐고, 아래 세 독립 감사도 통과했다. 원래 메인 실행은 `20261026-115239-3c89412c`, 추가 서비스 실행은 `20261026-115655-043f2ec6`이다.

| 항목 | 확인된 결과 |
| --- | --- |
| 고정 소스 | 373파일(Game 344·Backend 29), source manifest SHA-256 `a1d29a21fd9dbe7373e04cbda453f5fdfd9ce3843bc33b1c03cc38bb87e83ce7` |
| 빌드·패키지 | .NET 백엔드 빌드·언리얼 C++ Editor 빌드·Windows Development 패키징 모두 성공. 패키지 실행 ID `20261026-024509-157Z-41b39352`, 클라이언트 실행파일 SHA-256 `f03fbbda3d6f5257f39d387b4bbfb491791cba51517521af36946fde7077614b` |
| Native | 총 72개 중 성공 72·경고 동반 성공 0·실패 0·미실행 0. 서비스 2개는 5절의 메모리 fixture이며 실제 DB 증거와 구별한다. |
| 기존 메인 | 원래 582개 ordered typed 조건의 의미를 유지한 독립 감사 통과 |
| 저장 경계 | 추가 durability proof 감사 통과. API만 같은 DB로 재시작하는 저장 프로필 보존 범위이며, 게임 서버 재시작을 넘는 원정 ledger 보존을 뜻하지 않는다. |
| 실제 서비스 | 두 플레이어 각각 13라벨, 총 26개 서버 관찰을 클라이언트 체크포인트와 대조한 독립 감사 통과. 실제 서비스 위치의 UI/RPC와 정상 숲 전투 집계를 포함한다. |

두 플레이어는 의뢰 수락·숲 전투·보고·체력 물약 구매·숲 무기 강화 뒤 정상 이탈했다. 같은 DB를 사용하는 API를 재시작하고 두 계정이 다시 로그인한 뒤에도 각각 골드 90·광석 5·체력 물약 4·아이템 5개가 보존됐다. 각자의 숲 무기는 동일한 아이템 UUID로 강화 단계 `0 → 1`을 확인했으며 품질·옵션 등 다른 아이템 필드는 보존됐다. 기본 `power`의 증가를 별도로 실증했다는 뜻은 아니다.

서비스 실행의 `register/canonical.json`, `api-restart-canonical.json`, `login/canonical.json`은 원시 바이트의 SHA-256이 모두 `2c68ff7de73f7b71a47857189e4d1d2f3dbd58863e3f505c6a2b7377d74d9133`으로 같다. 저장 프로필·지갑·인벤토리·장비·캐릭터 요약의 보존은 이 기록과 실제 재로그인 비교로 확인했다.

실제 클라이언트 시작은 기존 메인 5회와 추가 서비스의 등록 2회·재로그인 2회를 합한 총 9회다. 추가 서비스 클라이언트 4회는 시작·종료 시각으로 최대 동시 2개를 계산했으며, 모두 강제 종료 없이 `exitCode=0`으로 끝났다. 기존 메인 5회는 기존의 순서가 있는 PID 기록과 두 계정·동시 참여 조건으로 검증했다. 기존 메인에는 종료 UTC 기록이 없어 9회 전체의 시간 구간을 새로 합산한 최대 동시 실행 수를 주장하지 않는다.

| 독립 감사 식별자 | 결과 및 SHA-256 |
| --- | --- |
| `audit-20261026T023627036Z-07dfbbc9-v002.json` | PASS · `7d25dbed631556fb1239a61b9f1c5b187e81d264a03a7b778153ea429e32ec0e` |
| `durability-proof-20261026T023627036Z-07dfbbc9-v001.json` | PASS · `0ccd907bbc3a84aae06590786cdb4e2057ed0ddfdefc35f1daac0641e922181e` |
| `legacy-services-audit-20261026T023627036Z-07dfbbc9-v001.json` | PASS · `c70723fc40f801a1330d18c1afc4360ba6f65517aaca7a3b693cdd1a7f19c8f3` |

Git 위탁은 다음 날짜·기능 단위로 나눠 요청한다. 이 표는 업로드 완료 기록이 아니다.

| 순서 | 작업 일자(KST) | 소스 전용 위탁 단위 |
| --- | --- | --- |
| 1 | 2026-10-25 | 마을 서비스 기능 22파일 |
| 2 | 2026-10-26 | QA 컴파일 이름 충돌 수정 1파일 |
| 3 | 2026-10-26 | QA 접근 이동·진단·제한 시간 수정 1파일 |
| 4 | 2026-10-26 | 검증 결과와 같은 내용의 한국어·일본어를 반영한 이 문서 1파일 |

원시 JSON·로그·스크린샷·빌드 산출물·DB·비밀 설정·감사 및 proof 영수증·소스 목록은 Git 대상에 넣지 않고 외부 검증 자료로 보존한다. 이 문서는 실제 검증 완료 뒤 정리한 2026-10-26 문서 변경이다. 위 실행은 고정 source manifest의 373파일을 검증했으며, 이 최종 문서 교체를 포함한 새 빌드·플레이 검증을 주장하지 않는다. 네 후보 마을의 미준비·미채택 상태와 7절의 검증 범위 한계도 유지한다.

## 日本語

### 1. 実装範囲

既存の`LegacyVillage`の依頼案内人・工房・商人・遠征ポータルを論理サービスIDで区別し、会話と変更操作へサーバーの権限検査を接続する。新しいNPCモデル・サービス座標・地形は追加しない。本書は現在のAPIと検証契約を説明する。2026-10-26に完了した実行と独立監査の結果は8節に記録する。

| 論理サービスID | 既存catalogアンカー | 接続した機能 |
| --- | --- | --- |
| `QuestGuide` | `Service.Board` | 依頼の受注・報告、職業・スキル・特性・育成リセット、物語の報告 |
| `Workshop` | `Service.Forge` | 装備の強化・製作・分解・オプション再設定・修理・倉庫 |
| `Merchant` | `Service.Merchant` | 購入・売却 |
| `ExpeditionPortal` | `Portal.Expedition` | 村での遠征入場・村から開始する再挑戦 |

装備の着用は`QuestGuide`または`Workshop`で許可する。公開森への参加・再挑戦も変更時点で`QuestGuide`を検査する。通常の移動・戦闘・ポーション、封印と地域帰還は、それぞれの既存サーバールールを維持する。既存の座標型サービスhelperと私有遠征内部の再挑戦経路も、必要な呼出元に残している。

### 2. 論理参照と読み取り専用UI

`FFableTownServiceRef`は村ID・サービスID・catalog版で構成する。`UFableTownCatalog::ResolveService`は、そのサービスの既存アンカーの最初のslotと階から`FFableTownServiceDescriptor`を生成する。未知のID、版の不一致、アンカー・支持tagの欠落、未準備の地形は操作に使えない。サービス参照は移動権限や安全な保存位置ではなく、durable安全アンカーの許可一覧は既存の`Entry`専用方針を維持する。

`ResolveNearbyService`はクライアントでも使える表示・選択用の空間照会である。Eキー候補と特性UIで使うが、画面の距離表示・preview・開いたpanelは変更権限を与えない。Eキーで四種類の村サービス会話を開く経路はサーバー検査を通る必要がある。遠征準備panelをローカルで開いても、実際の入場・変更要求は別途サーバー検査を通る必要がある。工房の費用・所持数・操作不可理由の照会も状態を変更しない。

### 3. 実変更時点のサーバー検査

サービスRPCの共通準備検査と`AFableGameMode::CanUseTownService`、`AFablePlayerState::AtTownService`は、実変更の直前に次の条件を確認する。

- 現サーバーworldのcontroller・profile・pawnが厳密に同じ所有関係にあり、現在の接続nonce・世代が登録名簿と一致する。
- 生存していてprofile・保存状態が使用可能であり、復元の阻止・進行中の村移動・party復旧など、その操作を阻止する状態がない。
- 保存済みpresenceやクライアント指定XYZではなく、サーバー上の現在のpawnから村・階・catalog版・地形の準備状態を再照会する。サービスと同じ村・階である必要があり、`Connector`内では許可しない。
- 座標と半径は有限である必要がある。サービス半径は水平330cm以下で、実際の高さの差は300cm未満である必要がある。呼出元はこの範囲を拡大できない。
- `IsGroundedAtCurrentTownAnchor`が新しいfloor/capsule queryで、実際のwalking状態、walkable支持面、catalogの支持component tag・高さ、床との間隔、capsuleの非衝突を検査する。支持面は現在のpersistent levelの`WorldStatic`である必要がある。

サービスに関する実際の依頼・職業・スキル・特性・育成リセット・物語・着用・工房・商店の変更には、上記の検査と各機能の既存の所有・費用・cooldown・itemルールを共に適用する。会話を先に開いていても、変更時点の検査を省略しない。照会自体は安全な帰還先・checkpoint・gold・item・保存状態を更新しない。

既存の遠征参加証を持つplayerが実際の村ポータルから再入場する場合には、現在所有する遠征の参加者であり、帰還中でないことを確認する限定的な例外がある。この例外も現在の接続・所有関係・実際の村・階・範囲・支持面の検査を省略しない。偽の遠征GUID、別のサービス、別の参加者、私有空間の同じXYは許可の根拠にならない。完了した私有遠征内部の再挑戦では、既存の置換ルールを別途維持する。

### 4. 候補村の有効化境界

`Sunbrook`、`Starbough`、`Snowbloom`、`Eldergrove`は、引き続き`geometryReady=false`、`adoptedCandidate=null`である。サービスAPIの追加によって、四村の配置・階接続・streaming・支持面・artが承認されることはない。候補の既存Entry/Return/Recoveryアンカー、bootstrap方針とContent採用の境界を維持する。新村サービスは、地形・アンカー・component tag・cook・実プレイの承認がそろう前には有効化しない。

### 5. Native検査と既存回帰契約

Native一覧は、既存70件と次のサービス検査2件を合わせた計72件である。

- `Fablewood.Native.TownService.CatalogScopeReadinessAndFiniteRange`
- `Fablewood.Native.TownService.CurrentOwnerAndSupportedFloor`

サービス検査2件は、memory worldと合成支持面で、catalog・未知ID・版・未準備地形・別の村/階・接続部・有限距離、現所有者・接続置換・浮遊・落下・異なるtag/高さを扱う。実際の購入・製作・強化・売却の費用と状態変化、拒否時の保持も検査する。このfixtureは、実際のDB保存、UI配置、新村地形の承認証跡を代替しない。

既存`party-results`メインの582件のordered typed条件は意味を維持する。既存メインは、実際のparty戦闘・結果・再挑戦・再接続・安全な退出・保存保持を検査し、商店・工房の実操作の証拠を代替しない。サービスの実プレイは、別のopt-in scenarioである`unit4-legacy-services`で検査する。

### 6. 実サービスプレイの検証契約

別scenarioは通常の移動入力と製品UI/RPCを使う。検証専用teleportやitem付与RPCで条件を作らない。各playerの次の13labelをclient記録とserver観測に結び付け、計26件のserver観測を照合する。

| label | 確認する操作 |
| --- | --- |
| `guide-out-of-range-reject` | 案内人の範囲外で要求を拒否 |
| `guide-open` | 案内人の会話を開く |
| `forest-accept` | 森の依頼を受注し保存を確認 |
| `forest-three-kills` | 通常の共有森戦闘で三回の討伐・報酬を集計 |
| `guide-turnin-open` | 報告のため案内人の会話を開く |
| `forest-turnin` | 森の依頼報告・報酬と保存を確認 |
| `shop-wrong-service-reject` | 別サービスの位置で購入要求を拒否 |
| `shop-open` | 商店の会話を開く |
| `shop-buy-one` | 体力ポーション一個の購入と保存を確認 |
| `forge-wrong-service-reject` | 別サービスの位置で強化要求を拒否 |
| `forge-open` | 工房の会話を開く |
| `forge-upgrade-one` | 獲得した森武器一個の一段階強化と保存を確認 |
| `portal-open` | 実際の村ポータルで遠征準備UIを開く |

拒否は実handlerへの到達・serverの拒否通知・状態保持で確認し、timeoutを成功扱いしない。単一サービス変更は実際の前後状態と厳密な保存ACKで確認する。他playerの状態保持も照合するが、共有森戦闘の正当な共同報酬には別の集計契約を適用する。実UI captureと現在の実行・process・packageの結び付きも検証対象となる。

追加サービス実行は、登録client二回と再login client二回で計四回起動する。元のメインの五回と合わせると九回で、同時clientは最大二つである。各実行は一台のWindows PCのloopback上で、Editor専用server一つと実際にpackageしたclientを使う。元のメインと追加サービス実行のregister/loginは順次server processを使うため、全体のserver process起動は四回である。

### 7. 保存・再起動証跡の限界

登録後に正常退出し、同じDBを使うAPIだけを再起動して、新しいserver・client processで二accountに再loginする。再起動前後の保存profile・wallet・inventory・装備・character summaryと、再login時の所有関係を照合する。この流れは、DB processの再起動や障害復旧を実施したことを意味しない。

遠征結果・再挑戦receiptの再閲覧と復元は、検査した同一game serverの生存期間内でのみ解釈する。API再起動後にprofileが保持されても、遠征結果ledgerがgame server再起動を越えて永続化されるとは主張しない。独立standalone ServerTarget、Linux、外部network、物理PC二台、四候補村のart品質も、この検証範囲には含めない。空中・落下状態での実UI拒否scenarioは実行していない。

### 8. 実行と独立監査の結果

2026-10-26(KST)にjob `20261026T023627036Z-07dfbbc9`の実行が`Passed / exit 0`で完了し、次の三つの独立監査も通過した。元のメイン実行は`20261026-115239-3c89412c`、追加サービス実行は`20261026-115655-043f2ec6`である。

| 項目 | 確認した結果 |
| --- | --- |
| 固定source | 373file(Game 344・Backend 29)、source manifest SHA-256 `a1d29a21fd9dbe7373e04cbda453f5fdfd9ce3843bc33b1c03cc38bb87e83ce7` |
| build・package | .NET backend build・Unreal C++ Editor build・Windows Development packageがすべて成功。package実行ID `20261026-024509-157Z-41b39352`、client実行file SHA-256 `f03fbbda3d6f5257f39d387b4bbfb491791cba51517521af36946fde7077614b` |
| Native | 計72件のうち成功72・警告付き成功0・失敗0・未実行0。サービス2件は5節のmemory fixtureであり、実際のDB証跡とは区別する。 |
| 既存メイン | 元の582件のordered typed条件の意味を維持した独立監査を通過 |
| 保存境界 | 追加durability proof監査を通過。同じDBを使うAPIだけを再起動した保存profile保持の範囲であり、game server再起動を越える遠征ledger保持を意味しない。 |
| 実サービス | 二人それぞれ13label、計26件のserver観測をclient checkpointと照合した独立監査を通過。実際のサービス位置でのUI/RPCと通常の森戦闘の集計を含む。 |

二人は依頼受注・森戦闘・報告・体力ポーション購入・森武器強化の後に正常退出した。同じDBを使うAPIを再起動し、二accountが再loginした後も、それぞれgold 90・鉱石5・体力ポーション4・item 5個が保持された。それぞれの森武器は同じitem UUIDで強化段階`0 → 1`を確認し、品質・オプションなど他のitem fieldは保持された。基礎`power`の増加を別途実証したことは意味しない。

サービス実行の`register/canonical.json`、`api-restart-canonical.json`、`login/canonical.json`は、raw byteのSHA-256がすべて`2c68ff7de73f7b71a47857189e4d1d2f3dbd58863e3f505c6a2b7377d74d9133`で一致する。保存profile・wallet・inventory・装備・character summaryの保持は、この記録と実際の再login比較で確認した。

実client起動は、既存メイン五回と追加サービスの登録二回・再login二回を合わせた計九回である。追加サービスのclient四回は開始・終了時刻から最大同時二つと計算し、すべて強制終了なしで`exitCode=0`となった。既存メイン五回は、従来の順序付きPID記録と二account・同時参加の条件で検証した。既存メインには終了UTC記録がないため、九回全体の時間区間を新たに合算した最大同時実行数は主張しない。

| 独立監査の識別子 | 結果とSHA-256 |
| --- | --- |
| `audit-20261026T023627036Z-07dfbbc9-v002.json` | PASS · `7d25dbed631556fb1239a61b9f1c5b187e81d264a03a7b778153ea429e32ec0e` |
| `durability-proof-20261026T023627036Z-07dfbbc9-v001.json` | PASS · `0ccd907bbc3a84aae06590786cdb4e2057ed0ddfdefc35f1daac0641e922181e` |
| `legacy-services-audit-20261026T023627036Z-07dfbbc9-v001.json` | PASS · `c70723fc40f801a1330d18c1afc4360ba6f65517aaca7a3b693cdd1a7f19c8f3` |

Gitへの委託は、次の日付・機能単位に分けて依頼する。この表はupload完了の記録ではない。

| 順序 | 作業日(KST) | sourceのみの委託単位 |
| --- | --- | --- |
| 1 | 2026-10-25 | 村サービス機能22file |
| 2 | 2026-10-26 | QA compileの名前衝突修正1file |
| 3 | 2026-10-26 | QA接近移動・診断・制限時間の修正1file |
| 4 | 2026-10-26 | 検証結果と同じ内容の韓国語・日本語を反映した本書1file |

raw JSON・log・screenshot・build成果物・DB・秘密設定・監査とproofのreceipt・source一覧はGit対象に含めず、外部検証資料として保存する。本書は実検証完了後に整理した2026-10-26の文書変更である。上記の実行は固定source manifestの373fileを検証したもので、この最終文書の置換を含む新しいbuild・play検証を主張しない。四候補村の未準備・未採用状態と、7節の検証範囲の限界も維持する。
