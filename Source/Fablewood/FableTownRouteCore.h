#pragma once

#include <string>
#include <vector>

/** 엔진에 의존하지 않는 논리 경로 계산 자료와 조회. / エンジン非依存の論理経路照会。 */
namespace FableTownRouteCore
{
    /** 정본에서 변환한 유한한 월드 좌표. / 定義から変換した有限なワールド座標。 */
    struct Point
    {
        double X = 0;
        double Y = 0;
        double Z = 0;
    };

    /** 명시한 두 층 사이의 선분 경로이며 자동 이동 권한은 없다. / 二階間の線分経路で自動移動権限ではない。 */
    struct Connection
    {
        std::string Id;
        std::string FromFloor;
        std::string ToFloor;
        bool Bidirectional = true;
        std::vector<Point> Points;
    };

    /** 층 이름의 존재·기하 준비 상태는 호출 어댑터가 확인한다. / 階名と地形準備は呼出側で確認する。 */
    struct Request
    {
        std::string StartFloor;
        std::string GoalFloor;
        Point Start;
        Point Goal;
        std::vector<Connection> Connections;
    };

    /** 실패와 연결 없음은 부분 경로를 반환하지 않는다. / 失敗・未接続時は部分経路を返さない。 */
    enum class Status
    {
        Ready,
        InvalidInput,
        Unreachable
    };

    /** 정본 통로의 진행 방향을 함께 보존한다. / 定義通路の進行方向を保持する。 */
    struct Traversal
    {
        std::string ConnectorId;
        bool Reverse = false;
    };

    /** 거리는 논리 선분 비용이며 충돌 없는 보행 경로의 증명이 아니다. / 距離は論理費用で歩行可能性の証明ではない。 */
    struct Result
    {
        Status State = Status::InvalidInput;
        std::vector<Traversal> Traversals;
        Point NextEntry;
        double TotalDistance = 0;
    };

    /** 층 내 직선·통로 전체 길이·목적지 접근을 합산한다. 동일 비용은 안정적인 식별자 순으로 선택한다. */
    Result Find(const Request& Input);
}
