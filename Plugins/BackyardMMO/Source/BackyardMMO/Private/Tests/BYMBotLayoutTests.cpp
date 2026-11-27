#include "BYMBotLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace BYMBotLayoutTests
{
    bool Overlaps(const FIntRect& A, const FIntRect& B)
    {
        return A.Min.X < B.Max.X && A.Max.X > B.Min.X && A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
    }

    bool Choose(const FString& Identity, int32 Type, int32 Occurrence, int32 Size,
        const FIntRect& Bounds, const TArray<FIntRect>& Existing, FIntRect& Result)
    {
        const BYMBotLayout::FPlan Plan = BYMBotLayout::MakePlan(Identity, Type, Occurrence, Bounds, Size);
        if (!Plan.bValid) return false;
        bool Found = false; uint64 Best = TNumericLimits<uint64>::Max();
        // This fixture supplies the ordinary bounds/occupied-footprint check;
        // the production integration must retain its own stronger Fits check.
        for (int32 Y = Bounds.Min.Y + 4; Y <= Bounds.Max.Y - Size - 4; ++Y)
            for (int32 X = Bounds.Min.X + 4; X <= Bounds.Max.X - Size - 4; ++X)
            {
                const FIntRect Candidate(X, Y, X + Size, Y + Size);
                bool Blocked = false;
                for (const FIntRect& Obstacle : Existing) if (Overlaps(Candidate, Obstacle)) { Blocked = true; break; }
                if (Blocked) continue;
                const uint64 Score = BYMBotLayout::RankCandidate(Plan, X, Y);
                if (!Found || Score < Best) { Best = Score; Result = Candidate; Found = true; }
            }
        return Found;
    }

    bool Starter(const FString& Identity, const FIntRect& Bounds, TArray<FIntRect>& Positions, FString& Fingerprint)
    {
        Positions.Reset(); Fingerprint.Reset();
        // Same eleven existing starters, largest footprints first so smaller
        // collectors cannot fragment the space needed by Housing/Flinger.
        static const int32 Types[] = {14, 15, 5, 6, 13, 8, 1, 2, 3, 4, 20};
        static const int32 Sizes[] = {10, 10, 10, 6, 6, 6, 5, 5, 5, 5, 4};
        for (int32 I = 0; I < UE_ARRAY_COUNT(Types); ++I)
        {
            FIntRect Position;
            if (!Choose(Identity, Types[I], 0, Sizes[I], Bounds, Positions, Position)) return false;
            Positions.Add(Position);
            Fingerprint += FString::Printf(TEXT("%d:%d,%d;"), Types[I], Position.Min.X, Position.Min.Y);
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBotLayoutDiversityTest, "BackyardMMO.Rivals.DeterministicDistinctLayouts",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBotLayoutDiversityTest::RunTest(const FString& Parameters)
    {
        TSet<FString> Layouts; TSet<int32> Styles;
        const FIntRect Bounds(0, 0, 50, 40);
        for (int32 Index = 1; Index <= 1000; ++Index)
        {
            const FString Identity = FString::Printf(TEXT("bot-%06d"), Index);
            TArray<FIntRect> Positions; FString Fingerprint;
            if (!Starter(Identity, Bounds, Positions, Fingerprint))
            { AddError(TEXT("A source-sized rival yard could not fit its eleven existing starter buildings.")); return false; }
            Layouts.Add(Fingerprint);
            Styles.Add(BYMBotLayout::MakePlan(Identity, 14, 0, Bounds, 10).Style);
            TestEqual(TEXT("Layout changes do not remove any starter buildings"), Positions.Num(), 11);
            for (int32 I = 0; I < Positions.Num(); ++I)
            {
                const FIntRect& A = Positions[I];
                TestTrue(TEXT("Every footprint remains inside the deployment margin"),
                    A.Min.X >= 4 && A.Min.Y >= 4 && A.Max.X <= 46 && A.Max.Y <= 36);
                for (int32 J = 0; J < I; ++J)
                    TestFalse(TEXT("Different district preferences never overlap buildings"), Overlaps(A, Positions[J]));
            }
            if (Index <= 20)
            {
                TArray<FIntRect> Repeated; FString RepeatedFingerprint;
                TestTrue(TEXT("The same identity can be regenerated"), Starter(Identity, Bounds, Repeated, RepeatedFingerprint));
                TestEqual(TEXT("Reload/repeat preferences produce identical coordinates"), Fingerprint, RepeatedFingerprint);
            }
        }
        TestEqual(TEXT("All six strategic layout styles are used"), Styles.Num(), 6);
        TestTrue(TEXT("At least 95 percent of the thousand rivals have distinct full layouts"), Layouts.Num() >= 950);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBotLayoutBoundsTest, "BackyardMMO.Rivals.LayoutBoundsAndOccupiedGround",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBotLayoutBoundsTest::RunTest(const FString& Parameters)
    {
        const FIntRect Expanded(-20, -16, 69, 55);
        TArray<FIntRect> Layout; FString Fingerprint;
        TestTrue(TEXT("Source expansions and negative origins support all existing buildings"),
            Starter(TEXT("bot-expanded"), Expanded, Layout, Fingerprint));
        for (const FIntRect& Position : Layout)
            TestTrue(TEXT("Expanded coordinates obey their actual rectangle"),
                Position.Min.X >= Expanded.Min.X + 4 && Position.Min.Y >= Expanded.Min.Y + 4
                && Position.Max.X <= Expanded.Max.X - 4 && Position.Max.Y <= Expanded.Max.Y - 4);

        const FIntRect Bounds(0, 0, 50, 40);
        const BYMBotLayout::FPlan Plan = BYMBotLayout::MakePlan(TEXT("bot-obstacles"), 14, 0, Bounds, 10);
        const FIntRect Blocked(Plan.Anchor.X, Plan.Anchor.Y, Plan.Anchor.X + 10, Plan.Anchor.Y + 10);
        FIntRect Chosen;
        TestTrue(TEXT("An occupied preferred anchor can choose another legal location"),
            Choose(TEXT("bot-obstacles"), 14, 0, 10, Bounds, {Blocked}, Chosen));
        TestFalse(TEXT("Preferences never bypass an existing footprint"), Overlaps(Blocked, Chosen));
        TestFalse(TEXT("Fully occupied yards cannot create phantom free space"),
            Choose(TEXT("bot-full"), 14, 0, 10, Bounds, {Bounds}, Chosen));

        const auto Invalid = BYMBotLayout::MakePlan(TEXT("bot-invalid"), 14, 0, FIntRect(0, 0, 12, 12), 10);
        TestFalse(TEXT("A too-small planning area is explicitly invalid"), Invalid.bValid);
        TestTrue(TEXT("Invalid plans cannot outrank valid locations"),
            BYMBotLayout::RankCandidate(Invalid, 0, 0) == TNumericLimits<uint64>::Max());
        TestFalse(TEXT("A nonpositive footprint is rejected"),
            BYMBotLayout::MakePlan(TEXT("bot-invalid"), 14, 0, Bounds, 0).bValid);
        const auto Repeated = BYMBotLayout::MakePlan(TEXT("bot-obstacles"), 14, 0, Bounds, 10);
        TestTrue(TEXT("Candidate ranking is stable across plan recreation"),
            BYMBotLayout::RankCandidate(Plan, 4, 5) == BYMBotLayout::RankCandidate(Repeated, 4, 5));
        const auto Next = BYMBotLayout::MakePlan(TEXT("bot-obstacles"), 14, 1, Bounds, 10);
        TestTrue(TEXT("Repeated building instances receive distinct preferences"), Plan.Seed != Next.Seed);
        return true;
    }
}
#endif
