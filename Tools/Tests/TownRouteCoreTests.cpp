#include "FableTownRouteCore.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace FableTownRouteCore;

    int Cases = 0;
    int Assertions = 0;
    int Failures = 0;
    std::string CaseName;

    /** 동작 경계별로 독립 사례를 기록한다. / 動作境界ごとに独立ケースを記録する。 */
    void BeginCase(const char* Name)
    {
        ++Cases;
        CaseName = Name;
    }

    /** 첫 실패 뒤에도 나머지 독립 조건을 확인한다. / 最初の失敗後も独立条件を確認する。 */
    void Expect(bool Condition, const char* Message)
    {
        ++Assertions;
        if (!Condition)
        {
            ++Failures;
            std::cerr << CaseName << ": " << Message << '\n';
        }
    }

    /** 부동소수점 계산값만 작은 오차 범위로 비교한다. / 浮動小数点の計算値だけ小誤差で比較する。 */
    bool Near(double First, double Second)
    {
        return std::isfinite(First) && std::isfinite(Second) && std::abs(First - Second) < 1e-8;
    }

    /** 실패 결과에 오래된 좌표나 부분 경로가 남지 않아야 한다. / 失敗結果に旧座標・部分経路を残さない。 */
    void ExpectFailure(const Result& Value, Status Expected)
    {
        Expect(Value.State == Expected, "failure category");
        Expect(Value.Traversals.empty(), "no partial traversals");
        Expect(Value.NextEntry.X == 0 && Value.NextEntry.Y == 0 && Value.NextEntry.Z == 0, "cleared entry");
        Expect(Value.TotalDistance == 0, "cleared distance");
    }

    /** 테스트 통로는 수학 입력이며 실제 마을 지형이 아니다. / テスト通路は数学入力で実地形ではない。 */
    Connection Link(std::string Id, std::string From, std::string To, Point Start, Point End, bool Both = true)
    {
        return {Id, From, To, Both, {Start, End}};
    }

    /** 같은 층에는 통로 없이 목적지 자체를 반환한다. / 同じ階は通路なしで目的地を返す。 */
    void DirectRoute()
    {
        BeginCase("same-floor");
        const Request Input{"A", "A", {0, 0, 0}, {3, 4, 0}, {}};
        const Result Value = Find(Input);
        Expect(Value.State == Status::Ready && Value.Traversals.empty(), "direct ready");
        Expect(Near(Value.TotalDistance, 5), "direct length");
        Expect(Value.NextEntry.X == 3 && Value.NextEntry.Y == 4, "actual goal");
    }

    /** 세 층의 접근·통로·출구 비용을 합산하고 역방향도 보존한다. */
    void MultipleFloors()
    {
        BeginCase("multiple-floors");
        Request Input{"A",
                      "C",
                      {-3, 0, 0},
                      {4, 0, 20},
                      {Link("AB", "A", "B", {0, 0, 0}, {0, 0, 10}), Link("BC", "B", "C", {0, 0, 10}, {0, 0, 20})}};
        const Result Forward = Find(Input);
        Expect(Forward.State == Status::Ready && Forward.Traversals.size() == 2, "two connectors");
        if (Forward.Traversals.size() == 2)
        {
            Expect(Forward.Traversals[0].ConnectorId == "AB" && Forward.Traversals[1].ConnectorId == "BC", "order");
            Expect(!Forward.Traversals[0].Reverse && !Forward.Traversals[1].Reverse, "forward flags");
        }
        Expect(Near(Forward.TotalDistance, 27), "complete route length");
        Expect(Forward.NextEntry.Z == 0 && Forward.NextEntry.X == 0, "current floor entry");

        std::swap(Input.StartFloor, Input.GoalFloor);
        std::swap(Input.Start, Input.Goal);
        const Result Back = Find(Input);
        Expect(Back.State == Status::Ready && Back.Traversals.size() == 2, "reverse reachable");
        if (Back.Traversals.size() == 2)
        {
            Expect(Back.Traversals[0].ConnectorId == "BC" && Back.Traversals[0].Reverse, "reverse starts at BC");
        }
        Expect(Back.NextEntry.Z == 20 && Near(Back.TotalDistance, 27), "reverse entry and length");
    }

    /** 일방통행은 반대편에서 안내 가능한 통로로 바뀌지 않는다. / 一方通行を逆向きに案内しない。 */
    void DirectedAndDisconnected()
    {
        BeginCase("direction-and-disconnection");
        Request Input{"A", "B", {}, {0, 0, 10}, {Link("Up", "A", "B", {}, {0, 0, 10}, false)}};
        Expect(Find(Input).State == Status::Ready, "permitted direction");
        std::swap(Input.StartFloor, Input.GoalFloor);
        std::swap(Input.Start, Input.Goal);
        ExpectFailure(Find(Input), Status::Unreachable);
        Input.GoalFloor = "Other";
        ExpectFailure(Find(Input), Status::Unreachable);
    }

    /** 가까운 계단부터 고르는 탐욕 선택 대신 목적지까지의 전체 비용을 사용한다. */
    void CompleteCostWins()
    {
        BeginCase("complete-cost");
        const Request Input{
            "A",
            "B",
            {},
            {100, 0, 100},
            {Link("NearEntry", "A", "B", {}, {0, 0, 100}), Link("BetterTotal", "A", "B", {20, 0, 0}, {100, 0, 100})}};
        const Result Value = Find(Input);
        Expect(Value.State == Status::Ready && Value.Traversals.size() == 1, "one usable connector");
        if (Value.Traversals.size() == 1)
        {
            Expect(Value.Traversals[0].ConnectorId == "BetterTotal", "not nearest-entry greedy");
        }
        Expect(Near(Value.TotalDistance, 20 + std::hypot(80.0, 0.0, 100.0)), "full cost");
        Expect(Value.NextEntry.X == 20, "chosen entry");
    }

    /** 경로 순환과 같은 층의 0거리 연결에도 탐색이 끝나야 한다. */
    void CyclesTerminate()
    {
        BeginCase("cycles");
        Request Input{"A",
                      "C",
                      {},
                      {0, 0, 20},
                      {Link("AB", "A", "B", {}, {0, 0, 10}, false), Link("BA", "B", "A", {0, 0, 10}, {}, false),
                       Link("BC", "B", "C", {0, 0, 10}, {0, 0, 20}, false)}};
        const Result Value = Find(Input);
        Expect(Value.State == Status::Ready && Near(Value.TotalDistance, 20), "cycle does not inflate cost");
        Expect(Value.Traversals.size() == 2, "cycle omitted");
        Input.GoalFloor = "Unconnected";
        ExpectFailure(Find(Input), Status::Unreachable);
    }

    /** 정본 배열을 재정렬해도 동일 비용의 선택과 입력 자체는 바뀌지 않는다. */
    void StableTies()
    {
        BeginCase("stable-ties");
        Request Input{
            "A", "B", {}, {0, 0, 10}, {Link("Z", "A", "B", {}, {0, 0, 10}), Link("A", "A", "B", {}, {0, 0, 10})}};
        for (int Index = 0; Index < 2; ++Index)
        {
            const std::string OriginalFirst = Input.Connections.front().Id;
            const Result Value = Find(Input);
            Expect(Value.State == Status::Ready && Value.Traversals.size() == 1, "tie reachable");
            if (Value.Traversals.size() == 1)
            {
                Expect(Value.Traversals[0].ConnectorId == "A", "lexical ID tie");
            }
            Expect(Input.Connections.front().Id == OriginalFirst, "input not mutated");
            std::reverse(Input.Connections.begin(), Input.Connections.end());
        }
    }

    /** 잘못된 입력은 같은 층의 간편 반환보다 먼저 거절한다. */
    void InvalidInputs()
    {
        BeginCase("invalid-inputs");
        const Request Valid{"A", "A", {}, {1, 0, 0}, {Link("AB", "A", "B", {}, {0, 0, 10})}};
        auto Input = Valid;
        Input.StartFloor.clear();
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Connections.push_back(Input.Connections.front());
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Connections[0].ToFloor = "A";
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Connections[0].Points.resize(1);
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Connections[0].Points[1] = {};
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Connections[0].Id.clear();
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Connections[0].Points[1].X = std::numeric_limits<double>::quiet_NaN();
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Goal.Z = std::numeric_limits<double>::infinity();
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Start.X = -std::numeric_limits<double>::max();
        Input.Goal.X = std::numeric_limits<double>::max();
        ExpectFailure(Find(Input), Status::InvalidInput);
        Input = Valid;
        Input.Connections[0].Points = {{0, 0, 0}, {1e308, 0, 0}, {0, 0, 1}};
        ExpectFailure(Find(Input), Status::InvalidInput);
    }

    /** 유효한 최대 연결 수는 지원하고 그 이상의 입력은 잘라 쓰지 않는다. */
    void QueryBounds()
    {
        BeginCase("bounded-query");
        Request Input{"F0", "F128", {}, {0, 0, 1280}, {}};
        for (int Index = 0; Index < 128; ++Index)
        {
            Input.Connections.push_back(Link("L" + std::to_string(Index), "F" + std::to_string(Index),
                                             "F" + std::to_string(Index + 1), {0, 0, Index * 10.0},
                                             {0, 0, (Index + 1) * 10.0}, false));
        }
        const Result Value = Find(Input);
        Expect(Value.State == Status::Ready && Value.Traversals.size() == 128, "maximum supported route");
        Expect(Near(Value.TotalDistance, 1280), "maximum route length");
        Input.Connections.push_back(Link("TooMany", "F128", "F129", {0, 0, 1280}, {0, 0, 1290}));
        ExpectFailure(Find(Input), Status::InvalidInput);

        Input = {"A", "B", {}, {0, 0, 4096}, {Link("Long", "A", "B", {}, {0, 0, 1})}};
        Input.Connections[0].Points.clear();
        for (int Index = 0; Index < 4097; ++Index)
        {
            Input.Connections[0].Points.push_back({0, 0, double(Index)});
        }
        ExpectFailure(Find(Input), Status::InvalidInput);
    }

    /** 회전·이동한 좌표에서도 통로 선택과 거리를 보존한다. */
    void RigidTransform()
    {
        BeginCase("rigid-transform");
        Request Input{"A",
                      "C",
                      {-3, 0, 0},
                      {4, 0, 20},
                      {Link("AB", "A", "B", {}, {0, 0, 10}), Link("BC", "B", "C", {0, 0, 10}, {0, 0, 20})}};
        const auto Transform = [](Point Value)
        {
            return Point{-Value.Y + 1000, Value.X - 500, Value.Z + 200};
        };
        Input.Start = Transform(Input.Start);
        Input.Goal = Transform(Input.Goal);
        for (Connection& Candidate : Input.Connections)
        {
            for (Point& Value : Candidate.Points)
            {
                Value = Transform(Value);
            }
        }
        const Result Value = Find(Input);
        Expect(Value.State == Status::Ready && Value.Traversals.size() == 2, "transformed route");
        Expect(Near(Value.TotalDistance, 27), "rotation preserves length");
        Expect(Value.NextEntry.X == 1000 && Value.NextEntry.Y == -500 && Value.NextEntry.Z == 200,
               "world entry transformed once");
    }
}

/** 실제 제품 코어를 연결한 로컬 실행 검사이며 Unreal·네트워크 실행을 대신하지 않는다. */
int main()
{
    DirectRoute();
    MultipleFloors();
    DirectedAndDisconnected();
    CompleteCostWins();
    CyclesTerminate();
    StableTies();
    InvalidInputs();
    QueryBounds();
    RigidTransform();

    std::cout << "{\"status\":\"" << (Failures == 0 ? "passed" : "failed") << "\",\"cases\":" << Cases
              << ",\"assertions\":" << Assertions << ",\"failures\":" << Failures
              << ",\"scope\":\"portable-product-core-only\"}\n";
    return Failures == 0 ? 0 : 1;
}
