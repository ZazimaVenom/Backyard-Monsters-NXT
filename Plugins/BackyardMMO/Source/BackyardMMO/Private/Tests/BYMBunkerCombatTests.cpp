#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// These fixtures enter through real host invasion/deployment commands. Save
// edits position valid armies at deterministic combat boundaries; the actual
// simulation performs release, movement, fighting, accounting and persistence.
namespace BYMBunkerCombatTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("bunker-combat-owner")), Other(TEXT("bunker-combat-other"));
    const FString BunkerId(TEXT("combat-bunker")), TownId(TEXT("combat-town")), CollectorId(TEXT("combat-collector"));
    struct FSettingsGuard
    {
        UBYMSettings* S = GetMutableDefault<UBYMSettings>();
        const int32 Bots = S->RivalBotCount, Cells = S->YardCells;
        const int32 Starting = S->StartingResources, Storage = S->ResourceStorageFloor;
        const float Damage = S->CombatDamageMultiplier, Movement = S->MonsterMovementMultiplier;
        const float Production = S->ResourceProductionMultiplier, Step = S->CombatSimulationStepSeconds;
        const float Duration = S->MaximumBattleSeconds;
        FSettingsGuard()
        {
            S->RivalBotCount = 0; S->YardCells = 100; S->StartingResources = 2000000; S->ResourceStorageFloor = 2000000;
            S->CombatDamageMultiplier = 1; S->MonsterMovementMultiplier = 1; S->ResourceProductionMultiplier = 0;
            S->CombatSimulationStepSeconds = .1f; S->MaximumBattleSeconds = 300;
        }
        ~FSettingsGuard()
        {
            S->RivalBotCount = Bots; S->YardCells = Cells; S->StartingResources = Starting; S->ResourceStorageFloor = Storage;
            S->CombatDamageMultiplier = Damage; S->MonsterMovementMultiplier = Movement; S->ResourceProductionMultiplier = Production;
            S->CombatSimulationStepSeconds = Step; S->MaximumBattleSeconds = Duration;
        }
    };
    FObject Obj() { return MakeShared<FJsonObject>(); }
    FObject Decode(const FString& Text)
    { FObject Result; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result); return Result; }
    FString Encode(const FObject& Value)
    { FString Text; FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FObject Clone(const FObject& Value) { return Decode(Encode(Value)); }
    FObject Child(const FObject& Value, const TCHAR* Key)
    { const FObject* Result = nullptr; return Value && Value->TryGetObjectField(Key, Result) && Result ? *Result : FObject(); }
    const FArray& Array(const FObject& Value, const TCHAR* Key)
    { static const FArray Empty; const FArray* Result = nullptr; return Value && Value->TryGetArrayField(Key, Result) && Result ? *Result : Empty; }
    FObject First(const FObject& Value, const TCHAR* Key)
    { const FArray& Values = Array(Value, Key); return Values.IsEmpty() ? FObject() : Values[0]->AsObject(); }
    double Number(const FObject& Value, const TCHAR* Key)
    { double Result = 0; if (Value) Value->TryGetNumberField(Key, Result); return Result; }
    FString String(const FObject& Value, const TCHAR* Key)
    { FString Result; if (Value) Value->TryGetStringField(Key, Result); return Result; }
    bool Flag(const FObject& Value, const TCHAR* Key)
    { bool Result = false; return Value && Value->TryGetBoolField(Key, Result) && Result; }
    bool Accepted(const FString& Reply) { return Flag(Decode(Reply), TEXT("ok")); }
    FObject Find(const FArray& Values, const FString& Id)
    { for (const auto& Value : Values) if (Value && Value->Type == EJson::Object && String(Value->AsObject(), TEXT("id")) == Id) return Value->AsObject(); return nullptr; }
    FObject SavedYard(const FObject& Save, const FString& Id = Other) { return Find(Array(Save, TEXT("yards")), Id); }
    FObject SavedBunker(const FObject& Save) { return Find(Array(SavedYard(Save), TEXT("buildings")), BunkerId); }
    FObject SavedBattle(const FObject& Save) { return First(Save, TEXT("battles")); }
    FObject Snapshot(const FBYMSimulation& Simulation) { return Decode(Simulation.Snapshot(Owner, Other)); }
    FObject Battle(const FBYMSimulation& Simulation) { return Child(Snapshot(Simulation), TEXT("battle")); }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    { FString Error; if (Simulation.Initialize(Save, Error)) return true; Test.AddError(TEXT("Bunker combat fixture: ") + Error); return false; }
    bool Rejected(const FObject& Save)
    { FString Error; FBYMSimulation Simulation; return !Simulation.Initialize(Save, Error) && !Error.IsEmpty(); }
    void Step(FBYMSimulation& Simulation, double& Now, int32 Count = 1)
    { for (int32 Index = 0; Index < Count; ++Index) { Now += .1; Simulation.Tick(Now, .1f); } }
    FObject Building(const FString& Id, int32 Type, int32 X, int32 Y, double HP)
    {
        FObject Value = Obj(); Value->SetStringField(TEXT("id"), Id); Value->SetNumberField(TEXT("type"), Type);
        Value->SetNumberField(TEXT("level"), 1); Value->SetNumberField(TEXT("targetLevel"), 1);
        Value->SetNumberField(TEXT("x"), X); Value->SetNumberField(TEXT("y"), Y); Value->SetNumberField(TEXT("hp"), HP);
        Value->SetNumberField(TEXT("readyAt"), 0); Value->SetNumberField(TEXT("stored"), 0); Value->SetNumberField(TEXT("workRemaining"), 0);
        Value->SetStringField(TEXT("jobId"), FString()); Value->SetBoolField(TEXT("repairing"), false);
        if (Type == 22) Value->SetObjectField(TEXT("garrison"), Obj()); return Value;
    }
    FObject Fixture(FAutomationTestBase& Test, const TCHAR* DefenderType = TEXT("C1"), int32 Stored = 2, const TCHAR* AttackerType = TEXT("C2"))
    {
        FBYMSimulation Seed; if (!Load(Test, Seed, nullptr)) return nullptr; FString Error;
        if (!Seed.AddPlayer(Owner, Owner, Error) || !Seed.AddPlayer(Other, Other, Error)) { Test.AddError(Error); return nullptr; }
        FObject Save = Seed.Serialize();
        for (const FString& Id : {Owner, Other})
        {
            FObject Army = Obj(), Levels = Obj();
            for (const TCHAR* Type : {TEXT("C1"), TEXT("C2"), TEXT("C5"), TEXT("C12"), TEXT("C14")})
            { Army->SetNumberField(Type, 10); Levels->SetNumberField(Type, 1); }
            SavedYard(Save, Id)->SetObjectField(TEXT("army"), Army); SavedYard(Save, Id)->SetObjectField(TEXT("monsterLevels"), Levels);
        }
        FArray Buildings; FObject HomeBunker = Building(BunkerId, 22, 20, 20, 10000);
        if (Stored > 0) Child(HomeBunker, TEXT("garrison"))->SetNumberField(DefenderType, Stored);
        Buildings.Add(MakeShared<FJsonValueObject>(Building(TownId, 14, 4, 4, 4000)));
        Buildings.Add(MakeShared<FJsonValueObject>(HomeBunker));
        Buildings.Add(MakeShared<FJsonValueObject>(Building(CollectorId, 1, 70, 70, 500)));
        SavedYard(Save)->SetArrayField(TEXT("buildings"), Buildings); SavedYard(Save)->SetArrayField(TEXT("hatcheries"), FArray());
        FBYMSimulation Prepared; if (!Load(Test, Prepared, Save)) return nullptr;
        if (!Test.TestTrue(TEXT("Begin a real invasion"), Accepted(Prepared.Execute(Owner, TEXT("invade"), TEXT("{\"target\":\"bunker-combat-other\"}"))))) return nullptr;
        if (!Test.TestTrue(TEXT("Deploy a real ready monster at the yard boundary"), Accepted(Prepared.Execute(Owner, TEXT("deploy"),
            FString::Printf(TEXT("{\"type\":\"%s\",\"count\":1,\"x\":124,\"y\":23}"), AttackerType))))) return nullptr;
        Save = Prepared.Serialize(); FObject Troop = First(SavedBattle(Save), TEXT("troops"));
        Troop->SetNumberField(TEXT("x"), 32); Troop->SetNumberField(TEXT("y"), 23); Troop->SetNumberField(TEXT("attackCooldown"), 60);
        Troop->SetStringField(TEXT("target"), BunkerId); return Save;
    }
    FObject ReleasedFixture(FAutomationTestBase& Test, const TCHAR* Type = TEXT("C1"), int32 Stored = 1, const TCHAR* AttackerType = TEXT("C2"))
    {
        FObject Save = Fixture(Test, Type, Stored, AttackerType); if (!Save) return nullptr;
        FBYMSimulation Simulation; if (!Load(Test, Simulation, Save)) return nullptr;
        double Now = Number(Snapshot(Simulation), TEXT("now")); Step(Simulation, Now, 10);
        if (!Test.TestTrue(TEXT("A real Bunker release creates a defensive creature"), First(Battle(Simulation), TEXT("defenders")).IsValid())) return nullptr;
        return Simulation.Serialize();
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerReleaseTest, "BackyardMMO.Bunker.ReleaseTimingAndSeparateAccounting",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerReleaseTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = Fixture(*this); if (!Save) return false;
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        const double FlingerUsed = Number(Battle(Simulation), TEXT("flingerUsed"));
        double Now = Number(Snapshot(Simulation), TEXT("now")); Step(Simulation, Now, 3);
        TestEqual(TEXT("The door opening does not instantly release the entire stock"), Array(Battle(Simulation), TEXT("defenders")).Num(), 0);
        FObject OpeningSave = Simulation.Serialize(); FObject OpeningState = First(SavedBattle(OpeningSave), TEXT("bunkers"));
        if (!TestTrue(TEXT("Opening door and release clock are saved"), OpeningState.IsValid())) return false;
        TestTrue(TEXT("Door progresses through its original fifteen-frame animation"), Number(OpeningState, TEXT("doorFrame")) > 0 && Number(OpeningState, TEXT("doorFrame")) < 15);
        FBYMSimulation Resumed; if (!Load(*this, Resumed, OpeningSave)) return false;
        TestEqual(TEXT("Reload retains fractional door progress"), Number(First(SavedBattle(Resumed.Serialize()), TEXT("bunkers")), TEXT("doorFrame")), Number(OpeningState, TEXT("doorFrame")));
        TestEqual(TEXT("Reload retains the release countdown"), Number(First(SavedBattle(Resumed.Serialize()), TEXT("bunkers")), TEXT("releaseRemaining")), Number(OpeningState, TEXT("releaseRemaining")));
        Now = Number(Snapshot(Resumed), TEXT("now")); Step(Resumed, Now, 2);
        TestEqual(TEXT("The first release clock emits one creature after opening"), Array(Battle(Resumed), TEXT("defenders")).Num(), 1);
        Step(Resumed, Now, 5);
        TestEqual(TEXT("The next release clock emits the second stored creature"), Array(Battle(Resumed), TEXT("defenders")).Num(), 2);
        TestEqual(TEXT("Released defenders remain owned ordinary Bunker stock"), Number(Child(SavedBunker(Resumed.Serialize()), TEXT("garrison")), TEXT("C1")), 2.);
        TestEqual(TEXT("Defenders never increase committed attacker deployments"), Number(Battle(Resumed), TEXT("deployed")), 1.);
        TestEqual(TEXT("Defenders are not appended to the attacker's troop array"), Array(Battle(Resumed), TEXT("troops")).Num(), 1);
        TestEqual(TEXT("Defenders use no attacking Flinger space"), Number(Battle(Resumed), TEXT("flingerUsed")), FlingerUsed);
        const FObject Defender = First(Battle(Resumed), TEXT("defenders"));
        TestTrue(TEXT("Released creatures have a visible combat animation state"), String(Defender, TEXT("state")) == TEXT("moving") || String(Defender, TEXT("state")) == TEXT("attacking"));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Resumed.Serialize())) return false;
        TestEqual(TEXT("Both live defenders survive an active-battle reload"), Array(Battle(Reloaded), TEXT("defenders")).Num(), 2);
        // Flying invaders do not trigger ordinary ground-only Bunker defense.
        Save = Fixture(*this, TEXT("C1"), 2, TEXT("C14")); if (!Save) return false;
        FBYMSimulation AirOnly; if (!Load(*this, AirOnly, Save)) return false;
        Now = Number(Snapshot(AirOnly), TEXT("now")); Step(AirOnly, Now, 20);
        TestEqual(TEXT("A flying-only invasion leaves the stored ground defenders inside"), Array(Battle(AirOnly), TEXT("defenders")).Num(), 0);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerReturnDeathTest, "BackyardMMO.Bunker.ReturnReuseDeathAndReload",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerReturnDeathTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = ReleasedFixture(*this); if (!Save) return false;
        FObject Defender = First(SavedBattle(Save), TEXT("defenders")); const FString DefenderId = String(Defender, TEXT("id"));
        First(SavedBattle(Save), TEXT("troops"))->SetNumberField(TEXT("hp"), 0);
        Defender->SetNumberField(TEXT("hp"), 75);
        FBYMSimulation Returning; if (!Load(*this, Returning, Save)) return false;
        double Now = Number(Snapshot(Returning), TEXT("now"));
        for (int32 Index = 0; Index < 200 && !Flag(First(Battle(Returning), TEXT("defenders")), TEXT("returned")); ++Index) Step(Returning, Now);
        Defender = First(Battle(Returning), TEXT("defenders"));
        if (!TestTrue(TEXT("With no live ground target the survivor returns through its home door"), Flag(Defender, TEXT("returned")))) return false;
        TestEqual(TEXT("Returning preserves ordinary count inventory"), Number(Child(SavedBunker(Returning.Serialize()), TEXT("garrison")), TEXT("C1")), 1.);
        TestEqual(TEXT("Returning does not manufacture ready army inventory"), Number(Child(SavedYard(Returning.Serialize()), TEXT("army")), TEXT("C1")), 10.);
        Save = Returning.Serialize();
        // Re-arm the same valid invasion fixture to exercise repeated sorties.
        for (int32 Cycle = 0; Cycle < 3; ++Cycle)
        {
            FObject Troop = First(SavedBattle(Save), TEXT("troops")); Troop->SetNumberField(TEXT("hp"), 1000);
            Troop->SetNumberField(TEXT("x"), 32); Troop->SetNumberField(TEXT("y"), 23); Troop->SetNumberField(TEXT("attackCooldown"), 60);
            FBYMSimulation Sortie; if (!Load(*this, Sortie, Save)) return false;
            Now = Number(Snapshot(Sortie), TEXT("now")); Step(Sortie, Now, 10);
            Defender = First(Battle(Sortie), TEXT("defenders"));
            TestFalse(TEXT("A returned defender may leave for the next threat"), Flag(Defender, TEXT("returned")));
            TestEqual(TEXT("Ordinary Bunkers respawn their returning stock at full health"), Number(Defender, TEXT("hp")), 200.);
            TestEqual(TEXT("Repeated sorties reuse the defensive creature identity"), String(Defender, TEXT("id")), DefenderId);
            TestEqual(TEXT("Repeated release/return never grows the saved defender list"), Array(Battle(Sortie), TEXT("defenders")).Num(), 1);
            Save = Sortie.Serialize(); First(SavedBattle(Save), TEXT("troops"))->SetNumberField(TEXT("hp"), 0);
            FBYMSimulation Back; if (!Load(*this, Back, Save)) return false;
            Now = Number(Snapshot(Back), TEXT("now"));
            for (int32 Index = 0; Index < 200 && !Flag(First(Battle(Back), TEXT("defenders")), TEXT("returned")); ++Index) Step(Back, Now);
            if (!TestTrue(TEXT("Each sortie returns without losing its inventory"), Flag(First(Battle(Back), TEXT("defenders")), TEXT("returned")))) return false;
            Save = Back.Serialize();
        }
        Save = ReleasedFixture(*this, TEXT("C1"), 2); if (!Save) return false;
        Defender = First(SavedBattle(Save), TEXT("defenders")); Defender->SetNumberField(TEXT("hp"), 1);
        Defender->SetNumberField(TEXT("x"), 32); Defender->SetNumberField(TEXT("y"), 23); Defender->SetNumberField(TEXT("attackCooldown"), 60);
        FObject Troop = First(SavedBattle(Save), TEXT("troops")); Troop->SetNumberField(TEXT("x"), 33); Troop->SetNumberField(TEXT("y"), 23);
        Troop->SetNumberField(TEXT("attackCooldown"), 0); Troop->SetStringField(TEXT("targetCreep"), String(Defender, TEXT("id")));
        FBYMSimulation Death; if (!Load(*this, Death, Save)) return false;
        Now = Number(Snapshot(Death), TEXT("now")); Step(Death, Now);
        TestEqual(TEXT("A real retaliating attacker kills the injured defender"), Number(First(Battle(Death), TEXT("defenders")), TEXT("hp")), 0.);
        TestEqual(TEXT("A defender death debits its home count once"), Number(Child(SavedBunker(Death.Serialize()), TEXT("garrison")), TEXT("C1")), 1.);
        FBYMSimulation AfterDeath; if (!Load(*this, AfterDeath, Death.Serialize())) return false;
        Now = Number(Snapshot(AfterDeath), TEXT("now")); Step(AfterDeath, Now, 2);
        TestEqual(TEXT("Reloading and ticking a handled death cannot debit inventory again"), Number(Child(SavedBunker(AfterDeath.Serialize()), TEXT("garrison")), TEXT("C1")), 1.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerActiveSaveTest, "BackyardMMO.Bunker.ActiveBattleSaveValidation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerActiveSaveTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; const FObject Good = ReleasedFixture(*this); if (!Good) return false;
        for (const TCHAR* Field : {TEXT("defenders"), TEXT("bunkers")})
        {
            FObject Bad = Clone(Good); SavedBattle(Bad)->RemoveField(Field);
            TestTrue(TEXT("A current active save requires explicit defensive arrays"), Rejected(Bad));
        }
        FObject Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->SetStringField(TEXT("bunker"), TownId);
        TestTrue(TEXT("Defenders cannot claim a non-Bunker home"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->SetStringField(TEXT("id"), String(First(SavedBattle(Bad), TEXT("troops")), TEXT("id")));
        TestTrue(TEXT("Attacker and defender identities share a uniqueness boundary"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->SetBoolField(TEXT("deathHandled"), true);
        TestTrue(TEXT("An alive defender cannot claim its death was already processed"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->SetNumberField(TEXT("hp"), 0);
        TestTrue(TEXT("An unhandled defensive death is not a valid persisted transaction"), Rejected(Bad));
        Bad = Clone(Good); Child(SavedBunker(Bad), TEXT("garrison"))->SetNumberField(TEXT("C1"), 0);
        TestTrue(TEXT("Living dispatched defenders must be backed by stored counts"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->SetStringField(TEXT("target"), TEXT("missing-attacker"));
        TestTrue(TEXT("A defensive target must reference this invasion's army"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("troops"))->SetStringField(TEXT("targetCreep"), TEXT("missing-defender"));
        TestTrue(TEXT("A retaliation target must reference this invasion's defenders"), Rejected(Bad));
        Bad = Clone(Good); SavedBattle(Bad)->SetArrayField(TEXT("bunkers"), FArray());
        TestTrue(TEXT("Every active defender requires its home's release clock"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("bunkers"))->SetNumberField(TEXT("releaseRemaining"), 100);
        TestTrue(TEXT("An impossible release countdown is rejected"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->RemoveField(TEXT("retargetRemaining"));
        TestTrue(TEXT("A current defender requires its persisted retarget clock"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->SetNumberField(TEXT("retargetRemaining"), 3.76);
        TestTrue(TEXT("Retarget clocks cannot exceed the source 150-frame interval"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("defenders"))->SetStringField(TEXT("targetCreep"), TEXT("invalid-nested-defender-target"));
        TestTrue(TEXT("Defenders cannot smuggle attacker-only retaliation references"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("bunkers"))->RemoveField(TEXT("targetRefreshRemaining"));
        TestTrue(TEXT("A current Bunker requires its persisted acquisition clock"), Rejected(Bad));
        Bad = Clone(Good); First(SavedBattle(Bad), TEXT("bunkers"))->SetNumberField(TEXT("targetRefreshRemaining"), 1.51);
        TestTrue(TEXT("Bunker acquisition clocks cannot exceed sixty source frames"), Rejected(Bad));
        Bad = Clone(Good); FArray MissingTargets; MissingTargets.Add(MakeShared<FJsonValueString>(TEXT("missing-attacker")));
        First(SavedBattle(Bad), TEXT("bunkers"))->SetArrayField(TEXT("targets"), MissingTargets);
        TestTrue(TEXT("Cached Bunker acquisition targets must belong to the attacking army"), Rejected(Bad));
        // Historical active invasions have neither defensive array. Preserve
        // their deployed army while migrating all four older world schemas.
        for (int32 Schema = 1; Schema <= 4; ++Schema)
        {
            FObject Old = Clone(Good); Old->SetNumberField(TEXT("schema"), Schema);
            SavedBattle(Old)->RemoveField(TEXT("defenders")); SavedBattle(Old)->RemoveField(TEXT("bunkers"));
            First(SavedBattle(Old), TEXT("troops"))->RemoveField(TEXT("targetCreep"));
            for (const auto& YardValue : Array(Old, TEXT("yards")))
            {
                FObject Yard = YardValue->AsObject();
                for (const auto& BuildingValue : Array(Yard, TEXT("buildings")))
                {
                    FObject Item = BuildingValue->AsObject(); Item->RemoveField(TEXT("garrison"));
                    if (Schema < 4) { Item->RemoveField(TEXT("jobId")); Item->RemoveField(TEXT("workRemaining")); }
                }
                if (Schema < 4) { Yard->RemoveField(TEXT("earnedExperience")); Yard->RemoveField(TEXT("baseValue")); }
                if (Schema < 3) { Yard->RemoveField(TEXT("hatcheries")); Yard->SetArrayField(TEXT("training"), FArray()); }
                if (Schema == 1) { Yard->RemoveField(TEXT("monsterLevels")); Yard->RemoveField(TEXT("monsterUnlock")); Yard->RemoveField(TEXT("monsterResearch")); }
            }
            FBYMSimulation Legacy; if (!Load(*this, Legacy, Old)) return false;
            TestEqual(TEXT("A historical active invasion receives no free defenders"), Array(Battle(Legacy), TEXT("defenders")).Num(), 0);
            TestEqual(TEXT("Migration keeps the attacker's original deployed count"), Number(Battle(Legacy), TEXT("deployed")), 1.);
            TestEqual(TEXT("The migrated active world writes schema five"), Number(Legacy.Serialize(), TEXT("schema")), 5.);
        }
        return true;
    }
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerDestructionTest, "BackyardMMO.Bunker.DestroyedHomeAndRetaliationReferences",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerDestructionTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard;
        for (const bool Returned : {false, true})
        {
            FObject Save = ReleasedFixture(*this); if (!Save) return false;
            FBYMSimulation Prepared; if (!Load(*this, Prepared, Save)) return false;
            if (!TestTrue(TEXT("A real flying reinforcement can finish the battle"), Accepted(Prepared.Execute(Owner, TEXT("deploy"), TEXT("{\"type\":\"C14\",\"count\":1,\"x\":124,\"y\":30}"))))) return false;
            Save = Prepared.Serialize(); FObject Defender = First(SavedBattle(Save), TEXT("defenders"));
            Defender->SetBoolField(TEXT("returned"), Returned); Defender->SetStringField(TEXT("target"), FString());
            Defender->SetNumberField(TEXT("x"), 50); Defender->SetNumberField(TEXT("y"), 50);
            FObject DeadAttacker = First(SavedBattle(Save), TEXT("troops")); DeadAttacker->SetNumberField(TEXT("hp"), 0);
            DeadAttacker->SetStringField(TEXT("targetCreep"), String(Defender, TEXT("id")));
            FObject Flyer = Array(SavedBattle(Save), TEXT("troops"))[1]->AsObject();
            Flyer->SetNumberField(TEXT("x"), 28); Flyer->SetNumberField(TEXT("y"), 23);
            Flyer->SetStringField(TEXT("target"), BunkerId); Flyer->SetNumberField(TEXT("attackCooldown"), 0);
            SavedBunker(Save)->SetNumberField(TEXT("hp"), 1);
            FBYMSimulation Combat; if (!Load(*this, Combat, Save)) return false;
            double Now = Number(Snapshot(Combat), TEXT("now")); Step(Combat, Now);
            Save = Combat.Serialize();
            TestEqual(TEXT("A real attack destroys the weakened Bunker"), Number(SavedBunker(Save), TEXT("hp")), 0.);
            TestEqual(TEXT("Destroyed ordinary Bunkers retain no hidden recoverable stock"), Number(Child(SavedBunker(Save), TEXT("garrison")), TEXT("C1")), 0.);
            TestEqual(TEXT("Only returned records are pruned; released survivors remain in the battle"), Array(SavedBattle(Save), TEXT("defenders")).Num(), Returned ? 0 : 1);
            if (Returned) TestTrue(TEXT("Removing a returned defender clears even dead attackers' stale retaliation references"), String(First(SavedBattle(Save), TEXT("troops")), TEXT("targetCreep")).IsEmpty());
            else TestTrue(TEXT("A living released defender may survive its destroyed home"), Number(First(SavedBattle(Save), TEXT("defenders")), TEXT("hp")) > 0);
            FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
            Now = Number(Snapshot(Reloaded), TEXT("now")); Step(Reloaded, Now, 5);
            TestEqual(TEXT("Post-destruction reload and return logic never resurrect lost stock"), Number(Child(SavedBunker(Reloaded.Serialize()), TEXT("garrison")), TEXT("C1")), 0.);
            TestTrue(TEXT("Ending the invasion remains possible after home destruction"), Accepted(Reloaded.Execute(Owner, TEXT("retreat"), TEXT("{}"))));
            TestEqual(TEXT("Ending the invasion does not restore the destroyed garrison"), Number(Child(SavedBunker(Reloaded.Serialize()), TEXT("garrison")), TEXT("C1")), 0.);
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerEyeRaTest, "BackyardMMO.Bunker.DefensiveEyeRaSuicideDeathAndGroundBlast",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerEyeRaTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard;
        for (const bool KilledByAttacker : {false, true})
        {
            FObject Save = ReleasedFixture(*this, TEXT("C5"), 1, TEXT("C12")); if (!Save) return false;
            FBYMSimulation Prepared; if (!Load(*this, Prepared, Save)) return false;
            if (!TestTrue(TEXT("Deploy real ground blast witnesses"), Accepted(Prepared.Execute(Owner, TEXT("deploy"), TEXT("{\"type\":\"C2\",\"count\":2,\"x\":124,\"y\":30}"))))) return false;
            Save = Prepared.Serialize(); FObject Defender = First(SavedBattle(Save), TEXT("defenders"));
            Defender->SetNumberField(TEXT("x"), 32); Defender->SetNumberField(TEXT("y"), 23);
            Defender->SetNumberField(TEXT("attackCooldown"), KilledByAttacker ? 60 : 0);
            FObject Primary = First(SavedBattle(Save), TEXT("troops")); Primary->SetNumberField(TEXT("x"), 33); Primary->SetNumberField(TEXT("y"), 23);
            Primary->SetNumberField(TEXT("attackCooldown"), KilledByAttacker ? 0 : 60);
            Primary->SetStringField(TEXT("targetCreep"), KilledByAttacker ? String(Defender, TEXT("id")) : FString());
            const FArray Troops = Array(SavedBattle(Save), TEXT("troops"));
            Troops[1]->AsObject()->SetNumberField(TEXT("x"), 36.45); Troops[1]->AsObject()->SetNumberField(TEXT("y"), 23);
            Troops[2]->AsObject()->SetNumberField(TEXT("x"), 37); Troops[2]->AsObject()->SetNumberField(TEXT("y"), 23);
            FBYMSimulation Combat; if (!Load(*this, Combat, Save)) return false;
            double Now = Number(Snapshot(Combat), TEXT("now")); Step(Combat, Now);
            const FArray After = Array(Battle(Combat), TEXT("troops"));
            TestEqual(TEXT("The defensive Eye-Ra explodes on suicide and ordinary lethal damage"), Number(First(Battle(Combat), TEXT("defenders")), TEXT("hp")), 0.);
            TestEqual(TEXT("Ground distance twenty gives the source-truncated linear blast"), Number(After[0]->AsObject(), TEXT("hp")), 4889.);
            TestEqual(TEXT("The inner edge of the radius retains the source twenty-percent minimum"), Number(After[1]->AsObject(), TEXT("hp")), 200.);
            TestEqual(TEXT("Outside ninety pixels no ground blast damage is applied"), Number(After[2]->AsObject(), TEXT("hp")), 1000.);
            TestEqual(TEXT("A defensive Eye-Ra death consumes its stock exactly once"), Number(Child(SavedBunker(Combat.Serialize()), TEXT("garrison")), TEXT("C5")), 0.);
            FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Combat.Serialize())) return false;
            Now = Number(Snapshot(Reloaded), TEXT("now")); Step(Reloaded, Now, 2);
            TestEqual(TEXT("A persisted defensive death never replays its explosion"), Number(First(Battle(Reloaded), TEXT("troops")), TEXT("hp")), 4889.);
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerOffensiveEyeRaTest, "BackyardMMO.Bunker.OffensiveEyeRaHitsDefendersAndBuildings",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerOffensiveEyeRaTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = ReleasedFixture(*this, TEXT("C1"), 2, TEXT("C5")); if (!Save) return false;
        if (!TestEqual(TEXT("The fixture has both Bunker defenders available"), Array(SavedBattle(Save), TEXT("defenders")).Num(), 2)) return false;
        FObject Attacker = First(SavedBattle(Save), TEXT("troops")); Attacker->SetNumberField(TEXT("x"), 22); Attacker->SetNumberField(TEXT("y"), 19);
        Attacker->SetNumberField(TEXT("attackCooldown"), 0); Attacker->SetStringField(TEXT("targetCreep"), String(First(SavedBattle(Save), TEXT("defenders")), TEXT("id")));
        for (int32 Index = 0; Index < 2; ++Index)
        {
            FObject Defender = Array(SavedBattle(Save), TEXT("defenders"))[Index]->AsObject();
            Defender->SetNumberField(TEXT("x"), 23 + Index * 2); Defender->SetNumberField(TEXT("y"), 19);
            Defender->SetNumberField(TEXT("attackCooldown"), 60);
        }
        FBYMSimulation Combat; if (!Load(*this, Combat, Save)) return false;
        double Now = Number(Snapshot(Combat), TEXT("now")); Step(Combat, Now);
        TestEqual(TEXT("An offensive Eye-Ra is consumed by its attack"), Number(First(Battle(Combat), TEXT("troops")), TEXT("hp")), 0.);
        TestEqual(TEXT("Its primary defender takes the direct blast"), Number(First(Battle(Combat), TEXT("defenders")), TEXT("hp")), 0.);
        TestEqual(TEXT("Its nearby second defender takes the projected splash"), Number(Array(Battle(Combat), TEXT("defenders"))[1]->AsObject(), TEXT("hp")), 0.);
        TestEqual(TEXT("Both actual defender deaths debit ordinary stored counts"), Number(Child(SavedBunker(Combat.Serialize()), TEXT("garrison")), TEXT("C1")), 0.);
        TestTrue(TEXT("A defensive-creature primary target still damages nearby buildings"), Number(SavedBunker(Combat.Serialize()), TEXT("hp")) < 10000.);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Combat.Serialize())) return false;
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerRetargetTest, "BackyardMMO.Bunker.ContactRetargetClocksAndCache",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerRetargetTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = ReleasedFixture(*this); if (!Save) return false;
        FBYMSimulation Prepared; if (!Load(*this, Prepared, Save)) return false;
        if (!TestTrue(TEXT("A second actual attacker supplies a closer competing target"), Accepted(Prepared.Execute(Owner, TEXT("deploy"), TEXT("{\"type\":\"C2\",\"count\":1,\"x\":124,\"y\":30}"))))) return false;
        Save = Prepared.Serialize(); FObject Defender = First(SavedBattle(Save), TEXT("defenders"));
        FObject Current = First(SavedBattle(Save), TEXT("troops")), Closer = Array(SavedBattle(Save), TEXT("troops"))[1]->AsObject();
        const FString CurrentId = String(Current, TEXT("id"));
        Defender->SetNumberField(TEXT("x"), 32); Defender->SetNumberField(TEXT("y"), 23);
        Defender->SetStringField(TEXT("target"), CurrentId); Defender->SetNumberField(TEXT("attackCooldown"), 0); Defender->SetNumberField(TEXT("retargetRemaining"), 0);
        Current->SetNumberField(TEXT("x"), 33); Current->SetNumberField(TEXT("y"), 23); Current->SetNumberField(TEXT("attackCooldown"), 60);
        Closer->SetNumberField(TEXT("x"), 32.4); Closer->SetNumberField(TEXT("y"), 23); Closer->SetNumberField(TEXT("attackCooldown"), 60);
        FBYMSimulation Contact; if (!Load(*this, Contact, Save)) return false;
        double Now = Number(Snapshot(Contact), TEXT("now")); Step(Contact, Now);
        TestEqual(TEXT("A live contact target is retained even when a nearer attacker exists"), String(First(Battle(Contact), TEXT("defenders")), TEXT("target")), CurrentId);
        TestEqual(TEXT("The retained opponent receives the actual melee strike"), Number(First(Battle(Contact), TEXT("troops")), TEXT("hp")), 940.);
        TestEqual(TEXT("The nearer competitor is not silently substituted"), Number(Array(Battle(Contact), TEXT("troops"))[1]->AsObject(), TEXT("hp")), 1000.);
        Save = Contact.Serialize(); Defender = First(SavedBattle(Save), TEXT("defenders"));
        Defender->SetNumberField(TEXT("retargetRemaining"), 2.5); Defender->SetNumberField(TEXT("attackCooldown"), 60);
        First(SavedBattle(Save), TEXT("troops"))->SetNumberField(TEXT("x"), 50);
        FBYMSimulation Pursuit; if (!Load(*this, Pursuit, Save)) return false;
        Now = Number(Snapshot(Pursuit), TEXT("now")); Step(Pursuit, Now);
        TestEqual(TEXT("A moving defender preserves its opponent until the original search clock expires"), String(First(Battle(Pursuit), TEXT("defenders")), TEXT("target")), CurrentId);
        TestTrue(TEXT("Pursuit actually advances toward that opponent"), Number(First(Battle(Pursuit), TEXT("defenders")), TEXT("x")) > 32);
        Save = Pursuit.Serialize(); const double Remaining = Number(First(SavedBattle(Save), TEXT("defenders")), TEXT("retargetRemaining"));
        TestTrue(TEXT("The retarget countdown advances by a fixed combat step"), FMath::IsNearlyEqual(Remaining, 2.4, .00001));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
        TestEqual(TEXT("An active reload preserves the partially elapsed retarget countdown"), Number(First(SavedBattle(Reloaded.Serialize()), TEXT("defenders")), TEXT("retargetRemaining")), Remaining);
        // Any dead cached target invalidates the whole source nearest-three
        // acquisition cache; it is refreshed on the short empty-cache clock.
        Save = Reloaded.Serialize(); FObject State = First(SavedBattle(Save), TEXT("bunkers"));
        FArray Targets; Targets.Add(MakeShared<FJsonValueString>(CurrentId)); Targets.Add(MakeShared<FJsonValueString>(String(Closer, TEXT("id"))));
        State->SetArrayField(TEXT("targets"), Targets); State->SetNumberField(TEXT("targetRefreshRemaining"), 1.5);
        First(SavedBattle(Save), TEXT("troops"))->SetNumberField(TEXT("hp"), 0);
        FBYMSimulation InvalidCache; if (!Load(*this, InvalidCache, Save)) return false;
        Now = Number(Snapshot(InvalidCache), TEXT("now")); Step(InvalidCache, Now);
        const FObject InvalidState = First(SavedBattle(InvalidCache.Serialize()), TEXT("bunkers"));
        TestEqual(TEXT("One dead cached opponent clears the complete nearest-three cache"), Array(InvalidState, TEXT("targets")).Num(), 0);
        TestTrue(TEXT("Invalid acquisition is retried within ten display frames"), Number(InvalidState, TEXT("targetRefreshRemaining")) <= .25);
        return true;
    }

}
#endif
