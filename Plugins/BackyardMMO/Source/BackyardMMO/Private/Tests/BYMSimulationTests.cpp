#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// These tests exercise the real C++ simulation and bundled catalog. They do not use a
// second implementation of the game rules, do not open a network port, and never write
// account/world saves. They must be executed in a built UE 5.8 editor or commandlet.
namespace BYMSimulationTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;

    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        int32 Bots = Settings->RivalBotCount;
        int32 Starting = Settings->StartingResources;
        int32 Cells = Settings->YardCells;
        int32 Win = Settings->RPWin;
        int32 Loss = Settings->RPLoss;
        float Shield = Settings->ShieldSeconds;
        float Training = Settings->TrainingTimeMultiplier;
        float Construction = Settings->ConstructionTimeMultiplier;
        float Production = Settings->ResourceProductionMultiplier;
        float BattleDuration = Settings->MaximumBattleSeconds;
        int32 Troops = Settings->MaximumTroopsPerBattle;
        int32 Buildings = Settings->MaxBuildingsPerYard;
        bool Clamp = Settings->bClampRPAtZero;
        explicit FSettingsGuard(int32 BotCount = 0)
        {
            Settings->RivalBotCount = BotCount;
            Settings->StartingResources = 5000;
            Settings->YardCells = 40;
            Settings->RPWin = 25;
            Settings->RPLoss = 15;
            Settings->ShieldSeconds = 3600;
            Settings->TrainingTimeMultiplier = 1;
            Settings->ConstructionTimeMultiplier = 1;
            Settings->ResourceProductionMultiplier = 1;
            Settings->MaximumBattleSeconds = 300;
            Settings->MaximumTroopsPerBattle = 100;
            Settings->MaxBuildingsPerYard = 150;
            Settings->bClampRPAtZero = true;
        }
        ~FSettingsGuard()
        {
            Settings->RivalBotCount = Bots;
            Settings->StartingResources = Starting;
            Settings->YardCells = Cells;
            Settings->RPWin = Win;
            Settings->RPLoss = Loss;
            Settings->ShieldSeconds = Shield;
            Settings->TrainingTimeMultiplier = Training;
            Settings->ConstructionTimeMultiplier = Construction;
            Settings->ResourceProductionMultiplier = Production;
            Settings->MaximumBattleSeconds = BattleDuration;
            Settings->MaximumTroopsPerBattle = Troops;
            Settings->MaxBuildingsPerYard = Buildings;
            Settings->bClampRPAtZero = Clamp;
        }
    };

    FObject Decode(const FString& Text)
    {
        FObject Object;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
        return Object;
    }
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
    FObject Building(const FObject& YardObject, int32 Type)
    {
        for (const TSharedPtr<FJsonValue>& Value : Array(YardObject, TEXT("buildings")))
            if (Number(Value->AsObject(), TEXT("type")) == Type) return Value->AsObject();
        return nullptr;
    }
    bool Accepted(const FString& Reply)
    {
        const FObject Object = Decode(Reply);
        bool Value = false;
        return Object && Object->TryGetBoolField(TEXT("ok"), Value) && Value;
    }
    FObject Yard(const FBYMSimulation& Simulation, const FString& Id)
    {
        return Child(Decode(Simulation.Snapshot(Id, FString())), TEXT("yard"));
    }
    FObject SavedYard(const FObject& Save, const FString& Id)
    {
        for (const TSharedPtr<FJsonValue>& Value : Array(Save, TEXT("yards")))
        {
            const FObject Candidate = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : FObject();
            if (String(Candidate, TEXT("id")) == Id) return Candidate;
        }
        return nullptr;
    }
    bool InitializeFixture(FAutomationTestBase& Test, FBYMSimulation& Simulation)
    {
        FString Error;
        const bool Result = Simulation.Initialize(nullptr, Error);
        if (!Result) Test.AddError(TEXT("Cannot initialize real bundled catalog: ") + Error);
        return Result;
    }
    bool AddFixturePlayer(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FString& Id)
    {
        FString Error;
        const bool Result = Simulation.AddPlayer(Id, Id, Error);
        if (!Result) Test.AddError(TEXT("Cannot create fixture yard: ") + Error);
        return Result;
    }
    FString MovePayload(const FString& Building, int32 X, int32 Y)
    {
        return FString::Printf(TEXT("{\"building\":\"%s\",\"x\":%d,\"y\":%d}"), *Building, X, Y);
    }
    void SameWallet(FAutomationTestBase& Test, const FObject& Before, const FObject& After)
    {
        for (const TCHAR* Resource : { TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo") })
            Test.TestEqual(FString(TEXT("Untrusted commands cannot change ")) + Resource,
                Number(Child(After, TEXT("resources")), Resource), Number(Child(Before, TEXT("resources")), Resource));
    }
}

using namespace BYMSimulationTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCommandValidationTest, "BackyardMMO.Simulation.RejectUntrustedCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMCommandValidationTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FBYMSimulation Simulation;
    if (!InitializeFixture(*this, Simulation) || !AddFixturePlayer(*this, Simulation, TEXT("player-a")) || !AddFixturePlayer(*this, Simulation, TEXT("player-b"))) return false;
    const FObject Before = Yard(Simulation, TEXT("player-a"));
    const FObject Other = Yard(Simulation, TEXT("player-b"));
    const FArray& Buildings = Array(Before, TEXT("buildings"));
    const FArray& OtherBuildings = Array(Other, TEXT("buildings"));
    if (!TestTrue(TEXT("The genuine starter layout supplies multiple buildings"), Buildings.Num() >= 2 && OtherBuildings.Num() >= 1)) return false;
    const FObject First = Buildings[0]->AsObject();
    const FObject Second = Buildings[1]->AsObject();
    const FString FirstId = String(First, TEXT("id"));

    TestFalse(TEXT("Reject negative build coordinate"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("build"), TEXT("{\"type\":20,\"x\":-1,\"y\":10}"))));
    TestFalse(TEXT("Reject fractional build coordinate"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("build"), TEXT("{\"type\":20,\"x\":1.5,\"y\":10}"))));
    TestFalse(TEXT("Reject outside-yard footprint"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("build"), TEXT("{\"type\":20,\"x\":40,\"y\":40}"))));
    TestFalse(TEXT("Reject overlapping move"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("move"),
        MovePayload(FirstId, static_cast<int32>(Number(Second, TEXT("x"))), static_cast<int32>(Number(Second, TEXT("y")))))));
    TestFalse(TEXT("Reject duplicate build footprint"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("build"),
        FString::Printf(TEXT("{\"type\":20,\"x\":%d,\"y\":%d}"), static_cast<int32>(Number(First, TEXT("x"))), static_cast<int32>(Number(First, TEXT("y")))))));
    TestFalse(TEXT("Reject a different owner's building identifier"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("move"),
        MovePayload(String(OtherBuildings[0]->AsObject(), TEXT("id")), 30, 30))));
    TestFalse(TEXT("Reject missing server-owned player identity"), Accepted(Simulation.Execute(TEXT("missing-player"), TEXT("collect"), TEXT("{}"))));

    TestFalse(TEXT("Client cannot declare a battle victory"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("win"), TEXT("{\"rp\":999999,\"winner\":\"player-a\"}"))));
    TestFalse(TEXT("Client cannot set RP"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("setRP"), TEXT("{\"rp\":999999}"))));
    TestFalse(TEXT("Client cannot donate resources to itself"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("give_resources"), TEXT("{\"goo\":999999,\"twigs\":999999}"))));
    TestFalse(TEXT("Research without a completed Academy is rejected"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("research"), TEXT("{\"type\":\"C1\"}"))));
    TestFalse(TEXT("Malformed JSON rejected"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("build"), TEXT("{"))));
    TestFalse(TEXT("Oversized command rejected"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("collect"), FString::ChrN(70000, TEXT('x')))));
    TestFalse(TEXT("Negative training count rejected"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":-1}"))));
    TestFalse(TEXT("Zero training count rejected"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":0}"))));
    TestFalse(TEXT("Fractional training count rejected"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":1.5}"))));
    TestFalse(TEXT("Excessive training count rejected"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":101}"))));
    TestTrue(TEXT("Collect ignores fabricated resource fields"), Accepted(Simulation.Execute(TEXT("player-a"), TEXT("collect"), TEXT("{\"resources\":{\"goo\":999999},\"amount\":999999}"))));

    const FObject After = Yard(Simulation, TEXT("player-a"));
    SameWallet(*this, Before, After);
    TestEqual(TEXT("Rejected actions preserve building count"), Array(After, TEXT("buildings")).Num(), Buildings.Num());
    TestEqual(TEXT("Rejected training does not create a queue"), Array(After, TEXT("training")).Num(), 0);
    TestEqual(TEXT("Rejected actions cannot manufacture RP"), Number(After, TEXT("rp")), 0.0);
    if (!TestTrue(TEXT("The yard still has its starter buildings"), !Array(After, TEXT("buildings")).IsEmpty())) return false;
    const FObject FirstAfter = Array(After, TEXT("buildings"))[0]->AsObject();
    TestEqual(TEXT("Rejected move preserves X"), Number(FirstAfter, TEXT("x")), Number(First, TEXT("x")));
    TestEqual(TEXT("Rejected move preserves Y"), Number(FirstAfter, TEXT("y")), Number(First, TEXT("y")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMThousandRivalsPersistenceTest, "BackyardMMO.Simulation.ThousandRivalsSaveReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMThousandRivalsPersistenceTest::RunTest(const FString& Parameters)
{
    // Only this test instantiates the requested 1,000 rivals. Other tests use zero to stay fast.
    FSettingsGuard Settings(1000);
    FBYMSimulation Simulation;
    if (!InitializeFixture(*this, Simulation)) return false;
    const FObject Save = Simulation.Serialize();
    const FArray& Yards = Array(Save, TEXT("yards"));
    TestEqual(TEXT("A new host owns exactly 1,000 rival yards"), Yards.Num(), 1000);
    TSet<FString> Identities;
    bool bAllZero = true;
    bool bAllBots = true;
    for (const TSharedPtr<FJsonValue>& Value : Yards)
    {
        const FObject Entry = Value->AsObject();
        Identities.Add(String(Entry, TEXT("id")));
        bAllZero &= Number(Entry, TEXT("rp")) == 0;
        bool bBot = false; Entry->TryGetBoolField(TEXT("bot"), bBot); bAllBots &= bBot;
    }
    TestEqual(TEXT("Rival identities are unique"), Identities.Num(), 1000);
    TestTrue(TEXT("Every rival begins at zero RP"), bAllZero);
    TestTrue(TEXT("All initial yards are rivals"), bAllBots);
    const FObject Ladder = Decode(Simulation.Ladder(0));
    TestEqual(TEXT("Ladder total covers all rivals"), Number(Ladder, TEXT("total")), 1000.0);
    TestEqual(TEXT("Ladder has 20 pages"), Number(Ladder, TEXT("totalPages")), 20.0);
    TestEqual(TEXT("Ladder page contains 50 entries"), Array(Ladder, TEXT("entries")).Num(), 50);
    const FObject LastPage = Decode(Simulation.Ladder(19));
    if (!TestTrue(TEXT("The final ladder page is populated"), !Array(LastPage, TEXT("entries")).IsEmpty())) return false;
    TestEqual(TEXT("Last rival is rank 1,000"), Number(Array(LastPage, TEXT("entries")).Last()->AsObject(), TEXT("rank")), 1000.0);

    FBYMSimulation Reloaded;
    FString Error;
    if (!TestTrue(TEXT("Load a complete rival snapshot"), Reloaded.Initialize(Save, Error))) { AddError(Error); return false; }
    const FObject ReloadedSave = Reloaded.Serialize();
    TestEqual(TEXT("Loading does not seed another 1,000 bots"), Array(ReloadedSave, TEXT("yards")).Num(), 1000);
    bool bStable = true;
    for (const TSharedPtr<FJsonValue>& Value : Array(ReloadedSave, TEXT("yards")))
    {
        const FObject Entry = Value->AsObject();
        bStable &= Identities.Contains(String(Entry, TEXT("id"))) && Number(Entry, TEXT("rp")) == 0;
    }
    TestTrue(TEXT("Reload preserves rival identities and initial RP"), bStable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMemorySnapshotTest, "BackyardMMO.Simulation.MemoryAndSnapshotBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMMemorySnapshotTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FBYMSimulation Simulation;
    const FString Player = TEXT("retained-player");
    if (!InitializeFixture(*this, Simulation) || !AddFixturePlayer(*this, Simulation, Player)) return false;
    const double Start = Number(Decode(Simulation.Snapshot(Player, FString())), TEXT("now"));
    if (!TestTrue(TEXT("Queue two monsters using real catalog costs"), Accepted(Simulation.Execute(Player, TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":2}"))))) return false;
    const FObject EarlierSnapshot = Simulation.Serialize();
    const FObject EarlierYard = SavedYard(EarlierSnapshot, Player);
    TestEqual(TEXT("Before the save interval there are two queued monsters"), Array(EarlierYard, TEXT("training")).Num(), 2);
    TestEqual(TEXT("Current native snapshots use schema five"), Number(EarlierSnapshot, TEXT("schema")), 5.0);
    const FArray& SavedHatcheries = Array(EarlierYard, TEXT("hatcheries"));
    if (!TestEqual(TEXT("The starter Hatchery persists one independent production state"), SavedHatcheries.Num(), 1)) return false;
    TestEqual(TEXT("One creature is actively incubating"), Number(Child(SavedHatcheries[0]->AsObject(), TEXT("active")), TEXT("count")), 1.0);
    if (!TestEqual(TEXT("The second creature occupies one waiting group"), Array(SavedHatcheries[0]->AsObject(), TEXT("queue")).Num(), 1)) return false;
    TestEqual(TEXT("Waiting groups retain their own creature count"), Number(Array(SavedHatcheries[0]->AsObject(), TEXT("queue"))[0]->AsObject(), TEXT("count")), 1.0);
    TestEqual(TEXT("Queued creatures do not consume housing before release"), Number(Yard(Simulation, Player), TEXT("housingUsed")), 0.0);
    // Delta=0 advances absolute economy timers but deliberately does not run combat or rival AI.
    Simulation.Tick(Start + 3600.0, 0.0f);
    TestTrue(TEXT("A yard remains in the authoritative model without a connected controller"), Simulation.HasYard(Player));
    const FObject LiveYard = Yard(Simulation, Player);
    TestEqual(TEXT("Training completes in server memory"), Number(Child(LiveYard, TEXT("army")), TEXT("C1")), 2.0);
    TestEqual(TEXT("The live queue is empty"), Array(LiveYard, TEXT("training")).Num(), 0);
    TestEqual(TEXT("A previous serialized snapshot is not mutated in place"), Array(EarlierYard, TEXT("training")).Num(), 2);
    TestEqual(TEXT("Paid Goo remains authoritative during accrual"), Number(Child(LiveYard, TEXT("resources")), TEXT("goo")), 4500.0);
    const FObject HourlySnapshot = Simulation.Serialize();
    FBYMSimulation Reloaded;
    FString Error;
    if (!TestTrue(TEXT("The later snapshot can be restored"), Reloaded.Initialize(HourlySnapshot, Error))) { AddError(Error); return false; }
    const FObject Restored = Yard(Reloaded, Player);
    TestEqual(TEXT("Snapshot restoration retains completed monsters"), Number(Child(Restored, TEXT("army")), TEXT("C1")), 2.0);
    TestEqual(TEXT("Snapshot restoration retains the paid resource wallet"), Number(Child(Restored, TEXT("resources")), TEXT("goo")), 4500.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMRaidOutcomeTest, "BackyardMMO.Simulation.RetreatShieldAndRanking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMRaidOutcomeTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FBYMSimulation Simulation;
    const FString Attacker = TEXT("attacker");
    const FString Defender = TEXT("defender");
    if (!InitializeFixture(*this, Simulation) || !AddFixturePlayer(*this, Simulation, Attacker) || !AddFixturePlayer(*this, Simulation, Defender)) return false;
    const double Start = Number(Decode(Simulation.Snapshot(Attacker, FString())), TEXT("now"));
    if (!TestTrue(TEXT("Attacker trains an army"), Accepted(Simulation.Execute(Attacker, TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":2}"))))) return false;
    Simulation.Tick(Start + 31.0, 0.0f);
    TestFalse(TEXT("Self invasion rejected"), Accepted(Simulation.Execute(Attacker, TEXT("invade"), TEXT("{\"target\":\"attacker\"}"))));
    if (!TestTrue(TEXT("Server starts a valid invasion"), Accepted(Simulation.Execute(Attacker, TEXT("invade"), TEXT("{\"target\":\"defender\"}"))))) return false;
    TestFalse(TEXT("An engaged defender cannot start another battle"), Accepted(Simulation.Execute(Defender, TEXT("invade"), TEXT("{\"target\":\"attacker\"}"))));
    if (!TestTrue(TEXT("Deploy one monster at a free outer edge"), Accepted(Simulation.Execute(Attacker, TEXT("deploy"), TEXT("{\"type\":\"C1\",\"count\":1,\"x\":39,\"y\":39}"))))) return false;
    TestTrue(TEXT("Contested retreat resolves on the host"), Accepted(Simulation.Execute(Attacker, TEXT("retreat"), TEXT("{}"))));
    const FObject Defended = Yard(Simulation, Defender);
    const FObject Retreated = Yard(Simulation, Attacker);
    TestEqual(TEXT("Defense winner receives configured RP"), Number(Defended, TEXT("rp")), 25.0);
    TestEqual(TEXT("Losing zero-RP attacker is clamped to zero"), Number(Retreated, TEXT("rp")), 0.0);
    TestEqual(TEXT("Defender receives exactly a one-hour shield"), Number(Defended, TEXT("shieldUntil")), Start + 31.0 + 3600.0);
    TestEqual(TEXT("Defense victory is recorded once"), Number(Defended, TEXT("wins")), 1.0);
    TestEqual(TEXT("Retreat is recorded as a loss"), Number(Retreated, TEXT("losses")), 1.0);
    TestEqual(TEXT("Deployment consumes only the deployed troop"), Number(Child(Retreated, TEXT("army")), TEXT("C1")), 1.0);
    TestFalse(TEXT("Repeated result request cannot award more RP"), Accepted(Simulation.Execute(Attacker, TEXT("retreat"), TEXT("{}"))));
    TestFalse(TEXT("Defense shield blocks another invasion"), Accepted(Simulation.Execute(Attacker, TEXT("invade"), TEXT("{\"target\":\"defender\"}"))));
    TestEqual(TEXT("Repeated actions preserve RP"), Number(Yard(Simulation, Defender), TEXT("rp")), 25.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMConstructionProgressionTest, "BackyardMMO.Simulation.ConstructionAndTownHallProgression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMConstructionProgressionTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FBYMSimulation Simulation;
    const FString Player = TEXT("builder");
    if (!InitializeFixture(*this, Simulation) || !AddFixturePlayer(*this, Simulation, Player)) return false;
    FObject Snapshot = Decode(Simulation.Snapshot(Player, FString()));
    FObject Options = Child(Snapshot, TEXT("buildOptions"));
    const FObject Twig = Child(Options, TEXT("1"));
    TestEqual(TEXT("Original TH1 producer quantity is one"), Number(Twig, TEXT("limit")), 1.0);
    TestEqual(TEXT("Starter producer counts toward that quantity"), Number(Twig, TEXT("owned")), 1.0);
    TestFalse(TEXT("UI cannot offer placement for a capped producer"), Flag(Twig, TEXT("canBuild")));
    TestEqual(TEXT("Next producer unlock is Town Hall 2"), Number(Twig, TEXT("nextTownHallLevel")), 2.0);
    const FObject Denied = Decode(Simulation.Execute(Player, TEXT("build"), TEXT("{\"type\":1,\"x\":1,\"y\":30}")));
    TestEqual(TEXT("Host rejection matches the UI's authoritative reason"), String(Denied, TEXT("error")), String(Twig, TEXT("reason")));
    FObject Town = Building(Child(Snapshot, TEXT("yard")), 14);
    if (!TestTrue(TEXT("Starter Town Hall exists"), Town.IsValid())) return false;
    TestTrue(TEXT("Unaffordable upgrade remains visible"), Flag(Town, TEXT("upgradeAvailable")));
    TestFalse(TEXT("Initial wallet cannot pay TH2's original cost"), Flag(Town, TEXT("canUpgrade")));
    TestEqual(TEXT("Town Hall upgrade retains original Twigs cost"), Number(Child(Town, TEXT("upgradeCosts")), TEXT("twigs")), 7000.0);
    TestEqual(TEXT("Town Hall upgrade retains original Pebbles cost"), Number(Child(Town, TEXT("upgradeCosts")), TEXT("pebbles")), 7000.0);
    TestTrue(TEXT("Unaffordable upgrade explains the missing resources"), String(Town, TEXT("upgradeReason")).Contains(TEXT("2000")));
    TestTrue(TEXT("Map Room prerequisite can be constructed"), Child(Options, TEXT("11")).IsValid());
    TestTrue(TEXT("A valid additional tower is offered at Town Hall 1"), Flag(Child(Options, TEXT("21")), TEXT("canBuild")));

    // Earn the original upgrade cost through the actual producer/collection loop.
    double Now = Number(Snapshot, TEXT("now"));
    for (int32 Hour = 0; Hour < 3; ++Hour)
    {
        Now += 3600.0; Simulation.Tick(Now, 0.0f);
        if (!TestTrue(TEXT("Collect producer output"), Accepted(Simulation.Execute(Player, TEXT("collect"), TEXT("{}"))))) return false;
    }
    Town = Building(Yard(Simulation, Player), 14);
    TestTrue(TEXT("Earning enough resources enables the upgrade"), Flag(Town, TEXT("canUpgrade")));
    const FString Upgrade = FString::Printf(TEXT("{\"building\":\"%s\"}"), *String(Town, TEXT("id")));
    if (!TestTrue(TEXT("Host starts Town Hall upgrade"), Accepted(Simulation.Execute(Player, TEXT("upgrade"), Upgrade)))) return false;
    Now = Number(Building(Yard(Simulation, Player), 14), TEXT("workReadyAt")) + .01; Simulation.Tick(Now, 0.0f);
    TestEqual(TEXT("Town Hall completes at level 2"), Number(Building(Yard(Simulation, Player), 14), TEXT("level")), 2.0);
    Now += 3600.0; Simulation.Tick(Now, 0.0f);
    TestTrue(TEXT("Collect resources for the second producer"), Accepted(Simulation.Execute(Player, TEXT("collect"), TEXT("{}"))));
    Snapshot = Decode(Simulation.Snapshot(Player, FString())); Options = Child(Snapshot, TEXT("buildOptions"));
    TestEqual(TEXT("TH2 unlocks a second producer"), Number(Child(Options, TEXT("1")), TEXT("limit")), 2.0);
    TestTrue(TEXT("Affordable, unlocked construction can be selected"), Flag(Child(Options, TEXT("1")), TEXT("canBuild")));
    const int32 Before = Array(Child(Snapshot, TEXT("yard")), TEXT("buildings")).Num();
    if (!TestTrue(TEXT("Valid ground click starts the second producer"), Accepted(Simulation.Execute(Player, TEXT("build"), TEXT("{\"type\":1,\"x\":1,\"y\":30}"))))) return false;
    TestEqual(TEXT("Construction creates one authoritative building"), Array(Yard(Simulation, Player), TEXT("buildings")).Num(), Before + 1);
    Options = Child(Decode(Simulation.Snapshot(Player, FString())), TEXT("buildOptions"));
    TestEqual(TEXT("Unfinished construction reserves its quantity"), Number(Child(Options, TEXT("1")), TEXT("owned")), 2.0);
    const FObject PendingYard = Yard(Simulation, Player);
    for (const auto& Value : Array(PendingYard, TEXT("buildings")))
        if (Number(Value->AsObject(), TEXT("type")) == 1 && Number(Value->AsObject(), TEXT("level")) == 0)
            Now = Number(Value->AsObject(), TEXT("workReadyAt")) + .01;
    Simulation.Tick(Now, 0.0f);
    const FObject Completed = Yard(Simulation, Player);
    int32 CompletedTwigs = 0;
    for (const TSharedPtr<FJsonValue>& Value : Array(Completed, TEXT("buildings")))
        if (Number(Value->AsObject(), TEXT("type")) == 1 && Number(Value->AsObject(), TEXT("level")) == 1) ++CompletedTwigs;
    TestEqual(TEXT("Both producer buildings are completed and usable"), CompletedTwigs, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMDeploymentLifecycleTest, "BackyardMMO.Simulation.DeploymentDeadlineAndReserves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMDeploymentLifecycleTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FBYMSimulation Simulation;
    const FString Attacker = TEXT("late-attacker"), Defender = TEXT("late-defender");
    if (!InitializeFixture(*this, Simulation) || !AddFixturePlayer(*this, Simulation, Attacker)
        || !AddFixturePlayer(*this, Simulation, Defender)) return false;
    const double Start = Number(Decode(Simulation.Snapshot(Attacker, FString())), TEXT("now"));
    if (!TestTrue(TEXT("Queue two invasion monsters"), Accepted(Simulation.Execute(Attacker, TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":2}"))))) return false;
    Simulation.Tick(Start + 31.0, 0.0f);
    const FString Invasion = FString::Printf(TEXT("{\"target\":\"%s\"}"), *Defender);
    if (!TestTrue(TEXT("Begin invasion"), Accepted(Simulation.Execute(Attacker, TEXT("invade"), Invasion)))) return false;
    const FObject InitialBattle = Child(Decode(Simulation.Snapshot(Attacker, FString())), TEXT("battle"));
    TestEqual(TEXT("Invasion uses the full visible battle duration"), Number(InitialBattle, TEXT("endsAt")), Start + 331.0);
    Simulation.Tick(Start + 76.0, .25f); // 45 seconds spent selecting an army.
    const FObject Snapshot = Decode(Simulation.Snapshot(Attacker, Attacker));
    const FObject Battle = Child(Snapshot, TEXT("battle"));
    if (!TestTrue(TEXT("Invasion survives the old hidden 30-second deadline"), Battle.IsValid())) return false;
    TestEqual(TEXT("Preparing army is an explicit deployment phase"), String(Battle, TEXT("phase")), FString(TEXT("deploying")));
    TestTrue(TEXT("Ready reserves remain deployable"), Flag(Battle, TEXT("canDeploy")));
    TestEqual(TEXT("A home-view request cannot change an active invasion target"), String(Child(Snapshot, TEXT("yard")), TEXT("id")), Defender);
    if (!TestTrue(TEXT("First deployment works after 30 seconds"), Accepted(Simulation.Execute(Attacker, TEXT("deploy"), TEXT("{\"type\":\"C1\",\"count\":1,\"x\":39,\"y\":39}"))))) return false;

    // Restore a legitimate battle with its first wave defeated. Dead troops are
    // valid persisted state; the undeployed reserve must keep the battle open.
    FObject Save = Simulation.Serialize();
    if (!TestTrue(TEXT("Saved invasion contains its deployed troop"), !Array(Save, TEXT("battles")).IsEmpty()
        && !Array(Array(Save, TEXT("battles"))[0]->AsObject(), TEXT("troops")).IsEmpty())) return false;
    Array(Array(Save, TEXT("battles"))[0]->AsObject(), TEXT("troops"))[0]->AsObject()->SetNumberField(TEXT("hp"), 0);
    FBYMSimulation Reloaded;
    FString Error;
    if (!TestTrue(TEXT("Reload defeated first-wave fixture"), Reloaded.Initialize(Save, Error))) { AddError(Error); return false; }
    Reloaded.Tick(Start + 77.0, .25f);
    TestTrue(TEXT("Undeployed reserve prevents premature defeat"), Child(Decode(Reloaded.Snapshot(Attacker, FString())), TEXT("battle")).IsValid());
    if (!TestTrue(TEXT("Player can deploy a second wave"), Accepted(Reloaded.Execute(Attacker, TEXT("deploy"), TEXT("{\"type\":\"C1\",\"count\":1,\"x\":39,\"y\":39}"))))) return false;
    Save = Reloaded.Serialize();
    for (const TSharedPtr<FJsonValue>& Troop : Array(Array(Save, TEXT("battles"))[0]->AsObject(), TEXT("troops"))) Troop->AsObject()->SetNumberField(TEXT("hp"), 0);
    FBYMSimulation FinalWave;
    if (!TestTrue(TEXT("Reload defeated final-wave fixture"), FinalWave.Initialize(Save, Error))) { AddError(Error); return false; }
    FinalWave.Tick(Start + 78.0, .25f);
    TestFalse(TEXT("Defeat resolves once no deployed or reserve troops remain"), Child(Decode(FinalWave.Snapshot(Attacker, FString())), TEXT("battle")).IsValid());
    TestEqual(TEXT("Both committed monsters were consumed once"), Number(Child(Yard(FinalWave, Attacker), TEXT("army")), TEXT("C1")), 0.0);

    FBYMSimulation Expiry;
    if (!InitializeFixture(*this, Expiry) || !AddFixturePlayer(*this, Expiry, Attacker) || !AddFixturePlayer(*this, Expiry, Defender)) return false;
    const double ExpiryStart = Number(Decode(Expiry.Snapshot(Attacker, FString())), TEXT("now"));
    if (!TestTrue(TEXT("Train expiry fixture army"), Accepted(Expiry.Execute(Attacker, TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":1}"))))) return false;
    Expiry.Tick(ExpiryStart + 16.0, 0.0f);
    if (!TestTrue(TEXT("Start expiry fixture invasion"), Accepted(Expiry.Execute(Attacker, TEXT("invade"), Invasion)))) return false;
    Expiry.Tick(ExpiryStart + 317.0, .25f);
    const FObject Expired = Decode(Expiry.Snapshot(Attacker, FString()));
    TestFalse(TEXT("Full battle deadline still bounds uncommitted invasions"), Child(Expired, TEXT("battle")).IsValid());
    TestFalse(TEXT("Uncommitted expiry is not a ranked loss"), Flag(Child(Expired, TEXT("lastBattle")), TEXT("contested")));
    TestTrue(TEXT("Expiry reason is visible instead of silently losing invasion state"), String(Child(Expired, TEXT("lastBattle")), TEXT("reason")).Contains(TEXT("expired")));
    TestEqual(TEXT("Uncommitted expiry preserves the army"), Number(Child(Child(Expired, TEXT("home")), TEXT("army")), TEXT("C1")), 1.0);
    TestEqual(TEXT("Uncommitted expiry grants no defender shield"), Number(Yard(Expiry, Defender), TEXT("shieldUntil")), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMInvalidSaveTest, "BackyardMMO.Simulation.RejectInvalidSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMInvalidSaveTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    FBYMSimulation Original;
    const FString Player = TEXT("saved-player");
    if (!InitializeFixture(*this, Original) || !AddFixturePlayer(*this, Original, Player)) return false;
    FObject Invalid = Original.Serialize();
    const FObject InvalidWallet = Child(SavedYard(Invalid, Player), TEXT("resources"));
    if (!TestTrue(TEXT("Saved yard includes a resource wallet"), InvalidWallet.IsValid())) return false;
    InvalidWallet->SetNumberField(TEXT("goo"), -1);
    FBYMSimulation NegativeWallet;
    FString Error;
    TestFalse(TEXT("Negative wallet fails loading instead of reseeding a world"), NegativeWallet.Initialize(Invalid, Error));
    TestFalse(TEXT("Failed load provides a recoverable error"), Error.IsEmpty());
    Invalid = Original.Serialize();
    FArray Duplicates = Array(Invalid, TEXT("yards"));
    Duplicates.Add(Duplicates[0]);
    Invalid->SetArrayField(TEXT("yards"), Duplicates);
    FBYMSimulation DuplicateYard;
    TestFalse(TEXT("Duplicate persisted identity is rejected"), DuplicateYard.Initialize(Invalid, Error));
    TestTrue(TEXT("Invalid save checks did not mutate the live model"), Original.HasYard(Player));
    TestEqual(TEXT("Live wallet remains intact"), Number(Child(Yard(Original, Player), TEXT("resources")), TEXT("goo")), 5000.0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
