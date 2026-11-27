#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Exercise the real authority and bundled original catalog. Fixtures are native
// save objects in memory: no account, network listener or on-disk save is created.
namespace BYMCombatPresentationTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Attacker(TEXT("combat-attacker"));
    const FString Defender(TEXT("combat-defender"));
    const FString TownId(TEXT("combat-town"));
    const FString ObstacleId(TEXT("combat-obstacle"));
    const FString TowerId(TEXT("combat-tower"));

    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Bots = Settings->RivalBotCount;
        const int32 Cells = Settings->YardCells;
        const int32 Starting = Settings->StartingResources;
        const int32 Troops = Settings->MaximumTroopsPerBattle;
        const int32 Battles = Settings->MaximumConcurrentBattles;
        const int32 Residents = Settings->MaxVisibleYardMonsters;
        const float Damage = Settings->CombatDamageMultiplier;
        const float Movement = Settings->MonsterMovementMultiplier;
        const float BattleSeconds = Settings->MaximumBattleSeconds;
        const float CombatStep = Settings->CombatSimulationStepSeconds;
        FSettingsGuard()
        {
            Settings->RivalBotCount = 0;
            Settings->YardCells = 40;
            Settings->StartingResources = 5000;
            Settings->MaximumTroopsPerBattle = 100;
            Settings->MaximumConcurrentBattles = 32;
            Settings->MaxVisibleYardMonsters = 50;
            Settings->CombatDamageMultiplier = 1;
            Settings->MonsterMovementMultiplier = 1;
            Settings->MaximumBattleSeconds = 300;
            Settings->CombatSimulationStepSeconds = 0.1f;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots;
            Settings->YardCells = Cells;
            Settings->StartingResources = Starting;
            Settings->MaximumTroopsPerBattle = Troops;
            Settings->MaximumConcurrentBattles = Battles;
            Settings->MaxVisibleYardMonsters = Residents;
            Settings->CombatDamageMultiplier = Damage;
            Settings->MonsterMovementMultiplier = Movement;
            Settings->MaximumBattleSeconds = BattleSeconds;
            Settings->CombatSimulationStepSeconds = CombatStep;
        }
    };

    FObject Decode(const FString& Text)
    {
        FObject Object;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
        return Object;
    }
    FString Encode(const FObject& Object)
    {
        FString Text;
        if (Object) FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        return Text;
    }
    FObject Clone(const FObject& Object) { return Decode(Encode(Object)); }
    FObject Child(const FObject& Object, const TCHAR* Field)
    {
        const FObject* Value = nullptr;
        return Object && Object->TryGetObjectField(Field, Value) && Value ? *Value : FObject();
    }
    const FArray& Array(const FObject& Object, const TCHAR* Field)
    {
        static const FArray Empty;
        const FArray* Value = nullptr;
        return Object && Object->TryGetArrayField(Field, Value) && Value ? *Value : Empty;
    }
    void SetLegacySchema(const FObject& Save, int32 Schema)
    {
        Save->SetNumberField(TEXT("schema"), Schema);
        for (const auto& YardValue : Array(Save, TEXT("yards")))
        {
            const FObject LegacyYard = YardValue->AsObject();
            LegacyYard->RemoveField(TEXT("earnedExperience")); LegacyYard->RemoveField(TEXT("baseValue"));
            if (Schema < 3) LegacyYard->RemoveField(TEXT("hatcheries"));
            for (const auto& BuildingValue : Array(LegacyYard, TEXT("buildings")))
            {
                BuildingValue->AsObject()->RemoveField(TEXT("jobId"));
                BuildingValue->AsObject()->RemoveField(TEXT("workRemaining"));
            }
        }
    }
    double Number(const FObject& Object, const TCHAR* Field, double Missing = -1)
    {
        double Value = Missing;
        if (Object) Object->TryGetNumberField(Field, Value);
        return Value;
    }
    FString String(const FObject& Object, const TCHAR* Field)
    {
        FString Value;
        if (Object) Object->TryGetStringField(Field, Value);
        return Value;
    }
    bool Accepted(const FString& Reply)
    {
        const FObject Object = Decode(Reply);
        bool Value = false;
        return Object && Object->TryGetBoolField(TEXT("ok"), Value) && Value;
    }
    FObject Snapshot(const FBYMSimulation& Simulation)
    { return Decode(Simulation.Snapshot(Attacker, FString())); }
    FObject Battle(const FBYMSimulation& Simulation) { return Child(Snapshot(Simulation), TEXT("battle")); }
    FObject Find(const FArray& Values, const FString& Id)
    {
        for (const TSharedPtr<FJsonValue>& Value : Values)
            if (Value.IsValid() && Value->Type == EJson::Object && String(Value->AsObject(), TEXT("id")) == Id) return Value->AsObject();
        return nullptr;
    }
    FObject SavedYard(const FObject& Save, const FString& Id) { return Find(Array(Save, TEXT("yards")), Id); }
    FObject FirstTroop(const FObject& BattleObject)
    {
        const FArray& Troops = Array(BattleObject, TEXT("troops"));
        return Troops.IsEmpty() ? FObject() : Troops[0]->AsObject();
    }
    FObject SavedBattle(const FObject& Save)
    {
        const FArray& Battles = Array(Save, TEXT("battles"));
        return Battles.IsEmpty() ? FObject() : Battles[0]->AsObject();
    }
    double BuildingHP(const FBYMSimulation& Simulation, const FString& Id)
    { return Number(Find(Array(Child(Snapshot(Simulation), TEXT("yard")), TEXT("buildings")), Id), TEXT("hp")); }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    {
        FString Error;
        if (Simulation.Initialize(Save, Error)) return true;
        Test.AddError(TEXT("Combat fixture load: ") + Error);
        return false;
    }
    FObject Building(const FString& Id, int32 Type, int32 X, int32 Y, double HP)
    {
        FObject Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("id"), Id);
        Object->SetNumberField(TEXT("type"), Type);
        Object->SetNumberField(TEXT("level"), 1);
        Object->SetNumberField(TEXT("targetLevel"), 1);
        Object->SetNumberField(TEXT("x"), X);
        Object->SetNumberField(TEXT("y"), Y);
        Object->SetNumberField(TEXT("hp"), HP);
        Object->SetNumberField(TEXT("readyAt"), 0);
        Object->SetNumberField(TEXT("stored"), 0);
        Object->SetStringField(TEXT("jobId"), FString()); Object->SetNumberField(TEXT("workRemaining"), 0);
        Object->SetBoolField(TEXT("repairing"), false);
        return Object;
    }
    FObject MakeFixture(FAutomationTestBase& Test, bool bObstacle = false, bool bTower = false, int32 PokeyLevel = 1)
    {
        FBYMSimulation Starter;
        if (!Load(Test, Starter, nullptr)) return nullptr;
        FString Error;
        if (!Starter.AddPlayer(Attacker, Attacker, Error) || !Starter.AddPlayer(Defender, Defender, Error))
        { Test.AddError(TEXT("Combat fixture player creation: ") + Error); return nullptr; }
        FObject Save = Starter.Serialize();
        FObject OwnYard = SavedYard(Save, Attacker);
        FObject Army = MakeShared<FJsonObject>(); Army->SetNumberField(TEXT("C1"), 10); Army->SetNumberField(TEXT("C2"), 10);
        FObject Levels = MakeShared<FJsonObject>(); Levels->SetNumberField(TEXT("C1"), PokeyLevel); Levels->SetNumberField(TEXT("C2"), 1);
        OwnYard->SetObjectField(TEXT("army"), Army); OwnYard->SetObjectField(TEXT("monsterLevels"), Levels);
        FArray Buildings;
        Buildings.Add(MakeShared<FJsonValueObject>(Building(TownId, 14, bTower ? 4 : 20, bTower ? 4 : 20, 4000)));
        // Original Twig Snapper collision is 70x70 px; its placement box is 100x100.
        if (bObstacle) Buildings.Add(MakeShared<FJsonValueObject>(Building(ObstacleId, 1, 30, 21, 500)));
        if (bTower) Buildings.Add(MakeShared<FJsonValueObject>(Building(TowerId, 20, 20, 20, 6000)));
        SavedYard(Save, Defender)->SetArrayField(TEXT("buildings"), Buildings);
        // This combat yard intentionally has no Hatchery. Its per-Hatchery
        // production state must match the replacement building fixture.
        SavedYard(Save, Defender)->SetArrayField(TEXT("hatcheries"), FArray());
        return Save;
    }
    bool InvadeAndDeploy(FAutomationTestBase& Test, FBYMSimulation& Simulation, const TCHAR* Type = TEXT("C1"), int32 Count = 1)
    {
        if (!Test.TestTrue(TEXT("A real host command starts the fixture invasion"), Accepted(Simulation.Execute(Attacker, TEXT("invade"), TEXT("{\"target\":\"combat-defender\"}"))))) return false;
        if (!Test.TestTrue(TEXT("A real host command deploys ready troops from the outer edge"), Accepted(Simulation.Execute(Attacker, TEXT("deploy"),
            FString::Printf(TEXT("{\"type\":\"%s\",\"count\":%d,\"x\":49,\"y\":23}"), Type, Count))))) return false;
        // Keep these source contact/rate fixtures at their original 13-cell
        // approach distance after the actual rectangular-edge deployment.
        const FObject Save = Simulation.Serialize();
        for (const auto& Value : Array(SavedBattle(Save), TEXT("troops")))
            Value->AsObject()->SetNumberField(TEXT("x"), Number(Value->AsObject(), TEXT("x")) - 10.);
        return Load(Test, Simulation, Save);
    }
    void Step(FBYMSimulation& Simulation, double& Now)
    { Now += 0.1; Simulation.Tick(Now, 0.1f); }
    FObject Event(const FObject& BattleObject, const TCHAR* Kind)
    {
        for (const TSharedPtr<FJsonValue>& Value : Array(BattleObject, TEXT("events")))
            if (String(Value->AsObject(), TEXT("kind")) == Kind) return Value->AsObject();
        return nullptr;
    }
    double TownContactDistance(const FObject& Troop)
    {
        // Fixture geometry from the original Town Hall's 130x130 px collision,
        // at (20,20), with 20 source pixels per grid cell. Do not use its 190px art.
        const double X = Number(Troop, TEXT("x")), Y = Number(Troop, TEXT("y"));
        return FVector2D(X - FMath::Clamp(X, 20.0, 26.5), Y - FMath::Clamp(Y, 20.0, 26.5)).Size();
    }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMeleeContactAndEventsTest, "BackyardMMO.Combat.MeleeContactAndDiscreteEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMMeleeContactAndEventsTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this, false, false, 2);
    FBYMSimulation Simulation;
    if (!Fixture || !Load(*this, Simulation, Fixture) || !InvadeAndDeploy(*this, Simulation)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now"));
    double PreviousHP = 4000, PreviousX = Number(FirstTroop(Battle(Simulation)), TEXT("x"));
    bool bMovedBeforeDamage = false, bMovingState = false, bFarDamage = false, bPartialHit = false;
    int32 HitCount = 0;
    double FirstHitAt = -1;
    TSet<FString> SeenEventIds;
    for (int32 Tick = 0; Tick < 200 && HitCount < 3; ++Tick)
    {
        Step(Simulation, Now);
        const FObject Current = Battle(Simulation), Troop = FirstTroop(Current);
        if (!TestTrue(TEXT("The approach remains an active invasion with one troop"), Current && Troop)) return false;
        const double HP = BuildingHP(Simulation, TownId), X = Number(Troop, TEXT("x"));
        if (HP == 4000 && X < PreviousX)
        { bMovedBeforeDamage = true; bMovingState |= String(Troop, TEXT("state")) == TEXT("moving"); }
        // The original isometric metric permits a 12px contact radius to span
        // up to .85 logical cells on its compressed diagonal. The right-hand
        // approach must still reach the actual 26.5-cell collision edge, never
        // the 30-cell right edge of the larger art/placement rectangle.
        if ((TownContactDistance(Troop) > 0.85 || X > 27.2) && HP < PreviousHP) bFarDamage = true;
        if (HP < PreviousHP)
        {
            ++HitCount;
            bPartialHit |= !FMath::IsNearlyEqual(PreviousHP - HP, 65.0, 0.0001);
            TestEqual(TEXT("A committed hit exposes the attacking state"), String(Troop, TEXT("state")), FString(TEXT("attacking")));
            TestTrue(TEXT("Attack animation starts at the authoritative simulation timestamp"),
                FMath::IsNearlyEqual(Number(Troop, TEXT("lastAttackAt")), Number(Current, TEXT("serverTime")), 0.0001));
            TestEqual(TEXT("The presentation target is the building actually receiving damage"), String(Troop, TEXT("target")), TownId);
            TestTrue(TEXT("The hit point is inside the original Town Hall collision rectangle"),
                Number(Troop, TEXT("targetX")) >= 20 && Number(Troop, TEXT("targetX")) <= 26.5001
                && Number(Troop, TEXT("targetY")) >= 20 && Number(Troop, TEXT("targetY")) <= 26.5001);
            TestTrue(TEXT("The presentation facing vector is usable by directional sprite animation"),
                FMath::Square(Number(Troop, TEXT("facingX"), 0)) + FMath::Square(Number(Troop, TEXT("facingY"), 0)) > 0.9);
            TestEqual(TEXT("A troop keeps its deployed research level"), Number(Troop, TEXT("level")), 2.0);
            TestEqual(TEXT("Level-two max HP comes from original level data"), Number(Troop, TEXT("maxHp")), 220.0);
            TestTrue(TEXT("Original 60-tick attack delay uses the source 72 Hz simulation clock"), FMath::IsNearlyEqual(Number(Troop, TEXT("attackInterval")), 60.0 / 72.0, 0.0001));
            if (FirstHitAt < 0) FirstHitAt = Now;
            const double Due = (HitCount - 1) * 60.0 / 72.0;
            TestTrue(TEXT("Whole attacks retain the source cadence without accumulating host-step rounding"),
                Now - FirstHitAt >= Due - 0.0001 && Now - FirstHitAt <= Due + 0.1001);
            TestEqual(TEXT("Hit sequence advances once per authority attack"), Number(Troop, TEXT("attackSequence")), static_cast<double>(HitCount));
        }
        for (const TSharedPtr<FJsonValue>& Value : Array(Current, TEXT("events")))
        {
            const FObject Attack = Value->AsObject();
            if (String(Attack, TEXT("kind")) != TEXT("monsterAttack")) continue;
            const FString Id = String(Attack, TEXT("id"));
            if (SeenEventIds.Contains(Id)) continue; // Repeated network snapshots retain a recent event with the same ID.
            SeenEventIds.Add(Id);
            TestFalse(TEXT("An attack event has a stable nonempty identity"), Id.IsEmpty());
            TestEqual(TEXT("Attack event identifies the actual troop"), String(Attack, TEXT("actor")), String(Troop, TEXT("id")));
            TestEqual(TEXT("Attack event identifies the attacked building"), String(Attack, TEXT("target")), TownId);
            TestEqual(TEXT("Attack event carries one original level-two hit"), Number(Attack, TEXT("damage")), 65.0);
        }
        PreviousX = X; PreviousHP = HP;
    }
    TestTrue(TEXT("Melee troops visibly approach before damage begins"), bMovedBeforeDamage);
    TestTrue(TEXT("Movement snapshots select the walk animation state"), bMovingState);
    TestFalse(TEXT("A Town Hall's larger art rectangle never permits a distant melee hit"), bFarDamage);
    TestFalse(TEXT("Melee damage occurs as whole attacks, not fractional tick damage"), bPartialHit);
    TestEqual(TEXT("The moving troop reaches the building and makes three attacks"), HitCount, 3);
    TestEqual(TEXT("Each committed attack creates exactly one identifiable event"), SeenEventIds.Num(), 3);
    const FObject Before = Battle(Simulation);
    const FObject After = Battle(Simulation);
    TestEqual(TEXT("Reading another snapshot does not advance the attack sequence"), Number(FirstTroop(After), TEXT("attackSequence")), Number(FirstTroop(Before), TEXT("attackSequence")));
    TestEqual(TEXT("Repeated snapshots retain event identity rather than replaying a new attack"), String(Event(After, TEXT("monsterAttack")), TEXT("id")), String(Event(Before, TEXT("monsterAttack")), TEXT("id")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMeleeBlockedApproachTest, "BackyardMMO.Combat.BlockedApproachUsesOriginalFootprints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMMeleeBlockedApproachTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this, true);
    FBYMSimulation Prepared;
    if (!Fixture || !Load(*this, Prepared, Fixture) || !InvadeAndDeploy(*this, Prepared)) return false;
    FObject Save = Prepared.Serialize();
    // Preserve an already-selected live target across reload. The other live
    // building blocks the direct line and must be walked around, not crossed.
    FirstTroop(SavedBattle(Save))->SetStringField(TEXT("target"), TownId);
    FBYMSimulation Simulation;
    if (!Load(*this, Simulation, Save)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now"));
    bool bCrossedObstacle = false, bDetoured = false, bFarDamage = false, bReached = false;
    for (int32 Tick = 0; Tick < 240 && !bReached; ++Tick)
    {
        Step(Simulation, Now);
        const FObject Troop = FirstTroop(Battle(Simulation));
        if (!TestTrue(TEXT("Blocked approach retains its deployed monster"), Troop.IsValid())) return false;
        const double X = Number(Troop, TEXT("x")), Y = Number(Troop, TEXT("y"));
        bCrossedObstacle |= X > 30.0 && X < 33.5 && Y > 21.0 && Y < 24.5;
        bDetoured |= X >= 29.0 && X <= 34.5 && (Y <= 21.0 || Y >= 24.5);
        bReached = BuildingHP(Simulation, TownId) < 4000;
        bFarDamage |= bReached && TownContactDistance(Troop) > 0.85;
    }
    TestFalse(TEXT("Ground movement cannot pass through a live source collision rectangle"), bCrossedObstacle);
    TestTrue(TEXT("The path moves around the blocking building before attacking"), bDetoured);
    TestTrue(TEXT("A reachable selected target is eventually attacked"), bReached);
    TestFalse(TEXT("An obstacle does not permit damage from a distant path node"), bFarDamage);
    TestEqual(TEXT("The path does not attack an unrelated reachable obstacle instead"), BuildingHP(Simulation, ObstacleId), 500.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCombatCooldownPersistenceTest, "BackyardMMO.Combat.CooldownSaveResumeCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMCombatCooldownPersistenceTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this);
    FBYMSimulation Simulation;
    if (!Fixture || !Load(*this, Simulation, Fixture) || !InvadeAndDeploy(*this, Simulation)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now"));
    for (int32 Tick = 0; Tick < 160 && BuildingHP(Simulation, TownId) == 4000; ++Tick) Step(Simulation, Now);
    if (!TestEqual(TEXT("A source level-one hit occurs before the save"), BuildingHP(Simulation, TownId), 3940.0)) return false;
    const double FirstHitAt = Now;
    const FObject Save = Simulation.Serialize();
    TestTrue(TEXT("The full remaining attack cooldown is persisted"), FMath::IsNearlyEqual(Number(FirstTroop(SavedBattle(Save)), TEXT("attackCooldown")), 60.0 / 72.0, 0.0001));
    TestFalse(TEXT("Transient presentation events are not replayed from a native save"), SavedBattle(Save)->HasField(TEXT("events")));
    FBYMSimulation Reloaded;
    if (!Load(*this, Reloaded, Save)) return false;
    Reloaded.Tick(Now, 0.0f);
    TestEqual(TEXT("Reload starts with no historical impact events"), Array(Battle(Reloaded), TEXT("events")).Num(), 0);
    for (int32 Tick = 1; Tick < 9; ++Tick)
    {
        Step(Reloaded, Now);
        TestEqual(TEXT("Reload cannot grant an extra hit during the saved cooldown"), BuildingHP(Reloaded, TownId), 3940.0);
    }
    Step(Reloaded, Now);
    TestEqual(TEXT("The next full hit resumes at the original cooldown boundary"), BuildingHP(Reloaded, TownId), 3880.0);
    TestTrue(TEXT("The resumed hit occurs on the first simulation step after 60 source ticks"), FMath::IsNearlyEqual(Now - FirstHitAt, 0.9, 0.0001));
    const FObject Legacy = Clone(Save);
    SetLegacySchema(Legacy, 2);
    FirstTroop(SavedBattle(Legacy))->RemoveField(TEXT("attackCooldown"));
    FBYMSimulation LegacyReload;
    TestTrue(TEXT("Existing schema-two saves without the new cooldown still load"), Load(*this, LegacyReload, Legacy));
    const FObject Invalid = Clone(Save);
    FirstTroop(SavedBattle(Invalid))->SetNumberField(TEXT("attackCooldown"), -1);
    FString Error;
    FBYMSimulation InvalidReload;
    TestFalse(TEXT("A malformed negative saved cooldown is rejected"), InvalidReload.Initialize(Invalid, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMFlyingBlockedLineTest, "BackyardMMO.Combat.FlyingRangedBlockedLineContinuesMoving",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMFlyingBlockedLineTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this, true);
    if (!Fixture) return false;
    const FObject Own = SavedYard(Fixture, Attacker);
    FObject Army = MakeShared<FJsonObject>(); Army->SetNumberField(TEXT("C14"), 1);
    Own->SetObjectField(TEXT("army"), Army);
    Child(Own, TEXT("monsterLevels"))->SetNumberField(TEXT("C14"), 1);
    FBYMSimulation Prepared;
    if (!Load(*this, Prepared, Fixture) || !InvadeAndDeploy(*this, Prepared, TEXT("C14"))) return false;
    FObject Save = Prepared.Serialize();
    const FObject SavedTroop = FirstTroop(SavedBattle(Save));
    SavedTroop->SetStringField(TEXT("target"), TownId);
    // This in-flight position is already inside Teratorn's original 150px
    // range, but the Twig Snapper interrupts the line to the Town Hall. A
    // flying unit must keep moving instead of freezing at nominal range.
    SavedTroop->SetNumberField(TEXT("x"), 34);
    SavedTroop->SetNumberField(TEXT("y"), 22.5);
    FBYMSimulation Simulation;
    if (!Load(*this, Simulation, Save)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now"));
    Step(Simulation, Now);
    TestTrue(TEXT("A blocked flying attacker continues moving inside nominal attack range"), Number(FirstTroop(Battle(Simulation)), TEXT("x")) < 34);
    TestEqual(TEXT("A blocked line cannot apply remote damage through the obstacle"), BuildingHP(Simulation, TownId), 4000.0);
    for (int32 Tick = 0; Tick < 120 && BuildingHP(Simulation, TownId) == 4000; ++Tick) Step(Simulation, Now);
    TestEqual(TEXT("The original Teratorn hit lands after obtaining a clear line"), BuildingHP(Simulation, TownId), 3700.0);
    TestEqual(TEXT("Flying past the obstacle does not destroy it as a pathfinding shortcut"), BuildingHP(Simulation, ObstacleId), 500.0);
    const FObject Attack = Event(Battle(Simulation), TEXT("monsterAttack"));
    bool bRanged = false;
    if (Attack) Attack->TryGetBoolField(TEXT("ranged"), bRanged);
    TestTrue(TEXT("The cleared shot is explicitly presented as a ranged attack"), bRanged);
    TestEqual(TEXT("The ranged event retains its original monster species"), String(Attack, TEXT("monsterType")), FString(TEXT("C14")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMDefenseEventsAndRangeTest, "BackyardMMO.Combat.DefenseTargetRangeAndCooldown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMDefenseEventsAndRangeTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this, false, true);
    if (!Fixture) return false;
    const FObject Own = SavedYard(Fixture, Attacker);
    Child(Own, TEXT("army"))->SetNumberField(TEXT("C1"), 0);
    Child(Own, TEXT("army"))->SetNumberField(TEXT("C14"), 1);
    Child(Own, TEXT("monsterLevels"))->SetNumberField(TEXT("C14"), 1);
    FBYMSimulation Prepared;
    if (!Load(*this, Prepared, Fixture) || !InvadeAndDeploy(*this, Prepared, TEXT("C2"), 2)) return false;
    if (!TestTrue(TEXT("The fixture also deploys a genuine unlocked airborne Teratorn"),
        Accepted(Prepared.Execute(Attacker, TEXT("deploy"), TEXT("{\"type\":\"C14\",\"count\":1,\"x\":49,\"y\":23}"))))) return false;
    FObject Save = Prepared.Serialize();
    const FArray& SavedTroops = Array(SavedBattle(Save), TEXT("troops"));
    if (!TestEqual(TEXT("Three real deployments are available for the target-selection fixture"), SavedTroops.Num(), 3)) return false;
    const FObject Near = SavedTroops[0]->AsObject(), Far = SavedTroops[1]->AsObject(), Air = SavedTroops[2]->AsObject();
    const FString NearId = String(Near, TEXT("id")), FarId = String(Far, TEXT("id")), AirId = String(Air, TEXT("id"));
    // Tower range is measured after FromISO in logical coordinates. This
    // ground troop is inside radius eight, but outside an incorrect projected
    // radius eight. The even closer flying troop is an invalid Cannon target.
    Near->SetNumberField(TEXT("x"), 29.5); Near->SetNumberField(TEXT("y"), 21.75);
    Far->SetNumberField(TEXT("x"), 39); Far->SetNumberField(TEXT("y"), 2);
    Air->SetNumberField(TEXT("x"), 23.75); Air->SetNumberField(TEXT("y"), 21.75);
    // Keep the airborne unit's own hit outside this short defense observation.
    Air->SetNumberField(TEXT("attackCooldown"), 60);
    FBYMSimulation Simulation;
    if (!Load(*this, Simulation, Save)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now"));
    Step(Simulation, Now);
    const FObject First = Battle(Simulation), Shot = Event(First, TEXT("defenseAttack"));
    if (!TestTrue(TEXT("A ready Cannon Tower emits a real firing event"), Shot.IsValid())) return false;
    TestEqual(TEXT("The firing event identifies the Cannon Tower"), String(Shot, TEXT("actor")), TowerId);
    const FObject DefenseVisual = Find(Array(First, TEXT("defenses")), TowerId);
    TestEqual(TEXT("The defense snapshot selects its source firing animation"), String(DefenseVisual, TEXT("state")), FString(TEXT("attacking")));
    TestEqual(TEXT("The defense animation sequence advances once for this shot"), Number(DefenseVisual, TEXT("attackSequence")), 1.0);
    TestTrue(TEXT("Defense animation shares the firing event's authoritative timestamp"),
        FMath::IsNearlyEqual(Number(DefenseVisual, TEXT("lastAttackAt")), Number(Shot, TEXT("at")), 0.0001));
    TestEqual(TEXT("The firing event selects a ground troop inside the original Cartesian range"), String(Shot, TEXT("target")), NearId);
    TestEqual(TEXT("A Cannon Tower delivers its original level-one hit"), Number(Shot, TEXT("damage")), 20.0);
    TestEqual(TEXT("Only the in-range troop loses health"), Number(Find(Array(First, TEXT("troops")), NearId), TEXT("hp")), 980.0);
    TestEqual(TEXT("The distant troop does not receive impossible defense damage"), Number(Find(Array(First, TEXT("troops")), FarId), TEXT("hp")), 1000.0);
    TestEqual(TEXT("A closer flying monster cannot become a ground-only Cannon target"), Number(Find(Array(First, TEXT("troops")), AirId), TEXT("hp")), 1600.0);
    const FObject CooldownSave = Simulation.Serialize();
    TestTrue(TEXT("The original Cannon fireTick rate-times-two cooldown is saved"), FMath::IsNearlyEqual(Number(Child(SavedBattle(CooldownSave), TEXT("defenseCooldown")), *TowerId), 80.0 / 72.0, 0.0001));
    FBYMSimulation Reloaded;
    if (!Load(*this, Reloaded, CooldownSave)) return false;
    Reloaded.Tick(Now, 0.0f);
    for (int32 Tick = 0; Tick < 11; ++Tick)
    {
        Step(Reloaded, Now);
        TestEqual(TEXT("Defense reload preserves the cooldown instead of firing every snapshot"), Number(Find(Array(Battle(Reloaded), TEXT("troops")), NearId), TEXT("hp")), 980.0);
    }
    Step(Reloaded, Now);
    TestEqual(TEXT("Defense fires again on the first simulation step after its cooldown"), Number(Find(Array(Battle(Reloaded), TEXT("troops")), NearId), TEXT("hp")), 960.0);
    TestEqual(TEXT("Distant troop stays outside the defense's damage selection"), Number(Find(Array(Battle(Reloaded), TEXT("troops")), FarId), TEXT("hp")), 1000.0);
    TestEqual(TEXT("The original ground-only target filter survives native save reload"), Number(Find(Array(Battle(Reloaded), TEXT("troops")), AirId), TEXT("hp")), 1600.0);
    TestEqual(TEXT("One post-reload shot creates one presentation event"), Array(Battle(Reloaded), TEXT("events")).Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMFinalCombatEventsTest, "BackyardMMO.Combat.TerminalImpactPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMFinalCombatEventsTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this);
    if (!Fixture) return false;
    Find(Array(SavedYard(Fixture, Defender), TEXT("buildings")), TownId)->SetNumberField(TEXT("hp"), 60);
    FBYMSimulation Simulation;
    if (!Load(*this, Simulation, Fixture) || !InvadeAndDeploy(*this, Simulation)) return false;
    double Now = Number(Snapshot(Simulation), TEXT("now"));
    for (int32 Tick = 0; Tick < 180 && Battle(Simulation).IsValid(); ++Tick) Step(Simulation, Now);
    if (!TestFalse(TEXT("Destroying the Town Hall completes the authoritative battle"), Battle(Simulation).IsValid())) return false;
    const FObject Result = Decode(Simulation.Snapshot(Attacker, Defender));
    const FObject Completed = Child(Result, TEXT("completedBattle"));
    if (!TestTrue(TEXT("The final impact survives removal of the active battle long enough to be rendered"), Completed.IsValid())) return false;
    TestEqual(TEXT("Terminal presentation is explicitly completed"), String(Completed, TEXT("phase")), FString(TEXT("completed")));
    bool bCanDeploy = true; Completed->TryGetBoolField(TEXT("canDeploy"), bCanDeploy);
    TestFalse(TEXT("Terminal effects never reopen deployment"), bCanDeploy);
    TestTrue(TEXT("The killing hit remains available for its attack animation"), Event(Completed, TEXT("monsterAttack")).IsValid());
    const FObject Destroyed = Event(Completed, TEXT("buildingDestroyed"));
    TestTrue(TEXT("The Town Hall destruction event remains available for its effects"), Destroyed.IsValid());
    TestEqual(TEXT("The destruction event identifies the actual Town Hall"), String(Destroyed, TEXT("target")), TownId);
    TestTrue(TEXT("The normal battle-result log is preserved beside terminal presentation"), Child(Result, TEXT("lastBattle")).IsValid());
    const FObject Saved = Simulation.Serialize();
    TestEqual(TEXT("A finished presentation is not persisted as a live battle"), Array(Saved, TEXT("battles")).Num(), 0);
    TestFalse(TEXT("Transient terminal effects are not added to native save state"), Saved->HasField(TEXT("completedBattle")));
    Simulation.Tick(Now + 3.1, 0.0f);
    TestFalse(TEXT("Terminal effects expire after their bounded presentation window"), Child(Decode(Simulation.Snapshot(Attacker, Defender)), TEXT("completedBattle")).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMTeslaBurstPersistenceTest, "BackyardMMO.Combat.TeslaBurstPhasesAndSaveResume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMTeslaBurstPersistenceTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this, false, true);
    if (!Fixture) return false;
    const FObject Own = SavedYard(Fixture, Attacker), Enemy = SavedYard(Fixture, Defender);
    FObject Army = MakeShared<FJsonObject>(); Army->SetNumberField(TEXT("C10"), 1);
    Own->SetObjectField(TEXT("army"), Army); Child(Own, TEXT("monsterLevels"))->SetNumberField(TEXT("C10"), 1);
    const FObject Tower = Find(Array(Enemy, TEXT("buildings")), TowerId);
    Tower->SetNumberField(TEXT("type"), 25); Tower->SetNumberField(TEXT("hp"), 15000);
    const FObject Town = Find(Array(Enemy, TEXT("buildings")), TownId);
    Town->SetNumberField(TEXT("level"), 4); Town->SetNumberField(TEXT("targetLevel"), 4); Town->SetNumberField(TEXT("hp"), 42000);
    FBYMSimulation Prepared;
    if (!Load(*this, Prepared, Fixture) || !InvadeAndDeploy(*this, Prepared, TEXT("C10"))) return false;
    FObject Placed = Prepared.Serialize();
    const FObject PlacedTroop = FirstTroop(SavedBattle(Placed));
    PlacedTroop->SetNumberField(TEXT("x"), 24); PlacedTroop->SetNumberField(TEXT("y"), 21.75);
    PlacedTroop->SetStringField(TEXT("target"), TowerId);
    // A full-health original D.A.V.E. survives a complete level-one burst. Its
    // own saved cooldown isolates tower timing from damage to the tower.
    PlacedTroop->SetNumberField(TEXT("attackCooldown"), 60);
    FBYMSimulation Charging;
    if (!Load(*this, Charging, Placed)) return false;
    double Now = Number(Snapshot(Charging), TEXT("now"));
    auto Health = [](const FBYMSimulation& Simulation) { return Number(FirstTroop(Battle(Simulation)), TEXT("hp")); };
    auto SavedBurst = [](const FObject& Save) { return Child(Child(SavedBattle(Save), TEXT("defenseBursts")), *TowerId); };
    auto Visual = [](const FBYMSimulation& Simulation) { return Find(Array(Battle(Simulation), TEXT("defenses")), TowerId); };
    Step(Charging, Now);
    TestEqual(TEXT("Tesla begins its original charge phase before any pulse"), String(Visual(Charging), TEXT("state")), FString(TEXT("charging")));
    TestEqual(TEXT("Beginning to charge does not damage its target"), Health(Charging), 4000.0);
    for (int32 Tick = 0; Tick < 3; ++Tick) Step(Charging, Now);
    const FObject ChargeSave = Charging.Serialize(), ChargeState = SavedBurst(ChargeSave);
    if (!TestTrue(TEXT("Tesla charge phase is part of the native battle save"), ChargeState.IsValid())) return false;
    TestEqual(TEXT("Saved Tesla remains in charging phase"), String(ChargeState, TEXT("phase")), FString(TEXT("charging")));
    TestTrue(TEXT("Original 32 display-frame charge lasts 0.8 seconds"), FMath::IsNearlyEqual(Number(ChargeState, TEXT("phaseSeconds")), 0.8, 0.0001));
    TestTrue(TEXT("Saving mid-charge retains only the remaining half-second"), FMath::IsNearlyEqual(Number(ChargeState, TEXT("remaining")), 0.5, 0.0001));
    TestEqual(TEXT("Charging reserves the original ten pulses"), Number(ChargeState, TEXT("shotsRemaining")), 10.0);
    FBYMSimulation Active;
    if (!Load(*this, Active, ChargeSave)) return false;
    Active.Tick(Now, 0.0f);
    for (int32 Tick = 0; Tick < 5; ++Tick)
    {
        Step(Active, Now);
        TestEqual(TEXT("A restored Tesla cannot fire before charging finishes"), Health(Active), 4000.0);
    }
    Step(Active, Now);
    TestEqual(TEXT("The first pulse follows the completed charge"), Health(Active), 3900.0);
    Step(Active, Now); Step(Active, Now);
    TestEqual(TEXT("Active Tesla emits one original 100-damage pulse per interval"), Health(Active), 3700.0);
    const FObject ActiveSave = Active.Serialize(), ActiveState = SavedBurst(ActiveSave);
    TestEqual(TEXT("The active phase is persisted separately from charge and cooling"), String(ActiveState, TEXT("phase")), FString(TEXT("active")));
    TestEqual(TEXT("Three fired pulses leave exactly seven, including across reload"), Number(ActiveState, TEXT("shotsRemaining")), 7.0);
    FBYMSimulation Cooling;
    if (!Load(*this, Cooling, ActiveSave)) return false;
    Cooling.Tick(Now, 0.0f);
    for (int32 Tick = 1; Tick <= 7; ++Tick)
    {
        Step(Cooling, Now);
        TestEqual(TEXT("Reload does not replenish or duplicate the remaining burst"), Health(Cooling), 3700.0 - Tick * 100.0);
    }
    TestEqual(TEXT("The finite ten-pulse burst deals exactly 1,000 damage"), Health(Cooling), 3000.0);
    TestEqual(TEXT("The last pulse starts the original cooling animation"), String(Visual(Cooling), TEXT("state")), FString(TEXT("cooling")));
    TestEqual(TEXT("The source burst ends on cooling frame 36"), Number(Visual(Cooling), TEXT("coolingStartFrame")), 36.0);
    for (int32 Tick = 0; Tick < 3; ++Tick)
    {
        Step(Cooling, Now);
        TestEqual(TEXT("Cooling never becomes an endless 0.1-second damage stream"), Health(Cooling), 3000.0);
    }
    const FObject CoolingSave = Cooling.Serialize(), CoolingState = SavedBurst(CoolingSave);
    TestEqual(TEXT("The native save retains a partially completed cooling phase"), String(CoolingState, TEXT("phase")), FString(TEXT("cooling")));
    TestTrue(TEXT("Frames 36 through 54 cool at the original half-display speed"), FMath::IsNearlyEqual(Number(CoolingState, TEXT("phaseSeconds")), 0.95, 0.0001));
    TestTrue(TEXT("Cooling reload preserves 0.65 seconds remaining"), FMath::IsNearlyEqual(Number(CoolingState, TEXT("remaining")), 0.65, 0.0001));
    TestEqual(TEXT("A cooling Tesla has no unspent pulses"), Number(CoolingState, TEXT("shotsRemaining")), 0.0);
    FBYMSimulation Resumed;
    if (!Load(*this, Resumed, CoolingSave)) return false;
    Resumed.Tick(Now, 0.0f);
    // Seven host steps finish the remaining cooling, then one starts a new
    // charge and eight complete it. None may deliver a premature next burst.
    for (int32 Tick = 0; Tick < 16; ++Tick)
    {
        Step(Resumed, Now);
        TestEqual(TEXT("Restored cooling and the next full charge remain damage-free"), Health(Resumed), 3000.0);
    }
    Step(Resumed, Now);
    TestEqual(TEXT("A fresh finite burst begins only after cooling and charging again"), Health(Resumed), 2900.0);
    const FObject Legacy = Clone(ActiveSave);
    SetLegacySchema(Legacy, 2);
    SavedBattle(Legacy)->RemoveField(TEXT("defenseBursts"));
    FBYMSimulation LegacyReload;
    TestTrue(TEXT("Earlier schema-two saves without Tesla phase state still load"), Load(*this, LegacyReload, Legacy));
    const FObject Invalid = Clone(ActiveSave);
    SavedBurst(Invalid)->SetNumberField(TEXT("shotsRemaining"), 11);
    FString Error;
    FBYMSimulation InvalidReload;
    TestFalse(TEXT("A save cannot manufacture more pulses than the original burst"), InvalidReload.Initialize(Invalid, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMPublicResidentRosterTest, "BackyardMMO.Combat.ReadyResidentsAndVisitorPrivacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMPublicResidentRosterTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FObject Fixture = MakeFixture(*this);
    if (!Fixture) return false;
    const FObject Own = SavedYard(Fixture, Attacker);
    FObject Army = MakeShared<FJsonObject>(); Army->SetNumberField(TEXT("C1"), 50); Army->SetNumberField(TEXT("C2"), 3); Army->SetNumberField(TEXT("C3"), 2);
    FObject Levels = MakeShared<FJsonObject>();
    for (const TCHAR* Type : { TEXT("C1"), TEXT("C2"), TEXT("C3"), TEXT("C4") }) Levels->SetNumberField(Type, 1);
    Own->SetObjectField(TEXT("army"), Army); Own->SetObjectField(TEXT("monsterLevels"), Levels);
    // A completed level-four Housing accommodates the 55 trained units
    // (560 of 670 housing). Queued monsters are not reserved against Housing.
    // The source prerequisite for this Housing level is Town Hall five.
    for (const TSharedPtr<FJsonValue>& Value : Array(Own, TEXT("buildings")))
    {
        const FObject Existing = Value->AsObject();
        const int32 Type = static_cast<int32>(Number(Existing, TEXT("type")));
        if (Type == 14 || Type == 15)
        {
            Existing->SetNumberField(TEXT("level"), Type == 14 ? 5 : 4);
            Existing->SetNumberField(TEXT("targetLevel"), Type == 14 ? 5 : 4);
            Existing->SetNumberField(TEXT("hp"), Type == 14 ? 94000 : 43000);
        }
    }
    FBYMSimulation Simulation;
    if (!Load(*this, Simulation, Fixture)) return false;
    if (!TestTrue(TEXT("A genuine Hatchery request creates the unfinished resident fixture"),
        Accepted(Simulation.Execute(Attacker, TEXT("train"), TEXT("{\"type\":\"C4\",\"count\":1}"))))) return false;
    auto ViewedYard = [&Simulation]() { return Child(Decode(Simulation.Snapshot(Defender, Attacker)), TEXT("yard")); };
    const FObject Public = ViewedYard();
    TestEqual(TEXT("Cosmetic roster provenance identifies the viewed yard, not its visitor"), String(Public, TEXT("residentYardId")), Attacker);
    const FArray& Roster = Array(Public, TEXT("residentRoster"));
    TestEqual(TEXT("A large army shows at most the requested 50 yard residents"), Roster.Num(), 50);
    TMap<FString, int32> Counts;
    TSet<FString> Ids;
    for (const TSharedPtr<FJsonValue>& Value : Roster)
    {
        const FObject Resident = Value->AsObject();
        ++Counts.FindOrAdd(String(Resident, TEXT("type")));
        Ids.Add(String(Resident, TEXT("id")));
        TestEqual(TEXT("A public resident exposes only its cosmetic identity and species"), Resident->Values.Num(), 2);
    }
    TestEqual(TEXT("Every displayed resident has its own stable identity"), Ids.Num(), 50);
    TestFalse(TEXT("Cosmetic identity is never empty"), Ids.Contains(FString()));
    TestTrue(TEXT("Available trained species are represented instead of only the largest stack"), Counts.FindRef(TEXT("C1")) > 0 && Counts.FindRef(TEXT("C2")) > 0 && Counts.FindRef(TEXT("C3")) > 0);
    TestTrue(TEXT("A visible species never exceeds its completed trained count"), Counts.FindRef(TEXT("C1")) <= 50 && Counts.FindRef(TEXT("C2")) <= 3 && Counts.FindRef(TEXT("C3")) <= 2);
    TestEqual(TEXT("An unfinished hatching queue does not create a roaming monster"), Counts.FindRef(TEXT("C4")), 0);
    const FObject PublicArmy = Child(Public, TEXT("army"));
    TestTrue(TEXT("A visitor cannot read the full ready army"), !PublicArmy || PublicArmy->Values.IsEmpty());
    TestTrue(TEXT("A visitor cannot read private hatching work"), Array(Public, TEXT("training")).IsEmpty());
    TestFalse(TEXT("A visitor cannot read the authoritative per-Hatchery queues"), Public->HasField(TEXT("hatcheries")));
    TestFalse(TEXT("Cosmetic residents do not expose private research levels"), Public->HasField(TEXT("monsterLevels")));
    const FObject AgainYard = ViewedYard();
    const FArray& Again = Array(AgainYard, TEXT("residentRoster"));
    TestEqual(TEXT("Reading another snapshot keeps the same resident count"), Again.Num(), Roster.Num());
    for (int32 I = 0; I < FMath::Min(Again.Num(), Roster.Num()); ++I)
        TestEqual(TEXT("Resident identities remain stable between snapshots"), String(Again[I]->AsObject(), TEXT("id")), String(Roster[I]->AsObject(), TEXT("id")));
    Settings.Settings->MaxVisibleYardMonsters = 500;
    TestEqual(TEXT("The authority hard-clamps an edited configuration to 50 residents"), Array(ViewedYard(), TEXT("residentRoster")).Num(), 50);
    Settings.Settings->MaxVisibleYardMonsters = 3;
    TestEqual(TEXT("A smaller configured presentation cap is honored"), Array(ViewedYard(), TEXT("residentRoster")).Num(), 3);
    Settings.Settings->MaxVisibleYardMonsters = 0;
    TestEqual(TEXT("Disabling yard residents leaves no cosmetic actors to spawn"), Array(ViewedYard(), TEXT("residentRoster")).Num(), 0);
    TestEqual(TEXT("Presentation never consumes the authoritative ready army"), Number(Child(SavedYard(Simulation.Serialize(), Attacker), TEXT("army")), TEXT("C1")), 50.0);
    TestEqual(TEXT("Presentation never completes queued training"), Array(SavedYard(Simulation.Serialize(), Attacker), TEXT("training")).Num(), 1);
    return true;
}
} // namespace BYMCombatPresentationTests

#endif
