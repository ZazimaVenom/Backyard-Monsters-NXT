#include "BYMEconomy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMEconomyTests
{
    using namespace BYMEconomy;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCollectorCyclesTest, "BackyardMMO.Economy.CollectorCyclesAndBanking",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMCollectorCyclesTest::RunTest(const FString& Parameters)
    {
        FCollectorRules Rules; Rules.BaseCycle = 10.; Rules.Amount = 2.; Rules.Capacity = 720.;
        FRepairState Building; Building.Health = 500.; Building.MaxHealth = 500.;
        FCollectorState Collector;
        TestEqual(TEXT("Undamaged source cycle"), ProductionTimeout(10., 500., 500.), 10.);
        TestEqual(TEXT("Three-quarter health doubles the source cycle"), ProductionTimeout(10., 375., 500.), 20.);
        TestEqual(TEXT("Half health triples the source cycle"), ProductionTimeout(10., 250., 500.), 30.);
        TestTrue(TEXT("Initial countdown advances"), Advance(Collector, Building, Rules, false, 9.));
        TestEqual(TEXT("No fractional resource grant before completion"), Collector.Stored, 0.);
        TestEqual(TEXT("One source second remains"), Collector.Remaining, 1.);
        TestTrue(TEXT("Exact deadline completes one batch"), Advance(Collector, Building, Rules, false, 1.));
        TestEqual(TEXT("First exact batch"), Collector.Stored, 2.);
        TestEqual(TEXT("Next source cycle is scheduled"), Collector.Remaining, 10.);
        Advance(Collector, Building, Rules, false, 5.);
        Building.Health = 375.;
        Advance(Collector, Building, Rules, false, 5.);
        TestEqual(TEXT("Damage preserves the in-flight countdown"), Collector.Stored, 4.);
        TestEqual(TEXT("Only the newly started cycle uses damaged speed"), Collector.Remaining, 20.);
        Building.Health = 249.;
        TestFalse(TEXT("Below half health stops work"), CanProduce(Collector, Rules, Building, false));
        Advance(Collector, Building, Rules, false, 100.);
        TestEqual(TEXT("Below-half damage does not advance production"), Collector.Stored, 4.);
        TestEqual(TEXT("Damage pause retains remaining time"), Collector.Remaining, 20.);
        Building.Health = 250.;
        TestTrue(TEXT("Exactly half health can work"), CanProduce(Collector, Rules, Building, false));
        Advance(Collector, Building, Rules, true, 100.);
        TestEqual(TEXT("Construction or upgrade pause retains time"), Collector.Remaining, 20.);
        Advance(Collector, Building, Rules, false, 20.);
        TestEqual(TEXT("Repaired eligibility resumes the original countdown"), Collector.Stored, 6.);
        TestEqual(TEXT("Next cycle uses current half-health penalty"), Collector.Remaining, 30.);

        Collector.Stored = 720.; Collector.Remaining = 0.;
        int64 Wallet = 99;
        TestEqual(TEXT("Only wallet room is banked"), Bank(Collector, Rules, Building, Wallet, 100), static_cast<int64>(1));
        TestEqual(TEXT("Wallet reaches its cap"), Wallet, static_cast<int64>(100));
        TestEqual(TEXT("Unaccepted stock remains in the collector"), Collector.Stored, 719.);
        TestEqual(TEXT("Taking from a full collector starts a fresh damaged cycle"), Collector.Remaining, 30.);
        Advance(Collector, Building, Rules, false, 30.);
        TestEqual(TEXT("The final batch is capped to collector storage"), Collector.Stored, 720.);
        TestEqual(TEXT("Full collector stops its countdown"), Collector.Remaining, 0.);
        TestFalse(TEXT("Full collector is not working"), CanProduce(Collector, Rules, Building, false));
        Collector.Stored = 20.75; Collector.Remaining = 4.; Wallet = 0;
        TestEqual(TEXT("Banking floors only the transfer"), Bank(Collector, Rules, Building, Wallet, 100), static_cast<int64>(20));
        TestEqual(TEXT("Migrated fractional stored stock is preserved"), Collector.Stored, .75);
        TestEqual(TEXT("An active collector's countdown is not restarted by banking"), Collector.Remaining, 4.);
        Wallet = 101;
        TestEqual(TEXT("An over-cap wallet accepts nothing"), Bank(Collector, Rules, Building, Wallet, 100), static_cast<int64>(0));
        TestEqual(TEXT("Banking never truncates an existing over-cap wallet"), Wallet, static_cast<int64>(101));
        TestEqual(TEXT("Completed silo retains its current level capacity"), StorageContribution(1, 4000.), 4000.);
        TestEqual(TEXT("New silo construction supplies no capacity"), StorageContribution(0, 4000.), 0.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCollectorRepairTest, "BackyardMMO.Economy.CollectorRepairCatchup",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMCollectorRepairTest::RunTest(const FString& Parameters)
    {
        FCollectorRules Rules; Rules.Amount = 2.; Rules.Capacity = 720.;
        FRepairState Building; Building.Health = 200.; Building.MaxHealth = 500.;
        Building.Rate = RepairRate(500., 120.); Building.bRepairing = true;
        TestEqual(TEXT("Free repair uses rounded source HP per second"), Building.Rate, 5.);
        TestEqual(TEXT("Source repair cap and ceiling apply together"), RepairRate(10., 86400.), 1.);
        TestEqual(TEXT("Configured construction time scales repair duration"), RepairRate(500., 120., 2.), 2.5);
        TestEqual(TEXT("Remaining repair duration uses the rounded rate"), RepairDuration(Building), 60.);
        TestEqual(TEXT("Half-health crossing is explicit for dependent buildings"), SecondsToHealth(Building, .5), 10.);
        FCollectorState Collector; Collector.Remaining = 4.;
        const FCollectorState InitialCollector = Collector;
        const FRepairState InitialBuilding = Building;
        Advance(Collector, Building, Rules, false, 9.);
        TestEqual(TEXT("Repair progresses continuously before production resumes"), Building.Health, 245.);
        TestEqual(TEXT("Paused production time is retained below threshold"), Collector.Remaining, 4.);
        Advance(Collector, Building, Rules, false, 1.);
        TestEqual(TEXT("Exactly crossing the threshold produces no retroactive batch"), Collector.Stored, 0.);
        TestEqual(TEXT("Crossing health threshold preserves production countdown"), Collector.Remaining, 4.);
        Advance(Collector, Building, Rules, false, 4.);
        TestTrue(TEXT("Repair and production advance together"), FMath::IsNearlyEqual(Building.Health, 270., 1.e-9));
        TestEqual(TEXT("First batch completes after the preserved countdown"), Collector.Stored, 2.);
        TestTrue(TEXT("New cycle uses health at batch completion"), FMath::IsNearlyEqual(Collector.Remaining, 29., 1.e-9));
        FCollectorState Whole = InitialCollector, Split = InitialCollector;
        FRepairState WholeBuilding = InitialBuilding, SplitBuilding = InitialBuilding;
        Advance(Whole, WholeBuilding, Rules, false, 60.);
        for (int32 Second = 0; Second < 60; ++Second) Advance(Split, SplitBuilding, Rules, false, 1.);
        TestTrue(TEXT("Offline and per-second repairs reach the same health"), FMath::IsNearlyEqual(WholeBuilding.Health, SplitBuilding.Health, 1.e-8));
        TestEqual(TEXT("Offline and per-second production grant the same batches"), Whole.Stored, Split.Stored);
        TestTrue(TEXT("Offline catch-up preserves identical cycle phase"), FMath::IsNearlyEqual(Whole.Remaining, Split.Remaining, 1.e-8));
        TestEqual(TEXT("Repair completes at full health"), WholeBuilding.Health, 500.);
        TestFalse(TEXT("Finished repair clears its active state"), WholeBuilding.bRepairing);
        TestEqual(TEXT("Source repair/cycle scenario completes three batches"), Whole.Stored, 6.);
        TestTrue(TEXT("Final full-health cycle remains"), FMath::IsNearlyEqual(Whole.Remaining, 10., 1.e-8));

        FRepairState Blocked = InitialBuilding;
        FCollectorState BlockedCollector = InitialCollector;
        Advance(BlockedCollector, Blocked, Rules, true, 60.);
        TestEqual(TEXT("A production-blocked building can still repair"), Blocked.Health, 500.);
        TestEqual(TEXT("Blocked production receives no work during repair"), BlockedCollector.Stored, 0.);
        TestEqual(TEXT("Blocked production retains its exact prior countdown"), BlockedCollector.Remaining, 4.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCollectorPersistenceTest, "BackyardMMO.Economy.CollectorOfflineAndValidation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMCollectorPersistenceTest::RunTest(const FString& Parameters)
    {
        FCollectorRules Rules; Rules.Amount = 2.; Rules.Capacity = 720.;
        FRepairState Building; Building.Health = 500.; Building.MaxHealth = 500.;
        FCollectorState Collector;
        Advance(Collector, Building, Rules, false, 7.);
        // These are the complete persisted collector fields. Restoring them
        // must not restart a cycle or award work before its remaining deadline.
        FCollectorState Reloaded = Collector;
        Advance(Reloaded, Building, Rules, false, 2.);
        TestEqual(TEXT("A restored mid-cycle collector has not completed early"), Reloaded.Stored, 0.);
        Advance(Reloaded, Building, Rules, false, 1.);
        TestEqual(TEXT("A restored mid-cycle collector completes once"), Reloaded.Stored, 2.);
        Advance(Reloaded, Building, Rules, false, 31536000.);
        TestEqual(TEXT("One-year offline catch-up caps without a per-second loop"), Reloaded.Stored, 720.);
        TestEqual(TEXT("Full offline collector is stopped"), Reloaded.Remaining, 0.);
        Rules.RateMultiplier = 2.;
        Collector = FCollectorState();
        Advance(Collector, Building, Rules, false, 5.);
        TestEqual(TEXT("NXT rate setting scales time while retaining whole source batches"), Collector.Stored, 2.);
        Rules.RateMultiplier = 0.;
        Advance(Collector, Building, Rules, false, 100.);
        TestEqual(TEXT("Zero production setting pauses income"), Collector.Stored, 2.);
        TestEqual(TEXT("Zero production setting retains phase"), Collector.Remaining, 5.);
        Rules.RateMultiplier = std::numeric_limits<double>::denorm_min();
        Collector = FCollectorState();
        TestTrue(TEXT("Tiny positive custom rate creates a valid bounded countdown"), Advance(Collector, Building, Rules, false, 0.));
        TestEqual(TEXT("Custom rate countdown cannot exceed saved time horizon"), Collector.Remaining, 31536000.);
        TestTrue(TEXT("A bounded custom-rate countdown remains valid on the next update"), Advance(Collector, Building, Rules, false, 1.));
        Rules.RateMultiplier = 1.;
        const FCollectorState Stable = Collector;
        const FRepairState StableBuilding = Building;
        TestFalse(TEXT("Negative elapsed time is rejected"), Advance(Collector, Building, Rules, false, -1.));
        TestFalse(TEXT("Non-finite elapsed time is rejected"), Advance(Collector, Building, Rules, false, std::numeric_limits<double>::quiet_NaN()));
        TestEqual(TEXT("Rejected updates preserve stored resources"), Collector.Stored, Stable.Stored);
        TestEqual(TEXT("Rejected updates preserve countdown"), Collector.Remaining, Stable.Remaining);
        TestEqual(TEXT("Rejected updates preserve health"), Building.Health, StableBuilding.Health);
        Rules.Capacity = -1.;
        TestFalse(TEXT("Invalid collector capacity is rejected"), Advance(Collector, Building, Rules, false, 10.));
        Rules.Capacity = 720.; Collector.Remaining = -1.;
        TestFalse(TEXT("Negative saved countdown is rejected"), Advance(Collector, Building, Rules, false, 10.));
        TestEqual(TEXT("Invalid repair rates are refused"), RepairRate(500., 0.), 0.);
        Building.Health = 0.; Building.bRepairing = false;
        TestEqual(TEXT("Unrepaired damage cannot promise a threshold time"), SecondsToHealth(Building, .5), -1.);
        Collector.Stored = 720.; Collector.Remaining = 0.;
        int64 Wallet = 0;
        TestEqual(TEXT("Existing stored stock can still be banked from a stopped state"), Bank(Collector, Rules, Building, Wallet, 10), static_cast<int64>(10));
        TestEqual(TEXT("Banking does not start a zero-health collector"), Collector.Remaining, 0.);
        TestEqual(TEXT("Pathological repair health is rejected before arithmetic overflow"), RepairRate(1.e308, 1.), 0.);
        TestEqual(TEXT("Subnormal source repair time is invalid"), RepairRate(500., std::numeric_limits<double>::denorm_min()), 0.);
        return true;
    }
}

#endif
