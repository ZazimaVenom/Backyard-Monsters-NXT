#if WITH_DEV_AUTOMATION_TESTS

#include "BYMBoard.h"
#include "BYMSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace BYMSpriteAtlasPresentationTests
{
    // Integration coverage for the real Board/material path. The separate
    // atlas-helper tests cover malformed metadata and fractional/padded sheets.
    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Residents = Settings->MaxVisibleYardMonsters;
        const int32 Cells = Settings->YardCells;
        const float CellSize = Settings->CellSize;
        const bool Music = Settings->bMusicEnabled;
        FSettingsGuard()
        {
            Settings->MaxVisibleYardMonsters = 50;
            Settings->YardCells = 40;
            Settings->CellSize = 100.f;
            Settings->bMusicEnabled = false;
        }
        ~FSettingsGuard()
        {
            Settings->MaxVisibleYardMonsters = Residents;
            Settings->YardCells = Cells;
            Settings->CellSize = CellSize;
            Settings->bMusicEnabled = Music;
        }
    };

    struct FResidentPlane
    {
        UStaticMeshComponent* Mesh = nullptr;
        UMaterialInstanceDynamic* Material = nullptr;
    };

    FString MakeSnapshot(const TArray<FString>& Identities)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true);
        Root->SetStringField(TEXT("selfId"), TEXT("atlas-presentation-yard"));
        TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
        Rules->SetNumberField(TEXT("yardCells"), 40);
        Rules->SetNumberField(TEXT("cellSize"), 100);
        Root->SetObjectField(TEXT("rules"), Rules);
        TSharedRef<FJsonObject> Yard = MakeShared<FJsonObject>();
        Yard->SetStringField(TEXT("id"), TEXT("atlas-presentation-yard"));
        Yard->SetArrayField(TEXT("buildings"), TArray<TSharedPtr<FJsonValue>>());
        TArray<TSharedPtr<FJsonValue>> Residents;
        for (const FString& Identity : Identities)
        {
            TSharedRef<FJsonObject> Resident = MakeShared<FJsonObject>();
            Resident->SetStringField(TEXT("id"), Identity);
            Resident->SetStringField(TEXT("type"), TEXT("C1"));
            Residents.Add(MakeShared<FJsonValueObject>(Resident));
        }
        Yard->SetArrayField(TEXT("residentRoster"), Residents);
        Root->SetObjectField(TEXT("yard"), Yard);
        FString Text;
        FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
        return Text;
    }

    TArray<FResidentPlane> ResidentPlanes(ABYMBoard& Board, UTexture2D& Atlas)
    {
        TArray<FResidentPlane> Result;
        TInlineComponentArray<UStaticMeshComponent*> Components(&Board);
        for (UStaticMeshComponent* Mesh : Components)
        {
            UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            if (Material && Material->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == &Atlas)
            {
                FResidentPlane Plane; Plane.Mesh = Mesh; Plane.Material = Material;
                Result.Add(Plane);
            }
        }
        return Result;
    }

    bool ValidatePlanes(FAutomationTestBase& Test, const TArray<FResidentPlane>& Planes, int32 ExpectedCount,
        TSet<int32>& ObservedColumns)
    {
        if (!Test.TestEqual(TEXT("Every roster entry has one original Pokey atlas plane"), Planes.Num(), ExpectedCount)) return false;
        TSet<UMaterialInstanceDynamic*> Materials;
        for (const FResidentPlane& Plane : Planes)
        {
            Materials.Add(Plane.Material);
            const FLinearColor Scale = Plane.Material->K2_GetVectorParameterValue(TEXT("UVScale"));
            const FLinearColor Offset = Plane.Material->K2_GetVectorParameterValue(TEXT("UVOffset"));
            // Original sprite.1.v1.png is 720x21: thirty 24x21 facing columns.
            // The material samples texel centres inside exactly one column.
            const int32 Column = FMath::RoundToInt((Offset.R * 720.f - .5f) / 24.f);
            const bool bValid = Plane.Mesh->IsRegistered() && Plane.Mesh->IsVisible()
                && FMath::IsFinite(Scale.R) && FMath::IsFinite(Scale.G)
                && FMath::IsFinite(Offset.R) && FMath::IsFinite(Offset.G)
                && FMath::IsNearlyEqual(Scale.R, 23.f / 720.f, 0.000001f)
                && FMath::IsNearlyEqual(Scale.G, 20.f / 21.f, 0.000001f)
                && Column >= 0 && Column < 30
                && FMath::IsNearlyEqual(Offset.R, (Column * 24.f + .5f) / 720.f, 0.000001f)
                && FMath::IsNearlyEqual(Offset.G, .5f / 21.f, 0.000001f)
                && FMath::IsNearlyEqual(Plane.Mesh->GetRelativeScale3D().X * 100., 120., 0.0001)
                && FMath::IsNearlyEqual(Plane.Mesh->GetRelativeScale3D().Y * 100., 105., 0.0001);
            if (!bValid)
            {
                Test.AddError(FString::Printf(TEXT("Pokey atlas plane lost its 24x21 source frame: scale=(%.9g,%.9g), offset=(%.9g,%.9g), column=%d, visible=%d."),
                    Scale.R, Scale.G, Offset.R, Offset.G, Column, Plane.Mesh->IsVisible() ? 1 : 0));
                return false;
            }
            ObservedColumns.Add(Column);
        }
        return Test.TestEqual(TEXT("Each resident owns an independent dynamic material"), Materials.Num(), ExpectedCount);
    }

    bool ReadSourcePixels(FAutomationTestBase& Test, UTexture2D& Atlas, TArray<uint8>& Out)
    {
        // Read the imported original source, never the asynchronous rendering
        // resource. No reimport, pixel resampling or texture repair is performed.
        if (!Test.TestTrue(TEXT("Original Pokey source remains 720x21 BGRA8"),
            Atlas.Source.GetSizeX() == 720 && Atlas.Source.GetSizeY() == 21 && Atlas.Source.GetFormat() == TSF_BGRA8)) return false;
        const uint8* Pixels = Atlas.Source.LockMipReadOnly(0);
        if (Pixels)
        {
            Out.SetNumUninitialized(720 * 21 * 4);
            FMemory::Memcpy(Out.GetData(), Pixels, Out.Num());
        }
        Atlas.Source.UnlockMip(0);
        return Test.TestTrue(TEXT("Original imported sprite pixels are readable"), Pixels != nullptr);
    }

    FString VisitSnapshot(const FString& Viewed, const FString& Type, int32 Count,
        bool bBattle = false, const FString& RosterOwner = FString())
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true); Root->SetStringField(TEXT("selfId"), TEXT("resident-home"));
        TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
        Rules->SetNumberField(TEXT("baseYardCells"), 40); Rules->SetNumberField(TEXT("cellSize"), 100);
        Root->SetObjectField(TEXT("rules"), Rules);
        auto MakeYard = [](const FString& Id, const FString& Species, int32 Size)
        {
            TSharedRef<FJsonObject> Yard = MakeShared<FJsonObject>();
            Yard->SetStringField(TEXT("id"), Id); Yard->SetStringField(TEXT("residentYardId"), Id);
            Yard->SetArrayField(TEXT("buildings"), TArray<TSharedPtr<FJsonValue>>());
            TArray<TSharedPtr<FJsonValue>> Roster;
            for (int32 Index = 0; Index < Size; ++Index)
            {
                TSharedRef<FJsonObject> Resident = MakeShared<FJsonObject>();
                // Deliberately collide local roster identities across yards.
                Resident->SetStringField(TEXT("id"), FString::Printf(TEXT("shared:%d"), Index));
                Resident->SetStringField(TEXT("type"), Species);
                Roster.Add(MakeShared<FJsonValueObject>(Resident));
            }
            Yard->SetArrayField(TEXT("residentRoster"), Roster);
            return Yard;
        };
        // A home roster must never be a fallback for an empty or foreign yard.
        Root->SetObjectField(TEXT("home"), MakeYard(TEXT("resident-home"), TEXT("C1"), 2));
        const TSharedRef<FJsonObject> Yard = MakeYard(Viewed, Type, Count);
        if (!RosterOwner.IsEmpty()) Yard->SetStringField(TEXT("residentYardId"), RosterOwner);
        Root->SetObjectField(TEXT("yard"), Yard);
        if (bBattle)
        {
            TSharedRef<FJsonObject> Battle = MakeShared<FJsonObject>();
            Battle->SetStringField(TEXT("id"), TEXT("resident-visit-battle"));
            Battle->SetNumberField(TEXT("serverTime"), 100.);
            Battle->SetArrayField(TEXT("troops"), TArray<TSharedPtr<FJsonValue>>());
            Root->SetObjectField(TEXT("battle"), Battle);
        }
        FString Text; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)); return Text;
    }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMSpriteAtlasResidentsTest, "BackyardMMO.Presentation.ResidentSpriteAtlasUVs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMSpriteAtlasResidentsTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    UTexture2D* Atlas = LoadObject<UTexture2D>(nullptr,
        *ABYMBoard::AssetObjectPath(TEXT("monsters/sprite.1.v1.png")));
    UMaterialInterface* SpriteMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/BackyardMMO/Materials/M_Sprite.M_Sprite"));
    UMaterialInterface* AlphaMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/BackyardMMO/Materials/M_SpriteAlpha.M_SpriteAlpha"));
    if (!Atlas || !SpriteMaterial || !AlphaMaterial)
    {
        AddError(TEXT("Resident atlas presentation test requires the original Pokey atlas and generated sprite materials. Finish the plugin's normal editor setup before running this test."));
        return false;
    }
    TArray<uint8> OriginalPixels;
    if (!ReadSourcePixels(*this, *Atlas, OriginalPixels)) return false;
    const auto OriginalCompression = Atlas->CompressionSettings;
    const bool OriginalSRGB = Atlas->SRGB;

    const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
        TEXT("BYMSpriteAtlasTestWorld"));
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, WorldName, GetTransientPackage(), true);
    if (!TestNotNull(TEXT("Create an isolated editor world for actual sprite materials"), World)) return false;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        if (World->IsRooted()) World->RemoveFromRoot();
    };
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags = RF_Transient;
    ABYMBoard* Board = World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(), FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Spawn the real board in the transient editor world"), Board)) return false;
    TestFalse(TEXT("Sprite validation does not start an account or enter Play"), Board->HasActorBegunPlay());
    if (!TestTrue(TEXT("Original presentation content resolves"), Board->GetPresentationError().IsEmpty())) return false;

    const FString Snapshot = MakeSnapshot({ TEXT("C1:0"), TEXT("C1:1"), TEXT("C1:2") });
    Board->ApplySnapshot(Snapshot);
    TSet<int32> ObservedColumns;
    TArray<FResidentPlane> Planes = ResidentPlanes(*Board, *Atlas);
    if (!TestEqual(TEXT("A first snapshot creates one material for each resident even while texture compilation is pending"), Planes.Num(), 3)) return false;
    TMap<UStaticMeshComponent*, UMaterialInstanceDynamic*> OriginalMaterials;
    for (const FResidentPlane& Plane : Planes) OriginalMaterials.Add(Plane.Mesh, Plane.Material);
    if (Atlas->IsDefaultTexture())
        for (const FResidentPlane& Plane : Planes)
            TestFalse(TEXT("Pending texture resources are hidden instead of displayed with placeholder UVs"), Plane.Mesh->IsVisible());
    // Bound the asynchronous setup within this editor test. The normal Board
    // retries on future ticks; it must not cache the first placeholder layout.
    // Finish only this test atlas, then exercise that same public retry path.
    UTexture* Textures[] = { Atlas };
    FTextureCompilingManager::Get().FinishCompilation(Textures);
    if (!TestFalse(TEXT("The test atlas has finished its asynchronous resource setup"), Atlas->IsDefaultTexture())) return false;
    Board->Tick(0.f);
    Planes = ResidentPlanes(*Board, *Atlas);
    if (!ValidatePlanes(*this, Planes, 3, ObservedColumns)) return false;
    TMap<UStaticMeshComponent*, FVector> PreviousLocations;
    TMap<UStaticMeshComponent*, int32> PreviousColumns;
    for (const FResidentPlane& Plane : Planes)
    {
        if (!TestTrue(TEXT("Resource readiness reconfigures the existing resident material instead of replacing it"),
            OriginalMaterials.FindRef(Plane.Mesh) == Plane.Material)) return false;
        PreviousLocations.Add(Plane.Mesh, Plane.Mesh->GetRelativeLocation());
        const float Offset = Plane.Material->K2_GetVectorParameterValue(TEXT("UVOffset")).R;
        PreviousColumns.Add(Plane.Mesh, FMath::RoundToInt((Offset * 720.f - .5f) / 24.f));
    }
    // Pausing time must keep each full sprite frame, not expose the whole sheet
    // or shrink a resident into one of the reported narrow pink strips.
    Board->Tick(0.f);
    Planes = ResidentPlanes(*Board, *Atlas);
    if (!ValidatePlanes(*this, Planes, 3, ObservedColumns)) return false;
    for (const FResidentPlane& Plane : Planes)
        TestTrue(TEXT("Paused residents keep their positions"), Plane.Mesh->GetRelativeLocation().Equals(PreviousLocations.FindChecked(Plane.Mesh), 0.0001));

    bool bMoved = false, bPausedAfterMovement = false, bChangedFacing = false;
    TSet<UStaticMeshComponent*> MovedPlanes;
    for (int32 Frame = 0; Frame < 900; ++Frame)
    {
        Board->Tick(1.f / 30.f);
        if (Frame % 30 == 0) Board->ApplySnapshot(Snapshot);
        Planes = ResidentPlanes(*Board, *Atlas);
        if (!ValidatePlanes(*this, Planes, 3, ObservedColumns)) return false;
        for (const FResidentPlane& Plane : Planes)
        {
            if (!TestTrue(TEXT("Snapshot refresh keeps each resident's independent material"),
                OriginalMaterials.FindRef(Plane.Mesh) == Plane.Material)) return false;
            const FVector Position = Plane.Mesh->GetRelativeLocation();
            const bool bChangedPosition = !Position.Equals(PreviousLocations.FindChecked(Plane.Mesh), 0.0001);
            bPausedAfterMovement |= MovedPlanes.Contains(Plane.Mesh) && !bChangedPosition;
            bMoved |= bChangedPosition;
            if (bChangedPosition) MovedPlanes.Add(Plane.Mesh);
            const float Offset = Plane.Material->K2_GetVectorParameterValue(TEXT("UVOffset")).R;
            const int32 Column = FMath::RoundToInt((Offset * 720.f - .5f) / 24.f);
            bChangedFacing |= bChangedPosition && Column != PreviousColumns.FindChecked(Plane.Mesh);
            PreviousLocations.Add(Plane.Mesh, Position);
            PreviousColumns.Add(Plane.Mesh, Column);
        }
    }
    TestTrue(TEXT("Real yard residents move while retaining full atlas frames"), bMoved);
    TestTrue(TEXT("Wandering residents pause without changing atlas dimensions"), bPausedAfterMovement);
    TestTrue(TEXT("Walking turns select another valid original facing column"), bChangedFacing && ObservedColumns.Num() > 1);

    // Reconcile the same yard: retain residents 0 and 2, remove 1, add 3 and 4.
    const FString Updated = MakeSnapshot({ TEXT("C1:0"), TEXT("C1:2"), TEXT("C1:3"), TEXT("C1:4") });
    Board->ApplySnapshot(Updated);
    Planes = ResidentPlanes(*Board, *Atlas);
    if (!ValidatePlanes(*this, Planes, 4, ObservedColumns)) return false;
    int32 RetainedMaterials = 0;
    TMap<UStaticMeshComponent*, UMaterialInstanceDynamic*> ReconciledMaterials;
    for (const FResidentPlane& Plane : Planes)
    {
        if (OriginalMaterials.FindRef(Plane.Mesh) == Plane.Material) ++RetainedMaterials;
        ReconciledMaterials.Add(Plane.Mesh, Plane.Material);
    }
    TestEqual(TEXT("Roster reconciliation retains exactly the two surviving residents' materials"), RetainedMaterials, 2);
    for (int32 Frame = 0; Frame < 180; ++Frame)
    {
        Board->Tick(1.f / 30.f);
        if (Frame % 30 == 0) Board->ApplySnapshot(Updated);
        Planes = ResidentPlanes(*Board, *Atlas);
        if (!ValidatePlanes(*this, Planes, 4, ObservedColumns)) return false;
        for (const FResidentPlane& Plane : Planes)
            if (!TestTrue(TEXT("New and retained residents keep separate materials after refresh"),
                ReconciledMaterials.FindRef(Plane.Mesh) == Plane.Material)) return false;
    }
    TArray<uint8> AfterPixels;
    if (!ReadSourcePixels(*this, *Atlas, AfterPixels)) return false;
    TestTrue(TEXT("Wandering and directional UV changes preserve every original sprite source byte"), OriginalPixels == AfterPixels);
    TestTrue(TEXT("Presentation never changes the imported sprite's compression or color interpretation"),
        Atlas->CompressionSettings == OriginalCompression && Atlas->SRGB == OriginalSRGB);
    TestTrue(TEXT("No asset error is introduced by repeated frame selection"), Board->GetPresentationError().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMVisitedYardResidentTransitionTest, "BackyardMMO.Presentation.VisitedYardResidentIdentityAndBattleTransitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBYMVisitedYardResidentTransitionTest::RunTest(const FString& Parameters)
{
    FSettingsGuard Settings;
    UTexture2D* HomeAtlas = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(TEXT("monsters/sprite.1.v1.png")));
    UTexture2D* RivalAtlas = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(TEXT("monsters/octoooze.png")));
    if (!HomeAtlas || !RivalAtlas)
    { AddError(TEXT("Complete normal editor setup to import original Pokey and Octo-ooze artwork before this test.")); return false; }
    UTexture* Textures[] = {HomeAtlas, RivalAtlas}; FTextureCompilingManager::Get().FinishCompilation(Textures);
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("BYMResidentVisitWorld"));
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, Name, GetTransientPackage(), true);
    if (!TestNotNull(TEXT("Create the real resident transition world"), World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); if (World->IsRooted()) World->RemoveFromRoot(); };
    FActorSpawnParameters Spawn; Spawn.ObjectFlags = RF_Transient;
    ABYMBoard* Board = World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(), FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Spawn the production Board"), Board)) return false;
    auto Apply = [Board](const FString& Snapshot) { Board->ApplySnapshot(Snapshot); Board->Tick(0.f); };
    Apply(VisitSnapshot(TEXT("resident-home"), TEXT("C1"), 2));
    TestEqual(TEXT("Home initially displays its own trained Pokeys"), ResidentPlanes(*Board, *HomeAtlas).Num(), 2);
    Apply(VisitSnapshot(TEXT("resident-rival"), TEXT("C2"), 3));
    TestEqual(TEXT("Scouting destroys home residents before showing a rival"), ResidentPlanes(*Board, *HomeAtlas).Num(), 0);
    TestEqual(TEXT("Before attacking, the rival displays its own trained Octo-ooze"), ResidentPlanes(*Board, *RivalAtlas).Num(), 3);
    Apply(VisitSnapshot(TEXT("resident-rival"), TEXT("C2"), 0));
    TestEqual(TEXT("An empty rival cannot reuse the player's ready army"), ResidentPlanes(*Board, *HomeAtlas).Num(), 0);
    TestEqual(TEXT("Empty rival removes its former residents"), ResidentPlanes(*Board, *RivalAtlas).Num(), 0);
    Apply(VisitSnapshot(TEXT("resident-rival"), TEXT("C1"), 1, false, TEXT("resident-home")));
    TestEqual(TEXT("A mismatched roster provenance is never displayed"), ResidentPlanes(*Board, *HomeAtlas).Num(), 0);
    Apply(VisitSnapshot(TEXT("resident-rival"), TEXT("C2"), 3));
    Apply(VisitSnapshot(TEXT("resident-rival"), TEXT("C1"), 1));
    TestEqual(TEXT("A same-yard roster species change discards its former texture"), ResidentPlanes(*Board, *RivalAtlas).Num(), 0);
    TestEqual(TEXT("The replacement species receives its own actual atlas"), ResidentPlanes(*Board, *HomeAtlas).Num(), 1);
    Apply(VisitSnapshot(TEXT("resident-rival"), TEXT("C2"), 3, true));
    TestEqual(TEXT("Battle entry removes all cosmetic home-like residents"), ResidentPlanes(*Board, *HomeAtlas).Num(), 0);
    TestEqual(TEXT("Battle entry removes cosmetic rival residents"), ResidentPlanes(*Board, *RivalAtlas).Num(), 0);
    Apply(VisitSnapshot(TEXT("resident-rival"), TEXT("C2"), 3));
    TestEqual(TEXT("After battle, only the viewed rival roster returns"), ResidentPlanes(*Board, *RivalAtlas).Num(), 3);
    TestEqual(TEXT("Post-battle viewing cannot restore the home roster"), ResidentPlanes(*Board, *HomeAtlas).Num(), 0);
    Apply(VisitSnapshot(TEXT("resident-home"), TEXT("C1"), 2));
    TestEqual(TEXT("Returning home clears the rival's roster"), ResidentPlanes(*Board, *RivalAtlas).Num(), 0);
    TestEqual(TEXT("Returning home restores its own roster"), ResidentPlanes(*Board, *HomeAtlas).Num(), 2);
    TestTrue(TEXT("Transitions retain valid original assets"), Board->GetPresentationError().IsEmpty());
    return true;
}
} // namespace BYMSpriteAtlasPresentationTests

#endif
