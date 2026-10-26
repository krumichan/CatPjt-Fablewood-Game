# 안전 귀환지 V10 — 2026-10-25 개발

캐릭터의 안전 귀환지를 TownId·AnchorId·CatalogVersion으로 저장한다. 위치 좌표, 원정 UUID, 접속 nonce와 이동 증서는 영구 저장하지 않는다. 네 후보 마을의 지형은 여전히 미채택 상태이며 새 캐릭터와 구형 프로필의 기본값은 LegacyVillage.Entry/catalog1이다.

원본 bytes/hash와 원래 버전의 wallet·inventory·summary를 먼저 검증하고 별도 후보를 V10으로 이전한다. UUID 도입 경계는9, inventory/summary/index wire는1로 유지한다. 유효 V9의 기존 필드·아이템 순서·UUID·옵션·내구도·절대 물약 UTC를 보존한다. 로드한 원본 V9와 후보에서 새 참조 세 필드와 버전만 제외한 V9 역투영을 동일한 현재 serializer로 비교하며, 기존 저장 필드가 하나라도 다르면 DB 이전 쓰기 전에 차단한다. 원본 raw bytes/hash는 별도로 그대로 보존한다. 엔진 changelist/custom-version 헤더 차이는 기존 게임 필드 변경으로 취급하지 않으며, 현재 엔진이 로드할 수 없는 과거 포맷까지 호환된다고 주장하지 않는다. 유효한 기존10은 새 기본 정책으로 덮어쓰지 않는다.

정확한 V9/inventory1/명시적 summary:null만 원본 bytes 그대로 요약 확인 저장을 수행한다. 실제 ACK와 lease 반환 이후 새 ticket/claim으로 재접속하고 그때 V10 이전을 시작한다. 누락·잘못된 mirror·현재10의 null은 보정하지 않는다. 일반 저장 대기열은10만 기록하며 Backend DTO/SQL은 변경하지 않는다.

실제 이동 receipt에 대응하는 snapshot hash·소유 연결·이동/앵커 순번을 전송 전에 등록한다. 정확한 Sent ACK 또는 이미 확인된 동일 bytes와 실제 소유자 도착이 모두 확인돼야 source 지형과 입력을 해제한다. 이미 공개된 이동이나 결과가 불확실한 DB 저장을 과거 데이터로 되돌리지 않는다. 실제 도착은 원래 5초 안에 세 표본으로 확인하고, 도착을 유지하는 동안에는 저장 ACK가 5초 넘게 늦어도 같은 증서를 기다린다. 도착을 잃으면 원래 물리 기한을 다시 검사하며 저장 ACK만으로 입력을 풀지 않는다.

로그인은 실제 기존 원정 증서를 우선하며 저장 귀환지는 서버 전용 복원 경로로 지형·유한 slot·실제 capsule/floor·클라이언트 도착을 확인한다. 알려진 미준비 마을은 저장 참조를 보존하고 복원을 보류한다. 접속 전 schema10/catalog1/protocol1의 정확한 단일 옵션을 검사한다.

로컬 파일은 최초 이전 전에 원본 해시 이름의 backup을 쓰고 다시 읽어 확인한다. 현재10 기록도 raw readback을 확인한다. SaveDataToSlot의 crash atomicity는 보장하지 않는다. 실패한 현재 파일을 새 캐릭터로 덮어쓰지 않으며 기존 continue index 형식1을 유지한다.

## 検証範囲

Nativeのmemory/独立slot fixtureは実際のDB移行や遊ばれたV9移行を証明しない。既存582条件を維持した新規ビルド・実2クライアント・保存/再login監査と、実際の旧V9データを使う別証拠を区別する。実行前にPASSとは記録しない。新マップ・外部ネットワーク・Linux・ゲームサーバー再起動後の結果台帳永続化は未検証である。
