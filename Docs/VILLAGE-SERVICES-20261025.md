# 2026-10-25 마을 서비스 기반 / 村サービス基盤

## 한국어

상태는 분리 복사본의 소스 구현입니다. 이 단위의 빌드·Native 실행·실제 클라이언트 플레이·패키징·Git 공개를 수행하지 않았습니다. 선행 Unit3의 최종 검증·공개 소스 수정분을 먼저 병합한 뒤 이 단위의 고정 입력 검증을 순서대로 진행해야 합니다.

기준선은 stage8 `parallel-development-start-20261025.json`에 기록된 368개 소스 복사본이며, stage7 `SourceManifest.json` SHA-256은 `54ff62c39e8914ee63547d045f7ae2f8976b61a161f9d2707db2f71dfd860f52`입니다. 선행 Unit3는 이 분리본 생성 당시 미검증·미공개였습니다.

### 논리 API

| 서비스 ID | 기존 서버 앵커 | 기존 대화 |
| --- | --- | --- |
| `QuestGuide` | `Service.Board` | 퀘스트 안내 |
| `Workshop` | `Service.Forge` | 공방 |
| `Merchant` | `Service.Merchant` | 상점 |
| `ExpeditionPortal` | `Portal.Expedition` | 원정 준비 |

`FFableTownServiceRef`는 마을·서비스 ID·카탈로그 버전만 지정합니다. `UFableTownCatalog::ResolveService`와 단일 정의용 `ResolveDefinitionService`는 해당 서버 앵커의 첫 슬롯·층·기존 반경에서 `FFableTownServiceDescriptor`를 만듭니다. 알 수 없는 ID, 버전 불일치, 누락 앵커·지원 태그와 미준비 지형을 기본 경로에서 거절합니다. 읽기 전용 미준비 조회 결과도 상호작용에 사용할 수 없습니다. 새 NPC 모델이나 후보 마을의 서비스 XYZ를 만들지 않습니다.

`AFableGameMode::CanUseTownService(Requester, ServiceRef, OutService)`는 현재 서버 정본과 실제 현재 연결의 nonce·세대·월드 명부를 대조합니다. controller, profile, pawn의 정확한 소유 관계, 프로필 준비·저장 차단·생존·사설 원정·진행 중 이동 상태를 검사합니다. 저장된 presence 대신 현재 소유 pawn에서 town·floor·catalog readiness를 다시 읽습니다. 반경은 수평 330cm 이하, 높이 차이는 300cm 미만이며 비유한 좌표·범위를 거절합니다. 연결부는 서비스 층으로 간주하지 않습니다.

마지막으로 `IsGroundedAtCurrentTownAnchor`의 새 실제 floor/capsule query를 사용해 walking, 지원 component tag, authored 높이, walkable 바닥과 capsule 비충돌을 확인합니다. 조회는 안전 귀환지·체크포인트·저장 상태를 변경하지 않습니다. 서비스 앵커는 기존 `Entry` 전용 durable 허용목록에 들어가지 않습니다.

기존 E 키 대상 선택과 네 서비스의 대화 종류는 유지하고, 해당 네 대화를 열기 직전에 새 서버 검사를 연결했습니다. 봉인·이야기·던전 귀환 선택은 기존 경로를 사용합니다. 기존 좌표형 `CanUseTownService`와 그 기존 호출자는 유지했으므로 모든 아이템·퀘스트·원정 변경 RPC가 새 API로 이전되었다고 주장하지 않습니다. 이후 서비스별 기능 이전은 같은 서버 검사를 각 동작 시점에 다시 적용해야 합니다.

Sunbrook·Starbough·Snowbloom·Eldergrove는 계속 `geometryReady=false`이며 Entry/Return/Recovery 세 앵커만 가집니다. Sunbrook bootstrap 채택, Content·배치·아트·층 연결·스트리밍 계약은 변경하지 않았습니다. 새 마을 서비스 활성화에는 최종 아트·cook·층·component tag·실제 클라이언트 승인이 필요합니다.

### 정적 확인과 실행 경계

- 기존 69개 Automation identity와 기존 모든 `*Tests.cpp`는 기준선과 동일하게 보존했습니다. 별도 서비스 테스트 파일에 2개 identity를 추가해 소스상 총 71개이며 중복은 없습니다.
- 새 테스트는 catalog/unknown/version/미준비/다른 마을·층·연결부/유한 반경/Entry 허용목록과 현재 소유자·현 월드 명부·부유·낙하·잘못된 태그·잘못된 높이의 실제 엔진 query용 메모리 fixture를 다룹니다. 합성 상자 검사는 실제 ForestClearing 맵 로드·네트워크·UI 성공과 구분합니다.
- 변경한 C++ 8개에 설치 `clang-format 22.1.3`의 `-i`와 `--dry-run --Werror`를 적용했습니다. Allman, 함수·논리 그룹 사이 빈 줄, 한국어/일본어 주석을 사용했습니다.
- 기준선 manifest와 SHA-256 비교에서 기존 Game 소스 339개 중 333개가 동일하고 6개만 변경되었으며 누락은 없습니다. 기존 TownTravelTests·Character·coordinator·streaming·profile schema·bootstrap 구현은 동일합니다. Backend·공유 runner·Content와 고정 검증 입력은 수정하지 않았습니다.
- UHT/C++ 빌드, 기존69개와 새2개 Native 실행, 실제 E 대화·거래·원정 플레이, 두 클라이언트·서버 재시작, cook/패키징, 공용 실행·감사와 Git 작업은 이 단위에서 수행하지 않았습니다. 정적 확인을 런타임 통과로 해석하지 않습니다.

## 日本語

分離コピー上のソース実装です。先行Unit3の最終検証・公開済み修正を先に取り込み、その後に本単位の固定入力で順番に検証します。本単位ではビルド、Native実行、実クライアント、cook、パッケージ、Git公開を行っていません。

QuestGuide、Workshop、Merchant、ExpeditionPortalを明示IDとし、村・catalog版を持つ参照から既存Board/Forge/Merchant/Portalアンカーを解決します。新しいNPC配置やXYZは作りません。未知ID、版不一致、未定義アンカー、未準備geometryを拒否し、読み取り専用の未準備記述も操作の許可にしません。

サーバー入口は現在のcontroller・profile・pawn、接続nonce・世代・現world名簿を照合し、実pawnの村・階・catalog readinessを再照会します。有限の水平330cm・高さ300cm未満、歩行、実際の支持component tagと高さ、walkable floor、新しいcapsule queryを必須にします。Eの既存4会話をこの検査へ接続し、他の選択と既存座標型の呼出元は維持しました。全変更RPCの移行完了は主張しません。

サービス参照は安全な保存先を更新せず、durable許可はEntry専用のままです。四候補は未準備・未採用で到着用3アンカーだけを保持し、Sunbrook bootstrapを変更しません。新村の有効化には最終アート、cook、階、component tag、実クライアントの承認が必要です。

既存69 identityと全既存テストファイルを保持し、専用ファイルへ2 identityを追加しました。合成物理床で所有権・範囲・階・浮遊・落下・異なるtag/高さを検査するコードですが、まだ実行していません。変更C++ 8ファイルのclang-format22.1.3検査と基準線SHA比較のみ実施しました。静的確認を実行成功の証拠として扱いません。


## Unit3 병합 후 소스 상태 / Unit3統合後のソース状態

2026-10-25: 최종 Unit3 freeze `runtime-20261025T105454893999Z/input`을 old54ff 기준의 3-way로 분리 개발본에 병합했습니다. 새 parent manifest SHA-256은 `9577eab3c10d7f94d217c9beaddcf4a3955df80e925416df899fe2cb4ccf7cad`입니다. PC.cpp 중첩은 GNU diff3의 충돌 없는 실제 3-way 결과를 사용했습니다. 기존 서비스 API·네 E 대화 gate·전용 테스트 2개와 이 문서는 보존했습니다.

병합 후 identity는 parent 70개와 서비스 2개를 합한 72개입니다. 위의 최초 71개 기록은 병합 전 정적 확인의 이력입니다. Backend 29개는 old/new parent와 분리본 사이 SHA-256이 같습니다. 경로별 SHA와 3-way 입력·결과, 포맷 검사 결과는 stage8의 별도 merge receipt에 기록합니다.

parent Unit3의 새 FIFO job `20261025T105756454Z-6cd1b80e`는 이 병합 시점에 제출·검증 대기 상태이며 PASS나 Git 공개를 확인한 상태가 아닙니다. 분리 서비스 개발본은 빌드·Native·클라이언트·cook·패키징·공용 큐를 실행하지 않았고 Git 작업도 하지 않았습니다. 이 병합을 모듈 검증·승인으로 해석하지 않습니다.

日本語: 最終Unit3 freezeを旧54ffとの実3-wayで統合し、PC.cppの競合はありません。サービス変更と2テストを保持し、統合後はparent70+service2の72 identityです。Backend29は不変です。parentの新FIFOは提出後の検証待ちで、PASS・Git公開を確認していません。この分離本のビルド、Native、実クライアント、cook、パッケージ、公用キュー、Gitは未実施です。


## 단계1 변경 동작 이관 / 段階1の変更操作移行

2026-10-25: 기존 좌표형 `AtTownService` 호출을 논리 ID로 이관했습니다. 직업·스킬·특성·의뢰·이야기 보고는 QuestGuide, 강화·제작·분해·재설정·수리·창고는 Workshop, 구매·판매는 Merchant, 마을의 원정 입장·마을 재도전은 ExpeditionPortal을 변경 시점에 현재 소유 pawn에서 다시 검사합니다. GameMode의 게시판 숲 참여/재도전도 논리 QuestGuide를 사용합니다. 전용 서버 helper가 실제 town·catalog 참조를 만들고 기존 강한 연결·소유권·층·지원 바닥 검사를 그대로 통과합니다.

`ResolveNearbyService`는 현재 town/catalog/floor의 유한 거리만 확인하는 순수 표시·선택용 API입니다. E의 네 마을 후보와 특성 UI가 이를 사용합니다. 클라이언트 GameMode 존재를 요구하지 않으며, 대화를 이미 열었거나 UI 힌트가 참이어도 실제 변경 함수에서 별도의 서버 검사를 통과해야 합니다. 공방의 비용·보유량·작업 사유 조회는 순수 데이터 의미를 유지합니다.

기존 실제 원정 참가 증서가 있는 사람의 실제 마을 portal 재입장만 해당 encounter의 참가자·귀환 중 아님을 추가로 확인해 허용합니다. 그 경우도 현재 연결·소유권·실제 town/floor/지원 바닥을 생략하지 않습니다. 가짜 GUID, 다른 서비스, 사설 공간의 같은 XY는 이 예외를 얻지 못합니다. 완료 원정 내부 retry의 기존 예외와 PartyEncounter의 MoonSeal/region 귀환 좌표 구현은 변경하지 않았습니다.

기존 Party/Town/Talent/Story fixture 4개 파일은 실제 서비스 양성 대조에 필요한 Meadow(WorldStatic, top Z0), walking, 캡슐 높이와 현재 연결 등록을 갖추도록 보강했습니다. 순수 명부·story 데이터 fixture는 불필요한 물리 변경을 피했습니다. 기존 identity를 유지하며 Talent의 수평330 실제 변경 허용과 순수 높이299 허용/300 거절을 분리했습니다. 강한 guard를 fixture 편의로 낮추지 않았습니다.

서비스의 기존 두 Native identity 안에 실제 구매·제작·강화·판매의 정확 비용과 결과, 부유 구매 금지, 다른 서비스에서 의뢰 거절, 실제 QuestGuide의 의뢰/직업 변경, 뒤늦은 부유 상태 재검사와 가짜 portal 증서 거절을 추가했습니다. parent70+service2의 총72 identity를 유지하며 모든 실행은 아직 수행하지 않았습니다.

parent234는 .NET 빌드 뒤 C++ JSON 내부 map 타입 불일치로 실패했고 Native·패키징·UI·DB 검증은 실행되지 않았습니다. root의 `Wallet->TryGetField`/shared-pointer 정확 수정 파일을 SHA 대조로 동기화했으며, 숫자 타입·값 엄격 비교를 유지했습니다. 이 개발본의 빌드·Native·실제 클라이언트·cook·패키징·공용 큐·Git은 미실행입니다. 정적 확인과 독립 소스 검토를 runtime PASS로 해석하지 않습니다.

다음 단위로 남긴 범위는 기존 Legacy 기반 내비게이션/이야기 목적지 표시와 파티 준비·전원 입장의 공간 정책 일반화입니다. 새 마을의 서비스 앵커·배치·지지면·층·cook·실제 플레이가 승인되기 전에 그 기능이나 네 후보를 활성화하지 않습니다. 현재 네 후보는 계속 geometryReady=false이며 서비스 XYZ/Content를 추가하지 않았습니다.

日本語: 実変更のQuestGuide/Workshop/Merchant/ExpeditionPortal検査を現所有pawnの論理IDへ移行しました。Eと特性UIは純粋catalog範囲照会を使い、実変更で別途サーバー所有権・接続・実床を再検査します。実遠征参加証のportal復帰だけを現参加者かつ帰還中でない場合に保持し、private retry・MoonSeal・region帰還座標は維持します。既存fixture4ファイルに必要な実支持床と所有接続を補い、72 identityを保持しました。parent234はC++で失敗した状態で、そのpublic JSON API修正のみ正確に同期しました。この分離本の実行・キュー・Gitは未実施で、四候補も未準備・未採用です。
