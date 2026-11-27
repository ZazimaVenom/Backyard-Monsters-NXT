#include "BYMMapRoomRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMMapRoomRulesTests
{
    using namespace BYMMapRoomRules;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapHexTopologyTest, "BackyardMMO.MapRoom.OriginalHexTopologyAndBounds",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapHexTopologyTest::RunTest(const FString& Parameters)
    {
        TestEqual(TEXT("MR3 retains its source width"), Width, 500);
        TestEqual(TEXT("MR3 retains its source height"), Height, 500);
        for (const FIntPoint P : {FIntPoint(0, 0), FIntPoint(499, 0), FIntPoint(0, 499), FIntPoint(499, 499)})
            TestTrue(TEXT("Each source map corner is playable"), IsValidCell(P));
        for (const FIntPoint P : {FIntPoint(-1, 0), FIntPoint(0, -1), FIntPoint(500, 0), FIntPoint(0, 500), FIntPoint(MIN_int32, MAX_int32)})
            TestFalse(TEXT("Cells outside the source map are rejected"), IsValidCell(P));

        // A breadth-first walk over the independently stated source neighbor
        // graph validates the production cube-distance formula, including
        // negative row parity and world edges. No production Neighbors call
        // is used to build this reference graph.
        const int32 Offsets[2][6][2] = {
            {{1, 0}, {-1, 0}, {0, -1}, {-1, -1}, {0, 1}, {-1, 1}},
            {{1, 0}, {-1, 0}, {1, -1}, {0, -1}, {1, 1}, {0, 1}}
        };
        for (const FIntPoint Origin : {FIntPoint(0, 0), FIntPoint(17, 23), FIntPoint(-3, -9), FIntPoint(499, 499)})
        {
            int32 Distances[41][41];
            for (int32 Y = 0; Y < 41; ++Y) for (int32 X = 0; X < 41; ++X) Distances[Y][X] = -1;
            TArray<FIntPoint> Queue; Queue.Add(FIntPoint(20, 20)); Distances[20][20] = 0;
            for (int32 Index = 0; Index < Queue.Num(); ++Index)
            {
                const FIntPoint P = Queue[Index];
                const int32 Parity = (Origin.Y + P.Y - 20) & 1;
                for (int32 Edge = 0; Edge < 6; ++Edge)
                {
                    const int32 X = P.X + Offsets[Parity][Edge][0], Y = P.Y + Offsets[Parity][Edge][1];
                    if (X >= 0 && X < 41 && Y >= 0 && Y < 41 && Distances[Y][X] < 0)
                    { Distances[Y][X] = Distances[P.Y][P.X] + 1; Queue.Add(FIntPoint(X, Y)); }
                }
            }
            for (int32 Y = 8; Y <= 32; ++Y) for (int32 X = 8; X <= 32; ++X)
            {
                const FIntPoint Destination(Origin.X + X - 20, Origin.Y + Y - 20);
                TestEqual(TEXT("Hex range agrees with independent shortest-path distance"), HexDistance(Origin, Destination), Distances[Y][X]);
                TestEqual(TEXT("Hex distance is symmetric"), HexDistance(Destination, Origin), Distances[Y][X]);
            }
            const TArray<FIntPoint> Adjacent = Neighbors(Origin);
            TestEqual(TEXT("Every ordinary cell has six source neighbors"), Adjacent.Num(), 6);
            for (int32 Edge = 0; Edge < Adjacent.Num(); ++Edge)
            {
                TestEqual(TEXT("Neighbor X follows the original odd-row offset table"), Adjacent[Edge].X, Origin.X + Offsets[Origin.Y & 1][Edge][0]);
                TestEqual(TEXT("Neighbor Y follows the original source edge order"), Adjacent[Edge].Y, Origin.Y + Offsets[Origin.Y & 1][Edge][1]);
                TestEqual(TEXT("Every adjacent cell is exactly one hex away"), HexDistance(Origin, Adjacent[Edge]), 1);
            }
        }
        TestEqual(TEXT("Extreme coordinates saturate without signed overflow"), HexDistance(FIntPoint(MIN_int32, MIN_int32), FIntPoint(MAX_int32, MAX_int32)), MAX_int32);
        TestEqual(TEXT("Extreme adjacent coordinates preserve distance one"), HexDistance(FIntPoint(MAX_int32, MAX_int32), FIntPoint(MAX_int32 - 1, MAX_int32)), 1);
        for (const FIntPoint Corner : {FIntPoint(MIN_int32, MIN_int32), FIntPoint(MAX_int32, MAX_int32), FIntPoint(MIN_int32, MAX_int32), FIntPoint(MAX_int32, MIN_int32)})
        {
            const TArray<FIntPoint> Safe = Neighbors(Corner);
            TestTrue(TEXT("Integer-boundary neighborhoods omit overflowing coordinates"), Safe.Num() >= 2 && Safe.Num() <= 3);
            for (const FIntPoint Neighbor : Safe)
                TestEqual(TEXT("Every retained extreme neighbor remains adjacent"), HexDistance(Corner, Neighbor), 1);
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapSourceBonusesTest, "BackyardMMO.MapRoom.SourceRangesProductionCapacityAndCombatBonuses",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapSourceBonusesTest::RunTest(const FString& Parameters)
    {
        const int64 Rates[] = {1, 2, 5, 11, 23};
        const int64 Capacities[] = {175000, 350000, 750000, 2000000, 4000000};
        for (int32 Index = 0; Index < 5; ++Index)
        {
            const int32 Level = (Index + 1) * 10;
            TestEqual(TEXT("Resource outpost radius follows its source level"), Range(Resource, Level), Index + 2);
            TestEqual(TEXT("Resource outpost production uses original per-second units"), ResourceRate(Level), Rates[Index]);
            TestEqual(TEXT("Resource outpost adds the original capacity"), ResourceCapacity(Level), Capacities[Index]);
        }
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const int32 Level = 30 + Index * 10;
            TestEqual(TEXT("Strongholds provide the source ten/fifteen/twenty cell ranges"), Range(Stronghold, Level), 10 + Index * 5);
            TestEqual(TEXT("Stronghold monster and tower bonus uses source percentages"), StrongholdBonus(Level), 10 + Index * 10);
        }
        for (const int32 Level : {MIN_int32, -1, 0, 9, 11, 49, 51, MAX_int32})
        {
            TestEqual(TEXT("Unknown resource levels grant no fabricated production"), ResourceRate(Level), static_cast<int64>(0));
            TestEqual(TEXT("Unknown resource levels grant no capacity"), ResourceCapacity(Level), static_cast<int64>(0));
            TestEqual(TEXT("Unknown structure levels grant no range"), Range(Resource, Level), 0);
            TestEqual(TEXT("Unknown stronghold levels grant no damage bonus"), StrongholdBonus(Level), 0);
        }
        TestEqual(TEXT("Main backyards use the source ten-cell radius"), Range(Player, 0), 10);
        TestEqual(TEXT("Fortifications do not independently expand attack range"), Range(Fortification, 50), 0);
        TestEqual(TEXT("Wild tribe camps do not grant owned expansion range"), Range(Tribe, 50), 0);
        const int32 Reductions[] = {0, 7, 13, 23, 35, 50, 75};
        for (int32 Defenders = 0; Defenders <= 6; ++Defenders)
            TestEqual(TEXT("Defender count uses the original reduction table"), DamageReduction(Defenders), Reductions[Defenders]);
        TestEqual(TEXT("Negative defender counts cannot increase incoming damage"), DamageReduction(-1), 0);
        TestEqual(TEXT("Oversized defender counts cannot exceed the six-neighbor cap"), DamageReduction(MAX_int32), 75);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapAttackPriceAndCaptureTest, "BackyardMMO.MapRoom.SourceAttackPricesAndCaptureThreshold",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapAttackPriceAndCaptureTest::RunTest(const FString& Parameters)
    {
        const int64 Factors[] = {0, 100, 200, 400, 800, 1600, 4000, 10000, 25000, 75000, 225000};
        for (int32 Level = 0; Level <= 10; ++Level)
        {
            TestEqual(TEXT("Targets within owned range cost no attack surcharge"), AttackCost(0, Level), static_cast<int64>(0));
            TestEqual(TEXT("Negative range excess cannot pay the attacker"), AttackCost(-50, Level), static_cast<int64>(0));
            for (int32 Distance = 1; Distance < 600; Distance += 7)
                TestEqual(TEXT("Per-resource attack charge follows source Town Hall factor"), AttackCost(Distance, Level), static_cast<int64>(Distance) * Factors[Level]);
        }
        TestEqual(TEXT("Large attack cost remains within int64 without overflow"), AttackCost(MAX_int32, 10), static_cast<int64>(MAX_int32) * 225000);
        TestEqual(TEXT("Excess Town Hall levels cannot index beyond the source table"), AttackCost(7, MAX_int32), static_cast<int64>(7) * 225000);
        TestEqual(TEXT("A healthy structure has zero capture damage"), CapturePercent(100., 100.), 0.);
        TestEqual(TEXT("Killing half the structure is not a Map Room capture"), CapturePercent(50., 100.), 50.);
        TestEqual(TEXT("Damage just below ninety percent remains below capture"), CapturePercent(10.000001, 100.), 89.);
        TestEqual(TEXT("Exactly ninety percent reaches the original capture threshold"), CapturePercent(10., 100.), 90.);
        TestEqual(TEXT("A completely destroyed structure reports one hundred percent"), CapturePercent(0., 100.), 100.);
        TestEqual(TEXT("Negative remaining HP is clamped to destruction"), CapturePercent(-1., 100.), 100.);
        TestEqual(TEXT("Over-healing cannot produce negative damage"), CapturePercent(101., 100.), 0.);
        TestEqual(TEXT("Missing maximum HP cannot manufacture a capture"), CapturePercent(0., 0.), 0.);
        TestEqual(TEXT("Negative maximum HP cannot manufacture a capture"), CapturePercent(0., -1.), 0.);
        TestEqual(TEXT("Invalid remaining HP is rejected"), CapturePercent(std::numeric_limits<double>::quiet_NaN(), 100.), 0.);
        TestEqual(TEXT("Infinite maximum HP is rejected"), CapturePercent(0., std::numeric_limits<double>::infinity()), 0.);
        TestEqual(TEXT("Original capture threshold is ninety percent"), CaptureThreshold, 90);
        return true;
    }
}
#endif
