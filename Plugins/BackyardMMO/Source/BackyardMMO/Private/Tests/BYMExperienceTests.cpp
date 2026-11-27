#include "BYMExperience.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMExperienceTests
{
    using namespace BYMExperience;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceThresholdsTest, "BackyardMMO.Experience.OriginalClientThresholds",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMExperienceThresholdsTest::RunTest(const FString& Parameters)
    {
        TestEqual(TEXT("Preserve all active client thresholds, not the truncated server copy"), ThresholdCount(), 100);
        TestEqual(TEXT("Original level-two threshold"), Threshold(1), static_cast<int64>(900));
        TestEqual(TEXT("Original level-fifty-six threshold"), Threshold(55), static_cast<int64>(212613620467LL));
        TestEqual(TEXT("The next original threshold is retained"), Threshold(56), static_cast<int64>(297659068653LL));
        TestEqual(TEXT("Final original upper boundary"), Threshold(99), static_cast<int64>(755984118210000LL));
        for (int32 Index = 0; Index < 99; ++Index)
        {
            const FProgress Exact = Progress(Threshold(Index), 0);
            TestEqual(TEXT("Each exact source threshold selects its original displayed level"), Exact.Level, Index + 1);
            TestEqual(TEXT("Exact threshold is the lower bound"), Exact.Lower, Threshold(Index));
            TestEqual(TEXT("Next threshold is the upper bound"), Exact.Upper, Threshold(Index + 1));
            TestEqual(TEXT("Progress resets exactly at a level threshold"), Exact.Fraction, 0.);
            if (Index > 0) TestEqual(TEXT("One point before a threshold remains in the prior level"), Progress(Threshold(Index) - 1, 0).Level, Index);
        }
        const FProgress Midpoint = Progress(450, 0);
        TestEqual(TEXT("Level-one midpoint is half a bar"), Midpoint.Fraction, .5);
        TestEqual(TEXT("Needed XP uses the current threshold"), Midpoint.Needed, static_cast<int64>(450));
        const FProgress Last = Progress(Threshold(99), 0);
        TestEqual(TEXT("Original client displays at most level99"), Last.Level, 99);
        TestEqual(TEXT("The final upper boundary does not invent level100"), Last.MaxLevel, 99);
        TestTrue(TEXT("Final displayed level is explicitly marked"), Last.bAtMaxLevel);
        TestEqual(TEXT("A final filled bar does not overrun the artwork"), Last.Fraction, 1.);
        TestEqual(TEXT("Final needed amount cannot become negative"), Last.Needed, static_cast<int64>(0));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceAwardsTest, "BackyardMMO.Experience.OriginalActionAwards",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMExperienceAwardsTest::RunTest(const FString& Parameters)
    {
        TestEqual(TEXT("Twig Snapper completion uses floor(time/2 + resources/10)"), ConstructionReward(15, 750), static_cast<int64>(82));
        TestEqual(TEXT("Town Hall construction includes its original100-point bonus"), ConstructionReward(10, 0, true), static_cast<int64>(105));
        TestEqual(TEXT("Upgrade completion receives the previous level cost row"), UpgradeReward(15, 750), static_cast<int64>(255));
        TestEqual(TEXT("Resource and time fractions are combined before flooring construction"), ConstructionReward(1, 5), static_cast<int64>(1));
        TestEqual(TEXT("Upgrade floors the whole sum after dividing by three"), UpgradeReward(1, 2), static_cast<int64>(1));
        TestEqual(TEXT("Posttutorial collector receives half accepted resources rounded up"), CollectorReward(3), static_cast<int64>(2));
        TestEqual(TEXT("Tutorial collection keeps its original one-for-one rule"), CollectorReward(3, true), static_cast<int64>(3));
        TestEqual(TEXT("A full wallet receives no collection XP"), CollectorReward(0), static_cast<int64>(0));
        TestEqual(TEXT("Collect-all rounds each collector independently"), CollectorReward(1) + CollectorReward(1), static_cast<int64>(2));
        TestEqual(TEXT("Aggregation would produce a different award and must not replace per-collector calls"), CollectorReward(2), static_cast<int64>(1));
        TestEqual(TEXT("Original loot XP is one-for-one"), LootReward(1753), static_cast<int64>(1753));
        TestEqual(TEXT("Invalid negative rewards cannot remove earned progress"), Add(90, LootReward(-20)), static_cast<int64>(90));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceBaseValueTest, "BackyardMMO.Experience.BaseValueHighWater",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMExperienceBaseValueTest::RunTest(const FString& Parameters)
    {
        int64 Value = RetainBaseValue(0, 765);
        TestEqual(TEXT("Current level-one Twig Snapper value"), Value, static_cast<int64>(77));
        TestEqual(TEXT("Aggregate eligible building costs before rounding"), RetainBaseValue(0, 765 + 10), static_cast<int64>(78));
        TestEqual(TEXT("Do not independently ceil every building"), RetainBaseValue(0, 1 + 1), static_cast<int64>(1));
        Value = RetainBaseValue(Value, 1875);
        TestEqual(TEXT("A completed upgrade raises the high-water base value"), Value, static_cast<int64>(188));
        Value = RetainBaseValue(Value, 0);
        TestEqual(TEXT("Destruction or recycling cannot lower the recorded base value"), Value, static_cast<int64>(188));
        TestEqual(TEXT("A refund cannot negate already earned experience"), Add(100, -50), static_cast<int64>(100));
        const FProgress Owner = Progress(255, Value);
        TestEqual(TEXT("Player points combine earned experience and retained value"), Owner.Points, static_cast<int64>(443));
        const FProgress Reloaded = Progress(Owner.Earned, Owner.BaseValue);
        TestEqual(TEXT("Restoring the two persisted counters does not duplicate awards"), Reloaded.Points, Owner.Points);
        TestEqual(TEXT("Recomputing the same completed yard has no extra award"), RetainBaseValue(Value, 1875), Value);
        TestEqual(TEXT("A legacy world can begin from known value without fabricated history"), Progress(0, RetainBaseValue(0, 765)).Points, static_cast<int64>(77));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceBoundsTest, "BackyardMMO.Experience.SaveBoundsAndOverflow",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMExperienceBoundsTest::RunTest(const FString& Parameters)
    {
        TestTrue(TEXT("Zero is a valid persisted counter"), IsValidSavedPoints(0));
        TestTrue(TEXT("Maximum counter remains exact in JSON numbers"), IsValidSavedPoints(static_cast<double>(MaximumPoints)));
        TestFalse(TEXT("Negative progress is invalid save data"), IsValidSavedPoints(-1));
        TestFalse(TEXT("Fractional progress is invalid save data"), IsValidSavedPoints(1.5));
        TestFalse(TEXT("Counters outside exact JSON precision are rejected"), IsValidSavedPoints(static_cast<double>(MaximumPoints) + 1.));
        TestFalse(TEXT("NaN is rejected"), IsValidSavedPoints(std::numeric_limits<double>::quiet_NaN()));
        TestFalse(TEXT("Infinity is rejected"), IsValidSavedPoints(std::numeric_limits<double>::infinity()));
        TestEqual(TEXT("Earned awards saturate without integer overflow"), Add(MaximumPoints - 2, 100), MaximumPoints);
        TestEqual(TEXT("Large component sums saturate before JSON serialization"), Progress(MaximumPoints, MaximumPoints).Points, MaximumPoints);
        TestEqual(TEXT("Maximum-scale base value uses an exact ceiling"), RetainBaseValue(0, MaximumPoints), MaximumPoints / 10 + 1);
        TestTrue(TEXT("Extreme source costs cannot overflow construction arithmetic"), ConstructionReward(std::numeric_limits<int64>::max(), std::numeric_limits<int64>::max(), true) > 0);
        TestTrue(TEXT("Extreme source costs cannot overflow upgrade arithmetic"), UpgradeReward(std::numeric_limits<int64>::max(), std::numeric_limits<int64>::max()) > 0);
        TestEqual(TEXT("The safe bound does not increase the client maximum level"), Progress(MaximumPoints, 0).Level, 99);
        TestEqual(TEXT("Over-threshold visual progress stays inside the original bar"), Progress(MaximumPoints, 0).Fraction, 1.);
        return true;
    }
}

#endif
