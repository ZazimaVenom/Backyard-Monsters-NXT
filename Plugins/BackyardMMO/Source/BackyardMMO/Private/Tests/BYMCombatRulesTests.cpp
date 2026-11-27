#include "BYMCombatRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMCombatRulesTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMOriginalTargetRulesTest, "BackyardMMO.Combat.OriginalTargetTiers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMOriginalTargetRulesTest::RunTest(const FString& Parameters)
{
    using namespace BYMCombatRules;
    TestEqual(TEXT("Eye-Ra prefers walls before any fallback building"), TargetTier(2, true, false, false), 0);
    TestEqual(TEXT("A nearby collector remains fallback for a wall specialist"), TargetTier(2, false, true, false), 1);
    TestEqual(TEXT("Bolt chooses lootable buildings before other buildings"), TargetTier(3, false, true, false), 0);
    TestEqual(TEXT("Octo-Ooze chooses towers before other buildings"), TargetTier(4, false, false, true), 0);
    TestEqual(TEXT("General targeting excludes walls until a path requires them"), TargetTier(1, true, false, false), -1);
    TestEqual(TEXT("Ordinary buildings remain targets when preferences are exhausted"), TargetTier(4, false, false, false), 1);
    TestEqual(TEXT("Ordinary attackers consider all main buildings equally"), TargetTier(1, false, true, false), 0);
    TestTrue(TEXT("Target distance follows source ground centre minus half-height"),
        FMath::IsNearlyEqual(TargetDistance(FVector2D(10, 5), FVector2D(4, 4), 1.), 4.));
    TestTrue(TEXT("Source distance can be negative inside a target's radius"),
        TargetDistance(FVector2D(5, 5), FVector2D(4, 4), 1.) < 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMOriginalBlastAndTowerRulesTest, "BackyardMMO.Combat.OriginalBlastAndTowerDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMOriginalBlastAndTowerRulesTest::RunTest(const FString& Parameters)
{
    using namespace BYMCombatRules;
    const FVector2D Monster(10.25, 10.25), Anchor(10, 10);
    TestEqual(TEXT("Blast centre respects original five-pixel position correction"), EyeRaBuildingDamage(4000, Monster, Anchor), 4000.);
    TestEqual(TEXT("Half-radius source blast uses quadratic falloff"), EyeRaBuildingDamage(4000, Monster, FVector2D(11.5, 10)), 3000.);
    TestEqual(TEXT("Exact sixty-pixel boundary takes no explosion damage"), EyeRaBuildingDamage(4000, Monster, FVector2D(13, 10)), 0.);
    TestEqual(TEXT("Outside the original radius takes no explosion damage"), EyeRaBuildingDamage(4000, Monster, FVector2D(13.1, 10)), 0.);
    TestEqual(TEXT("Explosion rounds positive damage like ActionScript Math.round"), EyeRaBuildingDamage(10, Monster, FVector2D(11.5, 10)), 8.);
    TestEqual(TEXT("Zero base damage cannot create a minimum explosion hit"), EyeRaBuildingDamage(0, Monster, Anchor), 0.);
    TestEqual(TEXT("Full-health tower retains its source shot"), TowerShotDamage(31, 6000, 6000), 31.);
    TestEqual(TEXT("Half-health tower deals truncated seventy-five percent damage"), TowerShotDamage(31, 3000, 6000), 23.);
    TestEqual(TEXT("Nearly destroyed tower retains source half-strength floor"), TowerShotDamage(100, 1, 6000), 50.);
    TestEqual(TEXT("Destroyed towers never fire"), TowerShotDamage(100, 0, 6000), 0.);
    TestEqual(TEXT("Global host scaling follows source shot truncation"), TowerShotDamage(31, 3000, 6000, 2), 46.);
    TestEqual(TEXT("Invalid max health cannot produce NaN damage"), TowerShotDamage(100, 1, 0), 0.);
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    TestEqual(TEXT("Invalid blast coordinates cannot produce damage"), EyeRaBuildingDamage(100, FVector2D(NaN, 1), Anchor), 0.);
    TestEqual(TEXT("Invalid tower multiplier cannot produce damage"), TowerShotDamage(100, 10, 10, NaN), 0.);
    return true;
}
}
#endif
