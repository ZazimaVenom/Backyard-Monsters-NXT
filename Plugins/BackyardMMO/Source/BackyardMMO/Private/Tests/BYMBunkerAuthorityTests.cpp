#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Real simulation commands and save loading. These tests do not write account
// files or connect to a host. Ordinary count-based Bunkers use GLOBAL's non-MR3
// capacity override, not the smaller raw YARD_PROPS persistent-MR3 capacities.
namespace BYMBunkerAuthorityTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("bunker-authority-owner")), Other(TEXT("bunker-authority-other"));
    const FString OwnBunker(TEXT("bunker-authority-owner-building")), OtherBunker(TEXT("bunker-authority-other-building"));
    const double BunkerHealth[] = {10000., 24500., 52000., 75000., 105000.};

    struct FSettingsGuard
    {
        UBYMSettings* S = GetMutableDefault<UBYMSettings>();
        const int32 Bots = S->RivalBotCount, Cells = S->YardCells;
        const int32 Starting = S->StartingResources, Storage = S->ResourceStorageFloor;
        const float Construction = S->ConstructionTimeMultiplier, Production = S->ResourceProductionMultiplier;
        FSettingsGuard()
        {
            S->RivalBotCount = 0; S->YardCells = 100;
            S->StartingResources = 2000000; S->ResourceStorageFloor = 2000000;
            S->ConstructionTimeMultiplier = 1; S->ResourceProductionMultiplier = 0;
        }
        ~FSettingsGuard()
        {
            S->RivalBotCount = Bots; S->YardCells = Cells;
            S->StartingResources = Starting; S->ResourceStorageFloor = Storage;
            S->ConstructionTimeMultiplier = Construction; S->ResourceProductionMultiplier = Production;
        }
    };

    FObject Obj() { return MakeShared<FJsonObject>(); }
    FObject Decode(const FString& Text)
    { FObject Value; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value); return Value; }
    FString Encode(const FObject& Value)
    { FString Text; FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FObject Clone(const FObject& Value) { return Decode(Encode(Value)); }
    FObject Child(const FObject& Value, const TCHAR* Key)
    { const FObject* Result = nullptr; return Value && Value->TryGetObjectField(Key, Result) && Result ? *Result : FObject(); }
    const FArray& Array(const FObject& Value, const TCHAR* Key)
    { static const FArray Empty; const FArray* Result = nullptr; return Value && Value->TryGetArrayField(Key, Result) && Result ? *Result : Empty; }
    double Number(const FObject& Value, const TCHAR* Key, double Default = 0.)
    { double Result = Default; if (Value) Value->TryGetNumberField(Key, Result); return Result; }
    FString String(const FObject& Value, const TCHAR* Key)
    { FString Result; if (Value) Value->TryGetStringField(Key, Result); return Result; }
    bool Flag(const FObject& Value, const TCHAR* Key)
    { bool Result = false; return Value && Value->TryGetBoolField(Key, Result) && Result; }
    bool Accepted(const FString& Reply) { return Flag(Decode(Reply), TEXT("ok")); }
    FObject Find(const FArray& Values, const FString& Id, const TCHAR* Key = TEXT("id"))
    {
        for (const auto& Value : Values)
            if (Value && Value->Type == EJson::Object && String(Value->AsObject(), Key) == Id) return Value->AsObject();
        return nullptr;
    }
    FObject SavedYard(const FObject& Save, const FString& Id = Owner) { return Find(Array(Save, TEXT("yards")), Id); }
    FObject Snapshot(const FBYMSimulation& Simulation, const FString& Id = Owner, const FString& View = FString())
    { return Decode(Simulation.Snapshot(Id, View)); }
    FObject Home(const FBYMSimulation& Simulation) { return Child(Snapshot(Simulation), TEXT("home")); }
    FObject Bunker(const FBYMSimulation& Simulation)
    { return Find(Array(Home(Simulation), TEXT("bunkers")), OwnBunker, TEXT("building")); }
    FObject SavedBunker(const FObject& Save, const FString& YardId = Owner)
    { return Find(Array(SavedYard(Save, YardId), TEXT("buildings")), YardId == Owner ? OwnBunker : OtherBunker); }
    FObject Request(const FString& Building, const TCHAR* Type, int32 Count)
    {
        FObject Result = Obj(); Result->SetStringField(TEXT("building"), Building);
        Result->SetStringField(TEXT("type"), Type); Result->SetNumberField(TEXT("count"), Count); return Result;
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Simulation, const FObject& Save)
    { FString Error; if (Simulation.Initialize(Save, Error)) return true; Test.AddError(TEXT("Bunker fixture: ") + Error); return false; }
    bool RejectedSave(const FObject& Save)
    { FString Error; FBYMSimulation Simulation; return !Simulation.Initialize(Save, Error) && !Error.IsEmpty(); }
    FObject Fixture(FAutomationTestBase& Test, int32 Level = 1)
    {
        FBYMSimulation Seed; if (!Load(Test, Seed, nullptr)) return nullptr;
        FString Error;
        if (!Seed.AddPlayer(Owner, Owner, Error) || !Seed.AddPlayer(Other, Other, Error)) { Test.AddError(Error); return nullptr; }
        FObject Save = Seed.Serialize();
        for (const FString& YardId : {Owner, Other})
        {
            FObject Yard = SavedYard(Save, YardId), Building = Obj();
            // This controlled Bunker placement fixture intentionally has no random obstacles.
            Child(Yard, TEXT("economy"))->SetArrayField(TEXT("mushrooms"), FArray());
            Building->SetStringField(TEXT("id"), YardId == Owner ? OwnBunker : OtherBunker);
            Building->SetNumberField(TEXT("type"), 22); Building->SetNumberField(TEXT("level"), Level);
            Building->SetNumberField(TEXT("targetLevel"), Level); Building->SetNumberField(TEXT("x"), 1); Building->SetNumberField(TEXT("y"), 1);
            Building->SetNumberField(TEXT("hp"), BunkerHealth[Level - 1]); Building->SetNumberField(TEXT("stored"), 0);
            Building->SetNumberField(TEXT("readyAt"), 0); Building->SetNumberField(TEXT("workRemaining"), 0);
            Building->SetStringField(TEXT("jobId"), FString()); Building->SetBoolField(TEXT("repairing"), false);
            Building->SetObjectField(TEXT("garrison"), Obj());
            FArray Buildings = Array(Yard, TEXT("buildings")); Buildings.Add(MakeShared<FJsonValueObject>(Building)); Yard->SetArrayField(TEXT("buildings"), Buildings);
            FObject Army = Obj(); Army->SetNumberField(TEXT("C1"), 100); Army->SetNumberField(TEXT("C2"), 20);
            Army->SetNumberField(TEXT("C14"), 1); Army->SetNumberField(TEXT("C17"), 1); Yard->SetObjectField(TEXT("army"), Army);
            FObject Levels = Child(Yard, TEXT("monsterLevels"));
            // C1 level three has an odd Goo price (675). Batch rounding must
            // floor once per selected species, not once per individual unit.
            Levels->SetNumberField(TEXT("C1"), 3); Levels->SetNumberField(TEXT("C2"), 1);
            Levels->SetNumberField(TEXT("C14"), 1); Levels->SetNumberField(TEXT("C17"), 1);
            Yard->SetNumberField(TEXT("lastEconomy"), Number(Snapshot(Seed), TEXT("now")) + 60.);
        }
        return Save;
    }
    void SameInventory(FAutomationTestBase& Test, const FObject& Before, const FObject& After)
    {
        for (const TCHAR* Type : {TEXT("C1"), TEXT("C2"), TEXT("C14"), TEXT("C17")})
            Test.TestEqual(FString(TEXT("Rejected command preserves ready ")) + Type,
                Number(Child(Before, TEXT("army")), Type), Number(Child(After, TEXT("army")), Type));
        for (const TCHAR* Resource : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")})
            Test.TestEqual(FString(TEXT("Rejected command preserves ")) + Resource,
                Number(Child(Before, TEXT("resources")), Resource), Number(Child(After, TEXT("resources")), Resource));
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerBatchTransferTest, "BackyardMMO.Bunker.AtomicTransferAndDismissal",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerBatchTransferTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; const FObject Save = Fixture(*this); if (!Save) return false;
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        if (!TestTrue(TEXT("Owned Bunker is exposed in the private home snapshot"), Bunker(Simulation).IsValid())) return false;
        TestEqual(TEXT("Ordinary level-one Bunker uses GLOBAL's 380-space override"), Number(Bunker(Simulation), TEXT("capacity")), 380.);
        const double BeforePutty = Number(Child(Home(Simulation), TEXT("resources")), TEXT("putty"));
        const double BeforeGoo = Number(Child(Home(Simulation), TEXT("resources")), TEXT("goo"));
        const double BeforeXP = Number(Child(Home(Simulation), TEXT("experience")), TEXT("earned"));
        const double BeforeHousing = Number(Home(Simulation), TEXT("housingUsed"));
        FObject Batch = Obj(), Selection = Obj(); Batch->SetStringField(TEXT("building"), OwnBunker);
        Selection->SetNumberField(TEXT("C1"), 3); Selection->SetNumberField(TEXT("C2"), 2); Batch->SetObjectField(TEXT("selection"), Selection);
        if (!TestTrue(TEXT("Transfer a selected mixed trained roster atomically"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Batch))))) return false;
        TestEqual(TEXT("Batch fee is floor(675*3/2) plus floor(500*2/2)"), Number(Child(Home(Simulation), TEXT("resources")), TEXT("putty")), BeforePutty - 1512.);
        TestEqual(TEXT("Moving already-trained monsters costs no second Goo payment"), Number(Child(Home(Simulation), TEXT("resources")), TEXT("goo")), BeforeGoo);
        TestEqual(TEXT("Transferred Pokeys leave the ready army"), Number(Child(Home(Simulation), TEXT("army")), TEXT("C1")), 97.);
        TestEqual(TEXT("Transferred Octo-Oozes leave the ready army"), Number(Child(Home(Simulation), TEXT("army")), TEXT("C2")), 18.);
        TestEqual(TEXT("Mixed transfer uses current researched housing"), Number(Bunker(Simulation), TEXT("used")), 50.);
        TestEqual(TEXT("Bunker inventory does not continue occupying Housing"), Number(Home(Simulation), TEXT("housingUsed")), BeforeHousing - 50.);
        TestEqual(TEXT("Transfer does not invent an XP award"), Number(Child(Home(Simulation), TEXT("experience")), TEXT("earned")), BeforeXP);

        const FObject BeforeRejected = Home(Simulation);
        Selection->SetNumberField(TEXT("C1"), 1); Selection->SetNumberField(TEXT("C14"), 1);
        TestFalse(TEXT("One ineligible species rejects the complete mixed request"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Batch))));
        SameInventory(*this, BeforeRejected, Home(Simulation));
        TestEqual(TEXT("Failed mixed request cannot partially add its valid species"), Number(Child(Bunker(Simulation), TEXT("monsters")), TEXT("C1")), 3.);
        TestFalse(TEXT("A foreign building identity does not transfer another owner's army"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Request(OtherBunker, TEXT("C1"), 1)))));
        TestFalse(TEXT("The server rejects a quantity above the ready army"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Request(OwnBunker, TEXT("C2"), 100)))));
        TestTrue(TEXT("A transfer may exactly fill the remaining Bunker space"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Request(OwnBunker, TEXT("C1"), 33)))));
        TestEqual(TEXT("Exact fill reaches the original ordinary capacity"), Number(Bunker(Simulation), TEXT("used")), 380.);
        const FObject Full = Home(Simulation);
        TestFalse(TEXT("The host rejects one more monster beyond capacity"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Request(OwnBunker, TEXT("C1"), 1)))));
        SameInventory(*this, Full, Home(Simulation));

        FObject Dismiss = Request(OwnBunker, TEXT("C1"), 2); Dismiss->SetNumberField(TEXT("expectedStored"), 36);
        if (!TestTrue(TEXT("Explicit removal discards the confirmed stored monsters"), Accepted(Simulation.Execute(Owner, TEXT("bunkerDismiss"), Encode(Dismiss))))) return false;
        SameInventory(*this, Full, Home(Simulation));
        TestEqual(TEXT("Discarding reduces only the confirmed stored species"), Number(Child(Bunker(Simulation), TEXT("monsters")), TEXT("C1")), 34.);
        TestEqual(TEXT("Discarding frees only their current housing size"), Number(Bunker(Simulation), TEXT("used")), 360.);
        TestFalse(TEXT("A stale removal confirmation cannot discard replacement inventory"), Accepted(Simulation.Execute(Owner, TEXT("bunkerDismiss"), Encode(Dismiss))));
        TestEqual(TEXT("Removal neither refunds transfer fees nor awards XP"), Number(Child(Home(Simulation), TEXT("experience")), TEXT("earned")), BeforeXP);
        FBYMSimulation Restored; if (!Load(*this, Restored, Simulation.Serialize())) return false;
        TestEqual(TEXT("Native save loading retains the exact garrison"), Number(Child(Bunker(Restored), TEXT("monsters")), TEXT("C1")), 34.);
        SameInventory(*this, Home(Simulation), Home(Restored));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerEligibilityTest, "BackyardMMO.Bunker.EligibilityCapacityAndPrivacy",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerEligibilityTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard;
        const double Capacity[] = {380., 450., 540., 660., 800.};
        for (int32 Level = 1; Level <= 5; ++Level)
        {
            const FObject Save = Fixture(*this, Level); if (!Save) return false;
            FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
            TestEqual(TEXT("Each completed ordinary Bunker level uses its source runtime capacity"), Number(Bunker(Simulation), TEXT("capacity")), Capacity[Level - 1]);
        }
        for (int32 State = 0; State < 4; ++State)
        {
            const FObject Save = Fixture(*this); if (!Save) return false;
            const FObject Building = SavedBunker(Save);
            if (State <= 1) Building->SetNumberField(TEXT("hp"), 9999);
            if (State == 1) Building->SetBoolField(TEXT("repairing"), true);
            if (State >= 2)
            {
                Building->SetNumberField(TEXT("level"), State == 2 ? 1 : 0); Building->SetNumberField(TEXT("targetLevel"), State == 2 ? 2 : 1);
                SavedYard(Save)->RemoveField(TEXT("workerState")); // Synthetic pre-worker paid job.
                Building->SetStringField(TEXT("jobId"), TEXT("bunker-work-fixture")); Building->SetNumberField(TEXT("workRemaining"), 600);
            }
            FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
            const FObject Before = Home(Simulation);
            TestFalse(TEXT("Damaged, repairing, upgrading or unfinished Bunkers cannot accept a transfer"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Request(OwnBunker, TEXT("C1"), 1)))));
            TestFalse(TEXT("The private quote exposes the same management gate"), Flag(Bunker(Simulation), TEXT("canManage")));
            SameInventory(*this, Before, Home(Simulation));
        }
        const FObject Save = Fixture(*this); if (!Save) return false;
        FBYMSimulation Simulation; if (!Load(*this, Simulation, Save)) return false;
        TestTrue(TEXT("A previously owned legacy Slimeattikus remains permitted by the original Bunker whitelist"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Request(OwnBunker, TEXT("C17"), 1)))));
        const FObject Public = Child(Snapshot(Simulation, Other, Owner), TEXT("yard"));
        if (!TestTrue(TEXT("Scouting returns the requested existing yard"), Public.IsValid())) return false;
        TestFalse(TEXT("Scouting cannot read another yard's private garrison-management list"), Public->HasField(TEXT("bunkers")));
        const FObject PublicBunker = Find(Array(Public, TEXT("buildings")), OwnBunker);
        if (!TestTrue(TEXT("Scouting retains the visible Bunker building"), PublicBunker.IsValid())) return false;
        TestFalse(TEXT("Scouting cannot read stored monster counts through building serialization"), PublicBunker->HasField(TEXT("garrison")));
        if (!TestTrue(TEXT("Begin a real invasion to engage the source management battle lock"), Accepted(Simulation.Execute(Owner, TEXT("invade"), TEXT("{\"target\":\"bunker-authority-other\"}"))))) return false;
        TestFalse(TEXT("An attacker cannot transfer reserves while engaged"), Accepted(Simulation.Execute(Owner, TEXT("bunkerTransfer"), Encode(Request(OwnBunker, TEXT("C1"), 1)))));
        TestFalse(TEXT("The defending owner also cannot refill a Bunker during its battle"), Accepted(Simulation.Execute(Other, TEXT("bunkerTransfer"), Encode(Request(OtherBunker, TEXT("C1"), 1)))));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerMigrationTest, "BackyardMMO.Bunker.LegacyMigrationAndStrictSaveValidation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMBunkerMigrationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; const FObject Original = Fixture(*this); if (!Original) return false;
        for (int32 Schema = 1; Schema <= 4; ++Schema)
        {
            const FObject Legacy = Clone(Original); Legacy->SetNumberField(TEXT("schema"), Schema);
            for (const auto& YardValue : Array(Legacy, TEXT("yards")))
            {
                const FObject Yard = YardValue->AsObject();
                for (const auto& BuildingValue : Array(Yard, TEXT("buildings")))
                {
                    const FObject Building = BuildingValue->AsObject(); Building->RemoveField(TEXT("garrison"));
                    if (Schema < 4) { Building->RemoveField(TEXT("jobId")); Building->RemoveField(TEXT("workRemaining")); }
                }
                if (Schema < 4) { Yard->RemoveField(TEXT("earnedExperience")); Yard->RemoveField(TEXT("baseValue")); }
                if (Schema < 3) { Yard->RemoveField(TEXT("hatcheries")); Yard->SetArrayField(TEXT("training"), FArray()); }
                if (Schema == 1) { Yard->RemoveField(TEXT("monsterLevels")); Yard->RemoveField(TEXT("monsterUnlock")); Yard->RemoveField(TEXT("monsterResearch")); }
            }
            FBYMSimulation Migrated; if (!Load(*this, Migrated, Legacy)) return false;
            TestEqual(TEXT("Older worlds gain no invented stored defenders"), Number(Bunker(Migrated), TEXT("used")), 0.);
            TestEqual(TEXT("Legacy ready Pokeys are not consumed by migration"), Number(Child(Home(Migrated), TEXT("army")), TEXT("C1")), 100.);
            TestEqual(TEXT("The next host snapshot uses the new Bunker-aware schema"), Number(Migrated.Serialize(), TEXT("schema")), 5.);
        }
        const FObject Good = Clone(Original); Child(SavedBunker(Good), TEXT("garrison"))->SetNumberField(TEXT("C1"), 3);
        FBYMSimulation RoundTrip; if (!Load(*this, RoundTrip, Good)) return false;
        TestEqual(TEXT("A current valid garrison is retained"), Number(Child(Bunker(RoundTrip), TEXT("monsters")), TEXT("C1")), 3.);
        for (const double BadCount : {-1., .5, 100001.})
        {
            const FObject Bad = Clone(Good); Child(SavedBunker(Bad), TEXT("garrison"))->SetNumberField(TEXT("C1"), BadCount);
            TestTrue(TEXT("Malformed stored counts reject the complete save"), RejectedSave(Bad));
        }
        FObject Bad = Clone(Good); SavedBunker(Bad)->RemoveField(TEXT("garrison"));
        TestTrue(TEXT("Schema five requires the Bunker's explicit inventory"), RejectedSave(Bad));
        Bad = Clone(Good); SavedBunker(Bad)->SetStringField(TEXT("garrison"), TEXT("not-an-object"));
        TestTrue(TEXT("A string cannot stand in for saved inventory"), RejectedSave(Bad));
        Bad = Clone(Good); Child(SavedBunker(Bad), TEXT("garrison"))->SetNumberField(TEXT("C14"), 1);
        TestTrue(TEXT("Source-ineligible stored species are not admitted by save loading"), RejectedSave(Bad));
        Bad = Clone(Good); Child(SavedBunker(Bad), TEXT("garrison"))->SetNumberField(TEXT("C3"), 1);
        TestTrue(TEXT("Stored creatures cannot bypass researched ownership"), RejectedSave(Bad));
        Bad = Clone(Good); Child(SavedBunker(Bad), TEXT("garrison"))->SetNumberField(TEXT("C1"), 39);
        TestTrue(TEXT("A saved ordinary inventory cannot exceed its level's capacity"), RejectedSave(Bad));
        Bad = Clone(Good); const FObject NonBunker = Array(SavedYard(Bad), TEXT("buildings"))[0]->AsObject();
        NonBunker->SetObjectField(TEXT("garrison"), Clone(Child(SavedBunker(Good), TEXT("garrison"))));
        TestTrue(TEXT("A Town Hall cannot masquerade as a second Bunker inventory"), RejectedSave(Bad));
        return true;
    }
}
#endif
