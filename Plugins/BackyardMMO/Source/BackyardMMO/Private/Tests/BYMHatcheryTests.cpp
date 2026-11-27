#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMHatcheryTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Bots = Settings->RivalBotCount, Starting = Settings->StartingResources;
        const int32 Cells = Settings->YardCells, Floor = Settings->ResourceStorageFloor;
        const float Training = Settings->TrainingTimeMultiplier, Production = Settings->ResourceProductionMultiplier;
        const float Loot = Settings->InvasionLootFraction, Damage = Settings->CombatDamageMultiplier;
        const float Movement = Settings->MonsterMovementMultiplier, Battle = Settings->MaximumBattleSeconds;
        const float CombatStep = Settings->CombatSimulationStepSeconds;
        const int32 BattleSlots = Settings->MaximumConcurrentBattles, Troops = Settings->MaximumTroopsPerBattle;
        FSettingsGuard()
        {
            Settings->RivalBotCount = 0; Settings->StartingResources = 1000000;
            Settings->YardCells = 100; Settings->ResourceStorageFloor = 1000000;
            Settings->TrainingTimeMultiplier = 1; Settings->ResourceProductionMultiplier = 0; Settings->InvasionLootFraction = 0;
            Settings->CombatDamageMultiplier = 1; Settings->MonsterMovementMultiplier = 1; Settings->MaximumBattleSeconds = 300;
            Settings->CombatSimulationStepSeconds = 0.1f; Settings->MaximumConcurrentBattles = 2; Settings->MaximumTroopsPerBattle = 100;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots; Settings->StartingResources = Starting;
            Settings->YardCells = Cells; Settings->ResourceStorageFloor = Floor;
            Settings->TrainingTimeMultiplier = Training; Settings->ResourceProductionMultiplier = Production; Settings->InvasionLootFraction = Loot;
            Settings->CombatDamageMultiplier = Damage; Settings->MonsterMovementMultiplier = Movement; Settings->MaximumBattleSeconds = Battle;
            Settings->CombatSimulationStepSeconds = CombatStep; Settings->MaximumConcurrentBattles = BattleSlots; Settings->MaximumTroopsPerBattle = Troops;
        }
    };
    FObject Obj() { return MakeShared<FJsonObject>(); }
    FObject Decode(const FString& Text)
    { FObject Value; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value); return Value; }
    FString Encode(const FObject& Value)
    { FString Text; FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FObject Clone(const FObject& Value) { return Decode(Encode(Value)); }
    FObject Child(const FObject& Value, const TCHAR* Field)
    { const FObject* Found = nullptr; return Value && Value->TryGetObjectField(Field, Found) && Found ? *Found : FObject(); }
    const FArray& Array(const FObject& Value, const TCHAR* Field)
    { static const FArray Empty; const FArray* Found = nullptr; return Value && Value->TryGetArrayField(Field, Found) && Found ? *Found : Empty; }
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
    double Number(const FObject& Value, const TCHAR* Field, double Missing = 0)
    { double Result = Missing; if (Value) Value->TryGetNumberField(Field, Result); return Result; }
    FString String(const FObject& Value, const TCHAR* Field)
    { FString Result; if (Value) Value->TryGetStringField(Field, Result); return Result; }
    bool Accepted(const FString& Reply)
    { bool Result = false; const FObject Value = Decode(Reply); return Value && Value->TryGetBoolField(TEXT("ok"), Result) && Result; }
    FObject Snapshot(const FBYMSimulation& Simulation, const FString& Owner)
    { return Decode(Simulation.Snapshot(Owner, FString())); }
    FObject Home(const FBYMSimulation& Simulation, const FString& Owner)
    { return Child(Snapshot(Simulation, Owner), TEXT("home")); }
    FObject SavedYard(const FObject& Save, const FString& Owner)
    {
        for (const auto& Value : Array(Save, TEXT("yards")))
            if (String(Value->AsObject(), TEXT("id")) == Owner) return Value->AsObject();
        return nullptr;
    }
    FObject Hatchery(const FObject& Yard, const FString& Building = FString())
    {
        for (const auto& Value : Array(Yard, TEXT("hatcheries")))
            if (Building.IsEmpty() || String(Value->AsObject(), TEXT("building")) == Building) return Value->AsObject();
        return nullptr;
    }
    FObject Building(const FObject& Yard, int32 Type)
    {
        for (const auto& Value : Array(Yard, TEXT("buildings")))
            if (Number(Value->AsObject(), TEXT("type")) == Type) return Value->AsObject();
        return nullptr;
    }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FString& Owner)
    {
        FString Error;
        if (!Simulation.Initialize(nullptr, Error) || !Simulation.AddPlayer(Owner, Owner, Error)) { Test.AddError(Error); return false; }
        return true;
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    {
        FString Error; if (!Simulation.Initialize(Save, Error)) { Test.AddError(Error); return false; } return true;
    }
    FObject Train(const FString& BuildingId, int32 Count, const FString& Type = TEXT("C1"))
    { FObject Params = Obj(); Params->SetStringField(TEXT("building"), BuildingId); Params->SetStringField(TEXT("type"), Type); Params->SetNumberField(TEXT("count"), Count); return Params; }
    FObject Cancel(const FString& BuildingId, const FObject& Entry)
    { FObject Params = Obj(); Params->SetStringField(TEXT("building"), BuildingId); Params->SetStringField(TEXT("entry"), String(Entry, TEXT("id"))); Params->SetNumberField(TEXT("expectedCount"), Number(Entry, TEXT("count"))); return Params; }
    void AddSecondHatchery(const FObject& Save, const FString& Owner)
    {
        const FObject Yard = SavedYard(Save, Owner);
        // Controlled parallel-Hatchery layout excludes unrelated random obstacles.
        Child(Yard, TEXT("economy"))->SetArrayField(TEXT("mushrooms"), FArray());
        FObject Second = Clone(Building(Yard, 13)); Second->SetStringField(TEXT("id"), TEXT("second-hatchery"));
        Second->SetNumberField(TEXT("x"), 1); Second->SetNumberField(TEXT("y"), 1);
        FArray Buildings = Array(Yard, TEXT("buildings")); Buildings.Add(MakeShared<FJsonValueObject>(Second)); Yard->SetArrayField(TEXT("buildings"), Buildings);
        const FObject Town = Building(Yard, 14); Town->SetNumberField(TEXT("level"), 2); Town->SetNumberField(TEXT("targetLevel"), 2);
        FObject Empty = Obj(); Empty->SetStringField(TEXT("building"), TEXT("second-hatchery")); Empty->SetObjectField(TEXT("active"), Obj()); Empty->SetArrayField(TEXT("queue"), FArray());
        FArray Hatcheries = Array(Yard, TEXT("hatcheries")); Hatcheries.Add(MakeShared<FJsonValueObject>(Empty)); Yard->SetArrayField(TEXT("hatcheries"), Hatcheries);
    }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMHatcheryGroupsTest, "BackyardMMO.Hatchery.GroupsParallelAndCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMHatcheryGroupsTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; const FString Owner = TEXT("hatchery-groups");
    FBYMSimulation Seed; if (!Start(*this, Seed, Owner)) return false;
    FObject Save = Seed.Serialize(); AddSecondHatchery(Save, Owner);
    FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
    const double StartAt = Number(Snapshot(Simulation, Owner), TEXT("now"));
    const FString First = String(Hatchery(Home(Simulation, Owner)), TEXT("building"));
    TestTrue(TEXT("Level-one Hatchery accepts one active plus two groups of twenty"), Accepted(Simulation.Execute(Owner, TEXT("train"), Encode(Train(First, 41)))));
    TestFalse(TEXT("A full Hatchery rejects another creature"), Accepted(Simulation.Execute(Owner, TEXT("train"), Encode(Train(First, 1)))));
    TestTrue(TEXT("Another Hatchery has its own queue"), Accepted(Simulation.Execute(Owner, TEXT("train"), Encode(Train(TEXT("second-hatchery"), 2)))));
    const FObject FirstState = Hatchery(Home(Simulation, Owner), First);
    if (!TestEqual(TEXT("Waiting slots are grouped"), Array(FirstState, TEXT("queue")).Num(), 2)) return false;
    TestEqual(TEXT("Each source waiting group holds twenty"), Number(Array(FirstState, TEXT("queue"))[0]->AsObject(), TEXT("count")), 20.0);
    TestEqual(TEXT("Queued monsters do not reserve housing"), Number(Home(Simulation, Owner), TEXT("housingUsed")), 0.0);
    TestEqual(TEXT("Compatibility projection includes every paid creature"), Array(Home(Simulation, Owner), TEXT("training")).Num(), 43);
    Simulation.Tick(StartAt + 15, 0.f);
    TestEqual(TEXT("Two independent incubators each hatch one Pokey"), Number(Child(Home(Simulation, Owner), TEXT("army")), TEXT("C1")), 2.0);
    const FObject Current = Hatchery(Home(Simulation, Owner), First);
    const FObject Waiting = Array(Current, TEXT("queue"))[0]->AsObject();
    const FString CancelRequest = Encode(Cancel(First, Waiting));
    const double Before = Number(Child(Home(Simulation, Owner), TEXT("resources")), TEXT("goo"));
    TestTrue(TEXT("Cancel removes one current waiting creature"), Accepted(Simulation.Execute(Owner, TEXT("cancelTraining"), CancelRequest)));
    TestEqual(TEXT("Cancellation returns source Goo price"), Number(Child(Home(Simulation, Owner), TEXT("resources")), TEXT("goo")), Before + 250);
    TestFalse(TEXT("A duplicate stale count cannot cancel another monster"), Accepted(Simulation.Execute(Owner, TEXT("cancelTraining"), CancelRequest)));
    const FObject Active = Child(Hatchery(Home(Simulation, Owner), First), TEXT("active"));
    const FString ActiveCancel = Encode(Cancel(First, Active));
    TestTrue(TEXT("Cancel active creature starts the next queued creature"), Accepted(Simulation.Execute(Owner, TEXT("cancelTraining"), ActiveCancel)));
    TestFalse(TEXT("Completed active identity cannot be cancelled twice"), Accepted(Simulation.Execute(Owner, TEXT("cancelTraining"), ActiveCancel)));
    TestFalse(TEXT("A foreign Hatchery ID cannot spend resources"), Accepted(Simulation.Execute(Owner, TEXT("train"), Encode(Train(TEXT("another-yard-building"), 1)))));
    Save = Simulation.Serialize(); Child(SavedYard(Save, Owner), TEXT("monsterLevels"))->SetNumberField(TEXT("C1"), 2);
    FBYMSimulation Researched; if (!Load(*this, Researched, Save)) return false;
    const double BeforeResearchRefund = Number(Child(Home(Researched, Owner), TEXT("resources")), TEXT("goo"));
    const FObject ResearchEntry = Child(Hatchery(Home(Researched, Owner), First), TEXT("active"));
    TestTrue(TEXT("Cancellation remains available after research changes the source price"), Accepted(Researched.Execute(Owner, TEXT("cancelTraining"), Encode(Cancel(First, ResearchEntry)))));
    TestEqual(TEXT("Source cancellation uses current researched Goo price"), Number(Child(Home(Researched, Owner), TEXT("resources")), TEXT("goo")), BeforeResearchRefund + 450);
    Save = Researched.Serialize(); Child(SavedYard(Save, Owner), TEXT("resources"))->SetNumberField(TEXT("goo"), 2000000);
    FBYMSimulation OverCap; if (!Load(*this, OverCap, Save)) return false;
    const FObject OverCapEntry = Child(Hatchery(Home(OverCap, Owner), First), TEXT("active"));
    TestTrue(TEXT("Cancellation is allowed with an over-cap wallet"), Accepted(OverCap.Execute(Owner, TEXT("cancelTraining"), Encode(Cancel(First, OverCapEntry)))));
    TestEqual(TEXT("Over-cap cancellation does not lower the existing wallet"), Number(Child(Home(OverCap, Owner), TEXT("resources")), TEXT("goo")), 2000000.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMHatcheryHousingTest, "BackyardMMO.Hatchery.HousingPauseAndRestart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMHatcheryHousingTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; const FString Owner = TEXT("hatchery-housing");
    FBYMSimulation Seed; if (!Start(*this, Seed, Owner)) return false;
    FObject Save = Seed.Serialize(); const FObject Yard = SavedYard(Save, Owner); Child(Yard, TEXT("army"))->SetNumberField(TEXT("C1"), 25);
    FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
    const double StartAt = Number(Snapshot(Simulation, Owner), TEXT("now"));
    const FString Id = String(Hatchery(Home(Simulation, Owner)), TEXT("building"));
    TestTrue(TEXT("Full housing does not prevent queueing"), Accepted(Simulation.Execute(Owner, TEXT("train"), Encode(Train(Id, 2)))));
    Simulation.Tick(StartAt + 100, 0.f);
    FObject State = Hatchery(Home(Simulation, Owner), Id);
    TestEqual(TEXT("Finished creature remains in the incubator"), String(State, TEXT("state")), FString(TEXT("housing_full")));
    TestEqual(TEXT("Ready army stays within housing capacity"), Number(Child(Home(Simulation, Owner), TEXT("army")), TEXT("C1")), 25.0);
    TestEqual(TEXT("Every paid creature is retained"), Number(State, TEXT("queuedCount")), 2.0);
    Save = Simulation.Serialize(); FBYMSimulation Restored; if (!Load(*this, Restored, Save)) return false;
    Restored.Tick(StartAt + 200, 0.f);
    TestEqual(TEXT("Restart preserves housing-blocked output"), String(Hatchery(Home(Restored, Owner), Id), TEXT("state")), FString(TEXT("housing_full")));
    Save = Restored.Serialize(); const FObject Updated = SavedYard(Save, Owner); Child(Updated, TEXT("army"))->SetNumberField(TEXT("C1"), 24);
    FBYMSimulation Released; if (!Load(*this, Released, Save)) return false;
    Released.Tick(StartAt + 200, 0.f);
    TestEqual(TEXT("Opening one housing slot releases exactly one monster"), Number(Child(Home(Released, Owner), TEXT("army")), TEXT("C1")), 25.0);
    State = Hatchery(Home(Released, Owner), Id);
    TestEqual(TEXT("Blocked time is not banked for the next monster"), Number(Child(State, TEXT("active")), TEXT("remaining")), 15.0);
    Save = Released.Serialize(); const FObject Damaged = SavedYard(Save, Owner);
    Child(Damaged, TEXT("army"))->SetNumberField(TEXT("C1"), 0); Building(Damaged, 13)->SetNumberField(TEXT("hp"), 1999);
    FBYMSimulation Paused; if (!Load(*this, Paused, Save)) return false;
    Paused.Tick(StartAt + 300, 0.f);
    TestFalse(TEXT("New Hatchery orders require repaired source building access"), Accepted(Paused.Execute(Owner, TEXT("train"), Encode(Train(Id, 1)))));
    TestEqual(TEXT("Below-half health pauses incubation"), Number(Child(Hatchery(Home(Paused, Owner), Id), TEXT("active")), TEXT("remaining")), 15.0);
    Save = Paused.Serialize(); const FObject Upgrading = SavedYard(Save, Owner); const FObject Hatch = Building(Upgrading, 13);
    Hatch->SetNumberField(TEXT("hp"), 4000); Hatch->SetNumberField(TEXT("targetLevel"), 2);
    Upgrading->RemoveField(TEXT("workerState")); // Synthetic pre-worker paid job.
    Hatch->SetStringField(TEXT("jobId"), TEXT("hatchery-upgrade-fixture")); Hatch->SetNumberField(TEXT("workRemaining"), 20);
    Hatch->SetNumberField(TEXT("readyAt"), StartAt + 320);
    FBYMSimulation Upgrade; if (!Load(*this, Upgrade, Save)) return false;
    Upgrade.Tick(StartAt + 325, 0.f);
    TestEqual(TEXT("Offline upgrade grants only five seconds after completion"), Number(Child(Hatchery(Home(Upgrade, Owner), Id), TEXT("active")), TEXT("remaining")), 10.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMHatcheryMigrationTest, "BackyardMMO.Hatchery.LegacyMigrationAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMHatcheryMigrationTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; const FString Owner = TEXT("hatchery-migration");
    FBYMSimulation Seed; if (!Start(*this, Seed, Owner)) return false;
    const double StartAt = Number(Snapshot(Seed, Owner), TEXT("now"));
    FObject Save = Seed.Serialize(); SetLegacySchema(Save, 2);
    const FObject Yard = SavedYard(Save, Owner);
    FArray Legacy;
    FObject First = Obj(); First->SetStringField(TEXT("type"), TEXT("C1")); First->SetNumberField(TEXT("count"), 3); First->SetNumberField(TEXT("readyAt"), StartAt + 60); Legacy.Add(MakeShared<FJsonValueObject>(First));
    FObject Second = Obj(); Second->SetStringField(TEXT("type"), TEXT("C1")); Second->SetNumberField(TEXT("count"), 2); Second->SetNumberField(TEXT("readyAt"), StartAt + 90); Legacy.Add(MakeShared<FJsonValueObject>(Second));
    Yard->SetArrayField(TEXT("training"), Legacy);
    FBYMSimulation Migrated; if (!Load(*this, Migrated, Save)) return false;
    Migrated.Tick(StartAt, 0.f); const FString Id = String(Hatchery(Home(Migrated, Owner)), TEXT("building"));
    TestEqual(TEXT("All five paid legacy monsters survive migration"), Number(Hatchery(Home(Migrated, Owner), Id), TEXT("queuedCount")), 5.0);
    TestFalse(TEXT("Grandfathered queue overflow cannot accept new orders"), Accepted(Migrated.Execute(Owner, TEXT("train"), Encode(Train(Id, 1)))));
    Migrated.Tick(StartAt + 60, 0.f);
    TestEqual(TEXT("Original three-creature completion deadline is retained"), Number(Child(Home(Migrated, Owner), TEXT("army")), TEXT("C1")), 3.0);
    Save = Migrated.Serialize(); TestEqual(TEXT("Grouped queues save as schema five"), Number(Save, TEXT("schema")), 5.0);
    FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
    Reloaded.Tick(StartAt + 90, 0.f);
    TestEqual(TEXT("Restart preserves remaining legacy deadline and inventory"), Number(Child(Home(Reloaded, Owner), TEXT("army")), TEXT("C1")), 5.0);
    FObject Invalid = Clone(Save); FObject BadHatchery = Hatchery(SavedYard(Invalid, Owner), Id);
    Child(BadHatchery, TEXT("active"))->SetNumberField(TEXT("count"), 2);
    FBYMSimulation Rejected; FString Error;
    TestFalse(TEXT("An active incubator cannot hold two creatures"), Rejected.Initialize(Invalid, Error));
    Invalid = Clone(Save); BadHatchery = Hatchery(SavedYard(Invalid, Owner), Id); BadHatchery->SetStringField(TEXT("building"), TEXT("foreign-building"));
    FBYMSimulation Foreign; TestFalse(TEXT("Foreign Hatchery ownership is rejected on load"), Foreign.Initialize(Invalid, Error));
    Invalid = Clone(Save); BadHatchery = Hatchery(SavedYard(Invalid, Owner), Id); Child(BadHatchery, TEXT("active"))->SetNumberField(TEXT("remaining"), -1);
    FBYMSimulation Negative; TestFalse(TEXT("Negative production time is rejected"), Negative.Initialize(Invalid, Error));
    Invalid = Clone(Save); BadHatchery = Hatchery(SavedYard(Invalid, Owner), Id);
    Array(BadHatchery, TEXT("queue"))[0]->AsObject()->SetStringField(TEXT("id"), String(Child(BadHatchery, TEXT("active")), TEXT("id")));
    FBYMSimulation Duplicate; TestFalse(TEXT("Duplicate active/waiting identities are rejected"), Duplicate.Initialize(Invalid, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMHatcheryOldMigrationTest, "BackyardMMO.Hatchery.LegacyCatchupBeyondOneYear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMHatcheryOldMigrationTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; const FString Owner = TEXT("hatchery-old-migration");
    FBYMSimulation Seed; if (!Start(*this, Seed, Owner)) return false;
    const double StartAt = Number(Snapshot(Seed, Owner), TEXT("now")), Year = 31536000.0;
    FObject Save = Seed.Serialize(); SetLegacySchema(Save, 2);
    const FObject Yard = SavedYard(Save, Owner);
    Yard->SetNumberField(TEXT("lastEconomy"), StartAt - 2 * Year);
    FArray Legacy;
    FObject Due = Obj(); Due->SetStringField(TEXT("type"), TEXT("C1")); Due->SetNumberField(TEXT("count"), 3);
    Due->SetNumberField(TEXT("readyAt"), StartAt - Year - 60); Legacy.Add(MakeShared<FJsonValueObject>(Due));
    FObject Future = Obj(); Future->SetStringField(TEXT("type"), TEXT("C1")); Future->SetNumberField(TEXT("count"), 2);
    Future->SetNumberField(TEXT("readyAt"), StartAt + 60); Legacy.Add(MakeShared<FJsonValueObject>(Future));
    Yard->SetArrayField(TEXT("training"), Legacy);
    FBYMSimulation Migrated; if (!Load(*this, Migrated, Save)) return false;
    const double LoadedAt = Number(Snapshot(Migrated, Owner), TEXT("now"));
    FObject State = Hatchery(SavedYard(Migrated.Serialize(), Owner));
    if (!TestEqual(TEXT("Migration preserves every creature from both old paid batches"), Array(State, TEXT("queue")).Num(), 5)) return false;
    TestEqual(TEXT("Already due first creature has no skipped-year delay"), Number(Array(State, TEXT("queue"))[0]->AsObject(), TEXT("remaining")), 0.0);
    TestEqual(TEXT("Simultaneous paid batch members retain zero interval"), Number(Array(State, TEXT("queue"))[2]->AsObject(), TEXT("remaining")), 0.0);
    TestEqual(TEXT("Future batch retains its interval from the bounded catch-up anchor"), Number(Array(State, TEXT("queue"))[3]->AsObject(), TEXT("remaining")), StartAt + 60 - (LoadedAt - Year));
    Migrated.Tick(LoadedAt, 0.f);
    State = Hatchery(Home(Migrated, Owner));
    TestEqual(TEXT("All three old due creatures hatch on the first catch-up"), Number(Child(Home(Migrated, Owner), TEXT("army")), TEXT("C1")), 3.0);
    TestEqual(TEXT("Both future paid creatures remain owned"), Number(State, TEXT("queuedCount")), 2.0);
    TestEqual(TEXT("The next active creature retains the original future deadline"), Number(Child(State, TEXT("active")), TEXT("remaining")), StartAt + 60 - LoadedAt);
    if (!TestEqual(TEXT("The future batch still has one waiting creature"), Array(State, TEXT("queue")).Num(), 1)) return false;
    TestEqual(TEXT("Second future creature retains its simultaneous completion"), Number(Array(State, TEXT("queue"))[0]->AsObject(), TEXT("remaining")), 0.0);
    Save = Migrated.Serialize(); FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
    Reloaded.Tick(StartAt + 60, 0.f);
    TestEqual(TEXT("Restart and the final deadline preserve all five paid monsters"), Number(Child(Home(Reloaded, Owner), TEXT("army")), TEXT("C1")), 5.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMHatcheryDestructionTest, "BackyardMMO.Hatchery.DestructionRefundOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMHatcheryDestructionTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; const FString Defender = TEXT("hatchery-defender"), Attacker = TEXT("hatchery-attacker");
    FBYMSimulation Seed; if (!Start(*this, Seed, Defender)) return false; FString Error;
    if (!TestTrue(TEXT("Create attacking fixture"), Seed.AddPlayer(Attacker, Attacker, Error))) return false;
    const FString Id = String(Hatchery(Home(Seed, Defender)), TEXT("building"));
    TestTrue(TEXT("Defender owns three paid monsters"), Accepted(Seed.Execute(Defender, TEXT("train"), Encode(Train(Id, 3)))));
    FObject Save = Seed.Serialize(); const FObject Yard = SavedYard(Save, Defender); FObject Hatch = Building(Yard, 13);
    Hatch->SetNumberField(TEXT("x"), 4); Hatch->SetNumberField(TEXT("y"), 4); Hatch->SetNumberField(TEXT("hp"), 1);
    FArray Structures; Structures.Add(MakeShared<FJsonValueObject>(Building(Yard, 14))); Structures.Add(MakeShared<FJsonValueObject>(Hatch)); Yard->SetArrayField(TEXT("buildings"), Structures);
    Child(SavedYard(Save, Attacker), TEXT("army"))->SetNumberField(TEXT("C1"), 1);
    FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
    const double StartAt = Number(Snapshot(Simulation, Attacker), TEXT("now"));
    const double Before = Number(Child(Home(Simulation, Defender), TEXT("resources")), TEXT("goo"));
    FObject Target = Obj(); Target->SetStringField(TEXT("target"), Defender);
    if (!TestTrue(TEXT("Start a real authoritative invasion"), Accepted(Simulation.Execute(Attacker, TEXT("invade"), Encode(Target))))) return false;
    FObject Deploy = Obj(); Deploy->SetStringField(TEXT("type"), TEXT("C1")); Deploy->SetNumberField(TEXT("count"), 1); Deploy->SetNumberField(TEXT("x"), 0); Deploy->SetNumberField(TEXT("y"), 4);
    if (!TestTrue(TEXT("Deploy a monster at the yard edge"), Accepted(Simulation.Execute(Attacker, TEXT("deploy"), Encode(Deploy))))) return false;
    bool Destroyed = false;
    for (int32 Step = 1; Step <= 600; ++Step)
    {
        Simulation.Tick(StartAt + Step * 0.1, 0.1f);
        const FObject DefendingHatchery = Building(Child(Snapshot(Simulation, Defender), TEXT("yard")), 13);
        if (!TestTrue(TEXT("The defending yard retains its Hatchery building"), DefendingHatchery.IsValid())) return false;
        if (Number(DefendingHatchery, TEXT("hp")) <= 0) { Destroyed = true; break; }
    }
    if (!TestTrue(TEXT("Invader reaches and destroys the damaged Hatchery"), Destroyed)) return false;
    TestEqual(TEXT("Destruction refunds ceil of 75 percent of 750 Goo"), Number(Child(Home(Simulation, Defender), TEXT("resources")), TEXT("goo")), Before + 563);
    TestEqual(TEXT("Destroyed Hatchery clears active and waiting creatures"), Number(Hatchery(Home(Simulation, Defender), Id), TEXT("queuedCount")), 0.0);
    Simulation.Tick(StartAt + 120, 0.f);
    TestEqual(TEXT("Later updates cannot pay the destruction refund again"), Number(Child(Home(Simulation, Defender), TEXT("resources")), TEXT("goo")), Before + 563);
    return true;
}
} // namespace BYMHatcheryTests

#endif
