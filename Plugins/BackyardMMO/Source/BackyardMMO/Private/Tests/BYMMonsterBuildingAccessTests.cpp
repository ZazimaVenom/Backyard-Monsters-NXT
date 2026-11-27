#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Source entrypoints are broader than the building context menu:
// CREATURELOCKER.Show/Start, ACADEMY.Show/StartMonsterUpgrade, and the registered
// Underused01MonsterLocker/Underused02Academy front-page shortcuts do not impose
// a health, repair or building-upgrade gate on an already completed building.
// BUILDING8.Upgrade and BUILDING26.Upgrade do block the opposite action order.
// These tests use only in-memory authoritative simulation saves and commands.
namespace BYMMonsterBuildingAccessTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("monster-building-access-owner"));

    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Bots = Settings->RivalBotCount;
        const int32 Cells = Settings->YardCells;
        const int32 Starting = Settings->StartingResources;
        const int32 Storage = Settings->ResourceStorageFloor;
        const float Construction = Settings->ConstructionTimeMultiplier;
        const float Progression = Settings->MonsterProgressionTimeMultiplier;
        FSettingsGuard()
        {
            Settings->RivalBotCount = 0; Settings->YardCells = 100;
            Settings->StartingResources = 1000000; Settings->ResourceStorageFloor = 2000000;
            Settings->ConstructionTimeMultiplier = 1; Settings->MonsterProgressionTimeMultiplier = 1;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots; Settings->YardCells = Cells;
            Settings->StartingResources = Starting; Settings->ResourceStorageFloor = Storage;
            Settings->ConstructionTimeMultiplier = Construction; Settings->MonsterProgressionTimeMultiplier = Progression;
        }
    };

    FObject Decode(const FString& Text)
    { FObject Object; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object); return Object; }
    FString Encode(const FObject& Object)
    { FString Text; FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FObject Child(const FObject& Object, const TCHAR* Field)
    { const FObject* Value = nullptr; return Object && Object->TryGetObjectField(Field, Value) && Value ? *Value : nullptr; }
    const FArray& Array(const FObject& Object, const TCHAR* Field)
    { static const FArray Empty; const FArray* Value = nullptr; return Object && Object->TryGetArrayField(Field, Value) && Value ? *Value : Empty; }
    FString String(const FObject& Object, const TCHAR* Field)
    { FString Value; if (Object) Object->TryGetStringField(Field, Value); return Value; }
    double Number(const FObject& Object, const TCHAR* Field)
    { double Value = -1; if (Object) Object->TryGetNumberField(Field, Value); return Value; }
    bool Flag(const FObject& Object, const TCHAR* Field)
    { bool Value = false; return Object && Object->TryGetBoolField(Field, Value) && Value; }
    bool Accepted(const FString& Text) { return Flag(Decode(Text), TEXT("ok")); }
    FObject Snapshot(const FBYMSimulation& Simulation) { return Decode(Simulation.Snapshot(Owner, FString())); }
    FObject Yard(const FBYMSimulation& Simulation) { return Child(Snapshot(Simulation), TEXT("yard")); }
    FObject Option(const FBYMSimulation& Simulation, const TCHAR* Type)
    { return Child(Child(Snapshot(Simulation), TEXT("monsterOptions")), Type); }
    FObject SavedYard(const FObject& Save)
    {
        for (const auto& Value : Array(Save, TEXT("yards")))
            if (Value && String(Value->AsObject(), TEXT("id")) == Owner) return Value->AsObject();
        return nullptr;
    }
    FObject Building(const FObject& YardObject, int32 Type)
    {
        for (const auto& Value : Array(YardObject, TEXT("buildings")))
            if (Value && Number(Value->AsObject(), TEXT("type")) == Type) return Value->AsObject();
        return nullptr;
    }
    FString Selected(const FObject& BuildingObject)
    {
        const FObject Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("building"), String(BuildingObject, TEXT("id")));
        return Encode(Request);
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    {
        FString Error;
        if (Simulation.Initialize(Save, Error)) return true;
        Test.AddError(TEXT("Monster building access fixture: ") + Error); return false;
    }
    void SameWallet(FAutomationTestBase& Test, const FObject& Before, const FObject& After)
    {
        for (const TCHAR* Resource : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")})
            Test.TestEqual(FString(TEXT("Rejected action preserves ")) + Resource,
                Number(Child(After, TEXT("resources")), Resource), Number(Child(Before, TEXT("resources")), Resource));
    }
    FObject Fixture(FAutomationTestBase& Test)
    {
        FBYMSimulation Seed; if (!Load(Test, Seed, nullptr)) return nullptr;
        FString Error;
        if (!Seed.AddPlayer(Owner, Owner, Error)) { Test.AddError(Error); return nullptr; }
        const FObject Save = Seed.Serialize(); const FObject Home = SavedYard(Save);
        const FObject Town = Building(Home, 14), Locker = Building(Home, 8);
        if (!Town || !Locker) { Test.AddError(TEXT("Starter Town Hall or Locker is missing.")); return nullptr; }
        // Completed source levels meet both subsequent upgrade prerequisites:
        // Academy 2 requires Town Hall 4/Locker 3; Locker 4 requires Town Hall 5.
        Town->SetNumberField(TEXT("level"), 5); Town->SetNumberField(TEXT("targetLevel"), 5); Town->SetNumberField(TEXT("hp"), 94000);
        Locker->SetNumberField(TEXT("level"), 3); Locker->SetNumberField(TEXT("targetLevel"), 3); Locker->SetNumberField(TEXT("hp"), 32000);
        const FObject Academy = MakeShared<FJsonObject>();
        Academy->SetStringField(TEXT("id"), TEXT("source-access-academy")); Academy->SetNumberField(TEXT("type"), 26);
        Academy->SetNumberField(TEXT("level"), 1); Academy->SetNumberField(TEXT("targetLevel"), 1);
        // Keep this synthetic Academy inside the mushroom-free interior of
        // the 100-cell fixture. (1,1) lies in the real random mushroom collar
        // and made this unrelated entrypoint test depend on GUID/test order.
        Academy->SetNumberField(TEXT("x"), 10); Academy->SetNumberField(TEXT("y"), 10);
        Academy->SetNumberField(TEXT("hp"), 6000); Academy->SetNumberField(TEXT("stored"), 0);
        Academy->SetNumberField(TEXT("readyAt"), 0); Academy->SetNumberField(TEXT("workRemaining"), 0);
        Academy->SetStringField(TEXT("jobId"), FString()); Academy->SetBoolField(TEXT("repairing"), false);
        FArray Buildings = Array(Home, TEXT("buildings")); Buildings.Add(MakeShared<FJsonValueObject>(Academy));
        Home->SetArrayField(TEXT("buildings"), Buildings);
        return Save;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMonsterDamagedBuildingAccessTest, "BackyardMMO.Progression.SourceDamagedBuildingEntryPoints",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMonsterDamagedBuildingAccessTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings;
        for (int32 State = 0; State < 3; ++State)
        {
            const FObject Save = Fixture(*this); if (!Save) return false;
            const FObject Home = SavedYard(Save);
            Building(Home, 8)->SetNumberField(TEXT("hp"), State == 0 ? 0 : 16000);
            Building(Home, 26)->SetNumberField(TEXT("hp"), State == 0 ? 0 : 3000);
            Building(Home, 8)->SetBoolField(TEXT("repairing"), State == 2);
            Building(Home, 26)->SetBoolField(TEXT("repairing"), State == 2);
            FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
            TestTrue(TEXT("The original alternate Locker entrypoint admits an eligible monster despite building damage or repair"), Flag(Option(Simulation, TEXT("C2")), TEXT("canUnlock")));
            TestTrue(TEXT("The original alternate Academy entrypoint admits research despite building damage or repair"), Flag(Option(Simulation, TEXT("C1")), TEXT("canResearch")));
            const double BeforePutty = Number(Child(Yard(Simulation), TEXT("resources")), TEXT("putty"));
            if (!TestTrue(TEXT("The host admits a source-valid unlock from the completed Locker"), Accepted(Simulation.Execute(Owner, TEXT("unlock"), TEXT("{\"type\":\"C2\"}"))))) return false;
            if (!TestTrue(TEXT("The host admits source-valid research from the completed Academy"), Accepted(Simulation.Execute(Owner, TEXT("research"), TEXT("{\"type\":\"C1\"}"))))) return false;
            TestEqual(TEXT("Damage does not change the source unlock and research Putty prices"), Number(Child(Yard(Simulation), TEXT("resources")), TEXT("putty")), BeforePutty - 12000.);
            const FObject Paid = Yard(Simulation);
            TestFalse(TEXT("Alternate Locker access cannot bypass its existing unlock slot"), Accepted(Simulation.Execute(Owner, TEXT("unlock"), TEXT("{\"type\":\"C3\"}"))));
            TestFalse(TEXT("Alternate Academy access cannot duplicate an existing research job"), Accepted(Simulation.Execute(Owner, TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
            SameWallet(*this, Paid, Yard(Simulation));
            const FObject PaidSave = Simulation.Serialize(); FBYMSimulation Restored;
            if (!Load(*this, Restored, PaidSave)) return false;
            TestEqual(TEXT("Reload retains the paid unlock in a damaged or repairing building"), String(Child(Yard(Restored), TEXT("monsterUnlock")), TEXT("type")), FString(TEXT("C2")));
            TestEqual(TEXT("Reload retains the paid research in a damaged or repairing building"), Array(Yard(Restored), TEXT("monsterResearch")).Num(), 1);
            SameWallet(*this, Paid, Yard(Restored));
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMonsterUpgradeThenResearchTest, "BackyardMMO.Progression.SourceBuildingUpgradeThenMonsterJob",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMonsterUpgradeThenResearchTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings;
        for (const int32 Type : {8, 26})
        {
            const FObject Save = Fixture(*this); if (!Save) return false;
            FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
            const FObject BeforeBuilding = Building(Yard(Simulation), Type);
            const double OldLevel = Number(BeforeBuilding, TEXT("level"));
            if (!TestTrue(TEXT("An idle completed building can begin its source upgrade"), Accepted(Simulation.Execute(Owner, TEXT("upgrade"), Selected(BeforeBuilding))))) return false;
            const FObject Work = Building(Yard(Simulation), Type);
            const FString JobId = String(Work, TEXT("jobId"));
            const double WorkDeadline = Number(Work, TEXT("workReadyAt"));
            TestFalse(TEXT("The building upgrade has an authoritative identity"), JobId.IsEmpty());
            const bool bLocker = Type == 8;
            const TCHAR* Monster = bLocker ? TEXT("C2") : TEXT("C1");
            const TCHAR* Operation = bLocker ? TEXT("unlock") : TEXT("research");
            const TCHAR* Payload = bLocker ? TEXT("{\"type\":\"C2\"}") : TEXT("{\"type\":\"C1\"}");
            TestTrue(TEXT("Original alternate entrypoints still expose a monster job during a building upgrade"), Flag(Option(Simulation, Monster), bLocker ? TEXT("canUnlock") : TEXT("canResearch")));
            if (!TestTrue(TEXT("Starting the building upgrade first permits the source monster job"), Accepted(Simulation.Execute(Owner, Operation, Payload)))) return false;
            const FObject Paid = Yard(Simulation);
            const double MonsterDeadline = Number(Option(Simulation, Monster), bLocker ? TEXT("unlockReadyAt") : TEXT("researchReadyAt"));
            TestTrue(TEXT("The chosen original monster job finishes before this building upgrade"), MonsterDeadline < WorkDeadline);
            const FObject ConcurrentSave = Simulation.Serialize(); FBYMSimulation Restored;
            if (!Load(*this, Restored, ConcurrentSave)) return false;
            const FObject RestoredWork = Building(Yard(Restored), Type);
            TestEqual(TEXT("Reload preserves the independent building job identity"), String(RestoredWork, TEXT("jobId")), JobId);
            TestEqual(TEXT("Reload preserves remaining paid building work without restarting it"), Number(RestoredWork, TEXT("workRemaining")), Number(Building(Paid, Type), TEXT("workRemaining")));
            TestEqual(TEXT("Reload preserves the independent monster deadline"), Number(Option(Restored, Monster), bLocker ? TEXT("unlockReadyAt") : TEXT("researchReadyAt")), MonsterDeadline);
            SameWallet(*this, Paid, Yard(Restored));
            Restored.Tick(MonsterDeadline, 0.f);
            if (bLocker) TestTrue(TEXT("Unlock finishes while its Locker upgrade remains active"), Flag(Option(Restored, Monster), TEXT("unlocked")));
            else TestEqual(TEXT("Research finishes while its Academy upgrade remains active"), Number(Option(Restored, Monster), TEXT("level")), 2.);
            TestEqual(TEXT("Monster completion does not prematurely upgrade its building"), Number(Building(Yard(Restored), Type), TEXT("level")), OldLevel);
            TestTrue(TEXT("Building work retains its remaining countdown after monster completion"), Number(Building(Yard(Restored), Type), TEXT("workRemaining")) > 0.);
            Restored.Tick(WorkDeadline, 0.f);
            TestEqual(TEXT("The building then completes once at its own work boundary"), Number(Building(Yard(Restored), Type), TEXT("level")), OldLevel + 1.);
            TestTrue(TEXT("Building completion clears only its finished work identity"), String(Building(Yard(Restored), Type), TEXT("jobId")).IsEmpty());
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMonsterResearchBlocksLaterUpgradeTest, "BackyardMMO.Progression.SourceMonsterJobBlocksLaterBuildingUpgrade",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMonsterResearchBlocksLaterUpgradeTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings;
        for (const int32 Type : {8, 26})
        {
            const FObject Save = Fixture(*this); if (!Save) return false;
            FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
            const bool bLocker = Type == 8;
            const TCHAR* Payload = bLocker ? TEXT("{\"type\":\"C2\"}") : TEXT("{\"type\":\"C1\"}");
            if (!TestTrue(TEXT("The idle building starts its monster job"), Accepted(Simulation.Execute(Owner, bLocker ? TEXT("unlock") : TEXT("research"), Payload)))) return false;
            const FObject Paid = Yard(Simulation);
            const FString BuildingRequest = Selected(Building(Paid, Type));
            TestFalse(TEXT("Original BUILDING8/BUILDING26 block the opposite action order"), Accepted(Simulation.Execute(Owner, TEXT("upgrade"), BuildingRequest)));
            TestFalse(TEXT("The snapshot exposes that later building-upgrade restriction"), Flag(Building(Yard(Simulation), Type), TEXT("canUpgrade")));
            TestTrue(TEXT("A rejected building upgrade creates no work identity"), String(Building(Yard(Simulation), Type), TEXT("jobId")).IsEmpty());
            SameWallet(*this, Paid, Yard(Simulation));
            if (!TestTrue(TEXT("Cancel the paid monster job through its real command"), Accepted(Simulation.Execute(Owner, bLocker ? TEXT("cancelUnlock") : TEXT("cancelResearch"), Payload)))) return false;
            TestTrue(TEXT("Releasing the monster job permits the previously blocked building upgrade"), Accepted(Simulation.Execute(Owner, TEXT("upgrade"), BuildingRequest)));
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMonsterUnfinishedBuildingAccessTest, "BackyardMMO.Progression.SourceUnfinishedBuildingCannotAdmitMonsterJob",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMonsterUnfinishedBuildingAccessTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; const FObject Save = Fixture(*this); if (!Save) return false;
        const FObject Home = SavedYard(Save);
        Home->RemoveField(TEXT("workerState")); // Synthetic pre-worker unfinished buildings.
        for (const int32 Type : {8, 26})
        {
            const FObject Work = Building(Home, Type);
            Work->SetNumberField(TEXT("level"), 0); Work->SetNumberField(TEXT("targetLevel"), 1);
            Work->SetNumberField(TEXT("hp"), Type == 8 ? 4000 : 6000);
            Work->SetStringField(TEXT("jobId"), FString::Printf(TEXT("unfinished-access-%d"), Type));
            Work->SetNumberField(TEXT("workRemaining"), Type == 8 ? 600 : 10800);
        }
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        const FObject Before = Yard(Simulation);
        TestFalse(TEXT("An unfinished Locker has no attained level for an unlock"), Flag(Option(Simulation, TEXT("C2")), TEXT("canUnlock")));
        TestFalse(TEXT("An unfinished Academy has no attained level for research"), Flag(Option(Simulation, TEXT("C1")), TEXT("canResearch")));
        TestFalse(TEXT("Target level is not an attained Locker level"), Accepted(Simulation.Execute(Owner, TEXT("unlock"), TEXT("{\"type\":\"C2\"}"))));
        TestFalse(TEXT("Target level is not an attained Academy level"), Accepted(Simulation.Execute(Owner, TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
        SameWallet(*this, Before, Yard(Simulation));
        TestEqual(TEXT("The unfinished Locker remains level zero"), Number(Building(Yard(Simulation), 8), TEXT("level")), 0.);
        TestEqual(TEXT("The unfinished Academy remains level zero"), Number(Building(Yard(Simulation), 26), TEXT("level")), 0.);
        return true;
    }
}

#endif // WITH_DEV_AUTOMATION_TESTS
