#include "BYMBuildingAnimation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMBunkerPresentationTests
{
    using namespace BYMBuildingAnimation;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerDoorTimingTest, "BackyardMMO.Presentation.BunkerDoorTiming",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMBunkerDoorTimingTest::RunTest(const FString& Parameters)
    {
        for (int32 Tick = 0; Tick <= 40; ++Tick)
        {
            const double Seconds = Tick / 40.;
            TestEqual(TEXT("Door opens at the original forty display ticks per second"),
                BunkerDoorPhase(0., Seconds, true), FMath::Min(15., static_cast<double>(Tick)));
            TestEqual(TEXT("Door closes through the same source phases in reverse"),
                BunkerDoorPhase(15., Seconds, false), FMath::Max(0., 15. - Tick));
        }
        const double Midpoint = BunkerDoorPhase(0., .2, true);
        TestEqual(TEXT("A source target change reverses at the current visible phase"), BunkerDoorPhase(Midpoint, .1, false), 4.);
        TestEqual(TEXT("Already open door never loops around"), BunkerDoorPhase(15., 3600., true), 15.);
        TestEqual(TEXT("Already closed door never becomes negative"), BunkerDoorPhase(0., 3600., false), 0.);
        TestEqual(TEXT("Repeated snapshot at the same time cannot advance the door"), BunkerDoorPhase(7.5, 0., true), 7.5);
        TestEqual(TEXT("Authoritative open state holds its endpoint instead of closing between snapshots"), BunkerDoorPhase(15., 1., false, false), 15.);
        TestEqual(TEXT("Authoritative closed state holds until opening starts"), BunkerDoorPhase(0., 1., true, false), 0.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerDoorCellsTest, "BackyardMMO.Presentation.BunkerDoorCells",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMBunkerDoorCellsTest::RunTest(const FString& Parameters)
    {
        for (int32 Phase = 0; Phase <= 15; ++Phase)
            TestEqual(TEXT("Every door phase selects a complete supplied cell"), BunkerDoorFrame(Phase), FMath::Min(14, Phase));
        TestEqual(TEXT("Opening endpoint never samples the empty sixteenth atlas slot"), BunkerDoorFrame(15.), 14);
        TestEqual(TEXT("Fractional interpolation preserves source cell boundaries"), BunkerDoorFrame(7.99), 7);
        TestEqual(TEXT("Float render delta cannot skip a closing boundary cell"), BunkerDoorFrame(BunkerDoorPhase(15., static_cast<double>(.2f), false)), 7);
        TestEqual(TEXT("Missing atlas frames fall back safely"), BunkerDoorFrame(15., 0), 0);
        TestEqual(TEXT("Partial valid atlas never samples past its last cell"), BunkerDoorFrame(15., 7), 6);
        TestEqual(TEXT("Invalid door phase resets to closed"), BunkerDoorFrame(std::numeric_limits<double>::quiet_NaN()), 0);
        TestEqual(TEXT("Invalid elapsed time holds the known authoritative phase"), BunkerDoorPhase(8., std::numeric_limits<double>::infinity(), true), 8.);
        TestEqual(TEXT("Rewound time holds the known authoritative phase"), BunkerDoorPhase(8., -1., false), 8.);
        return true;
    }
}
#endif
