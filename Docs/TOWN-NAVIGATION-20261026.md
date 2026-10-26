# 마을 목적지와 파티 출정 / 村の目的地とパーティー出発

개발일: 2026-10-26 (KST) / 開発日: 2026-10-26 (KST)

## 한국어

### 목적과 기준

기존 이야기 목표·길안내·미니맵과 파티 원정 준비 기능을 현재 마을·층·논리 앵커에 연결한다. 기존 기능의 표시 순서, 의뢰 조건, 서버 판정과 원정 결과 저장을 유지한다. 개발 기준은 Game `66152451db36dc93fcf4307536c1c4958911f359`, Backend `b0e4cee15450db61ba48a5a368f66cef7b4d3de1`이다.

길안내는 카탈로그를 읽는 표시 기능이다. 화면에 목적지가 표시되어도 이동·서비스 이용·저장 권한을 얻지 않는다. 현재 마을의 서비스 앵커와 같은 층일 때만 목적지를 표시하고, 준비되지 않은 지형·누락 서비스·다른 층에서는 기존 마을 좌표를 대체 목적지로 사용하지 않는다. 사설 원정의 전투·구조·귀환 목표는 소유 원정의 공간 변환을 유지한다.

### 서버의 출정 검사

서버는 파티의 현재 연결·소유 pawn·생존·저장·의뢰·현재 준비 리비전을 확인하고, 일반 출정 참가자가 현재 이용 가능한 같은 마을·층·카탈로그 버전에 있는지 확인한다. 해당 층에는 논리 원정 포털이 있어야 한다. 연결 통로를 독립적인 출정 층으로 인정하지 않는다. 파티장의 포털 근처 상호작용 조건과 파티원의 같은 마을·층 조건은 각각 유지한다.

입장 증서 발급과 실제 입장 직전에는 같은 공간 검사 경로를 사용한다. 완료된 소유 사설 원정 안의 재도전은 기존 참가자·원정·완료 결과 조건을 통과할 때만 기존 예외를 사용한다. 같은 완료 원정의 일부 참가자가 마을에 먼저 돌아왔더라도 재도전 명부는 유지하지만, 마을에 있는 모든 참가자는 현재 연결과 같은 마을·포털 층을 다시 검사한다. 전원 도착에 실패했을 때의 원래 위치 복원과 준비 무효화, 취소·접속 종료·재접속의 기존 명부 및 저장 규칙도 유지한다.

### 검증과 Git 경계

기존 Native 72개와 `party-results` 메인의 582개 ordered typed 조건을 회귀 기준으로 유지하고, 새 길안내·출정 공간 조건은 별도 검사와 실제 관찰로 구분한다. 메모리 월드의 다른 층·다른 마을·미준비 지형 검사는 실제 신규 마을 플레이의 증거가 아니다. 실제 빌드·플레이·독립 감사 결과는 완료 후 정확한 실행 범위와 함께 기록한다. 이 문서의 초기 작성 시점에는 새 실행이 완료되지 않았다.

추가 Native 대상은 다음 7개다. 아래 목록은 실행 결과와 구분한다.

- `Fablewood.Native.TownNavigation.LegacyLogicalDestinations`
- `Fablewood.Native.TownNavigation.UnreadyMissingAndInvalid`
- `Fablewood.Native.TownNavigation.CurrentFloorAndTransform`
- `Fablewood.Native.Story.CurrentTownNavigationContext`
- `Fablewood.Native.Expedition.DepartureResolvesLiveTownAndPortalFloor`
- `Fablewood.Native.Expedition.DepartureRejectsDifferentSpatialContexts`
- `Fablewood.Native.Expedition.DepartureCancellationAndReconnectRemainAtomic`

`Sunbrook`, `Starbough`, `Snowbloom`, `Eldergrove`는 계속 `geometryReady=false`, `adoptedCandidate=null`이다. 이 변경은 아트 채택, 층간 경로, 신규 NPC 배치 또는 새 마을 활성화를 수행하지 않는다.

Git에는 제품 소스·필수 작성 에셋·유지하는 테스트와 도구·한국어/일본어 문서를 넣는다. 빌드 산출물·캐시·DB·실제 비밀 설정·로그·스크린샷·일회성 검증 어댑터·감사/인계 영수증은 외부 검증 자료로 보존한다. 실제 개발일과 기능별로 동결한 소스를 사용해 의미 있는 반영 단위를 구분한다.

## 日本語

### 目的と基準

既存の物語目標・道案内・ミニマップとパーティー遠征準備を、現在の村・階・論理アンカーへ接続する。既存機能の表示優先順位、依頼条件、サーバー判定と遠征結果の保存を維持する。開発基準はGame `66152451db36dc93fcf4307536c1c4958911f359`、Backend `b0e4cee15450db61ba48a5a368f66cef7b4d3de1`である。

道案内はカタログを読む表示機能である。画面に目的地が表示されても、移動・サービス利用・保存の権限は得られない。現在の村のサービスアンカーと同じ階にいるときだけ目的地を表示し、未準備の地形・欠落サービス・別の階では旧村座標を代替目的地にしない。専用遠征内の戦闘・救助・帰還目標は、所有遠征の空間変換を維持する。

### サーバーの出発検査

サーバーはパーティーの現在接続・所有pawn・生存・保存・依頼・現在の準備revisionを確認し、通常出発の参加者が現在利用できる同じ村・階・カタログ版にいるかを確認する。その階には論理遠征ポータルが必要である。接続通路を独立した出発階として認めない。リーダーのポータル近傍の操作条件と、メンバーの同じ村・階の条件をそれぞれ維持する。

入場証書の発行と実入場の直前に同じ空間検査経路を使う。完了済みの所有専用遠征内での再挑戦は、既存の参加者・遠征・完了結果の条件を満たす場合だけ従来の例外を使う。同じ完了遠征の一部参加者が先に村へ戻っていても再挑戦名簿を維持するが、村にいる全参加者は現在接続と同じ村・ポータル階を再検査する。全員到着が失敗した場合の元の位置への復元と準備の無効化、取消・切断・再接続の既存名簿と保存規則も維持する。

### 検証とGitの境界

既存Native 72件と`party-results`メインの582件のordered typed条件を回帰基準として維持し、新しい道案内・出発空間条件は個別検査と実観察を区別する。メモリーワールドによる別階・別村・未準備地形の検査は、新しい村の実プレイの証跡ではない。実build・play・独立監査の結果は、完了後に正確な実行範囲とともに記録する。この文書の初稿作成時点では新しい実行は完了していない。

追加Nativeの対象は次の7件である。以下の一覧と実行結果を区別する。

- `Fablewood.Native.TownNavigation.LegacyLogicalDestinations`
- `Fablewood.Native.TownNavigation.UnreadyMissingAndInvalid`
- `Fablewood.Native.TownNavigation.CurrentFloorAndTransform`
- `Fablewood.Native.Story.CurrentTownNavigationContext`
- `Fablewood.Native.Expedition.DepartureResolvesLiveTownAndPortalFloor`
- `Fablewood.Native.Expedition.DepartureRejectsDifferentSpatialContexts`
- `Fablewood.Native.Expedition.DepartureCancellationAndReconnectRemainAtomic`

`Sunbrook`、`Starbough`、`Snowbloom`、`Eldergrove`は引き続き`geometryReady=false`、`adoptedCandidate=null`である。この変更はart採用、階間経路、新NPC配置や新しい村の有効化を行わない。

Gitには製品source・必須の作成asset・維持するtestとtool・韓国語/日本語文書を含める。build成果物・cache・DB・実際の秘密設定・log・screenshot・一時検証adapter・監査/引継ぎreceiptは外部の検証資料として保存する。実際の開発日と機能ごとに固定したsourceを使い、意味のある反映単位を区別する。

## 실제 검증 완료 기록 / 実検証完了記録

검증일: 2026-10-26 · 문서 보완일: 2026-10-26 (KST)

検証日: 2026-10-26 · 文書補完日: 2026-10-26 (KST)

### 한국어

job `20261026T103406589Z-3153597e` · source manifest `5a3fe98810ee5a24ec8394ac2cb527e9fe527c554a66852ce4411feb8340b297` · main `20261026-195146-39a98db9` · services `20261026-195607-3d91f8c3`

- 고정 실행 소스는 총 378개(Game 349·Backend 29)다. Windows Development 패키지 생성, Native 79개 전부 성공, 경고 동반 성공·실패·미실행·진행 중 0개를 확인했다. 기존 ordered typed 조건 582개도 의미·타입·순서를 유지한 채 통과했다.
- 한 Windows PC의 loopback에서 Unreal Editor 전용 서버 프로세스 1개와 실제 패키지 클라이언트 2개로 검증했다. 메인 5회 시작은 인과 순서와 두 참가자 조건으로, 별도 서비스 4회 시작은 시각 기록과 최대 동시 2개로 대조했다. 합계 9회 시작 전체의 종료 시각 타임라인을 검증했다고 주장하지 않는다.
- 서비스 서버 관찰 26개, 길안내 클라이언트 관찰 26개·서버 관찰 24개, 출정 판정 5개·파티 공간 표본 13개를 기존 네 감사와 별도 동일 참가자 식별 감사로 대조했다. 클라이언트와 서버 관찰의 `playerKey`를 같은 역할의 참가자에 연결했다. 현재 마을·층·포털 출정과 실제 전원 입장, 소유 완료 원정 재도전, 부분 연결 종료·같은 캐릭터 복귀를 연결해 확인했다. 서비스 비용·변경 및 재로그인·API 재시작 뒤 저장 보존은 별도 서비스 감사에서 확인했다.
- 다른 마을·층·미준비 지형 및 귀환 참가자의 재도전 경계 반례는 Native 메모리 fixture 범위다. Linux, cooked ServerTarget 실행 파일, 외부망, 신규 마을 실제 플레이·지형·아트 채택은 이번 검증 범위에 없다. 네 후보 마을의 `geometryReady=false`, `adoptedCandidate=null`은 유지한다.
- 위 실행은 원래 고정 문서를 포함한 소스를 검증했다. 이 완료 기록은 검증 후 문서 한 파일만 보완한 변경이며, 보완 문서로 새 빌드·플레이를 다시 했다는 뜻이 아니다. 제품 소스·필수 작성 에셋·유지 테스트/도구·KR/JA 문서만 Git 대상으로 삼고 실행 산출물·DB·비밀·로그·일회성 검증 및 인계 자료는 제외한다.

### 日本語

job `20261026T103406589Z-3153597e` · source manifest `5a3fe98810ee5a24ec8394ac2cb527e9fe527c554a66852ce4411feb8340b297` · main `20261026-195146-39a98db9` · services `20261026-195607-3d91f8c3`

- 固定した実行sourceは計378件(Game 349・Backend 29)である。Windows Development packageの生成、Native 79件すべて成功、警告付き成功・失敗・未実行・進行中0件を確認した。既存のordered typed条件582件も意味・型・順序を維持して通過した。
- 一台のWindows PCのloopbackで、Unreal Editor専用server process一つと実package client二つを使って検証した。メインの5回の起動は因果順序と二参加者条件で、別サービスの4回の起動は時刻記録と最大同時二つで照合した。合計9回すべての終了時刻timelineを検証したとは主張しない。
- サービスserver観察26件、道案内client観察26件・server観察24件、出発判定5件・party空間sample13件を既存の四監査と別の同一参加者識別監査で照合した。clientとserverの観察にある`playerKey`を同じroleの参加者へ結び付けた。現在の村・階・portalからの出発と実際の全員入場、所有する完了遠征の再挑戦、部分切断・同じcharacterの復帰を結び付けて確認した。サービスの費用・変更と、再login・API再起動後の保存保持は別のサービス監査で確認した。
- 別の村・階・未準備地形および帰還参加者の再挑戦境界の反例は、Nativeのmemory fixture範囲である。Linux、cooked ServerTarget実行ファイル、外部network、新しい村の実play・地形・art採用は今回の検証範囲に含まれない。四候補村の`geometryReady=false`、`adoptedCandidate=null`を維持する。
- 上記実行は元の固定文書を含むsourceを検証した。この完了記録は検証後に文書一ファイルだけを補完した変更であり、補完文書を含む新しいbuild・playを再実行した意味ではない。製品source・必須の作成asset・維持するtest/tool・KR/JA文書だけをGit対象とし、実行成果物・DB・秘密・log・一時検証および引継ぎ資料は除外する。
