#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMWorkerAuthorityTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("workers-owner"));
    struct FSettingsGuard
    {
        UBYMSettings* S = GetMutableDefault<UBYMSettings>();
        const int32 Bots = S->RivalBotCount, Cells = S->YardCells, Starting = S->StartingResources;
        const float Construction = S->ConstructionTimeMultiplier, Production = S->ResourceProductionMultiplier;
        FSettingsGuard()
        { S->RivalBotCount = 0; S->YardCells = 40; S->StartingResources = 5000; S->ConstructionTimeMultiplier = 1; S->ResourceProductionMultiplier = 0; }
        ~FSettingsGuard()
        { S->RivalBotCount = Bots; S->YardCells = Cells; S->StartingResources = Starting; S->ConstructionTimeMultiplier = Construction; S->ResourceProductionMultiplier = Production; }
    };
    FObject Obj() { return MakeShared<FJsonObject>(); }
    FObject Decode(const FString& Text)
    { FObject V; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), V); return V; }
    FString Encode(const FObject& V)
    { FString Text; FJsonSerializer::Serialize(V.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FObject Clone(const FObject& V) { return Decode(Encode(V)); }
    FObject Child(const FObject& V, const TCHAR* Key)
    { const FObject* R = nullptr; return V && V->TryGetObjectField(Key, R) && R ? *R : FObject(); }
    const FArray& Array(const FObject& V, const TCHAR* Key)
    { static const FArray Empty; const FArray* R = nullptr; return V && V->TryGetArrayField(Key, R) && R ? *R : Empty; }
    double Number(const FObject& V, const TCHAR* Key)
    { double R = 0; if (V) V->TryGetNumberField(Key, R); return R; }
    FString String(const FObject& V, const TCHAR* Key)
    { FString R; if (V) V->TryGetStringField(Key, R); return R; }
    bool Flag(const FObject& V, const TCHAR* Key)
    { bool R = false; if (V) V->TryGetBoolField(Key, R); return R; }
    FObject Snapshot(const FBYMSimulation& Sim) { return Decode(Sim.Snapshot(Owner, FString())); }
    FObject Home(const FBYMSimulation& Sim) { return Child(Snapshot(Sim), TEXT("yard")); }
    FObject SavedYard(const FObject& Save)
    { for (const auto& V : Array(Save, TEXT("yards"))) if (String(V->AsObject(), TEXT("id")) == Owner) return V->AsObject(); return nullptr; }
    FObject Building(const FObject& Yard, int32 Type)
    { for (const auto& V : Array(Yard, TEXT("buildings"))) if (Number(V->AsObject(), TEXT("type")) == Type) return V->AsObject(); return nullptr; }
    FObject Workers(const FBYMSimulation& Sim) { return Child(SavedYard(Sim.Serialize()), TEXT("workerState")); }
    FObject WorkerFor(const FObject& State, const FString& Task)
    { for (const auto& V : Array(State, TEXT("entries"))) if (String(V->AsObject(), TEXT("task")) == Task) return V->AsObject(); return nullptr; }
    bool Accepted(const FString& Reply) { return Flag(Decode(Reply), TEXT("ok")); }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Sim)
    { FString Error; if (!Sim.Initialize(nullptr, Error) || !Sim.AddPlayer(Owner, Owner, Error)) { Test.AddError(Error); return false; } return true; }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Sim, const FObject& Save)
    { FString Error; if (!Sim.Initialize(Save, Error)) { Test.AddError(Error); return false; } return true; }
    FString Selected(const FObject& B)
    { FObject P = Obj(); P->SetStringField(TEXT("building"), String(B, TEXT("id"))); return Encode(P); }
    void PaidWork(const FObject& B, const FString& Job, double Remaining)
    {
        B->SetNumberField(TEXT("targetLevel"), Number(B, TEXT("level")) + 1);
        B->SetStringField(TEXT("jobId"), Job); B->SetNumberField(TEXT("workRemaining"), Remaining); B->SetNumberField(TEXT("readyAt"), 0);
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMWorkerAdmissionAndTravelTest, "BackyardMMO.Workers.AuthoritativeAdmissionTravelAndRepair",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMWorkerAdmissionAndTravelTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        FObject Save = Seed.Serialize(); Building(SavedYard(Save), 2)->SetNumberField(TEXT("hp"), 250);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        TestEqual(TEXT("Every new yard starts with one free source worker"), Array(Workers(Sim), TEXT("entries")).Num(), 1);
        const FObject Twig = Building(Home(Sim), 1); const FString TwigId = String(Twig, TEXT("id"));
        if (!TestTrue(TEXT("A valid upgrade reserves the free worker"), Accepted(Sim.Execute(Owner, TEXT("upgrade"), Selected(Twig))))) return false;
        const FObject Travelling = WorkerFor(Workers(Sim), TwigId);
        if (!TestTrue(TEXT("The paid building has an exact worker assignment"), Travelling.IsValid())) return false;
        TestEqual(TEXT("Assignment records the source work type"), String(Travelling, TEXT("kind")), FString(TEXT("upgrade")));
        TestEqual(TEXT("Assignment binds to the current paid job identity"), String(Travelling, TEXT("job")), String(Building(Home(Sim), 1), TEXT("jobId")));
        const double Arrival = Number(Travelling, TEXT("arrivalAt")), Started = Number(Travelling, TEXT("startedAt"));
        const double Work = Number(Building(Home(Sim), 1), TEXT("workRemaining"));
        TestTrue(TEXT("Worker has a real finite positive travel phase"), FMath::IsFinite(Arrival) && Arrival > Started);
        TestTrue(TEXT("Travel carries the actual presentation route"), Array(Travelling, TEXT("route")).Num() >= 2);
        const FObject Before = Child(Home(Sim), TEXT("resources"));
        TestFalse(TEXT("A second upgrade cannot overbook the free worker"), Accepted(Sim.Execute(Owner, TEXT("upgrade"), Selected(Building(Home(Sim), 3)))));
        const FObject After = Child(Home(Sim), TEXT("resources"));
        for (const TCHAR* Resource : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")})
            TestEqual(FString(TEXT("Busy-worker rejection preserves ")) + Resource, Number(After, Resource), Number(Before, Resource));
        TestTrue(TEXT("Ordinary repair starts while the only worker is occupied"), Accepted(Sim.Execute(Owner, TEXT("repair"), Selected(Building(Home(Sim), 2)))));
        TestEqual(TEXT("Free repair does not allocate an extra worker"), Array(Workers(Sim), TEXT("entries")).Num(), 1);
        Sim.Tick(Arrival - .001, 0);
        TestEqual(TEXT("Construction does not advance while the worker is travelling"), Number(Building(Home(Sim), 1), TEXT("workRemaining")), Work);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestEqual(TEXT("Mid-route reload preserves the authoritative arrival"), Number(WorkerFor(Workers(Reloaded), TwigId), TEXT("arrivalAt")), Arrival);
        TestEqual(TEXT("Mid-route reload preserves the paid remaining work"), Number(Building(Home(Reloaded), 1), TEXT("workRemaining")), Work);
        Reloaded.Tick(Arrival, 0);
        TestEqual(TEXT("Arrival does not retroactively credit travel as building work"), Number(Building(Home(Reloaded), 1), TEXT("workRemaining")), Work);
        Reloaded.Tick(Arrival + Work - .01, 0);
        TestEqual(TEXT("Upgrade is not completed before its worker-adjusted deadline"), Number(Building(Home(Reloaded), 1), TEXT("level")), 1.);
        Reloaded.Tick(Arrival + Work, 0);
        TestEqual(TEXT("Upgrade completes at the worker-adjusted deadline"), Number(Building(Home(Reloaded), 1), TEXT("level")), 2.);
        TestTrue(TEXT("Completed assignment releases its task identity"), String(Array(Workers(Reloaded), TEXT("entries"))[0]->AsObject(), TEXT("task")).IsEmpty());
        TestTrue(TEXT("Released worker is available to another valid upgrade"), Accepted(Reloaded.Execute(Owner, TEXT("upgrade"), Selected(Building(Home(Reloaded), 3)))));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMWorkerLegacyJobsTest, "BackyardMMO.Workers.LegacyPaidJobsSurviveWithoutPermanentFreeWorkers",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMWorkerLegacyJobsTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        FObject Save = Seed.Serialize(), Yard = SavedYard(Save); Yard->RemoveField(TEXT("workerState"));
        PaidWork(Building(Yard, 1), TEXT("legacy-twig"), 30.);
        PaidWork(Building(Yard, 2), TEXT("legacy-pebble"), 50.);
        PaidWork(Building(Yard, 3), TEXT("legacy-putty"), 70.);
        const double Begin = Number(Yard, TEXT("lastEconomy"));
        const FObject Wallet = Clone(Child(Yard, TEXT("resources")));
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FObject InitialWorkers = Workers(Sim); int32 LegacyCount = 0;
        for (const auto& V : Array(InitialWorkers, TEXT("entries"))) if (Flag(V->AsObject(), TEXT("legacy"))) ++LegacyCount;
        TestEqual(TEXT("Existing paid jobs are all retained during migration"), Array(InitialWorkers, TEXT("entries")).Num(), 3);
        TestEqual(TEXT("Only over-cap paid jobs receive temporary legacy workers"), LegacyCount, 2);
        TestFalse(TEXT("Migrated paid jobs do not allow overbooking another job"), Accepted(Sim.Execute(Owner, TEXT("upgrade"), Selected(Building(Home(Sim), 4)))));
        Sim.Tick(Begin + 30., 0);
        TestEqual(TEXT("First old paid job finishes without charging travel again"), Number(Building(Home(Sim), 1), TEXT("level")), 2.);
        TestFalse(TEXT("An idle regular worker cannot ignore still-active legacy over-cap work"), Accepted(Sim.Execute(Owner, TEXT("upgrade"), Selected(Building(Home(Sim), 4)))));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        Reloaded.Tick(Begin + 70., 0);
        TestEqual(TEXT("Second migrated job completes normally"), Number(Building(Home(Reloaded), 2), TEXT("level")), 2.);
        TestEqual(TEXT("Third migrated job completes normally"), Number(Building(Home(Reloaded), 3), TEXT("level")), 2.);
        TestEqual(TEXT("Temporary legacy workers retire after their exact old jobs"), Array(Workers(Reloaded), TEXT("entries")).Num(), 1);
        for (const TCHAR* Resource : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")})
            TestEqual(FString(TEXT("Migration neither charges nor refunds ")) + Resource, Number(Child(Home(Reloaded), TEXT("resources")), Resource), Number(Wallet, Resource));
        TestTrue(TEXT("Original free worker remains usable after legacy jobs finish"), Accepted(Reloaded.Execute(Owner, TEXT("upgrade"), Selected(Building(Home(Reloaded), 4)))));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMWorkerSaveValidationTest, "BackyardMMO.Workers.StrictAssignmentSnapshotValidation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMWorkerSaveValidationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        const FObject Idle = Seed.Serialize();
        auto Reject = [&](const FObject& Save)
        { FBYMSimulation Sim; FString Error; return !Sim.Initialize(Save, Error) && !Error.IsEmpty(); };
        FObject Bad = Clone(Idle), State = Child(SavedYard(Bad), TEXT("workerState"));
        State->SetArrayField(TEXT("entries"), FArray()); TestTrue(TEXT("A current save cannot omit its purchased worker"), Reject(Bad));
        Bad = Clone(Idle); State = Child(SavedYard(Bad), TEXT("workerState")); FArray Entries = Array(State, TEXT("entries"));
        Entries.Add(MakeShared<FJsonValueObject>(Clone(Entries[0]->AsObject()))); State->SetArrayField(TEXT("entries"), Entries);
        TestTrue(TEXT("Duplicated worker identities are rejected"), Reject(Bad));
        Bad = Clone(Idle); Array(Child(SavedYard(Bad), TEXT("workerState")), TEXT("entries"))[0]->AsObject()->SetNumberField(TEXT("kind"), 7);
        TestTrue(TEXT("Wrong-type persisted worker state is rejected"), Reject(Bad));
        if (!TestTrue(TEXT("Fixture starts one genuine paid worker job"), Accepted(Seed.Execute(Owner, TEXT("upgrade"), Selected(Building(Home(Seed), 1)))))) return false;
        const FObject Working = Seed.Serialize();
        Bad = Clone(Working); Array(Child(SavedYard(Bad), TEXT("workerState")), TEXT("entries"))[0]->AsObject()->SetStringField(TEXT("job"), TEXT("foreign-job"));
        TestTrue(TEXT("Worker must match the building's exact paid job"), Reject(Bad));
        Bad = Clone(Working); Array(Child(SavedYard(Bad), TEXT("workerState")), TEXT("entries"))[0]->AsObject()->SetNumberField(TEXT("x"), 1000000);
        TestTrue(TEXT("Persisted worker cannot teleport outside supported terrain"), Reject(Bad));
        Bad = Clone(Working); Array(Child(SavedYard(Bad), TEXT("workerState")), TEXT("entries"))[0]->AsObject()->SetNumberField(TEXT("arrivalAt"), -1);
        TestTrue(TEXT("Invalid worker arrival time is rejected"), Reject(Bad));
        Bad = Clone(Working); State = Array(Child(SavedYard(Bad), TEXT("workerState")), TEXT("entries"))[0]->AsObject();
        if (!TestTrue(TEXT("Working fixture has a route to validate"), !Array(State, TEXT("route")).IsEmpty())) return false;
        Array(State, TEXT("route"))[0]->AsObject()->SetNumberField(TEXT("at"), .5);
        TestTrue(TEXT("Worker route must begin at its authoritative start"), Reject(Bad));
        return true;
    }
}
#endif
