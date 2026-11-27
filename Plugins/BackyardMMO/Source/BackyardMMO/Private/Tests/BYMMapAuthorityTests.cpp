#include "BYMSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMExperience.h"
#include "BYMMapRoomRules.h"
#include "BYMSettings.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include <cmath>

namespace BYMMapAuthorityTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    const FString Owner(TEXT("map-owner")), Other(TEXT("map-other"));
    struct FSettingsGuard
    {
        UBYMSettings* S = GetMutableDefault<UBYMSettings>();
        const int32 Bots = S->RivalBotCount, Starting = S->StartingResources, Cells = S->YardCells;
        const float Production = S->ResourceProductionMultiplier;
        FSettingsGuard() { S->RivalBotCount = 0; S->StartingResources = 5000; S->YardCells = 40; S->ResourceProductionMultiplier = 0; }
        ~FSettingsGuard() { S->RivalBotCount = Bots; S->StartingResources = Starting; S->YardCells = Cells; S->ResourceProductionMultiplier = Production; }
    };
    FObject Obj() { return MakeShared<FJsonObject>(); }
    FObject Decode(const FString& Text) { FObject V; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), V); return V; }
    FString Encode(const FObject& V) { FString Text; FJsonSerializer::Serialize(V.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FObject Clone(const FObject& V) { return Decode(Encode(V)); }
    FObject Child(const FObject& V, const TCHAR* Key) { const FObject* R = nullptr; return V && V->TryGetObjectField(Key, R) && R ? *R : FObject(); }
    const FArray& Array(const FObject& V, const TCHAR* Key) { static const FArray Empty; const FArray* R = nullptr; return V && V->TryGetArrayField(Key, R) && R ? *R : Empty; }
    double Number(const FObject& V, const TCHAR* Key, double Missing = 0) { double R = Missing; if (V) V->TryGetNumberField(Key, R); return R; }
    FString String(const FObject& V, const TCHAR* Key) { FString R; if (V) V->TryGetStringField(Key, R); return R; }
    bool Flag(const FObject& V, const TCHAR* Key) { bool R = false; if (V) V->TryGetBoolField(Key, R); return R; }
    FObject SavedYard(const FObject& Save, const FString& Id = Owner)
    { for (const auto& V : Array(Save, TEXT("yards"))) if (String(V->AsObject(), TEXT("id")) == Id) return V->AsObject(); return nullptr; }
    FObject Building(const FObject& Yard, int32 Type)
    { for (const auto& V : Array(Yard, TEXT("buildings"))) if (Number(V->AsObject(), TEXT("type")) == Type) return V->AsObject(); return nullptr; }
    FObject Snapshot(const FBYMSimulation& Sim, const FString& Id = Owner) { return Decode(Sim.Snapshot(Id, FString())); }
    FObject Execute(FBYMSimulation& Sim, const TCHAR* Op, const FObject& Payload, const FString& Id = Owner)
    { return Decode(Sim.Execute(Id, Op, Encode(Payload))); }
    FObject Map(FBYMSimulation& Sim, int32 X = -1, int32 Y = -1, int32 Radius = 12, const FString& Selected = FString(), const FString& Id = Owner)
    {
        FObject P = Obj(); P->SetNumberField(TEXT("x"), X); P->SetNumberField(TEXT("y"), Y); P->SetNumberField(TEXT("radius"), Radius);
        P->SetStringField(TEXT("selected"), Selected); return Child(Execute(Sim, TEXT("mapView"), P, Id), TEXT("map"));
    }
    FObject CellRequest(const FString& Cell) { FObject P = Obj(); P->SetStringField(TEXT("cell"), Cell); return P; }
    FObject Selected(FBYMSimulation& Sim, const FString& Cell, const FString& Id = Owner)
    { return Child(Map(Sim, -1, -1, 12, Cell, Id), TEXT("selected")); }
    FObject FirstCell(FBYMSimulation& Sim, int32 Kind)
    {
        const FObject View = Map(Sim, -1, -1, 15);
        for (const auto& Value : Array(View, TEXT("cells")))
            if (Number(Value->AsObject(), TEXT("kind")) == Kind) return Selected(Sim, String(Value->AsObject(), TEXT("id")));
        return nullptr;
    }
    FObject AttackRequest(const FObject& Cell)
    {
        FObject P = CellRequest(String(Cell, TEXT("id")));
        P->SetStringField(TEXT("expectedOwner"), String(Cell, TEXT("ownerId")));
        P->SetNumberField(TEXT("expectedRevision"), Number(Cell, TEXT("attackRevision")));
        P->SetObjectField(TEXT("maxCost"), Clone(Child(Cell, TEXT("attackCost")))); return P;
    }
    FObject SavedCell(const FObject& Save, const FString& Id)
    { for (const auto& Value : Array(Child(Save, TEXT("worldMap")), TEXT("cells"))) if (String(Value->AsObject(), TEXT("id")) == Id) return Value->AsObject(); return nullptr; }
    FObject SavedSite(const FObject& Save, const FString& Id)
    { for (const auto& Value : Array(Child(Save, TEXT("worldMap")), TEXT("sites"))) if (String(Value->AsObject(), TEXT("cell")) == Id) return Value->AsObject(); return nullptr; }
    FObject SavedBattle(const FObject& Save)
    { const FArray& Values = Array(Save, TEXT("battles")); return Values.IsEmpty() ? FObject() : Values[0]->AsObject(); }
    FObject Find(const FArray& Values, const FString& Id)
    { for (const auto& Value : Values) if (String(Value->AsObject(), TEXT("id")) == Id) return Value->AsObject(); return nullptr; }
    void ExpandSite(const FObject& Save, const FString& CellId, const FObject& PublicYard)
    {
        FObject Site = SavedSite(Save, CellId); if (!Site) return;
        FArray Buildings;
        for (const auto& Value : Array(PublicYard, TEXT("buildings")))
        {
            const FObject Source = Value->AsObject(); FObject B = Obj(); B->SetStringField(TEXT("id"), String(Source, TEXT("id")));
            for (const TCHAR* Key : {TEXT("x"), TEXT("y"), TEXT("hp")}) B->SetNumberField(Key, Number(Source, Key));
            B->SetBoolField(TEXT("repairing"), Flag(Source, TEXT("repairing"))); Buildings.Add(MakeShared<FJsonValueObject>(B));
        }
        Site->RemoveField(TEXT("buildingEncoding")); Site->SetArrayField(TEXT("buildings"), Buildings);
    }
    bool DeployEdge(FAutomationTestBase& Test, FBYMSimulation& Sim, const FString& Type = TEXT("C1"), int32 Count = 1, const FString& Id = Owner)
    {
        const FObject Yard = Child(Snapshot(Sim, Id), TEXT("yard"));
        FObject P = Obj(); P->SetStringField(TEXT("type"), Type); P->SetNumberField(TEXT("count"), Count);
        const int32 MinX = static_cast<int32>(Number(Yard, TEXT("minX"))), MinY = static_cast<int32>(Number(Yard, TEXT("minY")));
        const int32 MaxX = static_cast<int32>(Number(Yard, TEXT("maxX"))), MaxY = static_cast<int32>(Number(Yard, TEXT("maxY")));
        for (int32 X = MinX; X < MaxX; ++X) for (const int32 Y : {MinY, MaxY - 1})
        { P->SetNumberField(TEXT("x"), X); P->SetNumberField(TEXT("y"), Y); if (Flag(Execute(Sim, TEXT("deploy"), P, Id), TEXT("ok"))) return true; }
        Test.AddError(TEXT("No legal original-yard edge accepted a real deployment.")); return false;
    }
    void ClaimFixture(const FObject& Save, const FObject& PublicCell, const FString& NewOwner)
    {
        const FString Id = String(PublicCell, TEXT("id")); FObject Cell = SavedCell(Save, Id);
        if (!Cell)
        {
            Cell = Obj(); Cell->SetStringField(TEXT("id"), Id);
            Cell->SetStringField(TEXT("yard"), String(PublicCell, TEXT("yardId"))); Cell->SetStringField(TEXT("parent"), String(PublicCell, TEXT("parentId")));
            for (const TCHAR* Key : {TEXT("x"), TEXT("y"), TEXT("kind"), TEXT("level")}) Cell->SetNumberField(Key, Number(PublicCell, Key));
            Cell->SetBoolField(TEXT("homeDefense"), false); Cell->SetNumberField(TEXT("shieldUntil"), 0); Cell->SetNumberField(TEXT("destroyedUntil"), 0);
            FObject World = Child(Save, TEXT("worldMap")); FArray Cells = Array(World, TEXT("cells")); Cells.Add(MakeShared<FJsonValueObject>(Cell)); World->SetArrayField(TEXT("cells"), Cells);
        }
        Cell->SetStringField(TEXT("owner"), NewOwner); Cell->SetNumberField(TEXT("revision"), 1);
        // A captured site is durable even when it matches the original
        // template exactly. Use the actual loader's explicit source-delta
        // encoding; an absent site row is corruption, not a new capture.
        if (!NewOwner.IsEmpty() && !SavedSite(Save, Id))
        {
            FObject Site = Obj(); Site->SetStringField(TEXT("cell"), Id);
            Site->SetStringField(TEXT("buildingEncoding"), TEXT("source-delta-v1")); Site->SetArrayField(TEXT("buildings"), FArray());
            Site->SetNumberField(TEXT("lastEconomy"), Number(SavedYard(Save, NewOwner), TEXT("lastEconomy")));
            FObject Resources = Obj(); for (const TCHAR* Key : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")}) Resources->SetNumberField(Key, 0.);
            Site->SetObjectField(TEXT("resources"), Resources); Site->SetObjectField(TEXT("army"), Obj());
            FObject World = Child(Save, TEXT("worldMap")); FArray Sites = Array(World, TEXT("sites")); Sites.Add(MakeShared<FJsonValueObject>(Site)); World->SetArrayField(TEXT("sites"), Sites);
        }
    }
    double Wallet(const FBYMSimulation& Sim, const TCHAR* Resource, const FString& Id = Owner)
    { return Number(Child(Child(Snapshot(Sim, Id), TEXT("home")), TEXT("resources")), Resource); }
    bool Start(FAutomationTestBase& Test, FBYMSimulation& Sim)
    {
        FString Error;
        if (!Sim.Initialize(nullptr, Error) || !Sim.AddPlayer(Owner, Owner, Error) || !Sim.AddPlayer(Other, Other, Error))
        { Test.AddError(Error); return false; } return true;
    }
    bool Load(FAutomationTestBase& Test, FBYMSimulation& Sim, const FObject& Save)
    { FString Error; if (!Sim.Initialize(Save, Error)) { Test.AddError(FString(TEXT("Map fixture load: ")) + Error); return false; } return true; }
    FObject EnabledSave(FAutomationTestBase& Test)
    {
        FBYMSimulation Seed; if (!Start(Test, Seed)) return nullptr;
        FObject Save = Seed.Serialize();
        for (const FString& Id : {Owner, Other})
        {
            FObject Yard = SavedYard(Save, Id), Town = Building(Yard, 14), Locker = Building(Yard, 8);
            if (!Yard || !Town || !Locker) { Test.AddError(TEXT("The ordinary starting yard must supply source fixture buildings.")); return nullptr; }
            Town->SetNumberField(TEXT("level"), 6); Town->SetNumberField(TEXT("targetLevel"), 6); Town->SetNumberField(TEXT("hp"), 200000.);
            FObject Room = Clone(Locker); Room->SetStringField(TEXT("id"), Id + TEXT("-map-room")); Room->SetNumberField(TEXT("type"), 11);
            Room->SetNumberField(TEXT("level"), 2); Room->SetNumberField(TEXT("targetLevel"), 2); Room->SetNumberField(TEXT("hp"), 10000.);
            Room->SetNumberField(TEXT("x"), 40); Room->SetNumberField(TEXT("y"), 1);
            FArray Buildings = Array(Yard, TEXT("buildings")); Buildings.Add(MakeShared<FJsonValueObject>(Room)); Yard->SetArrayField(TEXT("buildings"), Buildings);
            FObject Economy = Child(Yard, TEXT("economy")); if (Economy) Economy->SetArrayField(TEXT("mushrooms"), FArray());
            FObject Resources = Child(Yard, TEXT("resources"));
            for (const TCHAR* Key : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")}) Resources->SetNumberField(Key, 200000000.);
            FObject Army = Obj(); Army->SetNumberField(TEXT("C1"), 20); Yard->SetObjectField(TEXT("army"), Army);
        }
        return Save;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapAdmissionAndWindowsTest, "BackyardMMO.MapRoom.AuthorityGateAndBoundedMapWindows",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapAdmissionAndWindowsTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FBYMSimulation Locked; if (!Start(*this, Locked)) return false;
        const FObject LockedMap = Map(Locked);
        if (!TestTrue(TEXT("The host supplies an explicit Map Room availability result"), LockedMap.IsValid())) return false;
        TestFalse(TEXT("A fresh yard cannot bypass the source Map Room progression gate"), Flag(LockedMap, TEXT("available")));
        TestFalse(TEXT("The unavailable map includes a user-facing reason"), String(LockedMap, TEXT("reason")).IsEmpty());
        TestFalse(TEXT("An unknown authenticated identity cannot read another map"), Flag(Execute(Locked, TEXT("mapView"), Obj(), TEXT("not-a-player")), TEXT("ok")));

        FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FObject HomeMap = Map(Sim);
        if (!TestTrue(TEXT("Completed source Map Room and Town Hall unlock the visual map"), Flag(HomeMap, TEXT("available")))) return false;
        TestEqual(TEXT("World map uses original MR3 width"), Number(HomeMap, TEXT("width")), 500.);
        TestEqual(TEXT("World map uses original MR3 height"), Number(HomeMap, TEXT("height")), 500.);
        TestFalse(TEXT("Every player has a persistent home cell"), String(HomeMap, TEXT("homeCellId")).IsEmpty());
        TestFalse(TEXT("Different players cannot share the same home map identity"), String(HomeMap, TEXT("homeCellId")) == String(Map(Sim, -1, -1, 12, FString(), Other), TEXT("homeCellId")));
        TestFalse(TEXT("A hostile oversized viewport request is rejected"), Map(Sim, 250, 250, 999999).IsValid());
        for (const FIntPoint Centre : {FIntPoint(0, 0), FIntPoint(499, 499), FIntPoint(0, 499), FIntPoint(250, 250)})
        {
            const FObject View = Map(Sim, Centre.X, Centre.Y, 100);
            if (!TestTrue(TEXT("A valid requested map center returns a bounded viewport"), View.IsValid())) return false;
            const FArray& Cells = Array(View, TEXT("cells"));
            TestTrue(TEXT("An oversized client radius cannot stream the full world"), Cells.Num() > 0 && Cells.Num() <= 4096);
            TSet<FString> Ids;
            for (const auto& Value : Cells)
            {
                const FObject Cell = Value->AsObject(); const FString Id = String(Cell, TEXT("id"));
                TestFalse(TEXT("A viewport contains no duplicate cell identities"), Ids.Contains(Id)); Ids.Add(Id);
                const double X = Number(Cell, TEXT("x"), -1), Y = Number(Cell, TEXT("y"), -1);
                TestTrue(TEXT("Every returned cell lies in the actual source world"), X >= 0 && X < 500 && Y >= 0 && Y < 500);
                TestFalse(TEXT("World map summaries never expose a raw private army"), Cell->HasField(TEXT("army")));
                TestFalse(TEXT("World map summaries never expose private worker assignments"), Cell->HasField(TEXT("workerState")));
            }
        }
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestEqual(TEXT("A host restart preserves the main-yard map position"), String(Map(Reloaded), TEXT("homeCellId")), String(HomeMap, TEXT("homeCellId")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapBookmarkOwnershipTest, "BackyardMMO.MapRoom.PrivateBookmarksAndScoutOwnership",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapBookmarkOwnershipTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FObject HomeMap = Map(Sim); const FString HomeCell = String(HomeMap, TEXT("homeCellId"));
        if (!TestFalse(TEXT("Home cell exists before bookmark operations"), HomeCell.IsEmpty())) return false;
        FObject Add = CellRequest(HomeCell); Add->SetBoolField(TEXT("enabled"), true);
        TestTrue(TEXT("The owner can bookmark a valid map location"), Flag(Execute(Sim, TEXT("mapBookmark"), Add), TEXT("ok")));
        TestTrue(TEXT("Repeating the same bookmark is idempotent"), Flag(Execute(Sim, TEXT("mapBookmark"), Add), TEXT("ok")));
        TestEqual(TEXT("Repeated bookmarks occupy one saved entry"), Array(Map(Sim), TEXT("bookmarks")).Num(), 1);
        TestEqual(TEXT("Other players cannot see this private bookmark"), Array(Map(Sim, -1, -1, 12, FString(), Other), TEXT("bookmarks")).Num(), 0);
        TestFalse(TEXT("Invalid cell identifiers cannot create bookmarks"), Flag(Execute(Sim, TEXT("mapBookmark"), CellRequest(TEXT("cell-500-500"))), TEXT("ok")));
        TestFalse(TEXT("Unknown sites cannot be inspected through a forged ID"), Flag(Execute(Sim, TEXT("mapInspect"), CellRequest(TEXT("not-a-map-cell"))), TEXT("ok")));
        TestTrue(TEXT("Home inspection resolves through the host's map cell"), Flag(Execute(Sim, TEXT("mapInspect"), CellRequest(HomeCell)), TEXT("ok")));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestEqual(TEXT("Bookmarks survive the existing host world snapshot"), Array(Map(Reloaded), TEXT("bookmarks")).Num(), 1);
        Add->SetBoolField(TEXT("enabled"), false);
        TestTrue(TEXT("An owner can remove a saved bookmark"), Flag(Execute(Reloaded, TEXT("mapBookmark"), Add), TEXT("ok")));
        TestEqual(TEXT("Removing a bookmark updates the private list"), Array(Map(Reloaded), TEXT("bookmarks")).Num(), 0);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapAttackQuoteTest, "BackyardMMO.MapRoom.AttackQuotesOwnershipAndReplay",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapAttackQuoteTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FObject Target = FirstCell(Sim, BYMMapRoomRules::Resource);
        if (!TestTrue(TEXT("The original generated neighborhood contains a resource outpost"), Target.IsValid())) return false;
        const FString CellId = String(Target, TEXT("id")); const FObject Request = AttackRequest(Target);
        const double Price = Number(Child(Target, TEXT("attackCost")), TEXT("twigs"));
        TestEqual(TEXT("Map Room three charges no Goo distance surcharge"), Number(Child(Target, TEXT("attackCost")), TEXT("goo")), 0.);
        TestEqual(TEXT("Distance price uses Town Hall six's source factor"), Price, Number(Target, TEXT("distanceBeyondRange")) * 4000.);
        const double Before = Wallet(Sim, TEXT("twigs")), Goo = Wallet(Sim, TEXT("goo"));
        FObject Stale = Clone(Request); Stale->SetNumberField(TEXT("expectedRevision"), Number(Target, TEXT("attackRevision")) + 1);
        TestFalse(TEXT("A stale revision cannot start a battle"), Flag(Execute(Sim, TEXT("mapInvade"), Stale), TEXT("ok")));
        TestEqual(TEXT("A stale quote cannot debit the account"), Wallet(Sim, TEXT("twigs")), Before);
        Stale = Clone(Request); Stale->SetStringField(TEXT("expectedOwner"), Other);
        TestFalse(TEXT("An owner mismatch cannot attack the wrong claimant"), Flag(Execute(Sim, TEXT("mapInvade"), Stale), TEXT("ok")));
        TestEqual(TEXT("An owner mismatch leaves resources unchanged"), Wallet(Sim, TEXT("twigs")), Before);
        Stale = Clone(Request); Stale->RemoveField(TEXT("maxCost"));
        TestFalse(TEXT("An attack requires the displayed host price confirmation"), Flag(Execute(Sim, TEXT("mapInvade"), Stale), TEXT("ok")));
        const FObject Home = Selected(Sim, String(Map(Sim), TEXT("homeCellId")));
        TestFalse(TEXT("A player cannot invade their own main backyard"), Flag(Execute(Sim, TEXT("mapInvade"), AttackRequest(Home)), TEXT("ok")));
        TestTrue(TEXT("The current exact quote starts a real territory invasion"), Flag(Execute(Sim, TEXT("mapInvade"), Request), TEXT("ok")));
        TestEqual(TEXT("A successful start debits the original distance price exactly once"), Wallet(Sim, TEXT("twigs")), Before - Price);
        TestEqual(TEXT("Distance admission does not charge Goo"), Wallet(Sim, TEXT("goo")), Goo);
        TestFalse(TEXT("Repeating an accepted attack cannot create a second battle"), Flag(Execute(Sim, TEXT("mapInvade"), Request), TEXT("ok")));
        TestEqual(TEXT("A repeated request cannot double-charge"), Wallet(Sim, TEXT("twigs")), Before - Price);
        const FObject Battle = Child(Snapshot(Sim), TEXT("battle"));
        TestTrue(TEXT("Map admission creates an authoritative battle snapshot"), Battle.IsValid());
        TestEqual(TEXT("The actual rendered defender is the selected map yard"), String(Child(Snapshot(Sim), TEXT("yard")), TEXT("id")), String(Target, TEXT("yardId")));
        TestFalse(TEXT("A second attacker cannot invade the same live territory battle"), Flag(Execute(Sim, TEXT("mapInvade"), AttackRequest(Selected(Sim, CellId, Other)), Other), TEXT("ok")));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
        TestTrue(TEXT("A live territory battle survives the existing save path"), Child(Snapshot(Reloaded), TEXT("battle")).IsValid());
        TestEqual(TEXT("Reload preserves the paid distance debit"), Wallet(Reloaded, TEXT("twigs")), Before - Price);
        TestFalse(TEXT("Reload cannot replay the attack purchase"), Flag(Execute(Reloaded, TEXT("mapInvade"), Request), TEXT("ok")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapResourceClockTest, "BackyardMMO.MapRoom.ResourceCapacityClockPartitionAndAbandonment",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapResourceClockTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Seed; if (!Load(*this, Seed, Save)) return false;
        const FObject Resource = FirstCell(Seed, BYMMapRoomRules::Resource);
        if (!TestTrue(TEXT("A real generated resource territory is available"), Resource.IsValid())) return false;
        const FString CellId = String(Resource, TEXT("id"));
        const double OriginalCapacity = Number(Child(Snapshot(Seed), TEXT("home")), TEXT("storageCapacity"));
        Save = Seed.Serialize(); ClaimFixture(Save, Resource, Owner);
        for (const TCHAR* Key : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")}) Child(SavedYard(Save), TEXT("resources"))->SetNumberField(Key, 0);
        FBYMSimulation Whole, Split; if (!Load(*this, Whole, Clone(Save)) || !Load(*this, Split, Clone(Save))) return false;
        const double Rate = Number(Resource, TEXT("productionPerSecond")), AddedCapacity = Number(Resource, TEXT("resourceCapacity"));
        TestEqual(TEXT("Owned resource capacity adds to completed-yard storage"), Number(Child(Snapshot(Whole), TEXT("home")), TEXT("storageCapacity")), OriginalCapacity + AddedCapacity);
        TestEqual(TEXT("The visual map reports the owned source production rate"), Number(Map(Whole), TEXT("resourceProductionPerSecond")), Rate);
        const double Now = Number(Snapshot(Whole), TEXT("now"));
        Whole.Tick(Now + 1., 1.f);
        for (int32 Index = 1; Index <= 10; ++Index) Split.Tick(Now + Index * .1, .1f);
        for (const TCHAR* Key : {TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo")})
        {
            TestEqual(TEXT("One source second produces the owned outpost's exact resource rate"), Wallet(Whole, Key), Rate);
            TestEqual(TEXT("Frequent host ticks cannot discard fractional production credit"), Wallet(Split, Key), Wallet(Whole, Key));
        }
        FBYMSimulation Partial; if (!Load(*this, Partial, Clone(Save))) return false;
        Partial.Tick(Now + .5, .5f); FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Partial.Serialize())) return false;
        Reloaded.Tick(Now + 1., .5f);
        TestEqual(TEXT("Reload retains a partial production interval"), Wallet(Reloaded, TEXT("twigs")), Rate);
        const FObject OwnedCell = Selected(Reloaded, CellId); FObject Abandon = CellRequest(CellId);
        Abandon->SetNumberField(TEXT("expectedRevision"), Number(OwnedCell, TEXT("attackRevision")));
        TestFalse(TEXT("A different player cannot abandon owned production"), Flag(Execute(Reloaded, TEXT("mapAbandon"), Abandon, Other), TEXT("ok")));
        TestTrue(TEXT("The owner can abandon territory through the checked command"), Flag(Execute(Reloaded, TEXT("mapAbandon"), Abandon), TEXT("ok")));
        TestEqual(TEXT("Abandonment removes the source capacity bonus"), Number(Child(Snapshot(Reloaded), TEXT("home")), TEXT("storageCapacity")), OriginalCapacity);
        TestEqual(TEXT("Abandonment removes future production"), Number(Map(Reloaded), TEXT("resourceProductionPerSecond")), 0.);
        Reloaded.Tick(Now + 2., 1.f);
        TestEqual(TEXT("A former owner gains no resource after abandonment"), Wallet(Reloaded, TEXT("twigs")), Rate);
        TestFalse(TEXT("Repeating abandonment cannot affect another ownership epoch"), Flag(Execute(Reloaded, TEXT("mapAbandon"), Abandon), TEXT("ok")));
        FBYMSimulation Again; if (!Load(*this, Again, Reloaded.Serialize())) return false;
        TestEqual(TEXT("Abandoned territory remains neutral after reload"), String(Selected(Again, CellId), TEXT("ownerId")), FString());
        TestEqual(TEXT("Territory shield state agrees before and after reload"), Number(Selected(Again, CellId), TEXT("shieldUntil")), Number(Selected(Reloaded, CellId), TEXT("shieldUntil")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapSaveValidationTest, "BackyardMMO.MapRoom.LegacyMigrationAndStrictTerritorySaves",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapSaveValidationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Original; if (!Load(*this, Original, Save)) return false;
        const FString Home = String(Map(Original), TEXT("homeCellId"));
        const double Balance = Wallet(Original, TEXT("twigs"));
        FObject Legacy = Clone(Save); Legacy->RemoveField(TEXT("worldMap"));
        FBYMSimulation Migrated; if (!Load(*this, Migrated, Legacy)) return false;
        TestFalse(TEXT("A pre-territory world receives a valid main-yard cell"), String(Map(Migrated), TEXT("homeCellId")).IsEmpty());
        TestEqual(TEXT("Map migration preserves existing resource balances"), Wallet(Migrated, TEXT("twigs")), Balance);
        TestEqual(TEXT("Map migration preserves the paid and ready army"), Number(Child(SavedYard(Migrated.Serialize()), TEXT("army")), TEXT("C1")), 20.);
        auto Rejected = [&](const FObject& Bad, const TCHAR* Message)
        { FBYMSimulation Check; FString Error; TestFalse(Message, Check.Initialize(Bad, Error)); TestFalse(TEXT("A rejected territory save explains its failure"), Error.IsEmpty()); };
        FObject Bad = Clone(Save); Child(Bad, TEXT("worldMap"))->SetNumberField(TEXT("version"), 99); Rejected(Bad, TEXT("Unknown territory versions are not silently reset"));
        Bad = Clone(Save); SavedCell(Bad, Home)->SetStringField(TEXT("owner"), TEXT("missing-player")); Rejected(Bad, TEXT("A saved cell cannot belong to a nonexistent account yard"));
        Bad = Clone(Save); SavedCell(Bad, Home)->SetNumberField(TEXT("x"), 500); Rejected(Bad, TEXT("A main yard cannot be restored outside the source world"));
        Bad = Clone(Save); SavedCell(Bad, Home)->SetNumberField(TEXT("revision"), 1.5); Rejected(Bad, TEXT("Ownership revisions cannot be fractional"));
        Bad = Clone(Save); FObject World = Child(Bad, TEXT("worldMap")); FArray Cells = Array(World, TEXT("cells")); Cells.Add(MakeShared<FJsonValueObject>(Clone(Cells[0]->AsObject()))); World->SetArrayField(TEXT("cells"), Cells);
        Rejected(Bad, TEXT("Duplicate persistent cell identities are rejected"));
        Bad = Clone(Save); World = Child(Bad, TEXT("worldMap")); World->SetArrayField(TEXT("players"), FArray()); Rejected(Bad, TEXT("A saved world cannot omit its existing players' positions"));
        Bad = Clone(Save); World = Child(Bad, TEXT("worldMap"));
        for (const auto& Value : Array(World, TEXT("players"))) if (String(Value->AsObject(), TEXT("id")) == Owner)
        { FArray Duplicate; Duplicate.Add(MakeShared<FJsonValueString>(Home)); Duplicate.Add(MakeShared<FJsonValueString>(Home)); Value->AsObject()->SetArrayField(TEXT("bookmarks"), Duplicate); }
        Rejected(Bad, TEXT("Duplicate bookmarks cannot inflate persisted lists"));
        TestEqual(TEXT("Validation attempts never mutate the original world"), String(Map(Original), TEXT("homeCellId")), Home);
        TestEqual(TEXT("Rejected loads do not debit the running world"), Wallet(Original, TEXT("twigs")), Balance);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapCaptureThresholdTest, "BackyardMMO.MapRoom.RealBattleCaptureAndOwnershipTransfer",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapCaptureThresholdTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Base = EnabledSave(*this); if (!Base) return false;
        // These three cells come from the original Map Room 3 data, not a
        // synthetic replacement map. Verify their identities before use.
        const TCHAR* SourceCells[] = {TEXT("cell-47-21"), TEXT("cell-63-21"), TEXT("cell-37-21")};
        const int32 Kinds[] = {102, 103, 104};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FBYMSimulation Seed; if (!Load(*this, Seed, Clone(Base))) return false;
            FObject Cell = Selected(Seed, SourceCells[Index]);
            if (!TestEqual(TEXT("Capture fixture is an actual source territory kind"), Number(Cell, TEXT("kind")), static_cast<double>(Kinds[Index]))) return false;
            FObject Owned = Seed.Serialize(); ClaimFixture(Owned, Cell, Other);
            FBYMSimulation Admitted; if (!Load(*this, Admitted, Owned)) return false;
            Cell = Selected(Admitted, SourceCells[Index]);
            Admitted.SetPlayerOnline(Other, true);
            FObject Legacy = Obj(); Legacy->SetStringField(TEXT("target"), String(Cell, TEXT("yardId")));
            TestFalse(TEXT("Legacy invasion commands cannot bypass territory distance pricing"), Flag(Execute(Admitted, TEXT("invade"), Legacy), TEXT("ok")));
            if (!TestTrue(TEXT("A checked original-territory attack starts"), Flag(Execute(Admitted, TEXT("mapInvade"), AttackRequest(Cell)), TEXT("ok"))) || !DeployEdge(*this, Admitted)) return false;
            const FObject DefenseView = Decode(Admitted.Snapshot(Other, String(Cell, TEXT("yardId"))));
            const FObject DefenseBattle = Child(DefenseView, TEXT("battle"));
            TestTrue(TEXT("A territory owner can see the actual battle attacking their outpost"), DefenseBattle.IsValid());
            TestFalse(TEXT("A defending territory owner cannot deploy the attacker's troops"), Flag(DefenseBattle, TEXT("canDeploy")));
            TestFalse(TEXT("Defender battle presentation excludes the attacker's Flinger inventory"), DefenseBattle && DefenseBattle->HasField(TEXT("flingerUsed")));
            const FObject BattleSave = Admitted.Serialize(); const FString CellId = SourceCells[Index];
            ExpandSite(BattleSave, CellId, Child(Snapshot(Admitted), TEXT("yard")));
            TestTrue(TEXT("A live contested original site is included in the host save"), SavedSite(BattleSave, CellId).IsValid());
            for (const double Remaining : {.11, .10})
            {
                FObject Damaged = Clone(BattleSave); FObject Site = SavedSite(Damaged, CellId);
                for (const auto& Value : Array(Site, TEXT("buildings")))
                { FObject B = Value->AsObject(); B->SetNumberField(TEXT("hp"), Number(B, TEXT("hp")) * Remaining); B->SetBoolField(TEXT("repairing"), false); }
                FBYMSimulation Sim; if (!Load(*this, Sim, Damaged)) return false;
                const double Damage = Number(Selected(Sim, CellId), TEXT("destructionPercent"));
                TestEqual(TEXT("Capture measures original full building HP, excluding wall HP"), Damage, Remaining == .10 ? 90. : 89.);
                TestTrue(TEXT("Ending the real contested battle resolves its territory objective"), Flag(Execute(Sim, TEXT("retreat"), Obj()), TEXT("ok")));
                const FObject Result = Selected(Sim, CellId);
                TestEqual(TEXT("Only reaching ninety percent transfers original territory ownership"), String(Result, TEXT("ownerId")), Remaining == .10 ? Owner : Other);
                TestEqual(TEXT("Map sites do not gain a main-backyard defense shield"), Number(Result, TEXT("shieldUntil")), 0.);
                const FObject Saved = Sim.Serialize();
                TestEqual(TEXT("A captured player's territory credits the correct ranked winner"), Number(SavedYard(Saved, Remaining == .10 ? Owner : Other), TEXT("rp")), static_cast<double>(Guard.S->RPWin));
                const FArray& DefenseLog = Array(SavedYard(Saved, Other), TEXT("log"));
                TestTrue(TEXT("The previous real owner receives the territory defense result"), !DefenseLog.IsEmpty() && String(DefenseLog[0]->AsObject(), TEXT("role")) == TEXT("defender"));
                TestEqual(TEXT("Resolved territory battle is no longer live"), Array(Saved, TEXT("battles")).Num(), 0);
                TestEqual(TEXT("Capture retains the original authored structures"), Array(Child(Decode(Sim.Snapshot(Owner, TEXT("map-") + CellId)), TEXT("yard")), TEXT("buildings")).Num(), Array(Site, TEXT("buildings")).Num());
                if (Remaining == .10)
                {
                    TestEqual(TEXT("Capture increments the ownership revision once"), Number(Result, TEXT("attackRevision")), Number(Cell, TEXT("attackRevision")) + 1.);
                    TestFalse(TEXT("The prior claimant cannot repair captured territory"), Flag(Execute(Sim, TEXT("mapRepair"), CellRequest(CellId), Other), TEXT("ok")));
                    TestTrue(TEXT("The new owner can start original outpost repair"), Flag(Execute(Sim, TEXT("mapRepair"), CellRequest(CellId)), TEXT("ok")));
                    TestEqual(TEXT("New owner gains only this resource territory's production"), Number(Map(Sim), TEXT("resourceProductionPerSecond")), Kinds[Index] == 102 ? Number(Cell, TEXT("productionPerSecond")) : 0.);
                    TestEqual(TEXT("Former owner loses captured resource production"), Number(Map(Sim, -1, -1, 12, FString(), Other), TEXT("resourceProductionPerSecond")), 0.);
                }
                FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Sim.Serialize())) return false;
                TestEqual(TEXT("The ownership result survives a host reload"), String(Selected(Reloaded, CellId), TEXT("ownerId")), String(Result, TEXT("ownerId")));
                if (Remaining == .10)
                {
                    TestTrue(TEXT("The former owner can challenge a captured site through the same quoted flow"), Flag(Execute(Reloaded, TEXT("mapInvade"), AttackRequest(Selected(Reloaded, CellId, Other)), Other), TEXT("ok")));
                    if (!DeployEdge(*this, Reloaded, TEXT("C1"), 1, Other)) return false;
                    FObject CounterSave = Reloaded.Serialize(); ExpandSite(CounterSave, CellId, Child(Snapshot(Reloaded, Other), TEXT("yard")));
                    for (const auto& Value : Array(SavedSite(CounterSave, CellId), TEXT("buildings"))) { Value->AsObject()->SetNumberField(TEXT("hp"), 0.); Value->AsObject()->SetBoolField(TEXT("repairing"), false); }
                    FBYMSimulation Counter; if (!Load(*this, Counter, CounterSave)) return false;
                    TestTrue(TEXT("A contested recapture resolves normally"), Flag(Execute(Counter, TEXT("retreat"), Obj(), Other), TEXT("ok")));
                    const FObject Recaptured = Selected(Counter, CellId);
                    TestEqual(TEXT("Territory can transfer back to its earlier owner"), String(Recaptured, TEXT("ownerId")), Other);
                    TestEqual(TEXT("Recapture advances the same ownership epoch again"), Number(Recaptured, TEXT("attackRevision")), Number(Result, TEXT("attackRevision")) + 1.);
                    TestEqual(TEXT("Recapture removes the previous owner's production"), Number(Map(Counter), TEXT("resourceProductionPerSecond")), 0.);
                    TestEqual(TEXT("Recapture grants the new owner the source resource rate"), Number(Map(Counter, -1, -1, 12, FString(), Other), TEXT("resourceProductionPerSecond")), Kinds[Index] == 102 ? Number(Cell, TEXT("productionPerSecond")) : 0.);
                }
            }
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapMainOwnershipTest, "BackyardMMO.MapRoom.MainBackyardsOnlineProtectionAndNoCapture",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapMainOwnershipTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FString TargetId = String(Map(Sim, -1, -1, 12, FString(), Other), TEXT("homeCellId"));
        Sim.SetPlayerOnline(Other, true);
        const double Before = Wallet(Sim, TEXT("twigs"));
        TestFalse(TEXT("Online human main yards reject Map Room attacks"), Flag(Execute(Sim, TEXT("mapInvade"), AttackRequest(Selected(Sim, TargetId))), TEXT("ok")));
        TestEqual(TEXT("Online protection never charges attack resources"), Wallet(Sim, TEXT("twigs")), Before);
        Sim.SetPlayerOnline(Other, false);
        TestTrue(TEXT("An offline main backyard uses the real map admission flow"), Flag(Execute(Sim, TEXT("mapInvade"), AttackRequest(Selected(Sim, TargetId))), TEXT("ok")));
        if (!DeployEdge(*this, Sim)) return false;
        Save = Sim.Serialize();
        for (const auto& V : Array(SavedYard(Save, Other), TEXT("buildings"))) V->AsObject()->SetNumberField(TEXT("hp"), 0.);
        FBYMSimulation Finished; if (!Load(*this, Finished, Save)) return false;
        TestTrue(TEXT("A contested main-yard victory resolves"), Flag(Execute(Finished, TEXT("retreat"), Obj()), TEXT("ok")));
        const FObject Main = Selected(Finished, TargetId);
        TestEqual(TEXT("A fully destroyed main backyard remains with its original owner"), String(Main, TEXT("ownerId")), Other);
        TestFalse(TEXT("Main yards never advertise capture"), Flag(Main, TEXT("capturable")));
        TestEqual(TEXT("Main yard receives the configured one-hour shield"), Number(Main, TEXT("shieldUntil")) - Number(Snapshot(Finished), TEXT("now")), static_cast<double>(Guard.S->ShieldSeconds));
        TestFalse(TEXT("The main-yard shield prevents an immediate repeat invasion"), Flag(Execute(Finished, TEXT("mapInvade"), AttackRequest(Main)), TEXT("ok")));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Finished.Serialize())) return false;
        TestEqual(TEXT("A main backyard's owner survives reload after defeat"), String(Selected(Reloaded, TargetId), TEXT("ownerId")), Other);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapSitePersistenceTest, "BackyardMMO.MapRoom.SourceSitePersistenceAndStrictMovedFootprints",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapSitePersistenceTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FString CellId(TEXT("cell-63-21")); const FObject Cell = Selected(Sim, CellId);
        TestTrue(TEXT("Original stronghold can be scouted without attacking"), Flag(Execute(Sim, TEXT("mapInspect"), CellRequest(CellId)), TEXT("ok")));
        Save = Sim.Serialize(); TestFalse(TEXT("Pristine scouting does not accumulate site payloads in host saves"), SavedSite(Save, CellId).IsValid());
        ClaimFixture(Save, Cell, Owner);
        FBYMSimulation Owned; if (!Load(*this, Owned, Save)) return false;
        TestTrue(TEXT("Owned source site can be entered"), Flag(Execute(Owned, TEXT("mapInspect"), CellRequest(CellId)), TEXT("ok")));
        Save = Owned.Serialize(); const FObject Site = SavedSite(Save, CellId);
        if (!TestTrue(TEXT("An instantiated owned site is durable"), Site.IsValid())) return false;
        FBYMSimulation Unchanged; if (!Load(*this, Unchanged, Clone(Save))) return false;
        TestEqual(TEXT("Authored fractional coordinates and source overlaps survive reload"), Encode(SavedSite(Unchanged.Serialize(), CellId)), Encode(Site));
        const FObject Public = Child(Decode(Owned.Snapshot(Owner, TEXT("map-") + CellId)), TEXT("yard"));
        ExpandSite(Save, CellId, Public);
        FObject Core = Building(Public, 138), Movable;
        for (const auto& V : Array(Public, TEXT("buildings")))
        { const double Type = Number(V->AsObject(), TEXT("type")); if (Type != 138 && Type != 139 && Type != 140) { Movable = V->AsObject(); break; } }
        if (!TestTrue(TEXT("Original stronghold has a fixed core and surrounding defenses"), Core.IsValid() && Movable.IsValid())) return false;
        // The source core is 130 px (6.5 cells), although its ordinary catalog
        // nominal box is only five cells. Clear only the small candidate box
        // using real legal moves so rejection cannot be caused by its walls.
        const double EdgeX = std::floor(Number(Core, TEXT("x")) + 5.) + 1., EdgeY = std::ceil(Number(Core, TEXT("y")));
        const double MoveSize = Number(Movable, TEXT("size"));
        auto OverlapsEdge = [&](double X, double Y, double Size)
        { return EdgeX < X + Size && EdgeX + MoveSize > X && EdgeY < Y + Size && EdgeY + MoveSize > Y; };
        for (const auto& Value : Array(Public, TEXT("buildings")))
        {
            const FObject Obstacle = Value->AsObject(); const FString Id = String(Obstacle, TEXT("id"));
            if (Id == String(Core, TEXT("id")) || Id == String(Movable, TEXT("id")) || !OverlapsEdge(Number(Obstacle, TEXT("x")), Number(Obstacle, TEXT("y")), Number(Obstacle, TEXT("size")))) continue;
            FObject Move = CellRequest(CellId); Move->SetStringField(TEXT("building"), Id); bool Moved = false;
            for (int32 Y = static_cast<int32>(Number(Public, TEXT("minY"))) + 1; Y < Number(Public, TEXT("maxY")) - 2 && !Moved; ++Y)
                for (int32 X = static_cast<int32>(Number(Public, TEXT("minX"))) + 1; X < Number(Public, TEXT("maxX")) - 2 && !Moved; ++X)
                {
                    if (OverlapsEdge(X, Y, Number(Obstacle, TEXT("size")))) continue;
                    Move->SetNumberField(TEXT("x"), X); Move->SetNumberField(TEXT("y"), Y); Moved = Flag(Execute(Owned, TEXT("mapMove"), Move), TEXT("ok"));
                }
            if (!TestTrue(TEXT("A surrounding defense can move legally away from the core-edge fixture"), Moved)) return false;
        }
        TestFalse(TEXT("The chosen edge is outside the incorrect nominal five-cell core"), OverlapsEdge(Number(Core, TEXT("x")), Number(Core, TEXT("y")), 5.));
        TestTrue(TEXT("The same candidate overlaps the exact original 130-pixel core"), OverlapsEdge(Number(Core, TEXT("x")), Number(Core, TEXT("y")), 6.5));
        FObject EdgeMove = CellRequest(CellId); EdgeMove->SetStringField(TEXT("building"), String(Movable, TEXT("id"))); EdgeMove->SetNumberField(TEXT("x"), EdgeX); EdgeMove->SetNumberField(TEXT("y"), EdgeY);
        TestFalse(TEXT("Ordinary map movement cannot overlap the outer 1.5 source cells of a core"), Flag(Execute(Owned, TEXT("mapMove"), EdgeMove), TEXT("ok")));
        auto Reject = [&](const FObject& Bad, const TCHAR* Message)
        { FBYMSimulation Check; FString Error; TestFalse(Message, Check.Initialize(Bad, Error)); TestFalse(TEXT("Rejected site state returns a useful reason"), Error.IsEmpty()); };
        FObject Bad = Clone(Save); FObject B = Find(Array(SavedSite(Bad, CellId), TEXT("buildings")), String(Core, TEXT("id")));
        B->SetNumberField(TEXT("x"), Number(B, TEXT("x")) + 1.); Reject(Bad, TEXT("A source stronghold core cannot be moved by altering the save"));
        Bad = Clone(Save); B = Find(Array(SavedSite(Bad, CellId), TEXT("buildings")), String(Movable, TEXT("id")));
        B->SetNumberField(TEXT("x"), Number(B, TEXT("x")) + .125); Reject(Bad, TEXT("New fractional player-authored moves cannot disguise themselves as source placements"));
        Bad = Clone(Save); B = Find(Array(SavedSite(Bad, CellId), TEXT("buildings")), String(Movable, TEXT("id")));
        B->SetNumberField(TEXT("x"), std::ceil(Number(Core, TEXT("x")))); B->SetNumberField(TEXT("y"), std::ceil(Number(Core, TEXT("y"))));
        Reject(Bad, TEXT("Moved defense footprints cannot overlap an original core"));
        Bad = Clone(Save); B = Find(Array(SavedSite(Bad, CellId), TEXT("buildings")), String(Core, TEXT("id")));
        B->SetNumberField(TEXT("hp"), Number(Core, TEXT("maxHp")) + 1.); Reject(Bad, TEXT("Original site health cannot exceed the authoritative catalog"));
        Bad = Clone(Save); FArray Sites = Array(Child(Bad, TEXT("worldMap")), TEXT("sites")); Sites.Add(MakeShared<FJsonValueObject>(Clone(Site))); Child(Bad, TEXT("worldMap"))->SetArrayField(TEXT("sites"), Sites);
        Reject(Bad, TEXT("A site cannot appear twice in persisted territory state"));
        Bad = Clone(Save); SavedCell(Bad, CellId)->SetNumberField(TEXT("shieldUntil"), Number(Save, TEXT("now")) + 3600.); Reject(Bad, TEXT("Source map sites cannot acquire a forged main-yard shield"));
        Bad = Clone(Save); Child(Bad, TEXT("worldMap"))->SetArrayField(TEXT("sites"), FArray()); Reject(Bad, TEXT("Dropping an owned site's durable damage state cannot resurrect its original full-health template"));
        for (const double Fraction : {-0.1, 1.0, 1000.})
        {
            Bad = Clone(Save); for (const auto& V : Array(Child(Bad, TEXT("worldMap")), TEXT("players"))) if (String(V->AsObject(), TEXT("id")) == Owner) V->AsObject()->SetNumberField(TEXT("productionFraction"), Fraction);
            Reject(Bad, TEXT("Malformed fractional production cannot mint resources on reload"));
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapStrongholdCombatTest, "BackyardMMO.MapRoom.StrongholdFourEmittersDamageAndReload",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapStrongholdCombatTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Base = EnabledSave(*this); if (!Base) return false;
        const FObject Home = SavedYard(Base); FObject Housing = Building(Home, 15), Flinger = Building(Home, 5);
        Housing->SetNumberField(TEXT("level"), 4); Housing->SetNumberField(TEXT("targetLevel"), 4); Housing->SetNumberField(TEXT("hp"), 43000);
        Flinger->SetNumberField(TEXT("level"), 2); Flinger->SetNumberField(TEXT("targetLevel"), 2); Flinger->SetNumberField(TEXT("hp"), 8000);
        FObject Army = Obj(); Army->SetNumberField(TEXT("C12"), 4); Home->SetObjectField(TEXT("army"), Army);
        Child(Home, TEXT("monsterLevels"))->SetNumberField(TEXT("C12"), 1);
        FBYMSimulation Admitted; if (!Load(*this, Admitted, Base)) return false;
        const FString CellId(TEXT("cell-63-21")); const FObject Cell = Selected(Admitted, CellId);
        if (!TestTrue(TEXT("The stronghold attack uses checked admission"), Flag(Execute(Admitted, TEXT("mapInvade"), AttackRequest(Cell)), TEXT("ok"))) || !DeployEdge(*this, Admitted, TEXT("C12"), 4)) return false;
        const FObject Public = Child(Snapshot(Admitted), TEXT("yard")), Core = Building(Public, 138);
        if (!TestTrue(TEXT("The original stronghold template contains its four-emitter core"), Core.IsValid())) return false;
        const FString CoreId = String(Core, TEXT("id")); FObject Save = Admitted.Serialize(); ExpandSite(Save, CellId, Public);
        const FObject Site = SavedSite(Save, CellId); const double FullHP = Number(Core, TEXT("hp"));
        for (const auto& Value : Array(Site, TEXT("buildings")))
            if (String(Value->AsObject(), TEXT("id")) != CoreId) Value->AsObject()->SetNumberField(TEXT("hp"), 0.);
        const FVector2D Intact[] = {FVector2D(-.5, -68), FVector2D(58, -37), FVector2D(-.5, -7), FVector2D(-59, -40)};
        const FVector2D Damaged[] = {FVector2D(-.5, -68), FVector2D(58, -26), FVector2D(-6, -7), FVector2D(-67, -53)};
        const double Degrees[] = {-90., 0., 90., 180.};
        for (const bool bDamaged : {false, true})
        {
            FObject Fixture = Clone(Save); Find(Array(SavedSite(Fixture, CellId), TEXT("buildings")), CoreId)->SetNumberField(TEXT("hp"), FullHP * (bDamaged ? .5 : 1.));
            const FObject Battle = SavedBattle(Fixture); int32 Index = 0;
            for (const auto& Value : Array(Battle, TEXT("troops")))
            {
                const FVector2D Offset = bDamaged ? Damaged[Index] : Intact[Index];
                const double SX = (Number(Core, TEXT("x")) - 25.) * 20., SY = (Number(Core, TEXT("y")) - 20.) * 20.;
                const double IX = std::floor(SX - SY) + Offset.X, IY = std::floor((SX + SY) * .5) + Offset.Y;
                const double Angle = Degrees[Index] * 3.14159265358979323846 / 180.;
                FObject Troop = Value->AsObject(); Troop->SetNumberField(TEXT("x"), 25. + (IY + IX * .5) / 20. + 2. * (std::sin(Angle) + std::cos(Angle) * .5));
                Troop->SetNumberField(TEXT("y"), 20. + (IY - IX * .5) / 20. + 2. * (std::sin(Angle) - std::cos(Angle) * .5));
                Troop->SetNumberField(TEXT("attackCooldown"), 60.); ++Index;
            }
            FBYMSimulation Sim; if (!Load(*this, Sim, Fixture)) return false;
            const double Now = Number(Snapshot(Sim), TEXT("now")); Sim.Tick(Now + .1, .1f);
            const FObject First = Child(Snapshot(Sim), TEXT("battle")); int32 Shots = 0; TSet<FString> Origins;
            const double Expected = Number(Core, TEXT("level")) == 1 ? 900. : Number(Core, TEXT("level")) == 2 ? 1000. : 1100.;
            for (const auto& Value : Array(First, TEXT("events")))
            {
                const FObject E = Value->AsObject(); if (String(E, TEXT("kind")) != TEXT("defenseAttack") || String(E, TEXT("actor")) != CoreId) continue;
                ++Shots; Origins.Add(FString::Printf(TEXT("%.4f,%.4f"), Number(E, TEXT("sourceX")), Number(E, TEXT("sourceY"))));
                TestEqual(TEXT("Every stronghold emitter applies original full damage even at half health"), Number(E, TEXT("damage")), Expected);
            }
            TestEqual(TEXT("Four source emitters fire independently on acquisition"), Shots, 4);
            TestEqual(TEXT("Four authoritative lightning origins remain distinct"), Origins.Num(), 4);
            FObject Active = Sim.Serialize(); const FObject ActiveBattle = SavedBattle(Active), Emitters = Child(ActiveBattle, TEXT("mapTowerEmitters"));
            const FArray& State = Array(Emitters, *CoreId);
            if (!TestEqual(TEXT("The host saves exactly four source emitter clocks"), State.Num(), 4)) return false;
            for (const auto& Value : State)
            { TestFalse(TEXT("A live acquired target is retained in each emitter"), String(Value->AsObject(), TEXT("target")).IsEmpty()); TestTrue(TEXT("Each independent shot clock has one source second remaining"), FMath::Abs(Number(Value->AsObject(), TEXT("remaining")) - 1.) < .000001); }
            FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Clone(Active))) return false;
            Reloaded.Tick(Now + .2, .1f);
            const FObject Quiet = Child(Snapshot(Reloaded), TEXT("battle")); int32 EarlyShots = 0;
            for (const auto& Value : Array(Quiet, TEXT("events"))) if (String(Value->AsObject(), TEXT("kind")) == TEXT("defenseAttack") && String(Value->AsObject(), TEXT("actor")) == CoreId) ++EarlyShots;
            TestEqual(TEXT("Reload cannot reset all emitters for an immediate extra volley"), EarlyShots, 0);
            FObject MovedTargets = Reloaded.Serialize();
            for (const auto& Value : Array(SavedBattle(MovedTargets), TEXT("troops")))
            { Value->AsObject()->SetNumberField(TEXT("x"), Number(Public, TEXT("maxX")) - 1.); Value->AsObject()->SetNumberField(TEXT("y"), Number(Public, TEXT("maxY")) - 1.); }
            FBYMSimulation Retained; if (!Load(*this, Retained, MovedTargets)) return false;
            Retained.Tick(Now + 1.2, 1.f);
            const FObject Second = Child(Snapshot(Retained), TEXT("battle")); int32 RetainedShots = 0;
            for (const auto& Value : Array(Second, TEXT("events")))
            {
                const FObject Event = Value->AsObject();
                if (String(Event, TEXT("kind")) != TEXT("defenseAttack") || String(Event, TEXT("actor")) != CoreId) continue;
                ++RetainedShots;
                const double DX = Number(Event, TEXT("targetX")) - Number(Event, TEXT("sourceX"));
                const double DY = Number(Event, TEXT("targetY")) - Number(Event, TEXT("sourceY"));
                TestTrue(TEXT("An acquired live target remains retained beyond the original acquisition radius"), DX * DX + DY * DY > 20. * 20.);
            }
            TestEqual(TEXT("Independent source clocks fire the retained targets again after one second"), RetainedShots, 4);
            auto Reject = [&](const FObject& Bad, const TCHAR* Message)
            { FBYMSimulation Check; FString Error; TestFalse(Message, Check.Initialize(Bad, Error)); };
            FObject Bad = Clone(Active); FObject BadEmitters = Child(SavedBattle(Bad), TEXT("mapTowerEmitters")); FArray Three = Array(BadEmitters, *CoreId); Three.RemoveAt(3); BadEmitters->SetArrayField(CoreId, Three); Reject(Bad, TEXT("Incomplete stronghold emitter arrays cannot silently reset combat"));
            Bad = Clone(Active); BadEmitters = Child(SavedBattle(Bad), TEXT("mapTowerEmitters")); Array(BadEmitters, *CoreId)[0]->AsObject()->SetStringField(TEXT("target"), TEXT("foreign-army-unit")); Reject(Bad, TEXT("Saved emitter targets must belong to the same invasion"));
            Bad = Clone(Active); BadEmitters = Child(SavedBattle(Bad), TEXT("mapTowerEmitters")); Array(BadEmitters, *CoreId)[0]->AsObject()->SetNumberField(TEXT("remaining"), 2.); Reject(Bad, TEXT("Emitter clocks cannot exceed their source one-second interval"));
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapBotParticipationTest, "BackyardMMO.MapRoom.BotsUseTheSameTerritoryAdmissionAndDeployment",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapBotParticipationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FObject Bot = SavedYard(Save); Bot->SetBoolField(TEXT("bot"), true); Bot->SetNumberField(TEXT("decisions"), 3); Bot->SetNumberField(TEXT("nextDecision"), 0);
        Guard.S->RivalBotCount = 1;
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const double Before = Wallet(Sim, TEXT("twigs")), Now = Number(Snapshot(Sim), TEXT("now"));
        const float DecisionStep = FMath::Clamp(Guard.S->SimulationStepSeconds, .1f, 60.f);
        Sim.Tick(Now + DecisionStep, DecisionStep);
        FObject Battle = Child(Snapshot(Sim), TEXT("battle"));
        if (!TestTrue(TEXT("A progressed rival starts a real original-map territory invasion"), Battle.IsValid())) return false;
        const FString CellId = String(Battle, TEXT("mapCellId"));
        TestFalse(TEXT("Rival battles retain a checked world-map cell identity"), CellId.IsEmpty());
        const FObject Cell = Selected(Sim, CellId);
        TestEqual(TEXT("Rival admission pays the same source distance cost as a human"), Wallet(Sim, TEXT("twigs")), Before - Number(Child(Cell, TEXT("attackCost")), TEXT("twigs")));
        const double Next = Now + DecisionStep + FMath::Clamp(static_cast<double>(Guard.S->BotDecisionSeconds), 1., 86400.) + 1.;
        Sim.Tick(Next, DecisionStep); Battle = Child(Snapshot(Sim), TEXT("battle"));
        TestTrue(TEXT("Rival territory attacks actually deploy their paid trained army"), Array(Battle, TEXT("troops")).Num() > 0);
        const FObject Current = Sim.Serialize(); const FObject SavedBot = SavedYard(Current);
        TestTrue(TEXT("Rival deployment consumes the same ready army inventory"), Number(Child(SavedBot, TEXT("army")), TEXT("C1")) < 20.);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Current)) return false;
        TestEqual(TEXT("Active rival map invasion survives host restart"), String(Child(Snapshot(Reloaded), TEXT("battle")), TEXT("mapCellId")), CellId);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMActualBotLayoutMigrationTest, "BackyardMMO.Rivals.LegacyLayoutMigrationPreservesPaidState",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMActualBotLayoutMigrationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; Guard.S->RivalBotCount = 8; FBYMSimulation Seed; if (!Start(*this, Seed)) return false;
        const FString BusyBot(TEXT("bot-000001")); FObject Train = Obj(); Train->SetStringField(TEXT("type"), TEXT("C1")); Train->SetNumberField(TEXT("count"), 1);
        if (!TestTrue(TEXT("A real bot can have an already-paid Hatchery job before migration"), Flag(Execute(Seed, TEXT("train"), Train, BusyBot), TEXT("ok")))) return false;
        FObject Save = Seed.Serialize(); const FObject Standard = SavedYard(Save, Owner);
        for (const auto& Value : Array(Save, TEXT("yards")))
        {
            FObject Yard = Value->AsObject(); if (!Flag(Yard, TEXT("bot"))) continue;
            Yard->SetNumberField(TEXT("layoutRevision"), 0);
            // Model the old identical starter layout while retaining real
            // identities, levels, paid queues, resources and worker records.
            for (const auto& B : Array(Yard, TEXT("buildings")))
            {
                const FObject Source = Building(Standard, static_cast<int32>(Number(B->AsObject(), TEXT("type"))));
                B->AsObject()->SetNumberField(TEXT("x"), Number(Source, TEXT("x"))); B->AsObject()->SetNumberField(TEXT("y"), Number(Source, TEXT("y")));
            }
            Child(Yard, TEXT("economy"))->SetArrayField(TEXT("mushrooms"), FArray());
        }
        FBYMSimulation Migrated; if (!Load(*this, Migrated, Save)) return false;
        const FObject After = Migrated.Serialize(); TSet<FString> Layouts;
        for (const auto& Value : Array(Save, TEXT("yards")))
        {
            const FObject Old = Value->AsObject(); if (!Flag(Old, TEXT("bot"))) continue;
            const FString Id = String(Old, TEXT("id")); const FObject New = SavedYard(After, Id); FString Fingerprint;
            TestEqual(TEXT("Layout migration retains all eleven actual starter structures"), Array(New, TEXT("buildings")).Num(), 11);
            for (const auto& B : Array(Old, TEXT("buildings")))
            {
                const FObject Original = B->AsObject(), Restored = Find(Array(New, TEXT("buildings")), String(Original, TEXT("id")));
                TestTrue(TEXT("Every original building identity remains owned by its bot"), Restored.IsValid());
                for (const TCHAR* Key : {TEXT("type"), TEXT("level"), TEXT("hp"), TEXT("targetLevel"), TEXT("workRemaining")}) TestEqual(TEXT("Layout changes retain original gameplay state"), Number(Restored, Key), Number(Original, Key));
                Fingerprint += FString::Printf(TEXT("%.0f:%.0f,%.0f;"), Number(Restored, TEXT("type")), Number(Restored, TEXT("x")), Number(Restored, TEXT("y")));
                if (Id == BusyBot)
                { TestEqual(TEXT("A paid Hatchery job defers coordinate migration"), Number(Restored, TEXT("x")), Number(Original, TEXT("x"))); TestEqual(TEXT("Deferred migration preserves the whole building position"), Number(Restored, TEXT("y")), Number(Original, TEXT("y"))); }
            }
            for (const TCHAR* Key : {TEXT("resources"), TEXT("army"), TEXT("monsterLevels")}) TestEqual(TEXT("Bot layout changes cannot grant or discard paid account state"), Encode(Child(New, Key)), Encode(Child(Old, Key)));
            TestEqual(TEXT("Mushroom records are preserved"), Array(Child(New, TEXT("economy")), TEXT("mushrooms")).Num(), Array(Child(Old, TEXT("economy")), TEXT("mushrooms")).Num());
            TestEqual(TEXT("Only idle source starter yards commit the layout migration marker"), Number(New, TEXT("layoutRevision")), Id == BusyBot ? 0. : 1.);
            if (Id != BusyBot) Layouts.Add(Fingerprint);
        }
        TestEqual(TEXT("Previously identical idle rivals adopt different legal layouts"), Layouts.Num(), 7);
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, After)) return false;
        TestEqual(TEXT("Reload does not reorder the preserved paid Hatchery groups"), Array(SavedYard(Reloaded.Serialize(), BusyBot), TEXT("hatcheries")).Num(), Array(SavedYard(Save, BusyBot), TEXT("hatcheries")).Num());
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapPagedPayloadTest, "BackyardMMO.MapRoom.BoundedPayloadAndOwnedTerritoryPaging",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapPagedPayloadTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Seed; if (!Load(*this, Seed, Save)) return false;
        const FObject View = Map(Seed, 250, 250, 15); Save = Seed.Serialize(); FArray Bookmarks; int32 Claimed = 0;
        for (const auto& Value : Array(View, TEXT("cells")))
        {
            const FObject Cell = Value->AsObject(); const int32 Kind = static_cast<int32>(Number(Cell, TEXT("kind")));
            if (Kind < 102 || Kind > 104 || !String(Cell, TEXT("ownerId")).IsEmpty()) continue;
            if (Bookmarks.Num() < 100) Bookmarks.Add(MakeShared<FJsonValueString>(String(Cell, TEXT("id"))));
            if (Claimed < 121) { ClaimFixture(Save, Cell, Owner); ++Claimed; }
        }
        if (!TestEqual(TEXT("Dense original terrain supports a full multi-page ownership fixture"), Claimed, 121)) return false;
        for (const auto& Value : Array(Child(Save, TEXT("worldMap")), TEXT("players"))) if (String(Value->AsObject(), TEXT("id")) == Owner) Value->AsObject()->SetArrayField(TEXT("bookmarks"), Bookmarks);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        TSet<FString> Seen;
        for (int32 Page = 0; Page < 3; ++Page)
        {
            FObject P = Obj(); P->SetNumberField(TEXT("x"), 250); P->SetNumberField(TEXT("y"), 250); P->SetNumberField(TEXT("radius"), 15); P->SetNumberField(TEXT("ownedPage"), Page);
            const FString Encoded = Sim.Execute(Owner, TEXT("mapView"), Encode(P)); const FObject Current = Child(Decode(Encoded), TEXT("map"));
            TestTrue(TEXT("Largest supported map window, full bookmarks and owned page stay inside the real client payload cap"), Encoded.Len() <= 1000000);
            TestEqual(TEXT("Every requested map page reports its exact page number"), Number(Current, TEXT("ownedPage")), static_cast<double>(Page));
            TestEqual(TEXT("Owned lists count the main yard and all captured structures"), Number(Current, TEXT("ownedTotal")), 122.);
            TestEqual(TEXT("Owned territories are split into bounded pages of fifty"), Array(Current, TEXT("ownedCells")).Num(), Page < 2 ? 50 : 22);
            TestEqual(TEXT("The complete private bookmark limit remains available"), Array(Current, TEXT("bookmarks")).Num(), 100);
            for (const auto& Value : Array(Current, TEXT("ownedCells")))
            { const FString Id = String(Value->AsObject(), TEXT("id")); TestFalse(TEXT("Paging cannot repeat or omit owned identities"), Seen.Contains(Id)); Seen.Add(Id); }
        }
        TestEqual(TEXT("Paging exposes every owned cell exactly once"), Seen.Num(), 122);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapRelocationTest, "BackyardMMO.MapRoom.AtomicRandomRelocationRetainsHoldings",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapRelocationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Seed; if (!Load(*this, Seed, Save)) return false;
        const FObject HomeView = Map(Seed); const FString OldHome = String(HomeView, TEXT("homeCellId"));
        const FObject Resource = Selected(Seed, TEXT("cell-47-21")); Save = Seed.Serialize(); ClaimFixture(Save, Resource, Owner);
        const FIntPoint OldPosition(static_cast<int32>(Number(HomeView, TEXT("homeX"))), static_cast<int32>(Number(HomeView, TEXT("homeY"))));
        for (const FIntPoint& P : BYMMapRoomRules::Neighbors(OldPosition))
            ClaimFixture(Save, Selected(Seed, FString::Printf(TEXT("cell-%d-%d"), P.X, P.Y)), Owner);
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        TestEqual(TEXT("Six same-owner original defenders protect their main yard"), Number(Selected(Sim, OldHome), TEXT("damageReduction")), 75.);
        const double Before = Wallet(Sim, TEXT("twigs")); const FObject BeforeYard = SavedYard(Sim.Serialize());
        FObject P = Obj(); P->SetStringField(TEXT("mode"), TEXT("random")); P->SetStringField(TEXT("expectedHome"), TEXT("stale-home"));
        TestFalse(TEXT("Stale relocation cannot erase the current sector"), Flag(Execute(Sim, TEXT("mapRelocate"), P), TEXT("ok")));
        TestEqual(TEXT("Failed relocation leaves main identity unchanged"), String(Map(Sim), TEXT("homeCellId")), OldHome);
        P->SetStringField(TEXT("expectedHome"), OldHome);
        TestTrue(TEXT("Original random relocation succeeds with a checked current sector"), Flag(Execute(Sim, TEXT("mapRelocate"), P), TEXT("ok")));
        const FObject Current = Map(Sim); const FString NewHome = String(Current, TEXT("homeCellId"));
        TestFalse(TEXT("A successful relocation chooses a different legal sector"), NewHome == OldHome);
        TestEqual(TEXT("Source random relocation does not charge an invented resource fee"), Wallet(Sim, TEXT("twigs")), Before);
        TestEqual(TEXT("Distant resource ownership survives moving the backyard"), String(Selected(Sim, TEXT("cell-47-21")), TEXT("ownerId")), Owner);
        TestEqual(TEXT("The new surrounding defenders begin neutral"), Number(Selected(Sim, NewHome), TEXT("damageReduction")), 0.);
        const FObject After = Sim.Serialize(); const FObject AfterYard = SavedYard(After);
        for (const auto& Value : Array(BeforeYard, TEXT("buildings")))
        {
            const FObject Original = Value->AsObject(), Restored = Find(Array(AfterYard, TEXT("buildings")), String(Original, TEXT("id")));
            for (const TCHAR* Key : {TEXT("type"), TEXT("level"), TEXT("hp"), TEXT("x"), TEXT("y")}) TestEqual(TEXT("World relocation retains the complete built backyard"), Number(Restored, Key), Number(Original, Key));
        }
        TestFalse(TEXT("A stale relocation request cannot move the yard a second time"), Flag(Execute(Sim, TEXT("mapRelocate"), P), TEXT("ok")));
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, After)) return false;
        TestEqual(TEXT("Relocated map identity survives native host world persistence"), String(Map(Reloaded), TEXT("homeCellId")), NewHome);
        const FObject Target = FirstCell(Reloaded, 102);
        if (Target && Flag(Execute(Reloaded, TEXT("mapInvade"), AttackRequest(Target)), TEXT("ok")))
        { P->SetStringField(TEXT("expectedHome"), NewHome); TestFalse(TEXT("An active invasion blocks relocation"), Flag(Execute(Reloaded, TEXT("mapRelocate"), P), TEXT("ok"))); }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapScoutCacheTest, "BackyardMMO.MapRoom.EvictedScoutNeverFallsBackToHomeArmy",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapScoutCacheTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FObject View = Map(Sim, 250, 250, 15); FString FirstCellId, FirstYardId; int32 Scouted = 0;
        for (const auto& Value : Array(View, TEXT("cells")))
        {
            const FObject Cell = Value->AsObject(); const int32 Kind = static_cast<int32>(Number(Cell, TEXT("kind")));
            if (Kind != 1 && (Kind < 102 || Kind > 104)) continue;
            const FString CellId = String(Cell, TEXT("id"));
            if (Scouted == 0) { FirstCellId = CellId; FirstYardId = String(Cell, TEXT("yardId")); }
            if (!TestTrue(TEXT("A real map site can be inspected through the host"), Flag(Execute(Sim, TEXT("mapInspect"), CellRequest(CellId)), TEXT("ok")))) return false;
            if (++Scouted == 270) break;
        }
        if (!TestTrue(TEXT("The fixture exceeds the bounded pristine-site cache"), Scouted > 256)) return false;
        const FObject Viewed = Decode(Sim.Snapshot(Owner, FirstYardId)), Yard = Child(Viewed, TEXT("yard"));
        TestEqual(TEXT("A previously selected evicted map site is reconstructed instead of replaced by home"), String(Yard, TEXT("id")), FirstYardId);
        TestEqual(TEXT("Resident provenance identifies that reconstructed enemy yard"), String(Yard, TEXT("residentYardId")), FirstYardId);
        TestEqual(TEXT("The original empty site cannot inherit the viewer's twenty trained monsters"), Array(Yard, TEXT("residentRoster")).Num(), 0);
        TestEqual(TEXT("The viewer's separate home inventory remains intact"), Number(Child(Child(Viewed, TEXT("home")), TEXT("army")), TEXT("C1")), 20.);
        Save = Sim.Serialize();
        TestEqual(TEXT("Repeated pristine scouts never become permanent save payloads"), Array(Child(Save, TEXT("worldMap")), TEXT("sites")).Num(), 0);
        TestEqual(TEXT("Lazy map sites do not leak into the main player and bot yard array"), Array(Save, TEXT("yards")).Num(), 2);
        TestTrue(TEXT("The first cell remains a valid original scout target"), Flag(Selected(Sim, FirstCellId), TEXT("canScout")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapCachedAllocationTest, "BackyardMMO.MapRoom.NewPlayerReplacesCachedTribeWithoutGhostAccount",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapCachedAllocationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        FBYMSimulation Reference, Sim; if (!Load(*this, Reference, Clone(Save)) || !Load(*this, Sim, Clone(Save))) return false;
        FString NewOwner, Chosen; FObject Source; FString Error;
        for (int32 Index = 0; Index < 100; ++Index)
        {
            const FString Candidate = FString::Printf(TEXT("allocation-probe-%d"), Index);
            if (!Reference.AddPlayer(Candidate, Candidate, Error)) { AddError(Error); return false; }
            const FString Home = String(Map(Reference, -1, -1, 12, FString(), Candidate), TEXT("homeCellId"));
            Reference.RemovePlayer(Candidate);
            const FObject Original = Selected(Sim, Home);
            if (Number(Original, TEXT("kind")) == 1) { NewOwner = Candidate; Chosen = Home; Source = Original; break; }
        }
        if (!TestFalse(TEXT("The generated source world supplies a real tribe cell for deterministic allocation"), Chosen.IsEmpty())) return false;
        TestTrue(TEXT("The future sector can be cached by ordinary scouting"), Flag(Execute(Sim, TEXT("mapInspect"), CellRequest(Chosen)), TEXT("ok")));
        // Start then cancel scouting to retain an explicit, unchanged site
        // record as well as the cache; allocation must clean both aliases.
        if (!TestTrue(TEXT("The future tribe sector accepts a normal checked attack"), Flag(Execute(Sim, TEXT("mapInvade"), AttackRequest(Source)), TEXT("ok")))) return false;
        TestTrue(TEXT("Undeployed scouting can be cancelled without a shield"), Flag(Execute(Sim, TEXT("retreat"), Obj()), TEXT("ok")));
        TestTrue(TEXT("The previously viewed tribe has a durable dirty-site row before allocation"), SavedSite(Sim.Serialize(), Chosen).IsValid());
        if (!TestTrue(TEXT("A new account allocates into the validated deterministic source sector"), Sim.AddPlayer(NewOwner, NewOwner, Error))) return false;
        TestEqual(TEXT("Allocation still chooses the same source sector after scouting"), String(Map(Sim, -1, -1, 12, FString(), NewOwner), TEXT("homeCellId")), Chosen);
        Save = Sim.Serialize();
        TestEqual(TEXT("Cached tribal yards never become ghost player accounts"), Array(Save, TEXT("yards")).Num(), 3);
        TestFalse(TEXT("The replaced lazy tribe identity is removed from the main account array"), SavedYard(Save, TEXT("map-") + Chosen).IsValid());
        TestFalse(TEXT("The overwritten neutral site no longer leaves a dirty territory row"), SavedSite(Save, Chosen).IsValid());
        FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
        TestEqual(TEXT("Allocation after scouting produces a self-consistent reloadable world"), Array(Reloaded.Serialize(), TEXT("yards")).Num(), 3);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapRetaliationTest, "BackyardMMO.MapRoom.LevelProtectionAndPersistentRetaliationCredits",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapRetaliationTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Guard; FObject Save = EnabledSave(*this); if (!Save) return false;
        SavedYard(Save, Other)->SetNumberField(TEXT("earnedExperience"), static_cast<double>(BYMExperience::Threshold(49)));
        FBYMSimulation Sim; if (!Load(*this, Sim, Save)) return false;
        const FString LowHome = String(Map(Sim), TEXT("homeCellId")), HighHome = String(Map(Sim, -1, -1, 12, FString(), Other), TEXT("homeCellId"));
        TestTrue(TEXT("Fixture spans more than the source eleven-level protection window"), Number(Selected(Sim, HighHome), TEXT("level")) - Number(Selected(Sim, LowHome), TEXT("level")) > 11.);
        TestFalse(TEXT("A high-level player cannot attack downward without owed retaliation"), Flag(Execute(Sim, TEXT("mapInvade"), AttackRequest(Selected(Sim, LowHome, Other)), Other), TEXT("ok")));
        TestTrue(TEXT("The lower-level player can initiate an upward challenge"), Flag(Execute(Sim, TEXT("mapInvade"), AttackRequest(Selected(Sim, HighHome))), TEXT("ok")));
        TestTrue(TEXT("The scouting attack can end without fabricating damage"), Flag(Execute(Sim, TEXT("retreat"), Obj()), TEXT("ok")));
        TestEqual(TEXT("The stronger defender receives exactly one source retaliation credit"), Number(Selected(Sim, LowHome, Other), TEXT("retaliationsOwed")), 1.);
        Save = Sim.Serialize(); FBYMSimulation Reloaded; if (!Load(*this, Reloaded, Save)) return false;
        TestEqual(TEXT("Retaliation credit persists in the same host world state"), Number(Selected(Reloaded, LowHome, Other), TEXT("retaliationsOwed")), 1.);
        FObject Quote = AttackRequest(Selected(Reloaded, LowHome, Other)), Bad = Clone(Quote); Bad->SetNumberField(TEXT("expectedRevision"), Number(Bad, TEXT("expectedRevision")) + 1.);
        TestFalse(TEXT("A stale counterattack cannot consume the credit"), Flag(Execute(Reloaded, TEXT("mapInvade"), Bad, Other), TEXT("ok")));
        TestEqual(TEXT("Rejected admission retains the owed response"), Number(Selected(Reloaded, LowHome, Other), TEXT("retaliationsOwed")), 1.);
        Reloaded.SetPlayerOnline(Owner, true);
        TestFalse(TEXT("Retaliation does not bypass online main-yard protection"), Flag(Execute(Reloaded, TEXT("mapInvade"), Quote, Other), TEXT("ok")));
        TestEqual(TEXT("Online rejection cannot spend a retaliation credit"), Number(Selected(Reloaded, LowHome, Other), TEXT("retaliationsOwed")), 1.);
        Reloaded.SetPlayerOnline(Owner, false);
        TestTrue(TEXT("The owed source counterattack crosses the level gap once"), Flag(Execute(Reloaded, TEXT("mapInvade"), Quote, Other), TEXT("ok")));
        TestEqual(TEXT("Accepted counterattack consumes precisely one credit"), Number(Selected(Reloaded, LowHome, Other), TEXT("retaliationsOwed")), 0.);
        TestTrue(TEXT("The counterattack can end normally"), Flag(Execute(Reloaded, TEXT("retreat"), Obj(), Other), TEXT("ok")));
        TestFalse(TEXT("The consumed response cannot replay through another attack"), Flag(Execute(Reloaded, TEXT("mapInvade"), Quote, Other), TEXT("ok")));
        for (const double Invalid : {-1., .5, 2147483648.})
        {
            FObject Corrupt = Clone(Save);
            for (const auto& Value : Array(Child(Corrupt, TEXT("worldMap")), TEXT("players"))) if (String(Value->AsObject(), TEXT("id")) == Other)
            { FObject Owed = Obj(); Owed->SetNumberField(Owner, Invalid); Value->AsObject()->SetObjectField(TEXT("retaliations"), Owed); }
            FBYMSimulation Check; FString Error; TestFalse(TEXT("Persisted retaliation counters reject negative, fractional and overflowing quantities"), Check.Initialize(Corrupt, Error));
        }
        return true;
    }
}
#endif
