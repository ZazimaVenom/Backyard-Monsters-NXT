#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// These tests exercise the actual authoritative simulation and bundled source
// catalog. They never create accounts, open a port or write a save to disk.
namespace BYMProgressionTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;

    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Bots = Settings->RivalBotCount;
        const int32 Starting = Settings->StartingResources;
        const int32 Cells = Settings->YardCells;
        const int32 StorageFloor = Settings->ResourceStorageFloor;
        const int32 Troops = Settings->MaximumTroopsPerBattle;
        const float Training = Settings->TrainingTimeMultiplier;
        const float Progression = Settings->MonsterProgressionTimeMultiplier;
        const float Production = Settings->ResourceProductionMultiplier;
        const float Battle = Settings->MaximumBattleSeconds;
        const float BotDecision = Settings->BotDecisionSeconds;
        const float Step = Settings->SimulationStepSeconds;
        explicit FSettingsGuard(int32 BotCount = 0)
        {
            Settings->RivalBotCount = BotCount;
            Settings->StartingResources = 10000;
            Settings->YardCells = 100;
            Settings->ResourceStorageFloor = 100000;
            Settings->MaximumTroopsPerBattle = 100;
            Settings->TrainingTimeMultiplier = 1;
            Settings->MonsterProgressionTimeMultiplier = 1;
            Settings->ResourceProductionMultiplier = 1;
            Settings->MaximumBattleSeconds = 300;
            Settings->BotDecisionSeconds = 60;
            Settings->SimulationStepSeconds = 1;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots;
            Settings->StartingResources = Starting;
            Settings->YardCells = Cells;
            Settings->ResourceStorageFloor = StorageFloor;
            Settings->MaximumTroopsPerBattle = Troops;
            Settings->TrainingTimeMultiplier = Training;
            Settings->MonsterProgressionTimeMultiplier = Progression;
            Settings->ResourceProductionMultiplier = Production;
            Settings->MaximumBattleSeconds = Battle;
            Settings->BotDecisionSeconds = BotDecision;
            Settings->SimulationStepSeconds = Step;
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
    bool Flag(const FObject& Object, const TCHAR* Field)
    {
        bool Value = false;
        return Object && Object->TryGetBoolField(Field, Value) && Value;
    }
    bool Accepted(const FString& Reply) { return Flag(Decode(Reply), TEXT("ok")); }
    FObject Snapshot(const FBYMSimulation& Simulation, const FString& Id)
    { return Decode(Simulation.Snapshot(Id, FString())); }
    FObject Home(const FBYMSimulation& Simulation, const FString& Id)
    { return Child(Snapshot(Simulation, Id), TEXT("home")); }
    FObject Option(const FBYMSimulation& Simulation, const FString& Id, const TCHAR* Type)
    { return Child(Child(Snapshot(Simulation, Id), TEXT("monsterOptions")), Type); }
    FObject SavedYard(const FObject& Save, const FString& Id)
    {
        for (const TSharedPtr<FJsonValue>& Value : Array(Save, TEXT("yards")))
            if (Value.IsValid() && Value->Type == EJson::Object && String(Value->AsObject(), TEXT("id")) == Id) return Value->AsObject();
        return nullptr;
    }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FString& Id)
    {
        FString Error;
        if (!Simulation.Initialize(nullptr, Error)) { Test.AddError(Error); return false; }
        if (!Simulation.AddPlayer(Id, Id, Error)) { Test.AddError(Error); return false; }
        return true;
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    {
        FString Error;
        if (!Simulation.Initialize(Save, Error)) { Test.AddError(TEXT("Progression fixture load: ") + Error); return false; }
        return true;
    }
    void SameWallet(FAutomationTestBase& Test, const FObject& Before, const FObject& After)
    {
        for (const TCHAR* Resource : { TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo") })
            Test.TestEqual(FString(TEXT("Rejected request preserves ")) + Resource,
                Number(Child(After, TEXT("resources")), Resource), Number(Child(Before, TEXT("resources")), Resource));
    }
    void AddAcademy(const FObject& Save, const FString& Id, const FString& BuildingId = TEXT("progression-test-academy"), int32 X = 1)
    {
        // A valid completed level-one Academy fixture. The starter layout sits
        // around (50,50) in this 100-cell test yard, leaving (1,1) unobstructed.
        const FObject Yard = SavedYard(Save, Id);
        // The fixed Academy fixture is independent of procedural mushroom placement.
        Child(Yard, TEXT("economy"))->SetArrayField(TEXT("mushrooms"), FArray());
        FArray Buildings = Array(Yard, TEXT("buildings"));
        // Preserve the source's construction prerequisites and two-Academy
        // allowance in the advanced fixture: Town Hall 5 and Monster Locker 2.
        for (const TSharedPtr<FJsonValue>& Value : Buildings)
        {
            const FObject Building = Value->AsObject();
            if (Number(Building, TEXT("type")) == 14)
            {
                Building->SetNumberField(TEXT("level"), 5); Building->SetNumberField(TEXT("targetLevel"), 5);
                Building->SetNumberField(TEXT("hp"), 94000);
            }
            if (Number(Building, TEXT("type")) == 8)
            {
                Building->SetNumberField(TEXT("level"), 2); Building->SetNumberField(TEXT("targetLevel"), 2);
                Building->SetNumberField(TEXT("hp"), 16000);
            }
        }
        FObject Academy = MakeShared<FJsonObject>();
        Academy->SetStringField(TEXT("id"), BuildingId);
        Academy->SetNumberField(TEXT("type"), 26);
        Academy->SetNumberField(TEXT("level"), 1);
        Academy->SetNumberField(TEXT("targetLevel"), 1);
        Academy->SetNumberField(TEXT("x"), X);
        Academy->SetNumberField(TEXT("y"), 1);
        Academy->SetNumberField(TEXT("hp"), 6000);
        Academy->SetNumberField(TEXT("readyAt"), 0);
        Academy->SetNumberField(TEXT("stored"), 0);
        Academy->SetStringField(TEXT("jobId"), FString()); Academy->SetNumberField(TEXT("workRemaining"), 0);
        Academy->SetBoolField(TEXT("repairing"), false);
        Buildings.Add(MakeShared<FJsonValueObject>(Academy));
        Yard->SetArrayField(TEXT("buildings"), Buildings);
    }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMonsterUnlockLifecycleTest, "BackyardMMO.Progression.UnlockLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMMonsterUnlockLifecycleTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    const FString Player(TEXT("unlock-player"));
    FBYMSimulation Simulation;
    if (!Start(*this, Simulation, Player)) return false;
    const double StartTime = Number(Snapshot(Simulation, Player), TEXT("now"));
    TestTrue(TEXT("Original starter Pokey is unlocked"), Flag(Option(Simulation, Player, TEXT("C1")), TEXT("unlocked")));
    TestEqual(TEXT("Starter Pokey begins at research level one"), Number(Option(Simulation, Player, TEXT("C1")), TEXT("level")), 1.0);
    TestFalse(TEXT("Octo-ooze requires a real unlock"), Flag(Option(Simulation, Player, TEXT("C2")), TEXT("unlocked")));
    TestFalse(TEXT("An original source-blocked monster is not offered as a Locker unlock"), Flag(Option(Simulation, Player, TEXT("C17")), TEXT("canUnlock")));
    const FObject Before = Home(Simulation, Player);
    TestFalse(TEXT("Locked monsters cannot be hatched"), Accepted(Simulation.Execute(Player, TEXT("train"), TEXT("{\"type\":\"C2\",\"count\":1}"))));
    SameWallet(*this, Before, Home(Simulation, Player));
    TestEqual(TEXT("Original Octo-ooze unlock costs 8,000 Putty"), Number(Option(Simulation, Player, TEXT("C2")), TEXT("unlockCost")), 8000.0);
    TestEqual(TEXT("Original Octo-ooze unlock takes one hour"), Number(Option(Simulation, Player, TEXT("C2")), TEXT("unlockSeconds")), 3600.0);
    if (!TestTrue(TEXT("Host starts a valid Locker unlock"), Accepted(Simulation.Execute(Player, TEXT("unlock"), TEXT("{\"type\":\"C2\"}"))))) return false;
    TestEqual(TEXT("Unlock charges Putty rather than Goo"), Number(Child(Home(Simulation, Player), TEXT("resources")), TEXT("putty")), 2000.0);
    TestEqual(TEXT("Unlock leaves the Goo wallet intact"), Number(Child(Home(Simulation, Player), TEXT("resources")), TEXT("goo")), 10000.0);
    const FObject Paid = Home(Simulation, Player);
    TestFalse(TEXT("The Locker cannot start another unlock concurrently"), Accepted(Simulation.Execute(Player, TEXT("unlock"), TEXT("{\"type\":\"C3\"}"))));
    SameWallet(*this, Paid, Home(Simulation, Player));
    TestFalse(TEXT("An unfinished unlock does not enable training"), Accepted(Simulation.Execute(Player, TEXT("train"), TEXT("{\"type\":\"C2\",\"count\":1}"))));
    if (!TestTrue(TEXT("Canceling a Locker unlock succeeds"), Accepted(Simulation.Execute(Player, TEXT("cancelUnlock"), TEXT("{\"type\":\"C2\"}"))))) return false;
    TestEqual(TEXT("Original full refund restores the paid Putty once"), Number(Child(Home(Simulation, Player), TEXT("resources")), TEXT("putty")), 10000.0);
    TestFalse(TEXT("A repeated cancellation cannot refund again"), Accepted(Simulation.Execute(Player, TEXT("cancelUnlock"), TEXT("{\"type\":\"C2\"}"))));
    TestEqual(TEXT("Duplicate cancellation cannot create Putty"), Number(Child(Home(Simulation, Player), TEXT("resources")), TEXT("putty")), 10000.0);
    if (!TestTrue(TEXT("A canceled unlock can be restarted"), Accepted(Simulation.Execute(Player, TEXT("unlock"), TEXT("{\"type\":\"C2\"}"))))) return false;
    // Collecting resources while an unlock runs can fill storage before cancel.
    // Exercise that legitimate refund edge without changing the live fixture.
    FObject FullStorageSave = Simulation.Serialize();
    const double Capacity = Number(Home(Simulation, Player), TEXT("storageCapacity"));
    Child(SavedYard(FullStorageSave, Player), TEXT("resources"))->SetNumberField(TEXT("putty"), Capacity);
    FBYMSimulation FullStorage;
    if (!Load(*this, FullStorage, FullStorageSave)) return false;
    TestTrue(TEXT("A paid unlock can be canceled after storage fills"), Accepted(FullStorage.Execute(Player, TEXT("cancelUnlock"), TEXT("{\"type\":\"C2\"}"))));
    TestEqual(TEXT("Unlock refund respects resource storage capacity"), Number(Child(Home(FullStorage, Player), TEXT("resources")), TEXT("putty")), Capacity);
    Simulation.Tick(StartTime + 3599.0, 0.0f);
    TestFalse(TEXT("Unlock does not complete before its deadline"), Flag(Option(Simulation, Player, TEXT("C2")), TEXT("unlocked")));
    FBYMSimulation Restored;
    if (!Load(*this, Restored, Simulation.Serialize())) return false;
    TestEqual(TEXT("Restart retains the original absolute unlock deadline"), Number(Child(Home(Restored, Player), TEXT("monsterUnlock")), TEXT("readyAt")), StartTime + 3600.0);
    Restored.Tick(StartTime + 3600.0, 0.0f);
    TestTrue(TEXT("Unlock completes at its exact deadline after reload"), Flag(Option(Restored, Player, TEXT("C2")), TEXT("unlocked")));
    TestEqual(TEXT("Newly unlocked monster starts at level one"), Number(Option(Restored, Player, TEXT("C2")), TEXT("level")), 1.0);
    Restored.Tick(StartTime + 3600.0, 0.0f);
    TestEqual(TEXT("Repeated timer processing does not upgrade the monster again"), Number(Option(Restored, Player, TEXT("C2")), TEXT("level")), 1.0);
    TestEqual(TEXT("Completion does not charge or refund a second time"), Number(Child(Home(Restored, Player), TEXT("resources")), TEXT("putty")), 2000.0);
    TestTrue(TEXT("The genuinely unlocked monster can now be hatched"), Accepted(Restored.Execute(Player, TEXT("train"), TEXT("{\"type\":\"C2\",\"count\":1}"))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMonsterResearchTrainingTest, "BackyardMMO.Progression.ResearchAndLeveledTraining",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMMonsterResearchTrainingTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    // Original 7,200-second research is shortened through the real host setting,
    // so completion can be exercised while a normal 300-second invasion is live.
    Settings.Settings->MonsterProgressionTimeMultiplier = 0.01f;
    const FString Player(TEXT("research-player")), Defender(TEXT("research-defender"));
    FBYMSimulation Starter;
    if (!Start(*this, Starter, Player)) return false;
    const FObject Before = Home(Starter, Player);
    TestFalse(TEXT("Research requires a completed Academy"), Accepted(Starter.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
    SameWallet(*this, Before, Home(Starter, Player));
    FObject Fixture = Starter.Serialize();
    AddAcademy(Fixture, Player);
    FBYMSimulation Simulation;
    if (!Load(*this, Simulation, Fixture)) return false;
    FString Error;
    if (!TestTrue(TEXT("Create an independent defending yard"), Simulation.AddPlayer(Defender, Defender, Error))) { AddError(Error); return false; }
    const double StartTime = Number(Snapshot(Simulation, Player), TEXT("now"));
    const FObject Pokey = Option(Simulation, Player, TEXT("C1"));
    TestEqual(TEXT("Original first research step costs 4,000 Putty"), Number(Pokey, TEXT("researchCost")), 4000.0);
    const double Duration = Number(Pokey, TEXT("researchSeconds"));
    TestTrue(TEXT("Host progression multiplier scales the original two-hour duration"), FMath::IsNearlyEqual(Duration, 72.0, 0.001));
    TestEqual(TEXT("Level-two research requires Academy level one"), Number(Pokey, TEXT("requiredAcademyLevel")), 1.0);
    if (!TestTrue(TEXT("Academy accepts source-backed research"), Accepted(Simulation.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C1\"}"))))) return false;
    if (!TestTrue(TEXT("Academy research supports its original cancellation refund"), Accepted(Simulation.Execute(Player, TEXT("cancelResearch"), TEXT("{\"type\":\"C1\"}"))))) return false;
    TestEqual(TEXT("Canceled research refunds the paid Putty once"), Number(Child(Home(Simulation, Player), TEXT("resources")), TEXT("putty")), 10000.0);
    TestFalse(TEXT("Repeated research cancellation cannot mint Putty"), Accepted(Simulation.Execute(Player, TEXT("cancelResearch"), TEXT("{\"type\":\"C1\"}"))));
    if (!TestTrue(TEXT("Canceled research can be started again"), Accepted(Simulation.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C1\"}"))))) return false;
    const FObject Paid = Home(Simulation, Player);
    TestEqual(TEXT("Research reserves exactly the source Putty cost"), Number(Child(Paid, TEXT("resources")), TEXT("putty")), 6000.0);
    TestFalse(TEXT("Research cannot be started twice for the same monster"), Accepted(Simulation.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
    SameWallet(*this, Paid, Home(Simulation, Player));
    TestTrue(TEXT("Original Academy movement remains available during research"), Accepted(Simulation.Execute(Player, TEXT("move"), TEXT("{\"building\":\"progression-test-academy\",\"x\":10,\"y\":1}"))));
    const FObject MovedResearch = Home(Simulation, Player);
    if (!TestEqual(TEXT("Moving the Academy retains its job"), Array(MovedResearch, TEXT("monsterResearch")).Num(), 1)) return false;
    TestEqual(TEXT("Research stays bound to the same Academy identity"), String(Array(MovedResearch, TEXT("monsterResearch"))[0]->AsObject(), TEXT("building")), FString(TEXT("progression-test-academy")));
    FObject RepairFixture = Simulation.Serialize();
    for (const TSharedPtr<FJsonValue>& Value : Array(SavedYard(RepairFixture, Player), TEXT("buildings")))
        if (Number(Value->AsObject(), TEXT("type")) == 26) Value->AsObject()->SetNumberField(TEXT("hp"), 4500);
    FBYMSimulation DamagedAcademy;
    if (!Load(*this, DamagedAcademy, RepairFixture)) return false;
    if (!TestTrue(TEXT("An occupied damaged Academy can be repaired"), Accepted(DamagedAcademy.Execute(Player, TEXT("repair"), TEXT("{\"building\":\"progression-test-academy\"}"))))) return false;
    FBYMSimulation RepairReload;
    if (!Load(*this, RepairReload, DamagedAcademy.Serialize())) return false;
    TestEqual(TEXT("An Academy repair and its legitimate research job survive together"), Array(Home(RepairReload, Player), TEXT("monsterResearch")).Num(), 1);
    if (!TestTrue(TEXT("Hatching and Academy research use separate queues"), Accepted(Simulation.Execute(Player, TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":2}"))))) return false;
    Simulation.Tick(StartTime + 31.0, 0.0f);
    if (!TestTrue(TEXT("Research does not prevent a valid invasion"), Accepted(Simulation.Execute(Player, TEXT("invade"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Defender))))) return false;
    if (!TestTrue(TEXT("Deploy a level-one monster before research finishes"), Accepted(Simulation.Execute(Player, TEXT("deploy"), TEXT("{\"type\":\"C1\",\"count\":1,\"x\":99,\"y\":99}"))))) return false;
    const FObject MidResearchSave = Simulation.Serialize();
    TestEqual(TEXT("Research persists one job bound to the Academy"), Array(SavedYard(MidResearchSave, Player), TEXT("monsterResearch")).Num(), 1);
    FBYMSimulation Restored;
    if (!Load(*this, Restored, MidResearchSave)) return false;
    Restored.Tick(StartTime + Duration - 0.01, 0.0f);
    TestEqual(TEXT("Research level remains unchanged before its deadline"), Number(Option(Restored, Player, TEXT("C1")), TEXT("level")), 1.0);
    Restored.Tick(StartTime + Duration, 0.0f);
    const FObject Upgraded = Option(Restored, Player, TEXT("C1"));
    TestEqual(TEXT("Research reaches exactly level two after reload"), Number(Upgraded, TEXT("level")), 2.0);
    const FObject Stats = Child(Upgraded, TEXT("currentStats"));
    TestEqual(TEXT("Original level-two Pokey health is applied"), Number(Stats, TEXT("hp")), 220.0);
    TestEqual(TEXT("Original level-two Pokey damage is applied"), Number(Stats, TEXT("damage")), 65.0);
    TestEqual(TEXT("Original level-two hatch cost is applied"), Number(Stats, TEXT("goo")), 450.0);
    TestEqual(TEXT("Original level-two hatch duration is applied"), Number(Stats, TEXT("time")), 10.0);
    TestEqual(TEXT("Original level-two housing is applied"), Number(Stats, TEXT("housing")), 10.0);
    const FObject ExistingBattleSnapshot = Snapshot(Restored, Player);
    const FArray& ExistingTroops = Array(Child(ExistingBattleSnapshot, TEXT("battle")), TEXT("troops"));
    if (!TestEqual(TEXT("The earlier deployed troop remains in the live invasion"), ExistingTroops.Num(), 1)) return false;
    TestEqual(TEXT("Already deployed monsters retain their frozen research level"), Number(ExistingTroops[0]->AsObject(), TEXT("level")), 1.0);
    TestEqual(TEXT("Research cannot heal an existing deployed monster to new max HP"), Number(ExistingTroops[0]->AsObject(), TEXT("hp")), 200.0);
    if (!TestTrue(TEXT("A later deployment uses the newly researched level"), Accepted(Restored.Execute(Player, TEXT("deploy"), TEXT("{\"type\":\"C1\",\"count\":1,\"x\":99,\"y\":99}"))))) return false;
    const FObject LaterBattleSnapshot = Snapshot(Restored, Player);
    const FArray& Troops = Array(Child(LaterBattleSnapshot, TEXT("battle")), TEXT("troops"));
    if (!TestEqual(TEXT("Both valid deployments remain present"), Troops.Num(), 2)) return false;
    TestEqual(TEXT("New deployment snapshots level two"), Number(Troops.Last()->AsObject(), TEXT("level")), 2.0);
    TestEqual(TEXT("New deployment begins with researched health"), Number(Troops.Last()->AsObject(), TEXT("hp")), 220.0);
    TestTrue(TEXT("Complete the fixture invasion through the genuine retreat command"), Accepted(Restored.Execute(Player, TEXT("retreat"), TEXT("{}"))));
    Restored.Tick(StartTime + Duration, 0.0f);
    TestEqual(TEXT("Research completion is idempotent"), Number(Option(Restored, Player, TEXT("C1")), TEXT("level")), 2.0);
    TestEqual(TEXT("Completed research releases the Academy slot"), Array(Home(Restored, Player), TEXT("monsterResearch")).Num(), 0);
    TestEqual(TEXT("Next research step requires Academy level two"), Number(Option(Restored, Player, TEXT("C1")), TEXT("requiredAcademyLevel")), 2.0);
    TestFalse(TEXT("Academy level one cannot skip the level-two gate"), Accepted(Restored.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
    if (!TestTrue(TEXT("Level-two hatching can be queued after returning home"), Accepted(Restored.Execute(Player, TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":2}"))))) return false;
    TestEqual(TEXT("Two level-two monsters cost 900 Goo after the original 500 Goo wave"), Number(Child(Home(Restored, Player), TEXT("resources")), TEXT("goo")), 8600.0);
    TestEqual(TEXT("Queued monsters do not reserve Housing before they hatch"), Number(Home(Restored, Player), TEXT("housingUsed")), 0.0);
    TestEqual(TEXT("The UI still reports the queued monsters' researched housing requirement"), Number(Home(Restored, Player), TEXT("housingQueued")), 20.0);
    const FObject TrainingHome = Home(Restored, Player);
    const FArray& Training = Array(TrainingHome, TEXT("training"));
    if (!TestEqual(TEXT("Two monsters occupy two ordered hatch jobs"), Training.Num(), 2)) return false;
    TestTrue(TEXT("First level-two monster uses its ten-second hatch duration"), FMath::IsNearlyEqual(Number(Training[0]->AsObject(), TEXT("readyAt")), StartTime + Duration + 10.0, 0.001));
    Restored.Tick(StartTime + Duration + 9.99, 0.0f);
    TestEqual(TEXT("No queued monster appears before its first hatch deadline"), Number(Child(Home(Restored, Player), TEXT("army")), TEXT("C1")), 0.0);
    Restored.Tick(StartTime + Duration + 10.0, 0.0f);
    TestEqual(TEXT("The first hatch deadline releases exactly one monster"), Number(Child(Home(Restored, Player), TEXT("army")), TEXT("C1")), 1.0);
    TestEqual(TEXT("Only the completed monster now occupies Housing"), Number(Home(Restored, Player), TEXT("housingUsed")), 10.0);
    TestEqual(TEXT("The second monster remains queued after the first hatches"), Array(Home(Restored, Player), TEXT("training")).Num(), 1);
    Restored.Tick(StartTime + Duration + 20.0, 0.0f);
    TestEqual(TEXT("Both researched hatch jobs complete exactly once"), Number(Child(Home(Restored, Player), TEXT("army")), TEXT("C1")), 2.0);
    TestEqual(TEXT("The final hatch deadline clears the remaining job"), Array(Home(Restored, Player), TEXT("training")).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMonsterProgressionSaveTest, "BackyardMMO.Progression.SaveMigrationAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMMonsterProgressionSaveTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    const FString Player(TEXT("legacy-monster-player"));
    FBYMSimulation Original;
    if (!Start(*this, Original, Player)) return false;
    const double StartTime = Number(Snapshot(Original, Player), TEXT("now"));
    FObject Legacy = Original.Serialize();
    SetLegacySchema(Legacy, 1);
    FObject OldYard = SavedYard(Legacy, Player);
    OldYard->RemoveField(TEXT("monsterLevels"));
    OldYard->RemoveField(TEXT("monsterUnlock"));
    OldYard->RemoveField(TEXT("monsterResearch"));
    Child(OldYard, TEXT("army"))->SetNumberField(TEXT("C3"), 2);
    Child(OldYard, TEXT("army"))->SetNumberField(TEXT("C5"), 0);
    FObject Queued = MakeShared<FJsonObject>();
    Queued->SetStringField(TEXT("type"), TEXT("C4"));
    Queued->SetNumberField(TEXT("count"), 1);
    Queued->SetNumberField(TEXT("readyAt"), StartTime + 3600);
    FArray Queue; Queue.Add(MakeShared<FJsonValueObject>(Queued));
    OldYard->SetArrayField(TEXT("training"), Queue);
    FBYMSimulation Migrated;
    if (!Load(*this, Migrated, Legacy)) return false;
    TestEqual(TEXT("Existing ready monsters survive schema-one migration"), Number(Child(Home(Migrated, Player), TEXT("army")), TEXT("C3")), 2.0);
    TestTrue(TEXT("Existing inventory receives its original level-one unlock"), Flag(Option(Migrated, Player, TEXT("C3")), TEXT("unlocked")));
    TestTrue(TEXT("A previously hatched species remains unlocked after its last legacy unit was spent"), Flag(Option(Migrated, Player, TEXT("C5")), TEXT("unlocked")));
    TestEqual(TEXT("An empty legacy army entry does not create free monsters"), Number(Child(Home(Migrated, Player), TEXT("army")), TEXT("C5")), 0.0);
    TestTrue(TEXT("Paid legacy queue types retain their unlock"), Flag(Option(Migrated, Player, TEXT("C4")), TEXT("unlocked")));
    TestEqual(TEXT("Migration does not invent research levels"), Number(Option(Migrated, Player, TEXT("C4")), TEXT("level")), 1.0);
    TestFalse(TEXT("Migration does not grant every other monster"), Flag(Option(Migrated, Player, TEXT("C2")), TEXT("unlocked")));
    TestEqual(TEXT("Paid legacy queue is retained"), Array(Home(Migrated, Player), TEXT("training")).Num(), 1);
    const FObject UpgradedSave = Migrated.Serialize();
    TestEqual(TEXT("The next snapshot persists explicit per-Hatchery state in schema five"), Number(UpgradedSave, TEXT("schema")), 5.0);
    TestEqual(TEXT("Migration binds the paid queue to the existing Hatchery"), Array(SavedYard(UpgradedSave, Player), TEXT("hatcheries")).Num(), 1);
    const FObject ModifiedProjection = Clone(UpgradedSave);
    FObject FabricatedTraining = MakeShared<FJsonObject>();
    FabricatedTraining->SetStringField(TEXT("type"), TEXT("C4"));
    FabricatedTraining->SetNumberField(TEXT("count"), 100);
    FabricatedTraining->SetNumberField(TEXT("readyAt"), StartTime);
    FArray FabricatedQueue; FabricatedQueue.Add(MakeShared<FJsonValueObject>(FabricatedTraining));
    SavedYard(ModifiedProjection, Player)->SetArrayField(TEXT("training"), FabricatedQueue);
    FBYMSimulation ProjectionReload;
    if (!Load(*this, ProjectionReload, ModifiedProjection)) return false;
    TestEqual(TEXT("Schema-three reload ignores fabricated compatibility queue estimates"), Array(Home(ProjectionReload, Player), TEXT("training")).Num(), 1);
    TestEqual(TEXT("Changing compatibility estimates cannot create ready monsters"), Number(Child(Home(ProjectionReload, Player), TEXT("army")), TEXT("C4"), 0), 0.0);
    FBYMSimulation RoundTrip;
    if (!Load(*this, RoundTrip, UpgradedSave)) return false;
    RoundTrip.Tick(StartTime + 3600.0, 0.0f);
    TestEqual(TEXT("Legacy queue completes without losing the paid monster"), Number(Child(Home(RoundTrip, Player), TEXT("army")), TEXT("C4")), 1.0);
    TestEqual(TEXT("Completing legacy queue preserves existing army"), Number(Child(Home(RoundTrip, Player), TEXT("army")), TEXT("C3")), 2.0);

    FString Error;
    for (const double InvalidLevel : { 0.0, 1.5, 999.0 })
    {
        FObject Invalid = Clone(UpgradedSave);
        Child(SavedYard(Invalid, Player), TEXT("monsterLevels"))->SetNumberField(TEXT("C1"), InvalidLevel);
        FBYMSimulation Rejected;
        TestFalse(TEXT("Current saves reject invalid research levels"), Rejected.Initialize(Invalid, Error));
        TestFalse(TEXT("Invalid research save reports a recoverable load error"), Error.IsEmpty());
    }
    FObject MissingProgression = Clone(UpgradedSave);
    SavedYard(MissingProgression, Player)->RemoveField(TEXT("monsterLevels"));
    FBYMSimulation Missing;
    TestFalse(TEXT("Current saves cannot silently downgrade to migration by deleting progression"), Missing.Initialize(MissingProgression, Error));
    FObject UnknownMonster = Clone(UpgradedSave);
    Child(SavedYard(UnknownMonster, Player), TEXT("monsterLevels"))->SetNumberField(TEXT("C9999"), 1);
    FBYMSimulation Unknown;
    TestFalse(TEXT("Unknown saved research identities are rejected"), Unknown.Initialize(UnknownMonster, Error));

    FObject TwoAcademySave = Clone(UpgradedSave);
    AddAcademy(TwoAcademySave, Player);
    AddAcademy(TwoAcademySave, Player, TEXT("progression-test-second-academy"), 10);
    Child(SavedYard(TwoAcademySave, Player), TEXT("monsterLevels"))->SetNumberField(TEXT("C2"), 1);
    Child(SavedYard(TwoAcademySave, Player), TEXT("resources"))->SetNumberField(TEXT("putty"), 20000);
    FBYMSimulation TwoAcademies;
    if (!Load(*this, TwoAcademies, TwoAcademySave)) return false;
    if (!TestTrue(TEXT("First Academy begins Pokey research"), Accepted(TwoAcademies.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C1\"}"))))) return false;
    TestFalse(TEXT("The same monster cannot occupy two Academies"), Accepted(TwoAcademies.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
    if (!TestTrue(TEXT("Second Academy can research a different monster concurrently"), Accepted(TwoAcademies.Execute(Player, TEXT("research"), TEXT("{\"type\":\"C2\"}"))))) return false;
    const FObject BothJobsSave = TwoAcademies.Serialize();
    TestEqual(TEXT("Two Academy jobs reserve their independent source costs"), Number(Child(Home(TwoAcademies, Player), TEXT("resources")), TEXT("putty")), 8000.0);
    const FArray& Jobs = Array(SavedYard(BothJobsSave, Player), TEXT("monsterResearch"));
    if (!TestEqual(TEXT("Both valid jobs are persisted"), Jobs.Num(), 2)) return false;
    TestTrue(TEXT("Concurrent jobs belong to different Academy identities"), String(Jobs[0]->AsObject(), TEXT("building")) != String(Jobs[1]->AsObject(), TEXT("building")));
    FBYMSimulation BothJobsReloaded;
    if (!Load(*this, BothJobsReloaded, BothJobsSave)) return false;
    FObject DuplicateAcademySave = Clone(BothJobsSave);
    const FArray& DuplicateAcademyJobs = Array(SavedYard(DuplicateAcademySave, Player), TEXT("monsterResearch"));
    DuplicateAcademyJobs[1]->AsObject()->SetStringField(TEXT("building"), String(DuplicateAcademyJobs[0]->AsObject(), TEXT("building")));
    FBYMSimulation DuplicateAcademy;
    TestFalse(TEXT("Two saved jobs cannot claim the same Academy"), DuplicateAcademy.Initialize(DuplicateAcademySave, Error));
    FObject DuplicateTypeSave = Clone(BothJobsSave);
    const FArray& DuplicateTypeJobs = Array(SavedYard(DuplicateTypeSave, Player), TEXT("monsterResearch"));
    DuplicateTypeJobs[1]->AsObject()->SetStringField(TEXT("type"), TEXT("C1"));
    DuplicateTypeJobs[1]->AsObject()->SetNumberField(TEXT("puttyPaid"), 4000);
    FBYMSimulation DuplicateType;
    TestFalse(TEXT("Two saved Academy jobs cannot research the same species"), DuplicateType.Initialize(DuplicateTypeSave, Error));

    if (!TestTrue(TEXT("Prepare a valid paid unlock for corruption testing"), Accepted(Original.Execute(Player, TEXT("unlock"), TEXT("{\"type\":\"C2\"}"))))) return false;
    FObject BadPayment = Original.Serialize();
    Child(SavedYard(BadPayment, Player), TEXT("monsterUnlock"))->SetNumberField(TEXT("puttyPaid"), -1);
    FBYMSimulation NegativePayment;
    TestFalse(TEXT("A malformed refund amount is rejected on load"), NegativePayment.Initialize(BadPayment, Error));
    FObject BadDeadline = Original.Serialize();
    Child(SavedYard(BadDeadline, Player), TEXT("monsterUnlock"))->SetNumberField(TEXT("readyAt"), StartTime - 1);
    FBYMSimulation Deadline;
    TestFalse(TEXT("An unlock deadline preceding its start is rejected"), Deadline.Initialize(BadDeadline, Error));
    for (const TCHAR* Type : { TEXT("C15"), TEXT("C200") })
    {
        FObject UnsupportedJobSave = Original.Serialize();
        const FObject Job = Child(SavedYard(UnsupportedJobSave, Player), TEXT("monsterUnlock"));
        const bool bHealer = FString(Type) == TEXT("C15");
        Job->SetStringField(TEXT("type"), Type);
        Job->SetNumberField(TEXT("puttyPaid"), bHealer ? 6192000 : 0);
        Job->SetNumberField(TEXT("readyAt"), StartTime + (bHealer ? 216000.0 : 1.0));
        FBYMSimulation UnsupportedJob;
        TestFalse(FString(TEXT("An unavailable species cannot gain a persisted unlock job: ")) + Type,
            UnsupportedJob.Initialize(UnsupportedJobSave, Error));
    }
    TestEqual(TEXT("Invalid-save checks cannot mutate the live authoritative wallet"), Number(Child(Home(Original, Player), TEXT("resources")), TEXT("putty")), 2000.0);
    const FString Observer(TEXT("progression-observer"));
    if (!TestTrue(TEXT("Create a separate observer for snapshot privacy"), Original.AddPlayer(Observer, Observer, Error))) { AddError(Error); return false; }
    const FObject Opponent = Child(Decode(Original.Snapshot(Observer, Player)), TEXT("yard"));
    if (!TestTrue(TEXT("Observer can scout the other yard"), Opponent.IsValid())) return false;
    TestFalse(TEXT("Opponent snapshot does not reveal private monster levels"), Opponent->HasField(TEXT("monsterLevels")));
    TestFalse(TEXT("Opponent snapshot does not reveal the paid unlock job"), Opponent->HasField(TEXT("monsterUnlock")));
    TestFalse(TEXT("Opponent snapshot does not reveal Academy research jobs"), Opponent->HasField(TEXT("monsterResearch")));
    TestFalse(TEXT("Opponent snapshot does not reveal per-Hatchery queue state"), Opponent->HasField(TEXT("hatcheries")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMRivalMonsterProgressionTest, "BackyardMMO.Progression.RivalProgressionUsesPlayerRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMRivalMonsterProgressionTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings(1);
    Settings.Settings->StartingResources = 0;
    FBYMSimulation Simulation;
    FString Error;
    if (!TestTrue(TEXT("Initialize one resource-poor rival"), Simulation.Initialize(nullptr, Error))) { AddError(Error); return false; }
    const FObject Save = Simulation.Serialize();
    const FArray& Yards = Array(Save, TEXT("yards"));
    if (!TestEqual(TEXT("The test has one persistent rival"), Yards.Num(), 1)) return false;
    const FString Rival = String(Yards[0]->AsObject(), TEXT("id"));
    const FObject Before = Home(Simulation, Rival);
    TestTrue(TEXT("Rivals receive the same starter Pokey unlock"), Flag(Option(Simulation, Rival, TEXT("C1")), TEXT("unlocked")));
    TestFalse(TEXT("Rivals do not receive free advanced unlocks"), Flag(Option(Simulation, Rival, TEXT("C2")), TEXT("unlocked")));
    TestFalse(TEXT("Rival cannot bypass the shared unlock resource charge"), Accepted(Simulation.Execute(Rival, TEXT("unlock"), TEXT("{\"type\":\"C2\"}"))));
    TestFalse(TEXT("Rival cannot hatch a locked type"), Accepted(Simulation.Execute(Rival, TEXT("train"), TEXT("{\"type\":\"C2\",\"count\":1}"))));
    TestFalse(TEXT("Rival cannot research without an Academy"), Accepted(Simulation.Execute(Rival, TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
    SameWallet(*this, Before, Home(Simulation, Rival));
    const double StartTime = Number(Snapshot(Simulation, Rival), TEXT("now"));
    for (int32 Minute = 1; Minute <= 10; ++Minute) Simulation.Tick(StartTime + Minute * 60.0, 1.0f);
    TestFalse(TEXT("Autonomous decisions cannot grant an unaffordable unlock"), Flag(Option(Simulation, Rival, TEXT("C2")), TEXT("unlocked")));
    TestEqual(TEXT("Autonomous decisions cannot invent Academy research"), Number(Option(Simulation, Rival, TEXT("C1")), TEXT("level")), 1.0);
    const FObject FinalSave = Simulation.Serialize();
    FBYMSimulation Restored;
    if (!Load(*this, Restored, FinalSave)) return false;
    TestEqual(TEXT("Rival identity and yard count persist through reload"), Array(Restored.Serialize(), TEXT("yards")).Num(), 1);
    TestTrue(TEXT("The original rival yard survives reload"), Restored.HasYard(Rival));
    TestFalse(TEXT("Restart cannot turn a locked rival monster into an unlock"), Flag(Option(Restored, Rival, TEXT("C2")), TEXT("unlocked")));
    return true;
}

} // namespace BYMProgressionTests

#endif // WITH_DEV_AUTOMATION_TESTS
