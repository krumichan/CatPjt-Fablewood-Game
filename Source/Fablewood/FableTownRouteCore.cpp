#include "FableTownRouteCore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
    constexpr std::size_t CoreRouteMaximumConnections = 128;
    constexpr std::size_t CoreRouteMaximumPoints = 4096;
    constexpr std::size_t CoreRouteMaximumIdentifierBytes = 4096;

    /** 공개 조회의 정렬·순회를 유한하게 제한한다. / 公開照会の整列・探索量を有限にする。 */
    bool CoreRouteIdentifierValid(const std::string& Value)
    {
        return !Value.empty() && Value.size() <= CoreRouteMaximumIdentifierBytes &&
               std::none_of(Value.begin(), Value.end(),
                            [](unsigned char Character)
                            {
                                return Character < 32 || Character == 127;
                            });
    }

    /** NaN·무한 좌표를 거리 계산 전에 거절한다. / NaN・無限座標を計算前に拒否する。 */
    bool CoreRoutePointFinite(const FableTownRouteCore::Point& Value)
    {
        return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
    }

    /** 큰 유한 성분의 제곱 오버플로를 피한다. / 大きい有限成分の二乗オーバーフローを避ける。 */
    double CoreRouteDistance(const FableTownRouteCore::Point& First, const FableTownRouteCore::Point& Second)
    {
        return std::hypot(First.X - Second.X, First.Y - Second.Y, First.Z - Second.Z);
    }

    /** 같은 거리의 기존 후보를 통로 식별자·방향으로 정렬한다. / 同距離候補を通路ID・方向で整列する。 */
    bool CoreRouteTraceLess(const std::vector<FableTownRouteCore::Traversal>& First,
                            const std::vector<FableTownRouteCore::Traversal>& Second)
    {
        return std::lexicographical_compare(
            First.begin(), First.end(), Second.begin(), Second.end(),
            [](const FableTownRouteCore::Traversal& A, const FableTownRouteCore::Traversal& B)
            {
                return A.ConnectorId < B.ConnectorId || (A.ConnectorId == B.ConnectorId && A.Reverse < B.Reverse);
            });
    }

    /** 원래 입력을 바꾸지 않는 연결점 자료. / 元入力を変更しない接続点の資料。 */
    struct CoreRouteNode
    {
        std::string Floor;
        FableTownRouteCore::Point Position;
        std::size_t Connection = CoreRouteMaximumConnections;
        bool From = false;
    };

    /** 방문이 끝난 정점은 다시 갱신하지 않아 0거리 층내 순환을 막는다. / 確定済み頂点を再更新しない。 */
    struct CoreRouteLabel
    {
        double Distance = std::numeric_limits<double>::infinity();
        std::vector<FableTownRouteCore::Traversal> Trace;
        bool Settled = false;
    };
}

/** 같은 층도 전체 입력을 먼저 검사하며 실패 결과에는 유효한 경로처럼 보이는 값이 남지 않는다. */
FableTownRouteCore::Result FableTownRouteCore::Find(const Request& Input)
{
    Result Failure;
    if (!CoreRouteIdentifierValid(Input.StartFloor) || !CoreRouteIdentifierValid(Input.GoalFloor) ||
        !CoreRoutePointFinite(Input.Start) || !CoreRoutePointFinite(Input.Goal) ||
        Input.Connections.size() > CoreRouteMaximumConnections)
    {
        return Failure;
    }

    // 정본 통로 전체를 검증하고 호출 순서와 무관한 탐색 순서를 만든다. / 全通路を検証し入力順に依存しない探索順を作る。
    std::set<std::string> Identifiers;
    std::vector<const Connection*> Connections;
    std::size_t PointCount = 0;
    for (const Connection& Candidate : Input.Connections)
    {
        if (!CoreRouteIdentifierValid(Candidate.Id) || !CoreRouteIdentifierValid(Candidate.FromFloor) ||
            !CoreRouteIdentifierValid(Candidate.ToFloor) || Candidate.FromFloor == Candidate.ToFloor ||
            !Identifiers.insert(Candidate.Id).second || Candidate.Points.size() < 2 ||
            Candidate.Points.size() > CoreRouteMaximumPoints - PointCount)
        {
            return Failure;
        }

        PointCount += Candidate.Points.size();
        for (std::size_t Index = 0; Index < Candidate.Points.size(); ++Index)
        {
            if (!CoreRoutePointFinite(Candidate.Points[Index]))
            {
                return Failure;
            }
            if (Index > 0)
            {
                const double Segment = CoreRouteDistance(Candidate.Points[Index - 1], Candidate.Points[Index]);
                if (!std::isfinite(Segment) || Segment <= 0)
                {
                    return Failure;
                }
            }
        }
        Connections.push_back(&Candidate);
    }
    std::sort(Connections.begin(), Connections.end(),
              [](const Connection* A, const Connection* B)
              {
                  return A->Id < B->Id;
              });

    // 未使用の通路でも合計長のオーバーフローを拒否する。 / 사용하지 않는 통로도 합계 길이를 먼저 검사한다.
    std::vector<double> ConnectionLengths;
    for (const Connection* Candidate : Connections)
    {
        double Length = 0;
        for (std::size_t Index = 1; Index < Candidate->Points.size(); ++Index)
        {
            Length += CoreRouteDistance(Candidate->Points[Index - 1], Candidate->Points[Index]);
        }
        if (!std::isfinite(Length))
        {
            return Failure;
        }
        ConnectionLengths.push_back(Length);
    }

    // 같은 층의 직선 안내는 기존 동작을 보존한다. / 同じ階は従来どおり目的地点を返す。
    if (Input.StartFloor == Input.GoalFloor)
    {
        const double Distance = CoreRouteDistance(Input.Start, Input.Goal);
        if (!std::isfinite(Distance))
        {
            return Failure;
        }
        Result Direct;
        Direct.State = Status::Ready;
        Direct.NextEntry = Input.Goal;
        Direct.TotalDistance = Distance;
        return Direct;
    }

    // 연결점 사이의 같은 층 접근 비용과 통로의 모든 선분 비용을 구분한다. / 階内接近費用と通路全線分費用を分ける。
    std::vector<CoreRouteNode> Nodes;
    Nodes.push_back({Input.StartFloor, Input.Start, CoreRouteMaximumConnections, false});
    for (std::size_t Index = 0; Index < Connections.size(); ++Index)
    {
        const Connection& Candidate = *Connections[Index];
        Nodes.push_back({Candidate.FromFloor, Candidate.Points.front(), Index, true});
        Nodes.push_back({Candidate.ToFloor, Candidate.Points.back(), Index, false});
    }
    const std::size_t GoalIndex = Nodes.size();
    Nodes.push_back({Input.GoalFloor, Input.Goal, CoreRouteMaximumConnections, false});

    for (std::size_t A = 0; A < Nodes.size(); ++A)
    {
        for (std::size_t B = A + 1; B < Nodes.size(); ++B)
        {
            if (Nodes[A].Floor == Nodes[B].Floor &&
                !std::isfinite(CoreRouteDistance(Nodes[A].Position, Nodes[B].Position)))
            {
                return Failure;
            }
        }
    }

    // 최단 거리 라벨을 확정하며 같은 비용만 식별자 순으로 비교한다. / 最短距離を確定し同費用候補だけID順で比較する。
    std::vector<CoreRouteLabel> Labels(Nodes.size());
    Labels[0].Distance = 0;
    bool OverflowObserved = false;
    for (std::size_t Iteration = 0; Iteration < Nodes.size(); ++Iteration)
    {
        std::size_t Current = Nodes.size();
        for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
        {
            if (Labels[Index].Settled || !std::isfinite(Labels[Index].Distance))
            {
                continue;
            }
            if (Current == Nodes.size() || Labels[Index].Distance < Labels[Current].Distance ||
                (Labels[Index].Distance == Labels[Current].Distance &&
                 CoreRouteTraceLess(Labels[Index].Trace, Labels[Current].Trace)))
            {
                Current = Index;
            }
        }
        if (Current == Nodes.size())
        {
            break;
        }
        if (Current == GoalIndex)
        {
            Result Found;
            Found.State = Status::Ready;
            Found.Traversals = Labels[Current].Trace;
            Found.TotalDistance = Labels[Current].Distance;
            const Traversal& First = Found.Traversals.front();
            const auto Entry = std::find_if(Connections.begin(), Connections.end(),
                                            [&First](const Connection* Candidate)
                                            {
                                                return Candidate->Id == First.ConnectorId;
                                            });
            Found.NextEntry = First.Reverse ? (*Entry)->Points.back() : (*Entry)->Points.front();
            return Found;
        }
        Labels[Current].Settled = true;

        const auto Relax = [&](std::size_t Next, double Cost, const Traversal* Step)
        {
            if (Labels[Next].Settled)
            {
                return;
            }
            const double Distance = Labels[Current].Distance + Cost;
            if (!std::isfinite(Distance))
            {
                OverflowObserved = true;
                return;
            }
            if (Distance > Labels[Next].Distance)
            {
                return;
            }

            auto Trace = Labels[Current].Trace;
            if (Step)
            {
                Trace.push_back(*Step);
            }
            if (Distance < Labels[Next].Distance || CoreRouteTraceLess(Trace, Labels[Next].Trace))
            {
                Labels[Next].Distance = Distance;
                Labels[Next].Trace = std::move(Trace);
            }
        };

        for (std::size_t Next = 0; Next < Nodes.size(); ++Next)
        {
            if (Next != Current && Nodes[Next].Floor == Nodes[Current].Floor)
            {
                Relax(Next, CoreRouteDistance(Nodes[Current].Position, Nodes[Next].Position), nullptr);
            }
        }
        const CoreRouteNode& Node = Nodes[Current];
        if (Node.Connection < Connections.size() && (Node.From || Connections[Node.Connection]->Bidirectional))
        {
            const std::size_t Next = Node.From ? Current + 1 : Current - 1;
            const Traversal Step{Connections[Node.Connection]->Id, !Node.From};
            Relax(Next, ConnectionLengths[Node.Connection], &Step);
        }
    }

    Failure.State = OverflowObserved ? Status::InvalidInput : Status::Unreachable;
    return Failure;
}
