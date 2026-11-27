#include "BYMSimulation.h"
#include "BYMBuildingLifecycle.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMBuildingLifecycleTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("lifecycle-owner")), Other(TEXT("lifecycle-other"));
    struct FSettingsGuard
    {
        UBYMSettings* S = GetMutableDefault<UBYMSettings>();
        const int32 Bots = S->RivalBotCount, Cells = S->YardCells, Starting = S->StartingResources, Floor = S->ResourceStorageFloor;
        const float Construction = S->ConstructionTimeMultiplier, Production = S->ResourceProductionMultiplier;
        const float Damage = S->CombatDamageMultiplier, Movement = S->MonsterMovementMultiplier, CombatStep = S->CombatSimulationStepSeconds;
        const float BattleSeconds = S->MaximumBattleSeconds;
        FSettingsGuard()
        {
            S->RivalBotCount = 0; S->YardCells = 40; S->StartingResources = 5000; S->ResourceStorageFloor = 10000;
            S->ConstructionTimeMultiplier = 1; S->ResourceProductionMultiplier = 0;
            S->CombatDamageMultiplier = 1; S->MonsterMovementMultiplier = 1; S->CombatSimulationStepSeconds = .1f;
            S->MaximumBattleSeconds = 300;
        }
        ~FSettingsGuard()
        {
            S->RivalBotCount = Bots; S->YardCells = Cells; S->StartingResources = Starting; S->ResourceStorageFloor = Floor;
            S->ConstructionTimeMultiplier = Construction; S->ResourceProductionMultiplier = Production;
            S->CombatDamageMultiplier = Damage; S->MonsterMovementMultiplier = Movement; S->CombatSimulationStepSeconds = CombatStep;
            S->MaximumBattleSeconds = BattleSeconds;
        }
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
    FObject SavedYard(const FObject& Save, const FString& Id = Owner)
    { for (const auto& V : Array(Save, TEXT("yards"))) if (String(V->AsObject(), TEXT("id")) == Id) return V->AsObject(); return nullptr; }
    FObject Building(const FObject& Yard, int32 Type)
    { for (const auto& V : Array(Yard, TEXT("buildings"))) if (Number(V->AsObject(), TEXT("type")) == Type) return V->AsObject(); return nullptr; }
    FObject FindBuilding(const FObject& Yard, const FString& Id)
    { for (const auto& V : Array(Yard, TEXT("buildings"))) if (String(V->AsObject(), TEXT("id")) == Id) return V->AsObject(); return nullptr; }
    FObject Snapshot(const FBYMSimulation& Sim, const FString& Id = Owner) { return Decode(Sim.Snapshot(Id, FString())); }
    // The compact home summary deliberately omits buildings. These tests use
    // an owner viewing their own yard whenever they inspect building actions.
    FObject Home(const FBYMSimulation& Sim, const FString& Id = Owner) { return Child(Snapshot(Sim, Id), TEXT("yard")); }
    bool Accepted(const FString& Reply) { return Flag(Decode(Reply), TEXT("ok")); }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Sim)
    {
        FString Error;
        if (!Sim.Initialize(nullptr, Error) || !Sim.AddPlayer(Owner, Owner, Error) || !Sim.AddPlayer(Other, Other, Error))
        { Test.AddError(Error); return false; } return true;
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Sim, const FObject& Save)
    { FString Error; if (!Sim.Initialize(Save, Error)) { Test.AddError(Error); return false; } return true; }
    void Work(const FObject& Save, const FObject& B, int32 Level, const FString& Id, double Remaining)
    {
        // This helper intentionally constructs a pre-worker paid-job fixture.
        for (const auto& Value : Array(Save, TEXT("yards"))) Value->AsObject()->RemoveField(TEXT("workerState"));
        B->SetNumberField(TEXT("level"), Level); B->SetNumberField(TEXT("targetLevel"), Level + 1);
        B->SetStringField(TEXT("jobId"), Id); B->SetNumberField(TEXT("workRemaining"), Remaining); B->SetNumberField(TEXT("readyAt"), 0);
    }
    FObject Command(const FObject& B, const FString& Job = FString())
    {
        FObject P = Obj(); P->SetStringField(TEXT("building"), String(B, TEXT("id")));
        P->SetStringField(TEXT("job"), Job.IsEmpty() ? String(B, TEXT("jobId")) : Job); return P;
    }
    FObject AddedConstruction(const FObject& Save)
    {
        FObject Yard = SavedYard(Save), B = Clone(Building(Yard, 1));
        B->SetStringField(TEXT("id"), TEXT("unfinished-twig")); B->SetNumberField(TEXT("x"), 1); B->SetNumberField(TEXT("y"), 30);
        Work(Save, B, 0, TEXT("construction-job"), 100); FArray Items = Array(Yard, TEXT("buildings"));
        Items.Add(MakeShared<FJsonValueObject>(B)); Yard->SetArrayField(TEXT("buildings"), Items); return B;
    }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingIndependentRepairTest, "BackyardMMO.BuildingLifecycle.DamageRepairAndReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMBuildingIndependentRepairTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
    FObject Save = Seed.Serialize(), B = Building(SavedYard(Save), 1); Work(Save, B, 1, TEXT("paid-upgrade"), 20);
    B->SetNumberField(TEXT("hp"), 330); FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
    const double Begin = Number(Snapshot(Sim), TEXT("now")); Sim.Tick(Begin + 20, 0);
    B = Building(Home(Sim), 1);
    TestEqual(TEXT("Any damage pauses paid work"), Number(B, TEXT("workRemaining")), 20.);
    TestEqual(TEXT("Paused work has no false deadline"), Number(B, TEXT("workReadyAt")), 0.);
    TestEqual(TEXT("Pause reason identifies damage"), String(B, TEXT("workPausedReason")), FString(TEXT("damaged")));
    TestTrue(TEXT("Repair is permitted while an upgrade is paused"), Accepted(Sim.Execute(Owner, TEXT("repair"), Encode(Command(B)))));
    B = Building(Home(Sim), 1);
    TestTrue(TEXT("Repair has its own deadline"), FMath::IsNearlyEqual(Number(B, TEXT("repairReadyAt")), Begin + 30, .00001));
    Sim.Tick(Begin + 30, 0); B = Building(Home(Sim), 1);
    TestEqual(TEXT("Reaching full health does not retroactively consume work"), Number(B, TEXT("workRemaining")), 20.);
    TestEqual(TEXT("Repair restores attained-level health"), Number(B, TEXT("hp")), 500.);
    Sim.Tick(Begin + 35, 0); B = Building(Home(Sim), 1);
    TestEqual(TEXT("Only time after repair advances work"), Number(B, TEXT("workRemaining")), 15.);
    FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
    TestEqual(TEXT("Save reload retains the exact paid job identity"), String(Building(Home(Reloaded), 1), TEXT("jobId")), FString(TEXT("paid-upgrade")));
    Reloaded.Tick(Begin + 49, 0);
    TestEqual(TEXT("Reload does not complete early"), Number(Building(Home(Reloaded), 1), TEXT("level")), 1.);
    Reloaded.Tick(Begin + 50, 0); B = Building(Home(Reloaded), 1);
    TestEqual(TEXT("Work completes at its remaining-time boundary"), Number(B, TEXT("level")), 2.);
    TestEqual(TEXT("Completion restores new-level health"), Number(B, TEXT("hp")), 950.);
    TestTrue(TEXT("Completion clears the paid job identity"), String(B, TEXT("jobId")).IsEmpty());
    TestEqual(TEXT("Completed work has no remaining time"), Number(B, TEXT("workRemaining")), 0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingCancellationTest, "BackyardMMO.BuildingLifecycle.CancellationIdentityAndRefunds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMBuildingCancellationTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
    FObject Save = Seed.Serialize(), B = AddedConstruction(Save); Child(SavedYard(Save), TEXT("resources"))->SetNumberField(TEXT("pebbles"), 0);
    FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
    const FString Request = Encode(Command(B));
    TestFalse(TEXT("Foreign owners cannot cancel another yard's work"), Accepted(Sim.Execute(Other, TEXT("cancelConstruction"), Request)));
    TestFalse(TEXT("A stale job identity is rejected"), Accepted(Sim.Execute(Owner, TEXT("cancelConstruction"), Encode(Command(B, TEXT("stale"))))));
    TestFalse(TEXT("An upgrade command cannot remove unfinished construction"), Accepted(Sim.Execute(Owner, TEXT("cancelUpgrade"), Request)));
    TestTrue(TEXT("Construction cancellation is accepted once"), Accepted(Sim.Execute(Owner, TEXT("cancelConstruction"), Request)));
    TestFalse(TEXT("Cancelled construction is removed"), FindBuilding(Home(Sim), TEXT("unfinished-twig")).IsValid());
    TestEqual(TEXT("Level-zero cancellation returns full original cost"), Number(Child(Home(Sim), TEXT("resources")), TEXT("pebbles")), 750.);
    TestFalse(TEXT("Replay cannot refund twice"), Accepted(Sim.Execute(Owner, TEXT("cancelConstruction"), Request)));
    Save = Seed.Serialize(); B = Building(SavedYard(Save), 1); Work(Save, B, 1, TEXT("upgrade-current"), 100);
    Child(SavedYard(Save), TEXT("resources"))->SetNumberField(TEXT("pebbles"), 17490);
    FBYMSimulation Upgrade; if (!Load(*this, Upgrade, Save)) return false;
    TestEqual(TEXT("Snapshot quotes the actual storage-capped refund"), Number(Child(Building(Home(Upgrade), 1), TEXT("cancelRefund")), TEXT("pebbles")), 10.);
    const FString CancelUpgrade = Encode(Command(B));
    TestTrue(TEXT("Upgrade cancellation succeeds"), Accepted(Upgrade.Execute(Owner, TEXT("cancelUpgrade"), CancelUpgrade)));
    B = Building(Home(Upgrade), 1);
    TestEqual(TEXT("Upgrade cancellation retains attained level"), Number(B, TEXT("level")), 1.);
    TestEqual(TEXT("Upgrade cancellation clears target promotion"), Number(B, TEXT("targetLevel")), 1.);
    TestEqual(TEXT("Refund cannot exceed storage capacity"), Number(Child(Home(Upgrade), TEXT("resources")), TEXT("pebbles")), 17500.);
    TestTrue(TEXT("Cancelled upgrade releases the worker and job"), String(B, TEXT("jobId")).IsEmpty());
    TestFalse(TEXT("An old confirmation cannot cancel or refund again"), Accepted(Upgrade.Execute(Owner, TEXT("cancelUpgrade"), CancelUpgrade)));
    Save = Upgrade.Serialize(); B = Building(SavedYard(Save), 1); Work(Save, B, 1, TEXT("replacement-job"), 100);
    Child(SavedYard(Save), TEXT("resources"))->SetNumberField(TEXT("pebbles"), 0);
    FBYMSimulation Replacement; if (!Load(*this, Replacement, Save)) return false;
    TestFalse(TEXT("An old confirmation cannot cancel a replacement job on the same building"), Accepted(Replacement.Execute(Owner, TEXT("cancelUpgrade"), CancelUpgrade)));
    TestEqual(TEXT("The replacement job survives the stale request"), String(Building(Home(Replacement), 1), TEXT("jobId")), FString(TEXT("replacement-job")));
    TestTrue(TEXT("The current replacement identity can cancel its job"), Accepted(Replacement.Execute(Owner, TEXT("cancelUpgrade"), Encode(Command(B)))));
    TestEqual(TEXT("An uncapped upgrade refund returns the full source next-level cost"), Number(Child(Home(Replacement), TEXT("resources")), TEXT("pebbles")), 1575.);
    Save = Seed.Serialize(); B = Building(SavedYard(Save), 1); Work(Save, B, 1, TEXT("damaged-job"), 100); B->SetNumberField(TEXT("hp"), 499);
    FBYMSimulation Damaged; if (!Load(*this, Damaged, Save)) return false;
    TestFalse(TEXT("Damaged work must be repaired before cancellation"), Accepted(Damaged.Execute(Owner, TEXT("cancelUpgrade"), Encode(Command(B)))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingPaidQueueCancellationTest, "BackyardMMO.BuildingLifecycle.MigratedPaidQueueCannotBeOrphaned",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMBuildingPaidQueueCancellationTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
    const FString HatcheryId = String(Building(Home(Seed), 13), TEXT("id"));
    FObject Train = Obj(); Train->SetStringField(TEXT("building"), HatcheryId); Train->SetStringField(TEXT("type"), TEXT("C1")); Train->SetNumberField(TEXT("count"), 1);
    if (!TestTrue(TEXT("A paid queue entry is created through host training"), Accepted(Seed.Execute(Owner, TEXT("train"), Encode(Train))))) return false;
    FObject Save = Seed.Serialize(), B = Building(SavedYard(Save), 13); Work(Save, B, 0, TEXT("migrated-hatchery-work"), 900);
    // Keep this synthetic migrated construction at its exact fixture state
    // while command dispatch catches up to real wall time on slower machines.
    SavedYard(Save)->SetNumberField(TEXT("lastEconomy"), Number(Snapshot(Seed), TEXT("now")) + 60.);
    FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
    const FString CancelConstruction = Encode(Command(B));
    TestFalse(TEXT("Removing migrated unfinished Hatchery cannot orphan paid training"), Accepted(Sim.Execute(Owner, TEXT("cancelConstruction"), CancelConstruction)));
    if (!TestEqual(TEXT("The migrated paid queue retains its Hatchery"), Array(Home(Sim), TEXT("hatcheries")).Num(), 1)) return false;
    const FObject Hatchery = Array(Home(Sim), TEXT("hatcheries"))[0]->AsObject(), Active = Child(Hatchery, TEXT("active"));
    FObject CancelTraining = Obj(); CancelTraining->SetStringField(TEXT("building"), HatcheryId);
    CancelTraining->SetStringField(TEXT("entry"), String(Active, TEXT("id"))); CancelTraining->SetNumberField(TEXT("expectedCount"), 1);
    TestTrue(TEXT("Existing training cancellation remains available for the migrated queue"), Accepted(Sim.Execute(Owner, TEXT("cancelTraining"), Encode(CancelTraining))));
    TestTrue(TEXT("Construction can be removed after the paid queue is explicitly refunded"), Accepted(Sim.Execute(Owner, TEXT("cancelConstruction"), CancelConstruction)));
    TestFalse(TEXT("The refunded Hatchery is removed"), FindBuilding(Home(Sim), HatcheryId).IsValid());
    FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
    TestEqual(TEXT("No orphan Hatchery queue remains in the saved world"), Array(Home(Reloaded), TEXT("hatcheries")).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingAttainedLevelTest, "BackyardMMO.BuildingLifecycle.AttainedPrerequisitesAndCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMBuildingAttainedLevelTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
    FObject Save = Seed.Serialize(), Yard = SavedYard(Save), Town = Building(Yard, 14), House = Building(Yard, 15);
    Work(Save, Town, 2, TEXT("town-upgrade"), 100); Town->SetNumberField(TEXT("hp"), 0);
    Work(Save, House, 1, TEXT("house-upgrade"), 100); House->SetNumberField(TEXT("hp"), 11); House->SetBoolField(TEXT("repairing"), true);
    FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
    const FObject HomeYard = Home(Sim), TwigOption = Child(Child(Snapshot(Sim), TEXT("buildOptions")), TEXT("1"));
    TestEqual(TEXT("Town Hall display retains its attained level while damaged and upgrading"), Number(HomeYard, TEXT("townHallLevel")), 2.);
    TestEqual(TEXT("Quantity uses attained Town Hall level"), Number(TwigOption, TEXT("limit")), 2.);
    TestTrue(TEXT("Damaged and upgrading prerequisite still counts at completed level"), Flag(TwigOption, TEXT("requirementsMet")));
    TestFalse(TEXT("Paused work continues to occupy the worker"), Flag(TwigOption, TEXT("canBuild")));
    TestEqual(TEXT("Housing retains attained capacity during repair and upgrade above ten HP"), Number(HomeYard, TEXT("housingCapacity")), 250.);
    House->SetNumberField(TEXT("hp"), 10); House->SetBoolField(TEXT("repairing"), false);
    FBYMSimulation LowHealth; if (!Load(*this, LowHealth, Save)) return false;
    TestEqual(TEXT("The source ten-health Housing boundary is exclusive"), Number(Home(LowHealth), TEXT("housingCapacity")), 0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingMigrationTest, "BackyardMMO.BuildingLifecycle.LegacyMigrationAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMBuildingMigrationTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
    const double Begin = Number(Snapshot(Seed), TEXT("now"));
    for (int32 Schema = 1; Schema <= 3; ++Schema)
    {
        FObject Save = Seed.Serialize(); Save->SetNumberField(TEXT("schema"), Schema);
        for (const auto& YardValue : Array(Save, TEXT("yards")))
        {
            const FObject Yard = YardValue->AsObject(); Yard->RemoveField(TEXT("workerState")); Yard->RemoveField(TEXT("earnedExperience")); Yard->RemoveField(TEXT("baseValue"));
            if (Schema < 3) Yard->RemoveField(TEXT("hatcheries"));
            for (const auto& Value : Array(Yard, TEXT("buildings")))
            { Value->AsObject()->RemoveField(TEXT("jobId")); Value->AsObject()->RemoveField(TEXT("workRemaining")); }
        }
        const FObject Yard = SavedYard(Save), B = Building(Yard, 1);
        Yard->SetNumberField(TEXT("lastEconomy"), Begin); B->SetNumberField(TEXT("targetLevel"), 2);
        B->SetNumberField(TEXT("readyAt"), Begin + 30); B->SetNumberField(TEXT("hp"), 499);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        Sim.Tick(Begin + 100, 0);
        const FObject Paused = Building(Home(Sim), 1);
        TestEqual(TEXT("Migrated damaged work retains the pre-accrual countdown"), Number(Paused, TEXT("workRemaining")), 30.);
        TestFalse(TEXT("Migration assigns a stable cancellable job identity"), String(Paused, TEXT("jobId")).IsEmpty());
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestEqual(TEXT("Migration is not applied again on schema-four reload"), String(Building(Home(Reloaded), 1), TEXT("jobId")), String(Paused, TEXT("jobId")));
    }
    FObject Good = Seed.Serialize(); FObject B = Building(SavedYard(Good), 1); Work(Good, B, 1, TEXT("validated-job"), 30);
    auto Rejected = [&](const FObject& Save) { FBYMSimulation Invalid; FString Error; return !Invalid.Initialize(Save, Error) && !Error.IsEmpty(); };
    FObject Bad = Clone(Good); Building(SavedYard(Bad), 1)->SetNumberField(TEXT("workRemaining"), -1);
    TestTrue(TEXT("Negative work is rejected without silently completing it"), Rejected(Bad));
    Bad = Clone(Good); Building(SavedYard(Bad), 1)->SetStringField(TEXT("jobId"), FString());
    TestTrue(TEXT("Pending work requires a job identity"), Rejected(Bad));
    Bad = Clone(Good); Building(SavedYard(Bad), 1)->SetNumberField(TEXT("targetLevel"), 3);
    TestTrue(TEXT("A job cannot skip an attained building level"), Rejected(Bad));
    Bad = Clone(Good); Work(Bad, Building(SavedYard(Bad), 2), 1, TEXT("validated-job"), 40);
    TestTrue(TEXT("Two jobs cannot share an identity within a yard"), Rejected(Bad));
    Bad = Clone(Good); Building(SavedYard(Bad), 1)->SetStringField(TEXT("repairing"), TEXT("false"));
    TestTrue(TEXT("Schema-four repair state is a strict boolean"), Rejected(Bad));
    Bad = Clone(Good); Building(SavedYard(Bad), 1)->SetNumberField(TEXT("targetLevel"), 1);
    TestTrue(TEXT("An idle building cannot retain a paid job"), Rejected(Bad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingLethalDamageTest, "BackyardMMO.BuildingLifecycle.LethalDamagePreservesPaidWork",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMBuildingLethalDamageTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Guard;
    // Exercise real invade/deploy/damage authority, not just the setback helper.
    for (const bool bConstruction : {false, true})
    {
        FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        FObject Save = Seed.Serialize(), Defending = SavedYard(Save, Other), Attacking = SavedYard(Save);
        Child(Attacking, TEXT("army"))->SetNumberField(TEXT("C5"), 2);
        Child(Attacking, TEXT("monsterLevels"))->SetNumberField(TEXT("C5"), 1);
        FObject Wall = Clone(Building(Defending, 1)); Wall->SetStringField(TEXT("id"), TEXT("target-wall"));
        Wall->SetNumberField(TEXT("type"), 17); Wall->SetNumberField(TEXT("x"), 22); Wall->SetNumberField(TEXT("y"), 20);
        Wall->SetNumberField(TEXT("hp"), 1000); Wall->SetNumberField(TEXT("productionRemaining"), 0);
        FObject Town = Clone(Building(Defending, 14)); Town->SetNumberField(TEXT("x"), 4); Town->SetNumberField(TEXT("y"), 4);
        FObject Remote = Clone(Building(Defending, 1)); Remote->SetNumberField(TEXT("x"), 4); Remote->SetNumberField(TEXT("y"), 20);
        FArray Buildings; Buildings.Add(MakeShared<FJsonValueObject>(Wall)); Buildings.Add(MakeShared<FJsonValueObject>(Town)); Buildings.Add(MakeShared<FJsonValueObject>(Remote));
        Defending->SetArrayField(TEXT("buildings"), Buildings); Defending->SetArrayField(TEXT("hatcheries"), FArray());
        FBYMSimulation Prepared; if (!Load(*this, Prepared, Save)) return false;
        if (!TestTrue(TEXT("A lifecycle fixture starts a real invasion"), Accepted(Prepared.Execute(Owner, TEXT("invade"), TEXT("{\"target\":\"lifecycle-other\"}"))))) return false;
        if (!TestTrue(TEXT("A ready Eye-Ra deploys through the host command"), Accepted(Prepared.Execute(Owner, TEXT("deploy"), TEXT("{\"type\":\"C5\",\"count\":1,\"x\":49,\"y\":20}"))))) return false;
        Save = Prepared.Serialize();
        const FObject Battle = Array(Save, TEXT("battles"))[0]->AsObject(), Troop = Array(Battle, TEXT("troops"))[0]->AsObject();
        Troop->SetNumberField(TEXT("x"), 23.25); Troop->SetNumberField(TEXT("y"), 20.25);
        Troop->SetStringField(TEXT("target"), TEXT("target-wall")); Troop->SetNumberField(TEXT("attackCooldown"), 0);
        // Preparing the invasion may have advanced time; retain the damaged
        // repair fixture immediately before the first authoritative hit.
        Wall = FindBuilding(SavedYard(Save, Other), TEXT("target-wall"));
        Wall->SetNumberField(TEXT("hp"), 1); Wall->SetBoolField(TEXT("repairing"), true); Work(Save, Wall, bConstruction ? 0 : 1, TEXT("surviving-job"), 2);
        const double Begin = Number(Snapshot(Prepared), TEXT("now")) + 60.;
        SavedYard(Save, Other)->SetNumberField(TEXT("lastEconomy"), Begin);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        Sim.Tick(Begin + .1, .1f);
        Wall = FindBuilding(SavedYard(Sim.Serialize(), Other), TEXT("target-wall"));
        TestEqual(TEXT("The source explosion destroys the target"), Number(Wall, TEXT("hp")), 0.);
        TestFalse(TEXT("Lethal damage clears repair independently"), Flag(Wall, TEXT("repairing")));
        TestEqual(TEXT("Destruction does not promote unfinished work or drop the attained level"), Number(Wall, TEXT("level")), bConstruction ? 0. : 1.);
        TestEqual(TEXT("Paid job identity survives destruction"), String(Wall, TEXT("jobId")), FString(TEXT("surviving-job")));
        TestEqual(TEXT("Construction retains time; a killed upgrade receives the original integer setback even while repairing"), Number(Wall, TEXT("workRemaining")), bConstruction ? 2. : 3.);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        Reloaded.Tick(Begin + 10, 0);
        TestEqual(TEXT("Destroyed paused work survives save reload without running"), Number(FindBuilding(SavedYard(Reloaded.Serialize(), Other), TEXT("target-wall")), TEXT("workRemaining")), bConstruction ? 2. : 3.);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingWorkBoundaryTest, "BackyardMMO.BuildingLifecycle.SourceCountdownBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMBuildingWorkBoundaryTest::RunTest(const FString& Parameters)
{
    using namespace BYMBuildingLifecycle;
    TestEqual(TEXT("One health point below full pauses work"), AdvanceWork(100, 50, 499, 500, false), 100.);
    TestEqual(TEXT("Repair consumes its own time, not paid work"), AdvanceWork(100, 50, 500, 500, true), 100.);
    TestEqual(TEXT("Healthy work clips at zero"), AdvanceWork(1, 2, 500, 500, false), 0.);
    TestEqual(TEXT("Clock rollback cannot add work"), AdvanceWork(100, -1, 500, 500, false), 100.);
    TestEqual(TEXT("Upgrade destruction returns half completed progress"), AfterDestruction(80, 100), 90.);
    TestEqual(TEXT("Setback truncates the source integer locals in order"), AfterDestruction(80.5, 100.9), 89.);
    TestEqual(TEXT("Destruction setback is capped at eight hours"), AfterDestruction(30000, 200000), 58800.);
    TestEqual(TEXT("Migration preserves work before its accrual origin"), MigrateRemaining(1030, 1000, 1010), 30.);
    TestEqual(TEXT("A future accrual timestamp cannot rewind the migration origin"), MigrateRemaining(1030, 1050, 1010), 20.);
    TestEqual(TEXT("A job already due before the bounded history remains due"), MigrateRemaining(1, 0, 63072000), 0.);
    return true;
}
}
#endif
