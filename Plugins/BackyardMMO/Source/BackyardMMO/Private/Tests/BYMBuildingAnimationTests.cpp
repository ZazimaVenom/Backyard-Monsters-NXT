#include "BYMBuildingAnimation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMBuildingAnimationTests
{
    using namespace BYMBuildingAnimation;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingAnimationCadenceTest, "BackyardMMO.Presentation.BuildingAnimationCadence",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMBuildingAnimationCadenceTest::RunTest(const FString& Parameters)
    {
        const int32 CollectorExpected[] = {0,0,0,1,1,1,2,2,2,3,4,4,5,5,5,6};
        const int32 HatcheryExpected[] = {0,0,1,1,2,2,3,4,5,5,6,6,7,7,8,8};
        FClock Collector, Hatchery;
        for (int32 Tick = 0; Tick < 16; ++Tick)
        {
            const double Seconds = Tick / 40. + 1.e-10;
            TestEqual(TEXT("Collector matches the original copy-before-increment 3-or-10 display cadence"),
                SelectFrame(Collector, Seconds, 40, 3, 10, 34, true, false, false), CollectorExpected[Tick]);
            TestEqual(TEXT("Hatchery includes the seventh display tick as well as even ticks"),
                SelectFrame(Hatchery, Seconds, 40, 2, 7, 30, true, false, true), HatcheryExpected[Tick]);
        }
        for (int32 Cycle : {20,21,24,25,26,30,34})
        {
            for (bool bSlow : {false,true})
            {
                FClock Clock;
                int32 Expected = 0, Next = 0;
                const bool bHatchery = Cycle == 30;
                const int32 Fast = bHatchery ? 2 : 3, Slow = bHatchery ? 7 : 10;
                for (int32 Tick = 0; Tick < 2400; ++Tick)
                {
                    if (Tick % Slow == 0 || (!bSlow && Tick % Fast == 0))
                    { Expected = Next; Next = (Next + 1) % Cycle; }
                    const double Seconds = Tick / 40. + 1.e-10;
                    const int32 Actual = SelectFrame(Clock, Seconds, 40, Fast, Slow, Cycle, true, bSlow, false);
                    if (!TestEqual(TEXT("Original source frame cycle survives repeated wraps"), Actual, Expected)) return false;
                    if (!TestEqual(TEXT("Repeated material/snapshot updates at the same time do not advance animation"),
                        SelectFrame(Clock, Seconds, 40, Fast, Slow, Cycle, true, bSlow, false), Actual)) return false;
                }
            }
        }
        // Render-rate and catch-up independence: the original display clock is
        // forty Hz even if Unreal renders thirty, sixty, or one hundred twenty.
        FClock Reference;
        SelectFrame(Reference, 0, 40, 3, 10, 34, true, false, false);
        const int32 AtSixtySeconds = SelectFrame(Reference, 60, 40, 3, 10, 34, true, false, false);
        for (int32 RenderHz : {30,40,60,120})
        {
            FClock Clock;
            for (int32 Frame = 0; Frame <= 60 * RenderHz; ++Frame)
                SelectFrame(Clock, static_cast<double>(Frame) / RenderHz, 40, 3, 10, 34, true, false, false);
            TestEqual(TEXT("Render rate does not change the source animation phase"), Clock.DisplayedOrdinal, AtSixtySeconds);
            TestEqual(TEXT("Bulk advancement preserves the next source frame"), Clock.NextOrdinal, Reference.NextOrdinal);
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingAnimationPauseTest, "BackyardMMO.Presentation.BuildingAnimationPause",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMBuildingAnimationPauseTest::RunTest(const FString& Parameters)
    {
        FClock Collector;
        SelectFrame(Collector, 0, 40, 3, 10, 34, true, false, false);
        const int32 Last = SelectFrame(Collector, 1, 40, 3, 10, 34, true, false, false);
        const int32 Next = Collector.NextOrdinal;
        TestEqual(TEXT("A full or damaged collector holds its last displayed pose"),
            SelectFrame(Collector, 20, 40, 3, 10, 34, false, false, false), Last);
        TestEqual(TEXT("Paused display time never consumes the producer's next frame"), Collector.NextOrdinal, Next);
        TestEqual(TEXT("Resuming at an already observed display tick does not restart or jump"),
            SelectFrame(Collector, 20, 40, 3, 10, 34, true, false, false), Last);
        TestEqual(TEXT("Resuming at the next qualified source tick continues from the held counter"),
            SelectFrame(Collector, 20.025 + 1.e-10, 40, 3, 10, 34, true, false, false), Next);

        FClock Hatchery;
        SelectFrame(Hatchery, 0, 40, 2, 7, 30, true, false, true);
        SelectFrame(Hatchery, 1, 40, 2, 7, 30, true, false, true);
        TestEqual(TEXT("An empty or blocked Hatchery returns to the original first pose"),
            SelectFrame(Hatchery, 2, 40, 2, 7, 30, false, false, true), 0);
        TestEqual(TEXT("Hatchery reset also resets the next source counter"), Hatchery.NextOrdinal, 0);
        TestEqual(TEXT("Starting hatching draws frame zero before incrementing"),
            SelectFrame(Hatchery, 2.05 + 1.e-10, 40, 2, 7, 30, true, false, true), 0);
        TestEqual(TEXT("Starting hatching prepares the second frame only after the first is drawn"), Hatchery.NextOrdinal, 1);

        // Original cadence changes when creeps are present. Change the divisor
        // gate without restarting the animation or swapping to another phase.
        FClock Variable;
        int32 Expected = 0, ExpectedNext = 5;
        for (int32 Tick = 0; Tick < 1600; ++Tick)
        {
            const bool bSlow = (Tick / 97) % 2 != 0;
            const bool bWorking = (Tick / 113) % 3 != 1;
            if (bWorking && (Tick % 10 == 0 || (!bSlow && Tick % 3 == 0)))
            { Expected = ExpectedNext; ExpectedNext = (ExpectedNext + 1) % 34; }
            else if (Tick == 0) Expected = 5;
            const int32 Actual = SelectFrame(Variable, Tick / 40. + 1.e-10, 40, 3, 10, 34,
                bWorking, bSlow, false, 5);
            if (!TestEqual(TEXT("Activity and slow-mode changes preserve the source counter"), Actual, Expected)) return false;
        }
        FClock Phased;
        TestEqual(TEXT("An initial phase without a qualified tick retains its initial source pose"),
            SelectFrame(Phased, 0, 40, 3, 10, 34, true, false, false, 8, 1), 8);
        TestEqual(TEXT("Phase remains part of the original global display cadence"),
            SelectFrame(Phased, .05 + 1.e-10, 40, 3, 10, 34, true, false, false, 8, 1), 8);
        TestEqual(TEXT("A shared multiple only advances once"), Phased.NextOrdinal, 9);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingAnimationValidationTest, "BackyardMMO.Presentation.BuildingAnimationValidation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMBuildingAnimationValidationTest::RunTest(const FString& Parameters)
    {
        FClock Clock;
        SelectFrame(Clock, 0, 40, 3, 10, 34, true, false, false);
        SelectFrame(Clock, 2, 40, 3, 10, 34, true, false, false);
        const FClock Saved = Clock;
        const auto Unchanged = [&]()
        {
            return Clock.bInitialized == Saved.bInitialized && Clock.DisplayTick == Saved.DisplayTick
                && Clock.DisplayedOrdinal == Saved.DisplayedOrdinal && Clock.NextOrdinal == Saved.NextOrdinal;
        };
        const double NaN = std::numeric_limits<double>::quiet_NaN();
        const double Infinity = std::numeric_limits<double>::infinity();
        for (double Seconds : {-1.,NaN,Infinity,1.e100})
        {
            SelectFrame(Clock, Seconds, 40, 3, 10, 34, true, false, false);
            TestTrue(TEXT("Invalid clock inputs leave the full animation state unchanged"), Unchanged());
        }
        for (double Hertz : {0.,-1.,121.,NaN,Infinity})
        {
            SelectFrame(Clock, 3, Hertz, 3, 10, 34, true, false, false);
            TestTrue(TEXT("Invalid display rates cannot corrupt frame counters"), Unchanged());
        }
        for (int32 Divisor : {0,-1,1001})
        {
            SelectFrame(Clock, 3, 40, Divisor, 10, 34, true, false, false);
            TestTrue(TEXT("Invalid fast divisors preserve state"), Unchanged());
            SelectFrame(Clock, 3, 40, 3, Divisor, 34, true, false, false);
            TestTrue(TEXT("Invalid slow divisors preserve state"), Unchanged());
        }
        for (int32 Cycle : {0,-1,65537})
        {
            SelectFrame(Clock, 3, 40, 3, 10, Cycle, true, false, false);
            TestTrue(TEXT("Invalid source frame cycles preserve state"), Unchanged());
        }
        for (int32 Phase : {-1,1001})
        {
            SelectFrame(Clock, 3, 40, 3, 10, 34, true, false, false, 0, Phase);
            TestTrue(TEXT("Invalid phase input preserves state"), Unchanged());
        }
        TestEqual(TEXT("A clock rollback holds the displayed frame"), SelectFrame(Clock, 1, 40, 3, 10, 34, true, false, false), Saved.DisplayedOrdinal);
        TestTrue(TEXT("Rollback cannot rewind the remembered display tick"), Unchanged());
        SelectFrame(Clock, 2.025 + 1.e-10, 40, 3, 10, 34, true, false, false);
        TestEqual(TEXT("Advancing after rollback resumes from the saved next frame"), Clock.DisplayedOrdinal, Saved.NextOrdinal);
        FClock One;
        TestEqual(TEXT("Single-frame source activity remains bounded during long catch-up"),
            SelectFrame(One, 1000000, 40, 1000, 999, 1, true, false, false, 999999), 0);
        return true;
    }
}
#endif
