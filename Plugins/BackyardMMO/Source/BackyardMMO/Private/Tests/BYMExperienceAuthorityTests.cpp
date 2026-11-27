#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMExperience.h"
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMExperienceAuthorityTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("experience-owner")), Rival(TEXT("experience-rival"));
    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Bots = Settings->RivalBotCount, Cells = Settings->YardCells;
        const int32 Starting = Settings->StartingResources, Storage = Settings->ResourceStorageFloor;
        const float Production = Settings->ResourceProductionMultiplier, Construction = Settings->ConstructionTimeMultiplier;
        const float Training = Settings->TrainingTimeMultiplier, Loot = Settings->InvasionLootFraction;
        const int32 Win = Settings->RPWin, Loss = Settings->RPLoss;
        FSettingsGuard()
        {
            Settings->RivalBotCount = 0; Settings->YardCells = 40;
            Settings->StartingResources = 1000000; Settings->ResourceStorageFloor = 1000000;
            Settings->ResourceProductionMultiplier = 0; Settings->ConstructionTimeMultiplier = .01f;
            Settings->TrainingTimeMultiplier = .01f; Settings->InvasionLootFraction = .1f;
            Settings->RPWin = 25; Settings->RPLoss = 15;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots; Settings->YardCells = Cells;
            Settings->StartingResources = Starting; Settings->ResourceStorageFloor = Storage;
            Settings->ResourceProductionMultiplier = Production; Settings->ConstructionTimeMultiplier = Construction;
            Settings->TrainingTimeMultiplier = Training; Settings->InvasionLootFraction = Loot;
            Settings->RPWin = Win; Settings->RPLoss = Loss;
        }
    };
    FObject Decode(const FString& Text)
    { FObject Object; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object); return Object; }
    FString Encode(const FObject& Object)
    { FString Text; FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FObject Clone(const FObject& Object) { return Decode(Encode(Object)); }
    FObject Child(const FObject& Object, const TCHAR* Field)
    { const FObject* Value = nullptr; return Object && Object->TryGetObjectField(Field, Value) && Value ? *Value : nullptr; }
    const FArray& Array(const FObject& Object, const TCHAR* Field)
    { static const FArray Empty; const FArray* Value = nullptr; return Object && Object->TryGetArrayField(Field, Value) && Value ? *Value : Empty; }
    FString String(const FObject& Object, const TCHAR* Field)
    { FString Value; if (Object) Object->TryGetStringField(Field, Value); return Value; }
    double Number(const FObject& Object, const TCHAR* Field)
    { double Value = -1; if (Object) Object->TryGetNumberField(Field, Value); return Value; }
    bool Accepted(const FString& Text)
    { bool Value = false; const FObject Result = Decode(Text); return Result && Result->TryGetBoolField(TEXT("ok"), Value) && Value; }
    FObject SavedYard(const FObject& Save, const FString& Id = Owner)
    {
        for (const auto& Value : Array(Save, TEXT("yards")))
            if (Value && String(Value->AsObject(), TEXT("id")) == Id) return Value->AsObject();
        return nullptr;
    }
    FObject Building(const FObject& Yard, int32 Type)
    {
        for (const auto& Value : Array(Yard, TEXT("buildings")))
            if (Value && Number(Value->AsObject(), TEXT("type")) == Type) return Value->AsObject();
        return nullptr;
    }
    FObject Snapshot(const FBYMSimulation& Simulation) { return Decode(Simulation.Snapshot(Owner, FString())); }
    FObject Home(const FBYMSimulation& Simulation) { return Child(Snapshot(Simulation), TEXT("home")); }
    FObject Yard(const FBYMSimulation& Simulation) { return Child(Snapshot(Simulation), TEXT("yard")); }
    double Earned(const FBYMSimulation& Simulation) { return Number(Child(Home(Simulation), TEXT("experience")), TEXT("earned")); }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    { FString Error; if (Simulation.Initialize(Save, Error)) return true; Test.AddError(Error); return false; }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Simulation)
    {
        if (!Load(Test, Simulation, nullptr)) return false;
        FString Error; if (Simulation.AddPlayer(Owner, Owner, Error)) return true; Test.AddError(Error); return false;
    }
    FString Selected(const FObject& Object, bool bJob = false)
    {
        FObject Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("building"), String(Object, TEXT("id")));
        if (bJob) Args->SetStringField(TEXT("job"), String(Object, TEXT("jobId")));
        return Encode(Args);
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceMigrationTest, "BackyardMMO.Experience.AuthorityMigrationAndHighWater",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMExperienceMigrationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        TestEqual(TEXT("A seeded yard invents no historical action XP"), Earned(Seed), 0.);
        const double InitialValue = Number(Child(Home(Seed), TEXT("experience")), TEXT("baseValue"));
        TestTrue(TEXT("Known completed starter structures contribute source base value"), InitialValue > 0.);
        FObject Save = Seed.Serialize(); Save->SetNumberField(TEXT("schema"), 3);
        const FObject OldYard = SavedYard(Save); OldYard->RemoveField(TEXT("earnedExperience")); OldYard->RemoveField(TEXT("baseValue"));
        for (const auto& Value : Array(OldYard, TEXT("buildings")))
        { Value->AsObject()->RemoveField(TEXT("jobId")); Value->AsObject()->RemoveField(TEXT("workRemaining")); }
        FBYMSimulation Migrated; if (!Load(*this, Migrated, Save)) return false;
        TestEqual(TEXT("Legacy migration does not fabricate past awards"), Earned(Migrated), 0.);
        TestEqual(TEXT("Legacy migration derives the known current completed base value"), Number(Child(Home(Migrated), TEXT("experience")), TEXT("baseValue")), InitialValue);
        Save = Migrated.Serialize(); SavedYard(Save)->SetNumberField(TEXT("earnedExperience"), 900);
        SavedYard(Save)->SetNumberField(TEXT("baseValue"), 1000001);
        for (const auto& Value : Array(SavedYard(Save), TEXT("buildings"))) Value->AsObject()->SetNumberField(TEXT("hp"), 0);
        FBYMSimulation Damaged; if (!Load(*this, Damaged, Save)) return false;
        TestEqual(TEXT("Destroyed buildings do not lower retained base value"), Number(Child(Home(Damaged), TEXT("experience")), TEXT("baseValue")), 1000001.);
        TestEqual(TEXT("Total player points combine earned XP and the high-water base value"), Number(Child(Home(Damaged), TEXT("experience")), TEXT("points")), 1000901.);
        TestEqual(TEXT("Experience is separate from ranking points"), Number(Home(Damaged), TEXT("rp")), 0.);
        for (const TCHAR* Field : {TEXT("earnedExperience"), TEXT("baseValue")})
        {
            for (const double Invalid : {-1., .5, 9007199254740992.})
            {
                const FObject Broken = Clone(Save); SavedYard(Broken)->SetNumberField(Field, Invalid);
                FBYMSimulation InvalidCounter; FString CounterError;
                TestFalse(TEXT("Invalid saved experience cannot grant or corrupt progress"), InvalidCounter.Initialize(Broken, CounterError));
            }
            const FObject WrongType = Clone(Save); SavedYard(WrongType)->SetStringField(Field, TEXT("invalid"));
            FBYMSimulation WrongCounter; FString CounterError;
            TestFalse(TEXT("Wrongly typed XP state is not silently defaulted"), WrongCounter.Initialize(WrongType, CounterError));
        }
        const FObject Missing = Clone(Save); SavedYard(Missing)->RemoveField(TEXT("baseValue"));
        FBYMSimulation Rejected; FString Error;
        TestFalse(TEXT("Current-schema missing XP state is not silently reset"), Rejected.Initialize(Missing, Error));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceCompletionTest, "BackyardMMO.Experience.CompletionAndCancellation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMExperienceCompletionTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; FBYMSimulation Simulation; if (!Start(*this, Simulation)) return false;
        const FString Town = Selected(Building(Yard(Simulation), 14));
        if (!TestTrue(TEXT("Begin source Town Hall upgrade"), Accepted(Simulation.Execute(Owner, TEXT("upgrade"), Town)))) return false;
        const FString Cancel = Selected(Building(Yard(Simulation), 14), true);
        TestTrue(TEXT("Cancelling unfinished work succeeds"), Accepted(Simulation.Execute(Owner, TEXT("cancelUpgrade"), Cancel)));
        TestEqual(TEXT("Starting and cancelling work award no completion XP"), Earned(Simulation), 0.);
        TestFalse(TEXT("A repeated cancellation cannot grant another refund or XP"), Accepted(Simulation.Execute(Owner, TEXT("cancelUpgrade"), Cancel)));
        if (!TestTrue(TEXT("Restart the Town Hall upgrade"), Accepted(Simulation.Execute(Owner, TEXT("upgrade"), Town)))) return false;
        double Now = Number(Building(Yard(Simulation), 14), TEXT("workReadyAt")) + .01;
        Simulation.Tick(Now, 0.f);
        TestEqual(TEXT("Town Hall reaches its next completed level"), Number(Building(Yard(Simulation), 14), TEXT("level")), 2.);
        TestEqual(TEXT("Upgrade awards floor(previous level 10 source seconds /3), independent of speed settings"), Earned(Simulation), 3.);
        FObject Build = MakeShared<FJsonObject>(); Build->SetNumberField(TEXT("type"), 1); Build->SetNumberField(TEXT("x"), 1); Build->SetNumberField(TEXT("y"), 30);
        if (!TestTrue(TEXT("The new attained Town Hall level allows a second Twig Snapper"), Accepted(Simulation.Execute(Owner, TEXT("build"), Encode(Build))))) return false;
        TestEqual(TEXT("An unfinished purchase adds no XP"), Earned(Simulation), 3.);
        const FObject PendingYard = Yard(Simulation);
        for (const auto& Value : Array(PendingYard, TEXT("buildings")))
            if (Number(Value->AsObject(), TEXT("type")) == 1 && Number(Value->AsObject(), TEXT("level")) == 0)
                Now = Number(Value->AsObject(), TEXT("workReadyAt")) + .01;
        Simulation.Tick(Now, 0.f);
        TestEqual(TEXT("Twig completion adds floor(15/2 +750/10)=82 source XP"), Earned(Simulation), 85.);
        const FObject Save = Simulation.Serialize(); FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
        Reloaded.Tick(Now + 20., 0.f);
        TestEqual(TEXT("Reloading completed jobs cannot replay their XP"), Earned(Reloaded), 85.);
        FObject Train = MakeShared<FJsonObject>(); Train->SetStringField(TEXT("type"), TEXT("C1")); Train->SetNumberField(TEXT("count"), 1);
        TestTrue(TEXT("Train through the real host command"), Accepted(Reloaded.Execute(Owner, TEXT("train"), Encode(Train))));
        Reloaded.Tick(Now + 22., 0.f);
        TestEqual(TEXT("Purchasing and hatching creatures do not invent action XP"), Earned(Reloaded), 85.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceCollectionTest, "BackyardMMO.Experience.AcceptedCollectorTransfers",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMExperienceCollectionTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        const double Capacity = Number(Home(Seed), TEXT("storageCapacity"));
        FObject Save = Seed.Serialize(); const FObject SavedHome = SavedYard(Save);
        Building(SavedHome, 1)->SetNumberField(TEXT("stored"), 3); Building(SavedHome, 2)->SetNumberField(TEXT("stored"), 1);
        Child(SavedHome, TEXT("resources"))->SetNumberField(TEXT("twigs"), Capacity - 1);
        Child(SavedHome, TEXT("resources"))->SetNumberField(TEXT("pebbles"), 0);
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        TestTrue(TEXT("Collect one accepted Twig from the selected collector"), Accepted(Simulation.Execute(Owner, TEXT("collect"), Selected(Building(Yard(Simulation), 1)))));
        TestEqual(TEXT("The two unaccepted Twigs remain stored"), Number(Building(Yard(Simulation), 1), TEXT("stored")), 2.);
        TestEqual(TEXT("Only the accepted transfer awards ceil(1/2) XP"), Earned(Simulation), 1.);
        TestTrue(TEXT("Collect All processes the other eligible collector"), Accepted(Simulation.Execute(Owner, TEXT("collect"), TEXT("{}"))));
        TestEqual(TEXT("Each collector rounds its own award; two accepted units yield two XP"), Earned(Simulation), 2.);
        TestTrue(TEXT("A second Collect All can safely find nothing accepted"), Accepted(Simulation.Execute(Owner, TEXT("collect"), TEXT("{}"))));
        TestEqual(TEXT("Empty/full transfers cannot repeat rewards"), Earned(Simulation), 2.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMExperienceLootTest, "BackyardMMO.Experience.AuthoritativeLootOnce",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMExperienceLootTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        FString Error; if (!Seed.AddPlayer(Rival, Rival, Error)) { AddError(Error); return false; }
        FObject Save = Seed.Serialize(); const FObject Own = SavedYard(Save);
        Child(Own, TEXT("army"))->SetNumberField(TEXT("C1"), 1);
        for (const TCHAR* Resource : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")}) Child(Own, TEXT("resources"))->SetNumberField(Resource, 0);
        const FObject Stock = Child(SavedYard(Save, Rival), TEXT("resources"));
        Stock->SetNumberField(TEXT("twigs"), 1000); Stock->SetNumberField(TEXT("pebbles"), 500);
        Stock->SetNumberField(TEXT("putty"), 100); Stock->SetNumberField(TEXT("goo"), 50);
        FBYMSimulation Prepared; if (!Load(*this, Prepared, Save)) return false;
        FObject Invade = MakeShared<FJsonObject>(); Invade->SetStringField(TEXT("target"), Rival);
        if (!TestTrue(TEXT("Begin invasion through host rules"), Accepted(Prepared.Execute(Owner, TEXT("invade"), Encode(Invade))))) return false;
        FObject Deploy = MakeShared<FJsonObject>(); Deploy->SetStringField(TEXT("type"), TEXT("C1")); Deploy->SetNumberField(TEXT("count"), 1);
        Deploy->SetNumberField(TEXT("x"), 0); Deploy->SetNumberField(TEXT("y"), 0);
        if (!TestTrue(TEXT("Make invasion contested through real deployment"), Accepted(Prepared.Execute(Owner, TEXT("deploy"), Encode(Deploy))))) return false;
        Save = Prepared.Serialize(); Building(SavedYard(Save, Rival), 14)->SetNumberField(TEXT("hp"), 0);
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        const double Started = Number(Snapshot(Simulation), TEXT("now")); Simulation.Tick(Started + .1, .1f);
        TestFalse(TEXT("The terminal Town Hall defeat resolves the invasion"), Simulation.HasActiveBattles());
        TestEqual(TEXT("Actually awarded 100+50+10+5 loot grants 165 XP"), Earned(Simulation), 165.);
        TestEqual(TEXT("The requested RP award remains its separate 25 points"), Number(Home(Simulation), TEXT("rp")), 25.);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Simulation.Serialize())) return false;
        Reloaded.Tick(Started + 5., .1f);
        TestEqual(TEXT("A completed/reloaded invasion cannot award loot XP twice"), Earned(Reloaded), 165.);
        return true;
    }
}
#endif
