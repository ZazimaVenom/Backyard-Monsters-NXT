#include "BYMBunkerRules.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace BYMBunkerRulesTests
{
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerSpeciesTest, "BackyardMMO.Bunker.SpeciesAndSourceMode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerSpeciesTest::RunTest(const FString& Parameters)
    {
        for (int32 Id = 1; Id <= 13; ++Id)
            TestTrue(TEXT("Ordinary original C1-C13 species may be garrisoned"), BYMBunkerRules::IsAllowedSpecies(FString::Printf(TEXT("C%d"), Id)));
        TestTrue(TEXT("Existing source C17 inventory remains bunker-compatible"), BYMBunkerRules::IsAllowedSpecies(TEXT("C17")));
        for (const TCHAR* Id : {TEXT("C0"), TEXT("C01"), TEXT("C14"), TEXT("C15"), TEXT("C16"), TEXT("C18"), TEXT("IC1"), TEXT("c1"), TEXT("")})
            TestFalse(TEXT("Flying, healing, offspring and other-mode species are excluded"), BYMBunkerRules::IsAllowedSpecies(Id));
        const int32 ExpectedCapacity[] = {380, 450, 540, 660, 800};
        for (int32 Level = 1; Level <= 5; ++Level)
        {
            TestEqual(TEXT("Ordinary capacity includes the GLOBAL runtime override"), BYMBunkerRules::Capacity(Level), ExpectedCapacity[Level - 1]);
            TestEqual(TEXT("Trigger range retains source progression"), BYMBunkerRules::TriggerRangePixels(Level), 250. + Level * 50.);
        }
        TestEqual(TEXT("Unfinished construction has no capacity"), BYMBunkerRules::Capacity(0), 0);
        TestEqual(TEXT("Invalid level cannot gain capacity"), BYMBunkerRules::Capacity(6), 0);
        TestEqual(TEXT("Invalid level cannot acquire targets"), BYMBunkerRules::TriggerRangePixels(-1), 0.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerTransferCostTest, "BackyardMMO.Bunker.TransferCost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerTransferCostTest::RunTest(const FString& Parameters)
    {
        TestEqual(TEXT("A ready Pokey costs 125 Putty to transfer"), BYMBunkerRules::TransferCost(250, 1), int64(125));
        TestEqual(TEXT("Odd price rounds once for the whole species batch"), BYMBunkerRules::TransferCost(251, 3), int64(376));
        TestEqual(TEXT("Two odd-price species are rounded separately"), BYMBunkerRules::TransferCost(251, 1) + BYMBunkerRules::TransferCost(251, 1), int64(250));
        TestEqual(TEXT("The same species selected twice in one batch retains its half units"), BYMBunkerRules::TransferCost(251, 2), int64(251));
        TestEqual(TEXT("Empty selection costs nothing"), BYMBunkerRules::TransferCost(250, 0), int64(0));
        TestEqual(TEXT("Negative count cannot generate a refund"), BYMBunkerRules::TransferCost(250, -1), int64(0));
        TestEqual(TEXT("Invalid price cannot charge a negative amount"), BYMBunkerRules::TransferCost(-250, 1), int64(0));
        const int64 Limit = std::numeric_limits<int64>::max();
        TestEqual(TEXT("Large exact product is not overflowed before division"), BYMBunkerRules::TransferCost(Limit, 2), Limit);
        TestEqual(TEXT("Out-of-range helper cost saturates safely"), BYMBunkerRules::TransferCost(Limit, 3), Limit);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerBlastTest, "BackyardMMO.Bunker.EyeRaAndSourceClocks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerBlastTest::RunTest(const FString& Parameters)
    {
        TestEqual(TEXT("Defensive Eye-Ra has full central damage"), BYMBunkerRules::DefensiveEyeRaDamage(4000., 0.), 4000.);
        TestEqual(TEXT("Defensive Eye-Ra uses linear half-radius damage"), BYMBunkerRules::DefensiveEyeRaDamage(4000., 45.), 2000.);
        TestEqual(TEXT("Defensive Eye-Ra truncates source distance before falloff"), BYMBunkerRules::DefensiveEyeRaDamage(4000., 45.9), 2000.);
        TestEqual(TEXT("Defensive Eye-Ra preserves minimum damage near its edge"), BYMBunkerRules::DefensiveEyeRaDamage(4000., 89.9), 800.);
        TestEqual(TEXT("Exact radius is outside source target selection"), BYMBunkerRules::DefensiveEyeRaDamage(4000., 90.), 0.);
        TestEqual(TEXT("Invalid distance has no blast effect"), BYMBunkerRules::DefensiveEyeRaDamage(4000., -1.), 0.);
        TestEqual(TEXT("Nonfinite damage is refused"), BYMBunkerRules::DefensiveEyeRaDamage(std::numeric_limits<double>::infinity(), 1.), 0.);
        TestEqual(TEXT("Primary defender in offensive contact takes full damage"), BYMBunkerRules::OffensiveEyeRaDamageToDefender(4000., 29., true), 4000.);
        TestEqual(TEXT("Primary defender outside contact is not hit by the secondary loop"), BYMBunkerRules::OffensiveEyeRaDamageToDefender(4000., 30., true), 0.);
        TestEqual(TEXT("Offensive secondary defenders have quadratic falloff"), BYMBunkerRules::OffensiveEyeRaDamageToDefender(4000., 45., false), 3000.);
        TestEqual(TEXT("Offensive secondary blast has no defensive minimum"), BYMBunkerRules::OffensiveEyeRaDamageToDefender(4000., 89., false), 88.);
        TestEqual(TEXT("Door opening lasts fifteen display steps"), BYMBunkerRules::DoorFrames / BYMBunkerRules::DoorFrameRate, .375);
        TestTrue(TEXT("First 30-step release boundary follows a fully opened door"), BYMBunkerRules::ReleaseIntervalSeconds > BYMBunkerRules::DoorFrames / BYMBunkerRules::DoorFrameRate);
        TestEqual(TEXT("Defensive motion retains the original 50-percent boost"), BYMBunkerRules::DefenderSpeedMultiplier, 1.5);
        return true;
    }
}
#endif
