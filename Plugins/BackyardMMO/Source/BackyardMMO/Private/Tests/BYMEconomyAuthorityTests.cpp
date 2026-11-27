#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMEconomyAuthorityTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("economy-owner"));
    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Bots = Settings->RivalBotCount, Cells = Settings->YardCells;
        const int32 Starting = Settings->StartingResources, Storage = Settings->ResourceStorageFloor;
        const float Production = Settings->ResourceProductionMultiplier, Construction = Settings->ConstructionTimeMultiplier;
        const float Training = Settings->TrainingTimeMultiplier;
        FSettingsGuard()
        {
            Settings->RivalBotCount = 0; Settings->YardCells = 40;
            Settings->StartingResources = 5000; Settings->ResourceStorageFloor = 10000;
            Settings->ResourceProductionMultiplier = 1; Settings->ConstructionTimeMultiplier = 1; Settings->TrainingTimeMultiplier = 1;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots; Settings->YardCells = Cells;
            Settings->StartingResources = Starting; Settings->ResourceStorageFloor = Storage;
            Settings->ResourceProductionMultiplier = Production; Settings->ConstructionTimeMultiplier = Construction; Settings->TrainingTimeMultiplier = Training;
        }
    };
    FObject Decode(const FString& Text)
    {
        FObject Result; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result); return Result;
    }
    FString Encode(const FObject& Object)
    {
        FString Result; if (Object) FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Result)); return Result;
    }
    FObject Clone(const FObject& Object) { return Decode(Encode(Object)); }
    FObject Child(const FObject& Object, const TCHAR* Key)
    {
        const FObject* Value = nullptr; return Object && Object->TryGetObjectField(Key, Value) && Value ? *Value : nullptr;
    }
    const FArray& Array(const FObject& Object, const TCHAR* Key)
    {
        static const FArray Empty; const FArray* Value = nullptr;
        return Object && Object->TryGetArrayField(Key, Value) && Value ? *Value : Empty;
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
    double Number(const FObject& Object, const TCHAR* Key, double Default = -1.)
    {
        double Value = Default; if (Object) Object->TryGetNumberField(Key, Value); return Value;
    }
    FString String(const FObject& Object, const TCHAR* Key)
    {
        FString Value; if (Object) Object->TryGetStringField(Key, Value); return Value;
    }
    bool Flag(const FObject& Object, const TCHAR* Key)
    {
        bool Value = false; return Object && Object->TryGetBoolField(Key, Value) && Value;
    }
    bool Accepted(const FString& Text) { return Flag(Decode(Text), TEXT("ok")); }
    FObject SavedYard(const FObject& Save)
    {
        for (const auto& Value : Array(Save, TEXT("yards")))
            if (Value && String(Value->AsObject(), TEXT("id")) == Owner) return Value->AsObject();
        return nullptr;
    }
    FObject Building(const FObject& Yard, int32 Type)
    {
        for (const auto& Value : Array(Yard, TEXT("buildings")))
            if (Value && Number(Value->AsObject(), TEXT("type")) == Type) return Value->AsObject();
        return nullptr;
    }
    // Collector assertions need the full own-yard snapshot; home is a compact
    // account summary and intentionally contains no building array.
    FObject Home(const FBYMSimulation& Simulation) { return Child(Decode(Simulation.Snapshot(Owner, FString())), TEXT("yard")); }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Simulation)
    {
        FString Error;
        if (!Simulation.Initialize(nullptr, Error) || !Simulation.AddPlayer(Owner, Owner, Error)) { Test.AddError(Error); return false; }
        return true;
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    {
        FString Error; if (!Simulation.Initialize(Save, Error)) { Test.AddError(Error); return false; } return true;
    }
    FString Selected(const FString& Id)
    {
        FObject Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("building"), Id); return Encode(Args);
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCollectorAuthorityTest, "BackyardMMO.Economy.AuthoritativeCollectorPersistence",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMCollectorAuthorityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; FBYMSimulation Simulation;
        if (!Start(*this, Simulation)) return false;
        const double Origin = Number(SavedYard(Simulation.Serialize()), TEXT("lastEconomy"));
        Simulation.Tick(Origin + 9., 0.f);
        TestEqual(TEXT("Actual source collector grants no fractional batch at nine seconds"), Number(Building(Home(Simulation), 1), TEXT("stored")), 0.);
        Simulation.Tick(Origin + 10., 0.f);
        TestEqual(TEXT("Actual source collector grants its two Twigs at ten seconds"), Number(Building(Home(Simulation), 1), TEXT("stored")), 2.);
        TestTrue(TEXT("Operational collector sends its working pose"), Flag(Building(Home(Simulation), 1), TEXT("working")));
        Simulation.Tick(Origin + 13., 0.f);
        const FObject Save = Simulation.Serialize();
        TestEqual(TEXT("Collector countdown is persisted alongside stock"), Number(Building(SavedYard(Save), 1), TEXT("productionRemaining")), 7.);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
        Reloaded.Tick(Origin + 19., 0.f);
        TestEqual(TEXT("Reload does not award an unfinished batch"), Number(Building(Home(Reloaded), 1), TEXT("stored")), 2.);
        Reloaded.Tick(Origin + 20., 0.f);
        TestEqual(TEXT("Reload completes the preserved source countdown exactly once"), Number(Building(Home(Reloaded), 1), TEXT("stored")), 4.);
        Reloaded.Tick(Origin + 18., 0.f);
        TestEqual(TEXT("Clock rollback cannot rewind collector phase"), Number(Building(Home(Reloaded), 1), TEXT("productionRemaining")), 10.);
        const FObject Legacy = Clone(Save);
        SetLegacySchema(Legacy, 2);
        for (const auto& Value : Array(SavedYard(Legacy), TEXT("buildings"))) Value->AsObject()->RemoveField(TEXT("productionRemaining"));
        FBYMSimulation Migrated; if (!Load(*this, Migrated, Legacy)) return false;
        TestEqual(TEXT("Old-schema migration preserves collector stock"), Number(Building(Home(Migrated), 1), TEXT("stored")), 2.);
        Migrated.Tick(Origin + 22., 0.f);
        TestEqual(TEXT("Missing old timer begins a fresh full cycle"), Number(Building(Home(Migrated), 1), TEXT("stored")), 2.);
        Migrated.Tick(Origin + 23., 0.f);
        TestEqual(TEXT("Migrated collector completes after that fresh cycle"), Number(Building(Home(Migrated), 1), TEXT("stored")), 4.);
        for (const double Invalid : { -1., 31536001. })
        {
            const FObject Broken = Clone(Save); Building(SavedYard(Broken), 1)->SetNumberField(TEXT("productionRemaining"), Invalid);
            FBYMSimulation Rejected; FString Error;
            TestFalse(TEXT("Invalid persisted collector countdown rejects the save"), Rejected.Initialize(Broken, Error));
            TestTrue(TEXT("Invalid countdown reports the collector field"), Error.Contains(TEXT("collector production countdown")));
        }
        const FObject WrongType = Clone(Save); Building(SavedYard(WrongType), 1)->SetStringField(TEXT("productionRemaining"), TEXT("bad"));
        FBYMSimulation Rejected; FString Error;
        TestFalse(TEXT("Non-numeric production phase is not silently defaulted"), Rejected.Initialize(WrongType, Error));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCollectorBankAuthorityTest, "BackyardMMO.Economy.AuthoritativeBankingAndStorage",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMCollectorBankAuthorityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        const FObject Save = Seed.Serialize(), Yard = SavedYard(Save);
        Yard->SetNumberField(TEXT("lastEconomy"), Number(Yard, TEXT("lastEconomy")) + 60.);
        const FObject Twigs = Building(Yard, 1), Pebbles = Building(Yard, 2), Silo = Building(Yard, 6);
        const FString TwigsId = String(Twigs, TEXT("id"));
        Twigs->SetNumberField(TEXT("stored"), 100); Twigs->SetNumberField(TEXT("productionRemaining"), 3);
        Pebbles->SetNumberField(TEXT("stored"), 80);
        Silo->SetNumberField(TEXT("hp"), 0);
        Child(Yard, TEXT("resources"))->SetNumberField(TEXT("twigs"), 17490);
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        TestEqual(TEXT("Destroyed completed silo retains original storage contribution"), Number(Home(Simulation), TEXT("storageCapacity")), 17500.);
        TestTrue(TEXT("Selected original collector accepts the remaining wallet room"), Accepted(Simulation.Execute(Owner, TEXT("collect"), Selected(TwigsId))));
        TestEqual(TEXT("Banking fills only available wallet room"), Number(Child(Home(Simulation), TEXT("resources")), TEXT("twigs")), 17500.);
        TestEqual(TEXT("Unaccepted collector stock is retained"), Number(Building(Home(Simulation), 1), TEXT("stored")), 90.);
        TestEqual(TEXT("Selected collection does not bank another collector"), Number(Building(Home(Simulation), 2), TEXT("stored")), 80.);
        TestEqual(TEXT("Partial collection preserves cycle phase"), Number(Building(Home(Simulation), 1), TEXT("productionRemaining")), 3.);
        TestFalse(TEXT("Foreign or unknown collector identity is rejected"), Accepted(Simulation.Execute(Owner, TEXT("collect"), Selected(TEXT("foreign-collector")))));
        const FObject Damaged = Clone(Simulation.Serialize()); Building(SavedYard(Damaged), 1)->SetNumberField(TEXT("hp"), 400);
        Child(SavedYard(Damaged), TEXT("resources"))->SetNumberField(TEXT("twigs"), 0);
        FBYMSimulation Ineligible; if (!Load(*this, Ineligible, Damaged)) return false;
        TestFalse(TEXT("Source UI full-health condition is enforced by the server"), Accepted(Ineligible.Execute(Owner, TEXT("collect"), Selected(TwigsId))));
        TestFalse(TEXT("Damaged collector snapshot explains collection is unavailable"), Flag(Building(Home(Ineligible), 1), TEXT("canCollect")));
        TestTrue(TEXT("Collect All still runs for other eligible collectors"), Accepted(Ineligible.Execute(Owner, TEXT("collect"), TEXT("{}"))));
        TestEqual(TEXT("Collect All skips damaged collector stock"), Number(Building(Home(Ineligible), 1), TEXT("stored")), 90.);
        const FObject Upgrading = Clone(Save); const FObject UpgradeSilo = Building(SavedYard(Upgrading), 6);
        UpgradeSilo->SetNumberField(TEXT("hp"), 750); UpgradeSilo->SetNumberField(TEXT("targetLevel"), 2);
        SavedYard(Upgrading)->RemoveField(TEXT("workerState")); // Synthetic pre-worker paid job.
        UpgradeSilo->SetStringField(TEXT("jobId"), TEXT("silo-upgrade-fixture")); UpgradeSilo->SetNumberField(TEXT("workRemaining"), 100);
        UpgradeSilo->SetNumberField(TEXT("readyAt"), Number(Yard, TEXT("lastEconomy")) + 100.);
        FBYMSimulation Upgrade; if (!Load(*this, Upgrade, Upgrading)) return false;
        TestEqual(TEXT("Upgrading silo retains its completed-level contribution"), Number(Home(Upgrade), TEXT("storageCapacity")), 17500.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMRepairAuthorityTest, "BackyardMMO.Economy.AuthoritativeRepairBoundaries",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMRepairAuthorityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        const FObject Initial = Seed.Serialize(); const FObject Yard = SavedYard(Initial);
        const FObject Twigs = Building(Yard, 1);
        const FString TwigsId = String(Twigs, TEXT("id"));
        Twigs->SetNumberField(TEXT("hp"), 200); Twigs->SetNumberField(TEXT("productionRemaining"), 4);
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Initial)) return false;
        const double Wallet = Number(Child(Home(Simulation), TEXT("resources")), TEXT("twigs"));
        TestTrue(TEXT("Damaged collector can start source free repair"), Accepted(Simulation.Execute(Owner, TEXT("repair"), Selected(TwigsId))));
        const double RepairStarted = Number(Decode(Simulation.Snapshot(Owner, FString())), TEXT("now"));
        TestEqual(TEXT("Repair does not charge resources"), Number(Child(Home(Simulation), TEXT("resources")), TEXT("twigs")), Wallet);
        Simulation.Tick(RepairStarted + 1., 0.f);
        TestEqual(TEXT("Repair is gradual at ceil(500/30)=17 HP per second"), Number(Building(Home(Simulation), 1), TEXT("hp")), 217.);
        TestEqual(TEXT("Below-half repair time does not count as collector work"), Number(Building(Home(Simulation), 1), TEXT("productionRemaining")), 4.);
        Simulation.Tick(RepairStarted + 7., 0.f);
        TestEqual(TEXT("Collector resumes its carried countdown after half-health crossing"), Number(Building(Home(Simulation), 1), TEXT("stored")), 2.);
        TestTrue(TEXT("Still-repairing but eligible collector uses its working animation"), Flag(Building(Home(Simulation), 1), TEXT("working")));
        Simulation.Tick(RepairStarted + 18., 0.f);
        TestEqual(TEXT("Source rounded repair rate completes at full health"), Number(Building(Home(Simulation), 1), TEXT("hp")), 500.);
        TestFalse(TEXT("Finished repair clears the repair state"), Flag(Building(Home(Simulation), 1), TEXT("repairing")));

        FBYMSimulation HatcherySeed; if (!Start(*this, HatcherySeed)) return false;
        TestTrue(TEXT("Queue one source Pokey for repair eligibility test"), Accepted(HatcherySeed.Execute(Owner, TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":1}"))));
        const FObject HatcherySave = HatcherySeed.Serialize(), HatcheryYard = SavedYard(HatcherySave);
        const double HatcheryTime = Number(HatcheryYard, TEXT("lastEconomy"));
        const FObject Hatchery = Building(HatcheryYard, 13);
        Hatchery->SetNumberField(TEXT("hp"), 1000); Hatchery->SetBoolField(TEXT("repairing"), true);
        Hatchery->SetNumberField(TEXT("readyAt"), HatcheryTime + 100.);
        FBYMSimulation Hatching; if (!Load(*this, Hatching, HatcherySave)) return false;
        Hatching.Tick(HatcheryTime + 20., 0.f);
        TestEqual(TEXT("Repair crossing does not retroactively hatch through damaged time"), Number(Child(Home(Hatching), TEXT("army")), TEXT("C1"), 0.), 0.);
        TestTrue(TEXT("Already paid active hatching resumes above half health during repair"), Flag(Building(Home(Hatching), 13), TEXT("working")));
        Hatching.Tick(HatcheryTime + 30., 0.f);
        TestEqual(TEXT("Resumed hatching completes after its full eligible time"), Number(Child(Home(Hatching), TEXT("army")), TEXT("C1"), 0.), 1.);
        return true;
    }
}

#endif
