#include "BYMWorkerMovement.h"
#include "BYMYardResidents.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMWorkerMovementTests
{
    using namespace BYMWorkerMovement;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMWorkerRouteClockTest, "BackyardMMO.Workers.AuthoritativeRouteClock",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMWorkerRouteClockTest::RunTest(const FString& Parameters)
    {
        TArray<FRoutePoint> Route; double Duration = -1.;
        const FVector2D Start(.5, .5), Target(10.5, .5);
        if (!TestTrue(TEXT("Open-ground worker route is available"), PlanRoute(Start, Target, FIntRect(0, 0, 50, 40), {}, Route, Duration))) return false;
        TestTrue(TEXT("Worker route contains start and arrival"), Route.Num() >= 2);
        TestTrue(TEXT("Worker starts exactly at its persisted position"), Route[0].Position.Equals(Start, .000001));
        TestTrue(TEXT("Worker arrives exactly at the task target"), Route.Last().Position.Equals(Target, .000001));
        const double Distance = FVector2D(10., 5.).Size() * 20.;
        TestTrue(TEXT("Travel integrates source acceleration and task speed in screen pixels"), FMath::IsNearlyEqual(Duration, 1. + (Distance - 40.) / 80., .000001));
        double LastTime = -1.;
        for (const FRoutePoint& P : Route)
        {
            TestTrue(TEXT("Route deadlines are finite and strictly ordered"), FMath::IsFinite(P.AtSeconds) && P.AtSeconds > LastTime);
            TestFalse(TEXT("Open-ground route does not invent hops"), P.bHop); LastTime = P.AtSeconds;
        }
        const FSample Before = SampleRoute(Route, -1.);
        TestTrue(TEXT("Clock rollback clamps to the original route start"), Before.Position.Equals(Start, .000001));
        TestFalse(TEXT("A new travelling worker has not arrived"), Before.bArrived);
        for (int32 I = 0; I < 100; ++I)
        {
            const FSample Sample = SampleRoute(Route, Duration * I / 100.);
            TestTrue(TEXT("Every pre-arrival sample stays finite and on the route"), FMath::IsFinite(Sample.Position.X) && FMath::IsFinite(Sample.Position.Y)
                && Sample.Position.X >= Start.X && Sample.Position.X <= Target.X && FMath::IsNearlyEqual(Sample.Position.Y, .5, .000001));
            TestFalse(TEXT("Arrival is not reported before its authoritative boundary"), Sample.bArrived);
        }
        TestTrue(TEXT("Exact route boundary reports arrival"), SampleRoute(Route, Duration).bArrived);
        TestTrue(TEXT("Late frames retain the exact final point"), SampleRoute(Route, Duration + 500.).Position.Equals(Target, .000001));
        TestTrue(TEXT("Nonfinite presentation time falls back to a finite initial state"), SampleRoute(Route, std::numeric_limits<double>::quiet_NaN()).Position.Equals(Start, .000001));
        if (!TestTrue(TEXT("An already present worker has a valid zero-length route"), PlanRoute(Target, Target, FIntRect(0, 0, 50, 40), {}, Route, Duration))) return false;
        TestEqual(TEXT("Already at task has no fake travel delay"), Duration, 0.);
        TestTrue(TEXT("Zero-distance task is immediately arrived"), SampleRoute(Route, 0.).bArrived);
        const FVector2D Subpixel(Target.X + .0002, Target.Y);
        TestTrue(TEXT("Subpixel destinations still produce an exact persistent route"), PlanRoute(Target, Subpixel, FIntRect(0, 0, 50, 40), {}, Route, Duration));
        TestTrue(TEXT("Small final offsets cannot be dropped from the saved destination"), Route.Last().Position.Equals(Subpixel, .00000001));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMWorkerObstaclesTest, "BackyardMMO.Workers.SourceObstacleHopsAndBounds",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMWorkerObstaclesTest::RunTest(const FString& Parameters)
    {
        TArray<FRoutePoint> Route; double Duration = 0.;
        const FIntRect Bounds(-20, -10, 30, 30);
        TArray<FIntRect> Occupied; Occupied.Add(FIntRect(-40, -30, 50, 50));
        if (!TestTrue(TEXT("Source workers can reach tasks even through enclosing structures"), PlanRoute(FVector2D(-10.5, -5.5), FVector2D(20.5, 15.5), Bounds, Occupied, Route, Duration))) return false;
        bool HasHop = false;
        for (const FRoutePoint& P : Route) HasHop |= P.bHop;
        TestTrue(TEXT("Obstacle traversal uses the original hop presentation"), HasHop);
        TestTrue(TEXT("First occupied waypoint begins the source jump"), Route[0].bHop);
        TestTrue(TEXT("The source hop reaches forty pixels after four tenths of a second"), FMath::IsNearlyEqual(SampleRoute(Route, .4).HopHeightPixels, 40., .000001));
        for (int32 I = 0; I <= 200; ++I)
        {
            const FSample S = SampleRoute(Route, Duration * I / 200.);
            TestTrue(TEXT("Hop remains finite and within its authored height"), FMath::IsFinite(S.HopHeightPixels) && S.HopHeightPixels >= -.000001 && S.HopHeightPixels <= 40.000001);
            TestTrue(TEXT("Negative-coordinate yard routes stay in their allowed terrain ring"), S.Position.X >= -40. && S.Position.X < 50. && S.Position.Y >= -30. && S.Position.Y < 50.);
        }
        TestTrue(TEXT("Mushrooms in the source outside-yard ring remain reachable"), PlanRoute(FVector2D(0, 0), FVector2D(-29.5, -19.5), Bounds, {}, Route, Duration));
        TestFalse(TEXT("Beyond the supported border ring is rejected"), PlanRoute(FVector2D(0, 0), FVector2D(-41, 0), Bounds, {}, Route, Duration));
        TestEqual(TEXT("Rejected route cannot leave a stale path"), Route.Num(), 0);
        TestEqual(TEXT("Rejected route cannot leave a stale deadline"), Duration, 0.);
        TestFalse(TEXT("Empty terrain bounds are rejected"), PlanRoute(FVector2D(0, 0), FVector2D(1, 1), FIntRect(0, 0, 0, 40), {}, Route, Duration));
        TestFalse(TEXT("Excessive route grid dimensions are rejected"), PlanRoute(FVector2D(0, 0), FVector2D(1, 1), FIntRect(0, 0, 257, 40), {}, Route, Duration));
        TestFalse(TEXT("Nonfinite task positions are rejected before indexing"), PlanRoute(FVector2D(0, 0), FVector2D(std::numeric_limits<double>::infinity(), 1), Bounds, {}, Route, Duration));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMResidentExpansionBoundsTest, "BackyardMMO.Presentation.ResidentsInExpandedRectangularYards",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMResidentExpansionBoundsTest::RunTest(const FString& Parameters)
    {
        using namespace BYMYardResidents;
        const FIntRect Bounds(-20, -8, 40, 24);
        TArray<FIntRect> Obstacles; Obstacles.Add(FIntRect(-4, -2, 4, 12));
        FGrid Grid; Grid.Reset(Bounds, Obstacles);
        TestFalse(TEXT("Expanded yard retains its left outer margin"), Grid.IsWalkable(FVector2D(-19.5, 4.5)));
        TestFalse(TEXT("Expanded yard retains its top outer margin"), Grid.IsWalkable(FVector2D(0.5, -7.5)));
        TestTrue(TEXT("New negative-coordinate interior is walkable"), Grid.IsWalkable(FVector2D(-18.5, -6.5)));
        TestFalse(TEXT("Rectangular height cannot be mistaken for square width"), Grid.IsWalkable(FVector2D(5.5, 25.5)));
        TestFalse(TEXT("Source building footprint remains blocked after expansion"), Grid.IsWalkable(FVector2D(0.5, 0.5)));
        FWalker Walker;
        if (!TestTrue(TEXT("Resident initializes in an expanded rectangular yard"), InitializeWalker(Walker, Grid, 2026))) return false;
        for (int32 I = 0; I < 800; ++I)
        {
            const FVector2D Before = Walker.Position;
            TickWalker(Walker, Grid, .1f, 2.f);
            TestTrue(TEXT("Expanded-yard resident never enters a building or border"), Grid.IsWalkable(Walker.Position));
            TestTrue(TEXT("Rectangular indexing does not teleport residents"), (Walker.Position - Before).Size() <= .20001);
        }
        return true;
    }
}
#endif
