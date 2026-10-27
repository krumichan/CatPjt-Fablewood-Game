# 전망대 계단 이동·2인 플레이 검증 결과

작업일: 2026-10-27 KST. 실행: actual369 / `20261027T011032166Z-8277d4fe`.
실제 실행 소스는 Game371+Backend29=400파일이며 SHA256은 `dcb7c91abc47ec4b12aaaf9bc6d4e2a4cce9de9ae30e5882f44d8332ad0c3425`이다. 전송 입력402는 이 소스400과 메타데이터2개다.

## 확인한 내용

- C++ 빌드·Windows 패키징 및 Native95/95 통과. 실패·경고·미실행은 모두0이다.
- 기존582 검증의 의미, 마을 서비스26, 파티 준비 취소 회귀를 보존했고 독립 감사5개가 모두 통과했다.
- 장비 옵션·품질·등급·내구도·골드 등 전체 프로필의 안전 이탈/API 재시작/재접속 전후 지속성을 확인했다.
- 두 클라이언트가 정상 이동 입력 코드로 실제 계단을 왕복했다. register8 milestone×2명=16개, login의 안전 Entry 복귀1개×2명=2개를 서버 표본과 대조했고 해당 PNG18개를 보존했다.
- 재접속은 이전 전망대 XYZ를 그대로 복원하는 정책이 아니라 안전한 Ground Entry로 복귀하는 정책이다.
- 온라인 패키지 클라이언트 시작은 main5+서비스4+계단4=13회, 최대 동시2개다. `UnrealEditor-Cmd.exe -server`의 register/login6단계는 순차 실행했다. 별도 HUD fixture2프로세스는 이13회에 포함하지 않는다.
- 독립 HUD fixture는 900×600·1280×800에서 각4개, 총8PNG다. Root가 직접 본 원본은 fixture8+실제 네트워크 Upper2/Entry2=12PNG이며 네트워크18PNG 전부를 직접 육안 검토한 것은 아니다.

## 증거와 감사 보정

아래 증거는 `.codex-workspace/verification/shared/fablewood-floor-traversal-20261027`에 별도 보관한다. 로그·사진·DB·감사 도구 자체는 Git 소스에 포함하지 않는다.

| 감사 | 증거 파일 | SHA256 |
| --- | --- | --- |
| main | `actual369-main-runtime-audit-v005-20261027.json` | `17cc984a6fc5ac65a4a3745edc718f3bd9637cab9677aa23f9c2f83ccd951d91` |
| durability | `actual369-durability-proof-20261027.json` | `548a24817b18110aaae9f65ec74cdd9b633a8966c443f729fc0b1e3d45a228a9` |
| services | `actual369-services-audit-20261027.json` | `41482f0f1d3d611a6405878c3a8357c36bf7f433b6d1543a5046f6aa1655d7bb` |
| cancellation | `actual369-party-ready-cancellation-audit-20261027.json` | `1c96147ca4b3b49cdc7e18b3213e109967a8512014b244df148204719a081f8a` |
| floorWalk | `actual369-floor-walk-audit-repaired-20261027.json` | `a8a4f8dcba79ae38f59d3888f9e9e531e8a103414624585381dddb92ab4d7627` |

시각 검토: `actual369-root-visual-review-20261027.json`, SHA256 `600098d327c827c52e995cc2c18480787a266e80c6bb3bdf4d53c7e0282d3c09`.

실패365(컴파일), 366(기존 합성 fixture), 368(마지막 PNG 저장 순서)의 원본 기록을 보존했다. raw 계단 감사의 최초 보고서 `actual369-floor-walk-audit-20261027.json`(`95922da4…`)도 덮어쓰지 않았다. 이 감사는 실제 서버의 `UnrealEditor-Cmd.exe`를 `UnrealEditor.exe`로 잘못 가정했다. Root 승인(`15ef68bd…`)을 받은 외부 감사 subclass(`7ed558d0…`)에서 그 basename 조건1개를 수정하고 실제 실행의 전체 경로 일치 조건을 추가했다. 원 게임·동결 입력·원 감사 도구·기존 감사4개는 변경하거나 재실행하지 않았다. 수정 감사가 통과했다는 사실은 새로운 게임 실행을 뜻하지 않는다.

## Git 범위와 한계

권장 커밋은 지형·fixture10파일 → 이동/재접속 검증 하네스4파일 → 이 문서1파일이다. 중간 source399만 별도로 런타임 실행한 것은 아니며 최종 source400으로 검증했다. Backend29와 기존 OBJ/UAsset68쌍은 그대로다. 이 문서 추가 후 Git 선택 소스401은 문서1개가 늘어난 집합이며 실제 게임 실행 입력400과 구분한다.

`Tools/Test-TownRouteUiRender.ps1` 한 파일의 실행 원본 CRLF는 보존하고 Git에서는 LF 정규화를 명시적으로 허용한다. after raw SHA256은 `087e1cfaaea4107a6ba21919f63224f677a3fe272cf8fb8dfc4803acae346ad1`, Git LF SHA256은 `7f09efee2bf3261f31c1b4669507cb1789ca5fb8467b22fbf5fd6917a86bc646`이다.

이번 결과는 한 PC의 Editor 전용 서버와 패키지 클라이언트2개 범위다. 진짜 cooked ServerTarget, Editor 없는 전용 서버, 물리 PC2대, 새 마을 도입은 검증하지 않았다. 정상 입력 코드·서버 궤적·HUD/peer 상태를 대조했으며 별도 실제 키보드 이벤트 계측이나 HUD 글자 픽셀의 자동 의미 판정은 주장하지 않는다.

---

# 展望台の階段移動・2人プレイの検証結果（日本語）

作業日: 2026-10-27 KST。実行: actual369 / `20261027T011032166Z-8277d4fe`。
実際に実行したソースはGame371+Backend29=400ファイルで、SHA256は`dcb7c91abc47ec4b12aaaf9bc6d4e2a4cce9de9ae30e5882f44d8332ad0c3425`である。転送入力402は、このソース400とメタデータ2個で構成する。

## 確認した内容

- C++ビルド、Windowsパッケージング、Native95/95が合格した。失敗・警告・未実行はいずれも0である。
- 既存582検証の意味、町のサービス26、パーティー準備キャンセルの回帰検証を維持し、独立監査5件がすべて合格した。
- 装備オプション・品質・等級・耐久度・ゴールドなど、プロファイル全体について、安全な退出/API再起動/再接続の前後で永続性を確認した。
- 2つのクライアントが通常の移動入力コードで実際の階段を往復した。registerの8 milestone×2人=16個、loginの安全なEntry復帰1個×2人=2個をサーバー標本と照合し、対応するPNG18個を保存した。
- 再接続時は以前の展望台XYZをそのまま復元する方針ではなく、安全なGround Entryに戻る方針である。
- オンラインのパッケージクライアント起動はmain5+サービス4+階段4=13回、最大同時数は2個である。`UnrealEditor-Cmd.exe -server`のregister/login6段階を順次実行した。別途実行するHUD fixtureの2プロセスは、この13回に含めない。
- 独立HUD fixtureは900×600・1280×800で各4個、合計8PNGである。Rootが直接見た原本はfixture8+実際のネットワークUpper2/Entry2=12PNGであり、ネットワークの18PNGすべてを直接目視確認したわけではない。

## 証拠と監査の補正

以下の証拠は`.codex-workspace/verification/shared/fablewood-floor-traversal-20261027`に別途保管する。ログ・画像・DB・監査ツール自体はGitソースに含めない。

| 監査 | 証拠ファイル | SHA256 |
| --- | --- | --- |
| main | `actual369-main-runtime-audit-v005-20261027.json` | `17cc984a6fc5ac65a4a3745edc718f3bd9637cab9677aa23f9c2f83ccd951d91` |
| durability | `actual369-durability-proof-20261027.json` | `548a24817b18110aaae9f65ec74cdd9b633a8966c443f729fc0b1e3d45a228a9` |
| services | `actual369-services-audit-20261027.json` | `41482f0f1d3d611a6405878c3a8357c36bf7f433b6d1543a5046f6aa1655d7bb` |
| cancellation | `actual369-party-ready-cancellation-audit-20261027.json` | `1c96147ca4b3b49cdc7e18b3213e109967a8512014b244df148204719a081f8a` |
| floorWalk | `actual369-floor-walk-audit-repaired-20261027.json` | `a8a4f8dcba79ae38f59d3888f9e9e531e8a103414624585381dddb92ab4d7627` |

目視確認: `actual369-root-visual-review-20261027.json`、SHA256 `600098d327c827c52e995cc2c18480787a266e80c6bb3bdf4d53c7e0282d3c09`。

失敗365（コンパイル）、366（既存の合成fixture）、368（最後のPNGの保存順序）の原本記録を保存した。raw階段監査の最初の報告書`actual369-floor-walk-audit-20261027.json`（`95922da4…`）も上書きしていない。この監査は実際のサーバーの`UnrealEditor-Cmd.exe`を`UnrealEditor.exe`と誤って想定していた。Root承認（`15ef68bd…`）を受けた外部監査subclass（`7ed558d0…`）で、そのbasename条件1個を修正し、実際に実行したファイルの完全パス一致条件を追加した。元のゲーム・凍結入力・元の監査ツール・既存の監査4件は変更も再実行もしていない。修正した監査が合格したことは、新たにゲームを実行したことを意味しない。

## Gitの対象範囲と制限

推奨コミットは地形・fixture10ファイル → 移動/再接続検証ハーネス4ファイル → この文書1ファイルの順である。中間のsource399だけを別途ランタイム実行したわけではなく、最終source400で検証した。Backend29と既存のOBJ/UAsset68組はそのままである。この文書追加後のGit選択ソース401は、文書1個を追加した集合であり、実際のゲーム実行入力400とは区別する。

`Tools/Test-TownRouteUiRender.ps1`の1ファイルは、実行した原本のCRLFを維持し、GitではLF正規化を明示的に許可する。after raw SHA256は`087e1cfaaea4107a6ba21919f63224f677a3fe272cf8fb8dfc4803acae346ad1`、Git LF SHA256は`7f09efee2bf3261f31c1b4669507cb1789ca5fb8467b22fbf5fd6917a86bc646`である。

今回の結果は、1台のPC上のEditor専用サーバーとパッケージクライアント2個の範囲である。正式なcooked ServerTarget、Editorを使わない専用サーバー、物理PC2台、新しい町の導入は検証していない。通常入力コード・サーバー軌跡・HUD/peer状態を照合したが、別途の実キーボードイベント計測やHUD文字ピクセルの自動的な意味判定は主張しない。
