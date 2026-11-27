#include "BYMYardResidents.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMYardResidentsTests
{
    using namespace BYMYardResidents;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMResidentRosterTest, "BackyardMMO.Presentation.ResidentRoster",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMResidentRosterTest::RunTest(const FString& Parameters)
    {
        TMap<FString, int32> Army;
        Army.Add(TEXT("C1"), 1000); Army.Add(TEXT("C2"), 2); Army.Add(TEXT("C3"), 1);
        Army.Add(TEXT("C4"), 1000); Army.Add(TEXT("C5"), 0); Army.Add(TEXT("C6"), -4); Army.Add(TEXT(""), 100);
        const auto Roster = SelectRoster(Army, 1000);
        TestEqual(TEXT("A larger setting cannot exceed the hard fifty-resident limit"), Roster.Num(), 50);
        TMap<FString, int32> Counts;
        TSet<FString> Ids;
        for (const FResident& Resident : Roster)
        {
            ++Counts.FindOrAdd(Resident.Type);
            TestFalse(TEXT("Resident identity is unique within its yard"), Ids.Contains(Resident.Id));
            Ids.Add(Resident.Id);
            TestTrue(TEXT("Every ordinal represents a real ready monster"), Resident.Ordinal >= 0 && Resident.Ordinal < Army.FindRef(Resident.Type));
        }
        TestEqual(TEXT("A scarce species is not hidden behind a thousand Pokeys"), Counts.FindRef(TEXT("C3")), 1);
        TestEqual(TEXT("A two-monster species contributes exactly its owned count"), Counts.FindRef(TEXT("C2")), 2);
        TestTrue(TEXT("Large owned populations share the remaining display slots"), FMath::Abs(Counts.FindRef(TEXT("C1")) - Counts.FindRef(TEXT("C4"))) <= 1);
        TestFalse(TEXT("Zero ready count never creates a cosmetic resident"), Counts.Contains(TEXT("C5")));
        TestFalse(TEXT("Invalid negative count never creates a cosmetic resident"), Counts.Contains(TEXT("C6")));
        TestFalse(TEXT("Empty monster identities are excluded"), Counts.Contains(TEXT("")));
        TestEqual(TEXT("Disabling residents produces an empty roster"), SelectRoster(Army, 0).Num(), 0);
        TestEqual(TEXT("Negative limits are safely disabled"), SelectRoster(Army, -10).Num(), 0);
        const auto FirstFour = SelectRoster(Army, 4);
        TestEqual(TEXT("Each species gets a turn before additional copies"), FirstFour.Num(), 4);
        TSet<FString> FirstSpecies;
        for (const FResident& Resident : FirstFour) FirstSpecies.Add(Resident.Type);
        TestEqual(TEXT("Four available slots show four owned species"), FirstSpecies.Num(), 4);
        const auto Repeated = SelectRoster(Army, 50);
        for (int32 I = 0; I < Roster.Num(); ++I)
            TestEqual(TEXT("Repeated snapshots retain stable resident identities"), Repeated[I].Id, Roster[I].Id);
        TMap<FString, int32> ReorderedArmy;
        ReorderedArmy.Add(TEXT("C4"), 1000); ReorderedArmy.Add(TEXT("C3"), 1);
        ReorderedArmy.Add(TEXT("C2"), 2); ReorderedArmy.Add(TEXT("C1"), 1000);
        const auto Reordered = SelectRoster(ReorderedArmy, 50);
        for (int32 I = 0; I < Roster.Num(); ++I)
            TestEqual(TEXT("Map insertion order cannot change stable resident identities"), Reordered[I].Id, Roster[I].Id);
        TMap<FString, int32> SmallArmy;
        SmallArmy.Add(TEXT("C1"), 2); SmallArmy.Add(TEXT("C2"), 1);
        TestEqual(TEXT("A small army is never padded to fifty monsters"), SelectRoster(SmallArmy, 50).Num(), 3);
        TestEqual(TEXT("No ready army means no residents"), SelectRoster(TMap<FString, int32>(), 50).Num(), 0);
        TestEqual(TEXT("Roster selection leaves authoritative counts untouched"), Army.FindRef(TEXT("C1")), 1000);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMResidentWanderingTest, "BackyardMMO.Presentation.ResidentWandering",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMResidentWanderingTest::RunTest(const FString& Parameters)
    {
        FGrid Grid;
        // A central enclosure with an opening. Direct straight-line wandering
        // would cross its walls; grid paths must route through the opening.
        TArray<FIntRect> Obstacles;
        Obstacles.Add(FIntRect(7, 5, 8, 15));
        Obstacles.Add(FIntRect(12, 5, 13, 15));
        Obstacles.Add(FIntRect(7, 5, 13, 6));
        Obstacles.Add(FIntRect(7, 14, 9, 15));
        Obstacles.Add(FIntRect(11, 14, 13, 15));
        Grid.Reset(20, Obstacles);
        TestFalse(TEXT("Roaming stays clear of the reserved outside edge"), Grid.IsWalkable(FVector2D(.5, 10.5)));
        TestFalse(TEXT("A building cell is not walkable"), Grid.IsWalkable(FVector2D(7.5, 8.5)));
        TArray<FVector2D> Path;
        if (!TestTrue(TEXT("A route into the enclosure uses its real opening"), Grid.FindPath(FVector2D(3.5, 8.5), FVector2D(9.5, 8.5), Path))) return false;
        FVector2D Previous(3.5, 8.5);
        bool bUsedOpening = false;
        for (const FVector2D& Point : Path)
        {
            TestTrue(TEXT("Every path waypoint remains on open terrain"), Grid.IsWalkable(Point));
            TestTrue(TEXT("Paths use adjacent orthogonal cells without cutting corners"),
                FMath::IsNearlyEqual(FMath::Abs(Point.X - Previous.X) + FMath::Abs(Point.Y - Previous.Y), 1.0));
            if (Point.Y == 14.5 && Point.X >= 9.5 && Point.X <= 10.5) bUsedOpening = true;
            Previous = Point;
        }
        TestTrue(TEXT("The path goes through the opening rather than through a wall"), bUsedOpening);

        TArray<FWalker> Walkers;
        Walkers.SetNum(50);
        for (int32 I = 0; I < Walkers.Num(); ++I)
            if (!TestTrue(TEXT("A bounded resident can spawn on free ground"), InitializeWalker(Walkers[I], Grid, 1000 + I))) return false;
        bool bObservedMovement = false, bObservedPause = false;
        for (int32 Tick = 0; Tick < 600; ++Tick)
        {
            for (FWalker& Walker : Walkers)
            {
                const FVector2D Before = Walker.Position;
                TickWalker(Walker, Grid, .1f, 2.f);
                if (!Grid.IsWalkable(Walker.Position)) { AddError(TEXT("A wandering resident entered a building or left the yard.")); return false; }
                if ((Walker.Position - Before).Size() > .20001) { AddError(TEXT("A wandering resident exceeded its frame movement budget.")); return false; }
                bObservedMovement |= Walker.bMoving;
                bObservedPause |= !Walker.bMoving;
            }
        }
        TestTrue(TEXT("Residents actually wander"), bObservedMovement);
        TestTrue(TEXT("Residents pause between routes"), bObservedPause);
        FWalker A, B;
        InitializeWalker(A, Grid, 77); InitializeWalker(B, Grid, 77);
        for (int32 Tick = 0; Tick < 100; ++Tick) { TickWalker(A, Grid, .1f, 2.f); TickWalker(B, Grid, .1f, 2.f); }
        TestTrue(TEXT("A stable yard/resident seed produces deterministic motion"), A.Position.Equals(B.Position, .000001));
        TestTrue(TEXT("A stable seed retains the same facing"), A.Facing.Equals(B.Facing, .000001));

        FWalker InvalidTime;
        InitializeWalker(InvalidTime, Grid, 91); InvalidTime.PauseRemaining = 0;
        const FVector2D BeforeInvalidTime = InvalidTime.Position;
        TickWalker(InvalidTime, Grid, std::numeric_limits<float>::quiet_NaN(), 2.f);
        TickWalker(InvalidTime, Grid, std::numeric_limits<float>::infinity(), 2.f);
        TickWalker(InvalidTime, Grid, -.1f, 2.f);
        TickWalker(InvalidTime, Grid, .1f, std::numeric_limits<float>::quiet_NaN());
        TickWalker(InvalidTime, Grid, .1f, std::numeric_limits<float>::infinity());
        TestTrue(TEXT("Nonfinite time/speed and negative time cannot move a resident"), InvalidTime.Position.Equals(BeforeInvalidTime, 0));
        TestFalse(TEXT("Rejected time steps do not animate walking"), InvalidTime.bMoving);
        TickWalker(InvalidTime, Grid, 10000.f, 10000.f);
        TestTrue(TEXT("A long frame and giant speed cannot exceed the capped movement budget"), (InvalidTime.Position - BeforeInvalidTime).Size() <= .800001);

        // Building placement may invalidate a resident's current cell. The
        // layout update relocates it safely instead of leaving it in a building.
        const int32 BlockX = FMath::FloorToInt(A.Position.X), BlockY = FMath::FloorToInt(A.Position.Y);
        Obstacles.Add(FIntRect(BlockX, BlockY, BlockX + 1, BlockY + 1)); Grid.Reset(20, Obstacles);
        TestTrue(TEXT("A newly covered resident is safely relocated"), RevalidateWalker(A, Grid));
        TestTrue(TEXT("Relocated resident is outside the new footprint"), Grid.IsWalkable(A.Position));
        TestEqual(TEXT("A changed layout invalidates the old route"), A.Path.Num(), 0);

        FGrid Closed;
        Closed.Reset(8, { FIntRect(3, 1, 4, 7) });
        TestFalse(TEXT("Unreachable cells do not create a through-building path"), Closed.FindPath(FVector2D(1.5, 3.5), FVector2D(5.5, 3.5), Path));
        TestEqual(TEXT("A failed route leaves no stale path"), Path.Num(), 0);
        FGrid Full;
        Full.Reset(8, { FIntRect(0, 0, 8, 8) });
        TestFalse(TEXT("A completely occupied yard has no spawn location"), InitializeWalker(B, Full, 1));
        TickWalker(B, Full, .1f, 2.f);
        TestFalse(TEXT("No-free-space state does not invent a walkable position"), B.bInitialized);
        TestFalse(TEXT("An unspawned resident cannot animate movement"), B.bMoving);
        FGrid Pocket;
        Pocket.Reset(3, {});
        TestTrue(TEXT("A single free cell can hold an idle resident"), InitializeWalker(B, Pocket, 1));
        for (int32 Tick = 0; Tick < 100; ++Tick) TickWalker(B, Pocket, .1f, 2.f);
        TestTrue(TEXT("An isolated resident stays in its only free cell"), B.Position.Equals(FVector2D(1.5, 1.5), 0));
        TestFalse(TEXT("An isolated resident has no walking animation"), B.bMoving);
        return true;
    }
}

#endif
