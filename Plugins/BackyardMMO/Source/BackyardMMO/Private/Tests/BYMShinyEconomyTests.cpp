#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMShinyEconomyTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("shiny-owner")), Other(TEXT("shiny-other"));
    struct FSettingsGuard
    {
        UBYMSettings* S = GetMutableDefault<UBYMSettings>();
        const int32 Bots = S->RivalBotCount, Cells = S->YardCells, Starting = S->StartingResources;
        const float Construction = S->ConstructionTimeMultiplier, Production = S->ResourceProductionMultiplier;
        FSettingsGuard()
        {
            S->RivalBotCount = 0; S->YardCells = 40; S->StartingResources = 5000;
            S->ConstructionTimeMultiplier = 1; S->ResourceProductionMultiplier = 0;
        }
        ~FSettingsGuard()
        {
            S->RivalBotCount = Bots; S->YardCells = Cells; S->StartingResources = Starting;
            S->ConstructionTimeMultiplier = Construction; S->ResourceProductionMultiplier = Production;
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
    double Number(const FObject& V, const TCHAR* Key, double Missing = 0)
    { double R = Missing; if (V) V->TryGetNumberField(Key, R); return R; }
    FString String(const FObject& V, const TCHAR* Key)
    { FString R; if (V) V->TryGetStringField(Key, R); return R; }
    bool Flag(const FObject& V, const TCHAR* Key)
    { bool R = false; if (V) V->TryGetBoolField(Key, R); return R; }
    FObject Snapshot(const FBYMSimulation& Sim, const FString& Id = Owner)
    { return Decode(Sim.Snapshot(Id, FString())); }
    FObject Home(const FBYMSimulation& Sim, const FString& Id = Owner)
    { return Child(Snapshot(Sim, Id), TEXT("yard")); }
    FObject Shop(const FBYMSimulation& Sim, const FString& Id = Owner)
    { return Child(Child(Snapshot(Sim, Id), TEXT("home")), TEXT("shop")); }
    FObject SavedYard(const FObject& Save, const FString& Id = Owner)
    { for (const auto& V : Array(Save, TEXT("yards"))) if (String(V->AsObject(), TEXT("id")) == Id) return V->AsObject(); return nullptr; }
    FObject Building(const FObject& Yard, int32 Type)
    { for (const auto& V : Array(Yard, TEXT("buildings"))) if (Number(V->AsObject(), TEXT("type")) == Type) return V->AsObject(); return nullptr; }
    FObject Product(const FObject& Store, const FString& Code)
    { for (const auto& V : Array(Store, TEXT("products"))) if (String(V->AsObject(), TEXT("code")) == Code) return V->AsObject(); return nullptr; }
    bool Accepted(const FString& Reply) { return Flag(Decode(Reply), TEXT("ok")); }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Sim)
    {
        FString Error;
        if (!Sim.Initialize(nullptr, Error) || !Sim.AddPlayer(Owner, Owner, Error) || !Sim.AddPlayer(Other, Other, Error))
        { Test.AddError(Error); return false; } return true;
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Sim, const FObject& Save)
    { FString Error; if (!Sim.Initialize(Save, Error)) { Test.AddError(Error); return false; } return true; }
    FString Purchase(const FString& Code, int32 Level)
    {
        FObject P = Obj(); P->SetStringField(TEXT("code"), Code); P->SetNumberField(TEXT("expectedLevel"), Level); return Encode(P);
    }
    FString Speedup(const FObject& B, const FString& Code, int32 Revision, int32 MaximumPrice)
    {
        FObject P = Obj(); P->SetStringField(TEXT("building"), String(B, TEXT("id")));
        P->SetStringField(TEXT("job"), String(B, TEXT("jobId"))); P->SetStringField(TEXT("code"), Code);
        P->SetNumberField(TEXT("revision"), Revision); P->SetNumberField(TEXT("maxPrice"), MaximumPrice); return Encode(P);
    }
    bool ConstructStore(FAutomationTestBase& Test, FBYMSimulation& Sim)
    {
        // Use the real host construction path. Find an available site rather than
        // assuming procedurally placed mushrooms cannot occupy a fixed cell.
        for (int32 Y = 0; Y <= 34; Y += 4) for (int32 X = 0; X <= 34; X += 4)
        {
            const FString P = FString::Printf(TEXT("{\"type\":12,\"x\":%d,\"y\":%d}"), X, Y);
            if (Accepted(Sim.Execute(Owner, TEXT("build"), P)))
            {
                Sim.Tick(Number(Snapshot(Sim), TEXT("now")) + 600., 0);
                return Test.TestEqual(TEXT("General Store completes through actual worker construction"), Number(Building(Home(Sim), 12), TEXT("level")), 1.);
            }
        }
        Test.AddError(TEXT("Could not construct the source General Store in any sampled free location.")); return false;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMShinyStoreAuthorityTest, "BackyardMMO.Shiny.StoreAuthorityAndReplay",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMShinyStoreAuthorityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Sim; if (!Start(*this, Sim)) return false;
        TestEqual(TEXT("The supplied normal source account starts with its configured Shiny"), Number(Shop(Sim), TEXT("shiny")), 1500.);
        const double OtherWallet = Number(Shop(Sim, Other), TEXT("shiny"));
        TestFalse(TEXT("Store purchase requires a completed General Store"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BEW"), 0))));
        TestEqual(TEXT("Rejected purchase retains the wallet"), Number(Shop(Sim), TEXT("shiny")), 1500.);
        if (!ConstructStore(*this, Sim)) return false;
        TestEqual(TEXT("First extra worker uses the original 250 Shiny price"), Number(Product(Shop(Sim), TEXT("BEW")), TEXT("price")), 250.);
        TestTrue(TEXT("Host purchases the first permanent extra worker"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BEW"), 0))));
        TestEqual(TEXT("Purchase charges exactly the source price"), Number(Shop(Sim), TEXT("shiny")), 1250.);
        TestEqual(TEXT("Purchase grants exactly one extra worker"), Number(Shop(Sim), TEXT("workersPurchased")), 1.);
        TestFalse(TEXT("Replayed previous-tier purchase cannot charge or grant twice"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BEW"), 0))));
        TestEqual(TEXT("Replay leaves Shiny unchanged"), Number(Shop(Sim), TEXT("shiny")), 1250.);
        TestFalse(TEXT("Fractional purchase tier is not rounded"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), TEXT("{\"code\":\"BEW\",\"expectedLevel\":1.5}"))));
        TestFalse(TEXT("Missing expected tier cannot be inferred"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), TEXT("{\"code\":\"BEW\"}"))));
        TestFalse(TEXT("Unknown products are rejected"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("CLIENT_FREE_SHINY"), 1))));
        TestTrue(TEXT("Second extra worker uses its next source tier"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BEW"), 1))));
        TestEqual(TEXT("Second tier charges 500 Shiny"), Number(Shop(Sim), TEXT("shiny")), 750.);
        TestFalse(TEXT("Insufficient funds cannot buy the 1000 Shiny tier"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), TEXT("{\"code\":\"BEW\",\"expectedLevel\":2,\"price\":0}"))));
        TestEqual(TEXT("Client-provided prices cannot change the authoritative charge"), Number(Shop(Sim), TEXT("shiny")), 750.);
        TestEqual(TEXT("Another player's wallet is untouched"), Number(Shop(Sim, Other), TEXT("shiny")), OtherWallet);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestEqual(TEXT("Shiny persists across host snapshot reload"), Number(Shop(Reloaded), TEXT("shiny")), 750.);
        TestEqual(TEXT("Permanent purchased workers persist once"), Number(Shop(Reloaded), TEXT("workersPurchased")), 2.);
        const FObject Foreign = Child(Decode(Reloaded.Snapshot(Other, Owner)), TEXT("yard"));
        TestFalse(TEXT("Foreign yard presentation omits the private store wallet"), Child(Foreign, TEXT("shop")).IsValid());
        TestEqual(TEXT("Foreign yard presentation omits raw Shiny"), Number(Foreign, TEXT("shiny"), -1), -1.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMShinyExpansionAuthorityTest, "BackyardMMO.Shiny.ExpansionPriceSequenceAndCap",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMShinyExpansionAuthorityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Sim; if (!Start(*this, Sim) || !ConstructStore(*this, Sim)) return false;
        double Wallet = Number(Shop(Sim), TEXT("shiny"));
        for (int32 Tier = 0; Tier < 6; ++Tier)
        {
            const double Cost = (Tier + 1) * 50.;
            TestEqual(TEXT("Expansion advertises the source tier price"), Number(Product(Shop(Sim), TEXT("ENL")), TEXT("price")), Cost);
            TestTrue(TEXT("Expansion purchase is accepted at its exact current tier"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("ENL"), Tier))));
            Wallet -= Cost;
            TestEqual(TEXT("Expansion charges once"), Number(Shop(Sim), TEXT("shiny")), Wallet);
            TestEqual(TEXT("Expansion tier increases by one"), Number(Shop(Sim), TEXT("expansionLevel")), static_cast<double>(Tier + 1));
            TestFalse(TEXT("A duplicated expansion request is stale"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("ENL"), Tier))));
            TestEqual(TEXT("Stale expansion cannot remove more Shiny"), Number(Shop(Sim), TEXT("shiny")), Wallet);
        }
        TestFalse(TEXT("The seventh expansion cannot exceed the source six-tier cap"), Accepted(Sim.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("ENL"), 6))));
        TestFalse(TEXT("Capped expansion is unavailable in its host quote"), Flag(Product(Shop(Sim), TEXT("ENL")), TEXT("canBuy")));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestEqual(TEXT("All six expansions survive reload"), Number(Shop(Reloaded), TEXT("expansionLevel")), 6.);
        TestEqual(TEXT("Expansion reload cannot repeat charges or refund"), Number(Shop(Reloaded), TEXT("shiny")), Wallet);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMushroomRewardAuthorityTest, "BackyardMMO.Shiny.MushroomRewardsArePrivateAndExactlyOnce",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMushroomRewardAuthorityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        TestEqual(TEXT("Fresh main yards retain the original twenty-mushroom seed"), Array(Child(SavedYard(Seed.Serialize()), TEXT("economy")), TEXT("mushrooms")).Num(), 20);
        for (const int32 Reward : {0, 3, 8})
        {
            FObject Save = Seed.Serialize(), Yard = SavedYard(Save), Economy = Child(Yard, TEXT("economy"));
            if (!TestTrue(TEXT("Mushroom state is present in the host snapshot"), Economy.IsValid() && !Array(Economy, TEXT("mushrooms")).IsEmpty())) return false;
            FObject Mushroom = Clone(Array(Economy, TEXT("mushrooms"))[0]->AsObject());
            Mushroom->SetNumberField(TEXT("frame"), Reward ? 5 : 1); Mushroom->SetNumberField(TEXT("reward"), Reward);
            Mushroom->SetBoolField(TEXT("picking"), false); Mushroom->SetNumberField(TEXT("pickFinishAt"), 0);
            FArray Mushrooms; Mushrooms.Add(MakeShared<FJsonValueObject>(Mushroom)); Economy->SetArrayField(TEXT("mushrooms"), Mushrooms);
            Economy->SetNumberField(TEXT("shiny"), 100); Economy->SetNumberField(TEXT("mushroomsPicked"), 0);
            Economy->SetNumberField(TEXT("goldenMushroomsPicked"), 0); Economy->SetNumberField(TEXT("mushroomShinyEarned"), 0);
            const FString Id = String(Mushroom, TEXT("id"));
            FObject P = Obj(); P->SetStringField(TEXT("id"), Id); P->SetNumberField(TEXT("reward"), 999999);
            const FString Request = Encode(P);
            FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
            TestFalse(TEXT("Another owner cannot collect this mushroom"), Accepted(Sim.Execute(Other, TEXT("clearmushroom"), Request)));
            TestTrue(TEXT("Owned mushroom reserves a worker through the host"), Accepted(Sim.Execute(Owner, TEXT("clearmushroom"), Request)));
            TestEqual(TEXT("Starting work does not award Shiny early"), Number(Shop(Sim), TEXT("shiny")), 100.);
            TestFalse(TEXT("A duplicate pick cannot reserve another worker or award currency"), Accepted(Sim.Execute(Owner, TEXT("clearmushroom"), Request)));
            const FObject During = Sim.Serialize(), DuringEconomy = Child(SavedYard(During), TEXT("economy"));
            TestEqual(TEXT("The private source reward is persisted before completion"), Number(Array(DuringEconomy, TEXT("mushrooms"))[0]->AsObject(), TEXT("reward")), static_cast<double>(Reward));
            const FObject Public = Child(Decode(Sim.Snapshot(Other, Owner)), TEXT("yard"));
            for (const auto& V : Array(Public, TEXT("mushrooms")))
                TestEqual(TEXT("Visitors cannot read private future mushroom rewards"), Number(V->AsObject(), TEXT("reward"), -1), -1.);
            FBYMSimulation Reloaded; if (!Load(*this, Reloaded, During)) return false;
            Reloaded.Tick(Number(Snapshot(Reloaded), TEXT("now")) + 600., 0);
            TestEqual(TEXT("Completion grants only the persisted source reward, ignoring client reward"), Number(Shop(Reloaded), TEXT("shiny")), 100. + Reward);
            const FObject Finished = Reloaded.Serialize(), FinishedEconomy = Child(SavedYard(Finished), TEXT("economy"));
            TestEqual(TEXT("Completed mushroom is removed once"), Array(FinishedEconomy, TEXT("mushrooms")).Num(), 0);
            TestEqual(TEXT("Completed pick increments its source statistic once"), Number(FinishedEconomy, TEXT("mushroomsPicked")), 1.);
            TestEqual(TEXT("Golden statistic matches the actual golden mushroom"), Number(FinishedEconomy, TEXT("goldenMushroomsPicked")), Reward ? 1. : 0.);
            TestEqual(TEXT("Earned Shiny statistic includes only the genuine reward"), Number(FinishedEconomy, TEXT("mushroomShinyEarned")), static_cast<double>(Reward));
            TestFalse(TEXT("Replaying a completed pick cannot mint Shiny"), Accepted(Reloaded.Execute(Owner, TEXT("clearmushroom"), Request)));
            FBYMSimulation Again; if (!Load(*this, Again, Finished)) return false;
            Again.Tick(Number(Snapshot(Again), TEXT("now")) + 600., 0);
            TestEqual(TEXT("Repeated reload and time advancement cannot re-award the pick"), Number(Shop(Again), TEXT("shiny")), 100. + Reward);
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMDecorationAuthorityTest, "BackyardMMO.Shiny.OriginalDecorationCatalogAndAtomicPlacement",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMDecorationAuthorityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Sim; if (!Start(*this, Sim) || !ConstructStore(*this, Sim)) return false;
        const FObject InitialShop = Shop(Sim);
        const FArray& Decorations = Array(InitialShop, TEXT("decorations"));
        TestEqual(TEXT("Only the source normal priced unblocked decoration shop is offered"), Decorations.Num(), 73);
        TSet<int32> Types;
        for (const auto& Entry : Decorations)
        {
            const int32 Type = static_cast<int32>(Number(Entry->AsObject(), TEXT("type"))); Types.Add(Type);
            TestTrue(TEXT("Every offered decoration has a positive original price"), Number(Entry->AsObject(), TEXT("price")) > 0);
            if (Type == 55) TestEqual(TEXT("Acorn uses the active source store price rather than stale catalog r5"), Number(Entry->AsObject(), TEXT("price")), 5.);
            if (Type == 49) TestEqual(TEXT("Pirate Flag uses the active source store price"), Number(Entry->AsObject(), TEXT("price")), 125.);
        }
        for (const int32 Blocked : {58, 59, 67, 69, 70, 110, 111, 120, 121, 131, 135})
        {
            TestFalse(TEXT("Blocked and reward-only decorations are absent from the shop"), Types.Contains(Blocked));
            TestFalse(TEXT("Client cannot directly buy a hidden decoration"), Accepted(Sim.Execute(Owner, TEXT("decorate"), FString::Printf(TEXT("{\"type\":%d,\"x\":0,\"y\":0}"), Blocked))));
        }
        const double Wallet = Number(Shop(Sim), TEXT("shiny"));
        TestFalse(TEXT("Free resource build command cannot bypass a decoration's Shiny purchase"), Accepted(Sim.Execute(Owner, TEXT("build"), TEXT("{\"type\":55,\"x\":0,\"y\":0}"))));
        TestFalse(TEXT("Fractional decoration type is rejected"), Accepted(Sim.Execute(Owner, TEXT("decorate"), TEXT("{\"type\":55.1,\"x\":0,\"y\":0}"))));
        TestFalse(TEXT("Fractional placement is rejected"), Accepted(Sim.Execute(Owner, TEXT("decorate"), TEXT("{\"type\":55,\"x\":0.5,\"y\":0}"))));
        TestFalse(TEXT("Outside-yard decoration is rejected"), Accepted(Sim.Execute(Owner, TEXT("decorate"), TEXT("{\"type\":55,\"x\":500,\"y\":500}"))));
        TestEqual(TEXT("Rejected shop actions do not spend Shiny"), Number(Shop(Sim), TEXT("shiny")), Wallet);
        const FString Twig = String(Building(Home(Sim), 1), TEXT("id"));
        if (!TestTrue(TEXT("A real building upgrade occupies the available worker"), Accepted(Sim.Execute(Owner, TEXT("upgrade"), FString::Printf(TEXT("{\"building\":\"%s\"}"), *Twig))))) return false;
        FString Placement;
        for (int32 Y = 0; Y < 38 && Placement.IsEmpty(); Y += 3) for (int32 X = 0; X < 48 && Placement.IsEmpty(); X += 3)
        {
            const FString P = FString::Printf(TEXT("{\"type\":55,\"x\":%d,\"y\":%d,\"price\":0}"), X, Y);
            if (Accepted(Sim.Execute(Owner, TEXT("decorate"), P))) Placement = P;
        }
        if (!TestFalse(TEXT("Source zero-time decoration can be placed while workers are busy"), Placement.IsEmpty())) return false;
        TestEqual(TEXT("Successful decoration costs exactly five Shiny"), Number(Shop(Sim), TEXT("shiny")), Wallet - 5.);
        const FObject Decoration = Building(Home(Sim), 55);
        TestEqual(TEXT("Decoration is fully placed immediately"), Number(Decoration, TEXT("level")), 1.);
        TestTrue(TEXT("Decoration does not create a construction worker job"), String(Decoration, TEXT("jobId")).IsEmpty());
        TestFalse(TEXT("Replaying placement on the occupied site does not buy another decoration"), Accepted(Sim.Execute(Owner, TEXT("decorate"), Placement)));
        TestEqual(TEXT("Failed overlapping placement is not charged"), Number(Shop(Sim), TEXT("shiny")), Wallet - 5.);
        const FString DecorationId = String(Decoration, TEXT("id"));
        TestFalse(TEXT("Another player cannot move an owned purchased decoration"), Accepted(Sim.Execute(Other, TEXT("move"), FString::Printf(TEXT("{\"building\":\"%s\",\"x\":1,\"y\":1}"), *DecorationId))));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestEqual(TEXT("Purchased decoration identity survives reload without duplication"), String(Building(Home(Reloaded), 55), TEXT("id")), DecorationId);
        TestEqual(TEXT("Decoration reload retains its paid wallet"), Number(Shop(Reloaded), TEXT("shiny")), Wallet - 5.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMGeneralStoreHealthAndPackingTest, "BackyardMMO.Shiny.GeneralStoreHealthAndPacking",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMGeneralStoreHealthAndPackingTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed) || !ConstructStore(*this, Seed)) return false;
        FObject Save = Seed.Serialize(); Building(SavedYard(Save), 12)->SetNumberField(TEXT("hp"), 1999);
        FBYMSimulation Damaged; if (!Load(*this, Damaged, Save)) return false;
        TestFalse(TEXT("Below-half-health General Store cannot authorize purchases"), Accepted(Damaged.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BIP"), 0))));
        TestEqual(TEXT("Closed damaged store cannot charge Shiny"), Number(Shop(Damaged), TEXT("shiny")), 1500.);
        Building(SavedYard(Save), 12)->SetNumberField(TEXT("hp"), 2000);
        FBYMSimulation HalfHealth; if (!Load(*this, HalfHealth, Save)) return false;
        const double Capacity = Number(Home(HalfHealth), TEXT("storageCapacity"));
        TestTrue(TEXT("Exactly half-health source store permits a purchase"), Accepted(HalfHealth.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BIP"), 0))));
        TestEqual(TEXT("First packing improvement costs the original fifty Shiny"), Number(Shop(HalfHealth), TEXT("shiny")), 1450.);
        TestTrue(TEXT("Packing increases actual resource capacity by ten percent"), FMath::IsNearlyEqual(Number(Home(HalfHealth), TEXT("storageCapacity")), Capacity * 1.1, .001));
        TestFalse(TEXT("Packing purchase replay does not reapply the multiplier"), Accepted(HalfHealth.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BIP"), 0))));
        TestTrue(TEXT("Second packing improvement advances the original next tier"), Accepted(HalfHealth.Execute(Owner, TEXT("shopBuy"), Purchase(TEXT("BIP"), 1))));
        TestTrue(TEXT("Packing bonuses add by source tier rather than compound"), FMath::IsNearlyEqual(Number(Home(HalfHealth), TEXT("storageCapacity")), Capacity * 1.2, .001));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, HalfHealth.Serialize())) return false;
        TestEqual(TEXT("Packing tier persists"), Number(Shop(Reloaded), TEXT("packingLevel")), 2.);
        TestEqual(TEXT("Reload preserves actual improved resource capacity"), Number(Home(Reloaded), TEXT("storageCapacity")), Number(Home(HalfHealth), TEXT("storageCapacity")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMSourceSpeedupBoundaryTest, "BackyardMMO.Shiny.SourceSpeedupThresholdsAndPrices",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMSourceSpeedupBoundaryTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        struct FCase { double Remaining; const TCHAR* Code; bool Allowed; int32 Cost; };
        const FCase Cases[] = {
            {300., TEXT("SP1"), true, 0}, {300.01, TEXT("SP1"), false, 0},
            {3599., TEXT("SP2"), false, 20}, {3600., TEXT("SP2"), true, 20},
            {7199., TEXT("SP3"), false, 40}, {7200., TEXT("SP3"), true, 40},
            {300., TEXT("SP4"), false, 0}, {301., TEXT("SP4"), true, 2},
            {7201., TEXT("SP4"), true, 41}, {100000., TEXT("SP4"), true, 282}
        };
        for (const FCase& Case : Cases)
        {
            FObject Save = Seed.Serialize(), Yard = SavedYard(Save), B = Building(Yard, 14);
            // A synthetic pre-worker paid job freezes this test exactly at each
            // source boundary without depending on native wall-clock fractions.
            Yard->RemoveField(TEXT("workerState")); Yard->SetNumberField(TEXT("lastEconomy"), Number(Snapshot(Seed), TEXT("now")) + 60.);
            B->SetNumberField(TEXT("targetLevel"), 2); B->SetStringField(TEXT("jobId"), TEXT("source-speedup-boundary"));
            B->SetNumberField(TEXT("workRemaining"), Case.Remaining); B->SetNumberField(TEXT("speedupRevision"), 0);
            FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
            const FString Request = Speedup(B, Case.Code, 0, Case.Cost);
            TestFalse(TEXT("A foreign player cannot purchase completion of this job"), Accepted(Sim.Execute(Other, TEXT("shopSpeedup"), Request)));
            TestEqual(TEXT("Host enforces the exact original speedup boundary"), Accepted(Sim.Execute(Owner, TEXT("shopSpeedup"), Request)), Case.Allowed);
            TestEqual(TEXT("Speedup charges only the source price when accepted"), Number(Shop(Sim), TEXT("shiny")), 1500. - (Case.Allowed ? Case.Cost : 0));
            if (Case.Allowed)
            {
                TestEqual(TEXT("Threshold cases finish their selected paid job"), Number(Building(Home(Sim), 14), TEXT("level")), 2.);
                TestFalse(TEXT("Finished paid-job request cannot charge or complete again"), Accepted(Sim.Execute(Owner, TEXT("shopSpeedup"), Request)));
            }
            else TestEqual(TEXT("Rejected speedup preserves remaining paid work"), Number(Building(Home(Sim), 14), TEXT("workRemaining")), Case.Remaining);
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMSpeedupReplayAndWorkerTest, "BackyardMMO.Shiny.SpeedupRevisionQuoteAndTravellingWorker",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMSpeedupReplayAndWorkerTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        FObject Save = Seed.Serialize(), Yard = SavedYard(Save), Town = Building(Yard, 14);
        Yard->RemoveField(TEXT("workerState")); Yard->SetNumberField(TEXT("lastEconomy"), Number(Snapshot(Seed), TEXT("now")) + 60.);
        Town->SetNumberField(TEXT("targetLevel"), 2); Town->SetStringField(TEXT("jobId"), TEXT("source-speedup-revision"));
        Town->SetNumberField(TEXT("workRemaining"), 7201); Town->SetNumberField(TEXT("speedupRevision"), 0);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        TestFalse(TEXT("A lower quoted maximum cannot authorize a higher charge"), Accepted(Sim.Execute(Owner, TEXT("shopSpeedup"), Speedup(Town, TEXT("SP2"), 0, 19))));
        TestFalse(TEXT("A future revision is not a current paid-job quote"), Accepted(Sim.Execute(Owner, TEXT("shopSpeedup"), Speedup(Town, TEXT("SP2"), 1, 20))));
        const FString FirstRequest = Speedup(Town, TEXT("SP2"), 0, 20);
        TestTrue(TEXT("One-hour purchase subtracts one source hour"), Accepted(Sim.Execute(Owner, TEXT("shopSpeedup"), FirstRequest)));
        TestEqual(TEXT("Partial speedup retains precise uncompleted work"), Number(Building(Home(Sim), 14), TEXT("workRemaining")), 3601.);
        TestEqual(TEXT("Partial speedup increments its replay guard"), Number(Building(Home(Sim), 14), TEXT("speedupRevision")), 1.);
        TestFalse(TEXT("Same revision cannot buy the same partial reduction twice"), Accepted(Sim.Execute(Owner, TEXT("shopSpeedup"), FirstRequest)));
        TestEqual(TEXT("Replay retains the first purchase's wallet"), Number(Shop(Sim), TEXT("shiny")), 1480.);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestFalse(TEXT("Old revision remains stale after host reload"), Accepted(Reloaded.Execute(Owner, TEXT("shopSpeedup"), FirstRequest)));
        TestTrue(TEXT("New revision permits the second explicit hour purchase"), Accepted(Reloaded.Execute(Owner, TEXT("shopSpeedup"), Speedup(Town, TEXT("SP2"), 1, 20))));
        TestEqual(TEXT("Two purchases subtract exactly two hours"), Number(Building(Home(Reloaded), 14), TEXT("workRemaining")), 1.);
        TestTrue(TEXT("The final source Close Enough completion is free"), Accepted(Reloaded.Execute(Owner, TEXT("shopSpeedup"), Speedup(Town, TEXT("SP1"), 2, 0))));
        TestEqual(TEXT("Only the two paid hour purchases charged Shiny"), Number(Shop(Reloaded), TEXT("shiny")), 1460.);

        FBYMSimulation Walking; if (!Start(*this, Walking)) return false;
        bool Began = false;
        for (int32 Y = 0; Y <= 34 && !Began; Y += 4) for (int32 X = 0; X <= 44 && !Began; X += 4)
            Began = Accepted(Walking.Execute(Owner, TEXT("build"), FString::Printf(TEXT("{\"type\":12,\"x\":%d,\"y\":%d}"), X, Y)));
        if (!TestTrue(TEXT("Start the first General Store through real construction"), Began)) return false;
        const FObject Store = Building(Home(Walking), 12); const FString Finish = Speedup(Store, TEXT("SP1"), 0, 0);
        TestEqual(TEXT("The first store is still under construction"), Number(Store, TEXT("level")), 0.);
        TestTrue(TEXT("Free source shortcut works before a General Store exists and before arrival"), Accepted(Walking.Execute(Owner, TEXT("shopSpeedup"), Finish)));
        TestEqual(TEXT("Instant completion creates the completed General Store"), Number(Building(Home(Walking), 12), TEXT("level")), 1.);
        TestEqual(TEXT("Free completion never spends Shiny"), Number(Shop(Walking), TEXT("shiny")), 1500.);
        const FObject FinishedWorkers = Child(SavedYard(Walking.Serialize()), TEXT("workerState"));
        TestTrue(TEXT("Forced completion releases the travelling worker"), String(Array(FinishedWorkers, TEXT("entries"))[0]->AsObject(), TEXT("kind")).IsEmpty());
        const double Earned = Number(Child(Child(Snapshot(Walking), TEXT("home")), TEXT("experience")), TEXT("earned"));
        TestFalse(TEXT("Repeated free completion cannot replay XP"), Accepted(Walking.Execute(Owner, TEXT("shopSpeedup"), Finish)));
        TestEqual(TEXT("Completion XP is stable across request replay"), Number(Child(Child(Snapshot(Walking), TEXT("home")), TEXT("experience")), TEXT("earned")), Earned);
        FBYMSimulation FinishedReload; if (!Load(*this, FinishedReload, Walking.Serialize())) return false;
        TestEqual(TEXT("Forced travelling-worker completion writes a valid host snapshot"), Number(Building(Home(FinishedReload), 12), TEXT("level")), 1.);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMushroomGrowthTest, "BackyardMMO.Shiny.MushroomGrowthCadenceCapAndReload",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMushroomGrowthTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        FObject Save = Seed.Serialize(), Yard = SavedYard(Save), Economy = Child(Yard, TEXT("economy"));
        if (!TestTrue(TEXT("Host economic state exists"), Economy.IsValid())) return false;
        const double StartAt = Number(Snapshot(Seed), TEXT("now"));
        Economy->SetArrayField(TEXT("mushrooms"), FArray()); Economy->SetNumberField(TEXT("lastGrowth"), StartAt);
        Economy->SetNumberField(TEXT("mushroomRandom"), 1234567);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        Sim.Tick(StartAt + 17279., 0);
        TestEqual(TEXT("No mushroom grows before the source 4h48m interval"), Array(Child(SavedYard(Sim.Serialize()), TEXT("economy")), TEXT("mushrooms")).Num(), 0);
        Sim.Tick(StartAt + 17280., 0);
        TestEqual(TEXT("One interval grows one mushroom"), Array(Child(SavedYard(Sim.Serialize()), TEXT("economy")), TEXT("mushrooms")).Num(), 1);
        const FObject First = Sim.Serialize();
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, First)) return false;
        Sim.Tick(StartAt + 34560., 0); Reloaded.Tick(StartAt + 34560., 0);
        const FObject A = Child(SavedYard(Sim.Serialize()), TEXT("economy")), B = Child(SavedYard(Reloaded.Serialize()), TEXT("economy"));
        TestEqual(TEXT("Second interval grows a second mushroom"), Array(A, TEXT("mushrooms")).Num(), 2);
        TestEqual(TEXT("Growth RNG phase survives reload"), Number(A, TEXT("mushroomRandom")), Number(B, TEXT("mushroomRandom")));
        if (!TestEqual(TEXT("Reload retains the complete mushroom population"), Array(A, TEXT("mushrooms")).Num(), Array(B, TEXT("mushrooms")).Num())) return false;
        for (int32 I = 0; I < Array(A, TEXT("mushrooms")).Num(); ++I)
            for (const TCHAR* Field : {TEXT("x"), TEXT("y"), TEXT("frame"), TEXT("reward")})
                TestEqual(FString(TEXT("Reload cannot reroll mushroom ")) + Field, Number(Array(A, TEXT("mushrooms"))[I]->AsObject(), Field), Number(Array(B, TEXT("mushrooms"))[I]->AsObject(), Field));
        Sim.Tick(StartAt + 17280. * 100., 0);
        TestEqual(TEXT("Long offline catch-up only fills the original ten-mushroom regrowth limit"), Array(Child(SavedYard(Sim.Serialize()), TEXT("economy")), TEXT("mushrooms")).Num(), 10);
        const FObject AtCap = Sim.Serialize();
        Sim.Tick(StartAt + 100., 0);
        TestEqual(TEXT("Clock rollback cannot respawn or remove mushrooms"), Array(Child(SavedYard(Sim.Serialize()), TEXT("economy")), TEXT("mushrooms")).Num(), 10);
        TestEqual(TEXT("Clock rollback cannot rewind the growth clock"), Number(Child(SavedYard(Sim.Serialize()), TEXT("economy")), TEXT("lastGrowth")), Number(Child(SavedYard(AtCap), TEXT("economy")), TEXT("lastGrowth")));
        FBYMSimulation OriginalTwenty; if (!Load(*this, OriginalTwenty, Seed.Serialize())) return false;
        OriginalTwenty.Tick(StartAt + 17280. * 100., 0);
        TestEqual(TEXT("Regrowth cap does not delete the original twenty starting mushrooms"), Array(Child(SavedYard(OriginalTwenty.Serialize()), TEXT("economy")), TEXT("mushrooms")).Num(), 20);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMShinySaveValidationTest, "BackyardMMO.Shiny.StrictEconomySnapshotValidation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMShinySaveValidationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        const FObject Good = Seed.Serialize();
        auto Reject = [&](const FObject& Save)
        {
            FBYMSimulation Sim; FString Error;
            return !Sim.Initialize(Save, Error) && !Error.IsEmpty();
        };
        for (const TCHAR* Field : {TEXT("shiny"), TEXT("purchasedWorkers"), TEXT("expansionLevel"), TEXT("packingLevel"), TEXT("mushroomsPicked"), TEXT("goldenMushroomsPicked"), TEXT("mushroomShinyEarned")})
        {
            for (const double Value : {-1., .5})
            {
                FObject Bad = Clone(Good); Child(SavedYard(Bad), TEXT("economy"))->SetNumberField(Field, Value);
                TestTrue(FString(TEXT("Reject invalid persisted economy integer: ")) + Field, Reject(Bad));
            }
            FObject Bad = Clone(Good); Child(SavedYard(Bad), TEXT("economy"))->SetStringField(Field, TEXT("bad"));
            TestTrue(FString(TEXT("Reject wrong-type persisted economy field: ")) + Field, Reject(Bad));
        }
        FObject Bad = Clone(Good); Child(SavedYard(Bad), TEXT("economy"))->SetNumberField(TEXT("purchasedWorkers"), 5);
        TestTrue(TEXT("Persisted extras cannot exceed four purchased workers"), Reject(Bad));
        Bad = Clone(Good); Child(SavedYard(Bad), TEXT("economy"))->SetNumberField(TEXT("expansionLevel"), 7);
        TestTrue(TEXT("Persisted expansions cannot exceed source tier six"), Reject(Bad));
        Bad = Clone(Good); FObject Economy = Child(SavedYard(Bad), TEXT("economy"));
        FArray Mushrooms = Array(Economy, TEXT("mushrooms")); Mushrooms.Add(MakeShared<FJsonValueObject>(Clone(Mushrooms[0]->AsObject()))); Economy->SetArrayField(TEXT("mushrooms"), Mushrooms);
        TestTrue(TEXT("Duplicated mushroom identities cannot duplicate rewards"), Reject(Bad));
        Bad = Clone(Good); Economy = Child(SavedYard(Bad), TEXT("economy"));
        Array(Economy, TEXT("mushrooms"))[0]->AsObject()->SetNumberField(TEXT("reward"), 999999);
        TestTrue(TEXT("Persisted mushroom rewards must belong to the original distribution"), Reject(Bad));
        const FString MushroomId = String(Array(Child(SavedYard(Good), TEXT("economy")), TEXT("mushrooms"))[0]->AsObject(), TEXT("id"));
        TestTrue(TEXT("Prepare a genuine picking task for cross-state validation"), Accepted(Seed.Execute(Owner, TEXT("clearmushroom"), FString::Printf(TEXT("{\"id\":\"%s\"}"), *MushroomId))));
        const FObject Picking = Seed.Serialize();
        Bad = Clone(Picking); SavedYard(Bad)->RemoveField(TEXT("workerState"));
        TestTrue(TEXT("Picking state cannot masquerade as a pre-worker legacy save"), Reject(Bad));
        Bad = Clone(Picking); Economy = Child(SavedYard(Bad), TEXT("economy"));
        for (const auto& V : Array(Economy, TEXT("mushrooms"))) if (String(V->AsObject(), TEXT("id")) == MushroomId)
            V->AsObject()->SetNumberField(TEXT("pickFinishAt"), Number(V->AsObject(), TEXT("pickFinishAt")) - .1);
        TestTrue(TEXT("Pick completion must match worker arrival plus the original shake duration"), Reject(Bad));
        return true;
    }
}
#endif
