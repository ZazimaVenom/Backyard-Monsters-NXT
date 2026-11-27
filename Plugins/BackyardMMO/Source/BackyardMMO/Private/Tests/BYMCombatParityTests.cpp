#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace BYMCombatParityTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Attacker(TEXT("parity-attacker")), Defender(TEXT("parity-defender"));
    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Bots = Settings->RivalBotCount, Cells = Settings->YardCells;
        const float Damage = Settings->CombatDamageMultiplier, Movement = Settings->MonsterMovementMultiplier;
        const float Production = Settings->ResourceProductionMultiplier;
        const float CombatStep = Settings->CombatSimulationStepSeconds;
        FSettingsGuard()
        {
            Settings->RivalBotCount = 0; Settings->YardCells = 40;
            Settings->CombatDamageMultiplier = 1; Settings->MonsterMovementMultiplier = 1;
            Settings->ResourceProductionMultiplier = 1;
            Settings->CombatSimulationStepSeconds = .1f;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots; Settings->YardCells = Cells;
            Settings->CombatDamageMultiplier = Damage; Settings->MonsterMovementMultiplier = Movement;
            Settings->ResourceProductionMultiplier = Production;
            Settings->CombatSimulationStepSeconds = CombatStep;
        }
    };
    FObject Decode(const FString& Text)
    { FObject Object; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object); return Object; }
    FObject Child(const FObject& Object, const TCHAR* Field)
    { const FObject* Value = nullptr; return Object && Object->TryGetObjectField(Field, Value) && Value ? *Value : FObject(); }
    const FArray& Array(const FObject& Object, const TCHAR* Field)
    { static const FArray Empty; const FArray* Value = nullptr; return Object && Object->TryGetArrayField(Field, Value) && Value ? *Value : Empty; }
    FString String(const FObject& Object, const TCHAR* Field)
    { FString Value; if (Object) Object->TryGetStringField(Field, Value); return Value; }
    double Number(const FObject& Object, const TCHAR* Field)
    { double Value = -1; if (Object) Object->TryGetNumberField(Field, Value); return Value; }
    FObject Find(const FArray& Values, const FString& Id)
    { for (const auto& Value : Values) if (Value && Value->Type == EJson::Object && String(Value->AsObject(), TEXT("id")) == Id) return Value->AsObject(); return nullptr; }
    FObject Yard(const FObject& Save, const FString& Id) { return Find(Array(Save, TEXT("yards")), Id); }
    FObject Snapshot(const FBYMSimulation& Simulation) { return Decode(Simulation.Snapshot(Attacker, FString())); }
    FObject Battle(const FBYMSimulation& Simulation) { return Child(Snapshot(Simulation), TEXT("battle")); }
    FObject First(const FArray& Items) { return Items.IsEmpty() ? FObject() : Items[0]->AsObject(); }
    bool Accepted(const FString& Text)
    { const FObject Object = Decode(Text); bool Value = false; return Object && Object->TryGetBoolField(TEXT("ok"), Value) && Value; }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    { FString Error; if (Simulation.Initialize(Save, Error)) return true; Test.AddError(TEXT("Combat parity fixture: ") + Error); return false; }
    FObject Building(const FString& Id, int32 Type, int32 X, int32 Y, double HP)
    {
        FObject Object = MakeShared<FJsonObject>(); Object->SetStringField(TEXT("id"), Id);
        Object->SetNumberField(TEXT("type"), Type); Object->SetNumberField(TEXT("level"), 1); Object->SetNumberField(TEXT("targetLevel"), 1);
        Object->SetNumberField(TEXT("x"), X); Object->SetNumberField(TEXT("y"), Y); Object->SetNumberField(TEXT("hp"), HP);
        Object->SetNumberField(TEXT("readyAt"), 0); Object->SetNumberField(TEXT("stored"), 0);
        Object->SetStringField(TEXT("jobId"), FString()); Object->SetNumberField(TEXT("workRemaining"), 0); Object->SetBoolField(TEXT("repairing"), false);
        return Object;
    }
    FObject Fixture(FAutomationTestBase& Test, const TCHAR* Type, const FArray& Buildings)
    {
        FBYMSimulation Starter; if (!Load(Test, Starter, nullptr)) return nullptr;
        FString Error; if (!Starter.AddPlayer(Attacker, Attacker, Error) || !Starter.AddPlayer(Defender, Defender, Error))
        { Test.AddError(Error); return nullptr; }
        FObject Save = Starter.Serialize(), Own = Yard(Save, Attacker);
        FObject Army = MakeShared<FJsonObject>(); Army->SetNumberField(TEXT("C1"), 2); Army->SetNumberField(Type, 2);
        Own->SetObjectField(TEXT("army"), Army); Child(Own, TEXT("monsterLevels"))->SetNumberField(Type, 1);
        Yard(Save, Defender)->SetArrayField(TEXT("buildings"), Buildings);
        Yard(Save, Defender)->SetArrayField(TEXT("hatcheries"), FArray());
        return Save;
    }
    FObject DeployedSave(FAutomationTestBase& Test, const FObject& FixtureSave, const TCHAR* Type)
    {
        FBYMSimulation Prepared; if (!FixtureSave || !Load(Test, Prepared, FixtureSave)) return nullptr;
        if (!Test.TestTrue(TEXT("Start a real authoritative invasion"), Accepted(Prepared.Execute(Attacker, TEXT("invade"), TEXT("{\"target\":\"parity-defender\"}"))))) return nullptr;
        if (!Test.TestTrue(TEXT("Deploy one ready monster through the host command"), Accepted(Prepared.Execute(Attacker, TEXT("deploy"),
            FString::Printf(TEXT("{\"type\":\"%s\",\"count\":1,\"x\":49,\"y\":20}"), Type))))) return nullptr;
        return Prepared.Serialize();
    }
    FObject SavedTroop(const FObject& Save) { return First(Array(First(Array(Save, TEXT("battles"))), TEXT("troops"))); }
    double HP(const FBYMSimulation& Simulation, const TCHAR* Id)
    { return Number(Find(Array(Child(Snapshot(Simulation), TEXT("yard")), TEXT("buildings")), Id), TEXT("hp")); }
    void Step(FBYMSimulation& Simulation, double& Now) { Now += .1; Simulation.Tick(Now, .1f); }
    int32 CountEvents(const FObject& Current, const TCHAR* Kind)
    { int32 Count = 0; for (const auto& Value : Array(Current, TEXT("events"))) if (String(Value->AsObject(), TEXT("kind")) == Kind) ++Count; return Count; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMEyeRaSingleBlastTest, "BackyardMMO.Combat.EyeRaSingleBlastAndReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMEyeRaSingleBlastTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FArray Buildings;
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("wall"), 17, 22, 20, 1000)));
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("town"), 14, 25, 20, 4000)));
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("remote"), 1, 4, 4, 500)));
    FObject Save = DeployedSave(*this, Fixture(*this, TEXT("C5"), Buildings), TEXT("C5"));
    if (!Save) return false;
    FObject Troop = SavedTroop(Save); if (!TestTrue(TEXT("Eye-Ra deployment is persisted"), Troop.IsValid())) return false;
    // Right-hand contact with a one-cell source wall. After the source (-5,-5)
    // correction, the blast origin is (23,20): the Town Hall anchor is 40px away.
    Troop->SetNumberField(TEXT("x"), 23.25); Troop->SetNumberField(TEXT("y"), 20.25);
    Troop->SetStringField(TEXT("target"), TEXT("wall")); Troop->SetNumberField(TEXT("attackCooldown"), 0);
    FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now")); Step(Simulation, Now);
    const FObject Current = Battle(Simulation);
    TestEqual(TEXT("One blast destroys the contacted wall"), HP(Simulation, TEXT("wall")), 0.);
    TestEqual(TEXT("Nearby Town Hall takes rounded quadratic splash, without ordinary wall-strike doubling"), HP(Simulation, TEXT("town")), 1778.);
    TestEqual(TEXT("A distant collector receives no blast damage"), HP(Simulation, TEXT("remote")), 500.);
    TestEqual(TEXT("Eye-Ra is consumed by its own explosion"), Number(First(Array(Current, TEXT("troops"))), TEXT("hp")), 0.);
    TestEqual(TEXT("Explosion presentation is emitted exactly once"), CountEvents(Current, TEXT("monsterExplode")), 1);
    TestEqual(TEXT("Two nearby buildings receive one hit each"), CountEvents(Current, TEXT("explosionHit")), 2);
    TestEqual(TEXT("Exactly one self-death is emitted"), CountEvents(Current, TEXT("troopDied")), 1);
    TestEqual(TEXT("Explosion does not also perform an ordinary melee hit"), CountEvents(Current, TEXT("monsterAttack")), 0);
    const FObject After = Simulation.Serialize();
    FBYMSimulation Reloaded; if (!Load(*this, Reloaded, After)) return false;
    for (int32 Tick = 0; Tick < 20; ++Tick) Step(Reloaded, Now);
    TestEqual(TEXT("Reloaded dead Eye-Ra cannot explode or damage again"), HP(Reloaded, TEXT("town")), 1778.);
    TestEqual(TEXT("Reload does not replay a terminal explosion event"), CountEvents(Battle(Reloaded), TEXT("monsterExplode")), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMPreferredTargetAuthorityTest, "BackyardMMO.Combat.PreferredTargetAuthority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMPreferredTargetAuthorityTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FArray Buildings;
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("near-town"), 14, 25, 20, 4000)));
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("far-tower"), 20, 4, 4, 6000)));
    // Keep the battle below its 50% victory threshold after the tower falls,
    // so the second assertion observes target fallback in an active invasion.
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("remote-collector"), 1, 4, 30, 500)));
    FObject Save = DeployedSave(*this, Fixture(*this, TEXT("C2"), Buildings), TEXT("C2")); if (!Save) return false;
    FObject Troop = SavedTroop(Save); Troop->SetNumberField(TEXT("x"), 33); Troop->SetNumberField(TEXT("y"), 32);
    Troop->SetStringField(TEXT("target"), TEXT(""));
    FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now")); Step(Simulation, Now);
    TestEqual(TEXT("A tower specialist selects a distant defense before a nearby Town Hall"), String(First(Array(Battle(Simulation), TEXT("troops"))), TEXT("target")), FString(TEXT("far-tower")));
    TestEqual(TEXT("Merely choosing a target does not apply remote damage"), HP(Simulation, TEXT("far-tower")), 6000.);
    FObject Fallback = Simulation.Serialize(); Find(Array(Yard(Fallback, Defender), TEXT("buildings")), TEXT("far-tower"))->SetNumberField(TEXT("hp"), 0);
    FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Fallback)) return false; Step(Reloaded, Now);
    TestEqual(TEXT("A specialist falls back to the main building after defenses are destroyed"), String(First(Array(Battle(Reloaded), TEXT("troops"))), TEXT("target")), FString(TEXT("near-town")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMDamagedTowerAuthorityTest, "BackyardMMO.Combat.DamagedTowerAuthority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMDamagedTowerAuthorityTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FArray Buildings;
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("town"), 14, 4, 4, 4000)));
    Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("tower"), 20, 20, 20, 3000)));
    FObject Save = DeployedSave(*this, Fixture(*this, TEXT("C2"), Buildings), TEXT("C2")); if (!Save) return false;
    FObject Troop = SavedTroop(Save); Troop->SetNumberField(TEXT("x"), 29.5); Troop->SetNumberField(TEXT("y"), 21.75);
    Troop->SetStringField(TEXT("target"), TEXT("town")); Troop->SetNumberField(TEXT("attackCooldown"), 60);
    FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now")); Step(Simulation, Now);
    TestEqual(TEXT("A half-health level-one Cannon deals 15 rather than 20 damage"), Number(First(Array(Battle(Simulation), TEXT("troops"))), TEXT("hp")), 985.);
    const FObject After = Simulation.Serialize(); FBYMSimulation Reloaded; if (!Load(*this, Reloaded, After)) return false;
    for (int32 Tick = 0; Tick < 10; ++Tick) Step(Reloaded, Now);
    TestEqual(TEXT("Damage scaling does not bypass the saved tower cooldown"), Number(First(Array(Battle(Reloaded), TEXT("troops"))), TEXT("hp")), 985.);

    // The original BTOWER.canAttack excludes build/upgrade countdowns, but
    // allows a living tower to defend while its repair countdown is running.
    FObject RepairingTower = Find(Array(Yard(Save, Defender), TEXT("buildings")), TEXT("tower"));
    RepairingTower->SetBoolField(TEXT("repairing"), true);
    RepairingTower->SetNumberField(TEXT("readyAt"), Now + 600);
    FBYMSimulation Repairing; if (!Load(*this, Repairing, Save)) return false;
    double RepairNow = Number(Snapshot(Repairing), TEXT("now")); Step(Repairing, RepairNow);
    TestTrue(TEXT("A living Cannon fires while being repaired"), Number(First(Array(Battle(Repairing), TEXT("troops"))), TEXT("hp")) < 1000.);
    TestEqual(TEXT("Repairing Cannon emits one authoritative shot"), CountEvents(Battle(Repairing), TEXT("defenseAttack")), 1);
    RepairingTower->SetBoolField(TEXT("repairing"), false);
    RepairingTower->SetNumberField(TEXT("targetLevel"), 2);
    Yard(Save, Defender)->RemoveField(TEXT("workerState")); // Synthetic pre-worker paid job.
    RepairingTower->SetStringField(TEXT("jobId"), TEXT("cannon-upgrade-fixture")); RepairingTower->SetNumberField(TEXT("workRemaining"), 600);
    FBYMSimulation Upgrading; if (!Load(*this, Upgrading, Save)) return false;
    double UpgradeNow = Number(Snapshot(Upgrading), TEXT("now")); Step(Upgrading, UpgradeNow);
    TestEqual(TEXT("A Cannon being upgraded cannot fire"), Number(First(Array(Battle(Upgrading), TEXT("troops"))), TEXT("hp")), 1000.);
    TestEqual(TEXT("Upgrade emits no defense shot"), CountEvents(Battle(Upgrading), TEXT("defenseAttack")), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBattleEndAccrualTest, "BackyardMMO.Combat.BattleEndAccrual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMBattleEndAccrualTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    // Both terminal paths must settle an unprocessed defender interval before
    // battle removal. Ordinary yard batching must not hide a lost interval.
    for (const bool bContested : {false, true})
    {
        FArray Buildings;
        Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("town"), 14, 4, 4, 4000)));
        Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("tower"), 20, 20, 20, 3000)));
        Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("collector"), 1, 30, 30, 500)));
        FObject Save = Fixture(*this, TEXT("C2"), Buildings); if (!Save) return false;
        FBYMSimulation Prepared; if (!Load(*this, Prepared, Save)) return false;
        if (!TestTrue(TEXT("Begin accrual regression invasion"), Accepted(Prepared.Execute(Attacker, TEXT("invade"), TEXT("{\"target\":\"parity-defender\"}"))))) return false;
        if (bContested && !TestTrue(TEXT("Make the invasion contested"), Accepted(Prepared.Execute(Attacker, TEXT("deploy"), TEXT("{\"type\":\"C2\",\"count\":1,\"x\":49,\"y\":20}"))))) return false;
        Save = Prepared.Serialize(); const FObject DefenderYard = Yard(Save, Defender);
        const double Start = Number(Snapshot(Prepared), TEXT("now"));
        DefenderYard->SetNumberField(TEXT("lastEconomy"), Start - 10);
        const FObject RepairingTower = Find(Array(DefenderYard, TEXT("buildings")), TEXT("tower"));
        RepairingTower->SetBoolField(TEXT("repairing"), true); RepairingTower->SetNumberField(TEXT("readyAt"), Start + 600);
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        if (!TestTrue(TEXT("Retreat/cancel settles the defender"), Accepted(Simulation.Execute(Attacker, TEXT("retreat"), TEXT("{}"))))) return false;
        const FObject Ended = Yard(Simulation.Serialize(), Defender);
        TestTrue(TEXT("EndBattle advances the elapsed repair interval"), Number(Find(Array(Ended, TEXT("buildings")), TEXT("tower")), TEXT("hp")) > 3000.);
        TestEqual(TEXT("Collectors stay blocked until battle removal"), Number(Find(Array(Ended, TEXT("buildings")), TEXT("collector")), TEXT("stored")), 0.);
        TestEqual(TEXT("Accrual cursor reaches the actual ending time"), Number(Ended, TEXT("lastEconomy")), Number(Snapshot(Simulation), TEXT("now")));
        TestFalse(TEXT("The terminal invasion is removed"), Simulation.HasActiveBattles());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMDecorationCombatExclusionTest, "BackyardMMO.Combat.DecorationsDoNotChangeVictoryOrTakeBlastDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMDecorationCombatExclusionTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    for (const bool bDecorated : {false, true})
    {
        FArray Buildings;
        Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("wall"), 17, 22, 20, 1000)));
        Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("town"), 14, 4, 4, 4000)));
        if (bDecorated)
        {
            Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("nearby-flag"), 28, 25, 21, 100)));
            Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("nearby-acorn"), 55, 27, 20, 100)));
            Buildings.Add(MakeShared<FJsonValueObject>(Building(TEXT("remote-flag"), 29, 35, 5, 100)));
        }
        FObject Save = DeployedSave(*this, Fixture(*this, TEXT("C5"), Buildings), TEXT("C5"));
        if (!Save) return false;
        const FObject InitialBattle = First(Array(Save, TEXT("battles")));
        TestEqual(TEXT("Victory denominator includes only the two targetable buildings"), Number(InitialBattle, TEXT("initialBuildings")), 2.);
        TestEqual(TEXT("Decorations do not inflate the initial combat health total"), Number(InitialBattle, TEXT("initialBuildingHP")), 5000.);
        const int32 MushroomsBefore = Array(Child(Yard(Save, Defender), TEXT("economy")), TEXT("mushrooms")).Num();
        const int32 WorkersBefore = Array(Child(Yard(Save, Defender), TEXT("workerState")), TEXT("entries")).Num();
        FObject Troop = SavedTroop(Save);
        Troop->SetNumberField(TEXT("x"), 23.25); Troop->SetNumberField(TEXT("y"), 20.25);
        Troop->SetStringField(TEXT("target"), TEXT("wall")); Troop->SetNumberField(TEXT("attackCooldown"), 0);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        double Now = Number(Snapshot(Sim), TEXT("now")); Step(Sim, Now);
        TestFalse(TEXT("Destroying one of two combat buildings wins even in a decorated yard"), Sim.HasActiveBattles());
        const FObject Ended = Sim.Serialize(), DefenderYard = Yard(Ended, Defender), AttackerYard = Yard(Ended, Attacker);
        TestEqual(TEXT("Half-destruction victory is awarded exactly once"), Number(AttackerYard, TEXT("wins")), 1.);
        TestEqual(TEXT("The remaining Town Hall is still standing"), Number(Find(Array(DefenderYard, TEXT("buildings")), TEXT("town")), TEXT("hp")), 4000.);
        if (bDecorated)
            for (const TCHAR* Id : {TEXT("nearby-flag"), TEXT("nearby-acorn"), TEXT("remote-flag")})
                TestEqual(TEXT("Original decorations do not take monster splash damage"), Number(Find(Array(DefenderYard, TEXT("buildings")), Id), TEXT("hp")), 100.);
        TestEqual(TEXT("Combat does not remove or attack mushrooms"), Array(Child(DefenderYard, TEXT("economy")), TEXT("mushrooms")).Num(), MushroomsBefore);
        TestEqual(TEXT("Combat does not remove authoritative worker records"), Array(Child(DefenderYard, TEXT("workerState")), TEXT("entries")).Num(), WorkersBefore);
        Step(Sim, Now);
        TestEqual(TEXT("Terminal battle cannot award its victory twice"), Number(Yard(Sim.Serialize(), Attacker), TEXT("wins")), 1.);
    }
    return true;
}
}
#endif
