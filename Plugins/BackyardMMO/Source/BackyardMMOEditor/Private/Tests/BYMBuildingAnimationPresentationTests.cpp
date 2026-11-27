#if WITH_DEV_AUTOMATION_TESTS
#include "BYMBoard.h"
#include "BYMSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace BYMBuildingAnimationPresentationTests
{
    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const int32 Residents = Settings->MaxVisibleYardMonsters;
        const bool Music = Settings->bMusicEnabled;
        FSettingsGuard() { Settings->MaxVisibleYardMonsters = 0; Settings->bMusicEnabled = false; }
        ~FSettingsGuard() { Settings->MaxVisibleYardMonsters = Residents; Settings->bMusicEnabled = Music; }
    };
    struct FFixture
    {
        int32 Type = 0;
        TSharedPtr<FJsonObject> Atlas;
        UTexture2D* Texture = nullptr;
    };
    FString MakeSnapshot(bool bWorking, double Fullness, double CollectorHealthRatio = 1., bool bIncludeOppositeCollector = true)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("selfId"), TEXT("building-animation-yard"));
        TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
        Rules->SetNumberField(TEXT("yardCells"), 40); Rules->SetNumberField(TEXT("cellSize"), 100);
        Root->SetObjectField(TEXT("rules"), Rules);
        TSharedRef<FJsonObject> Yard = MakeShared<FJsonObject>();
        Yard->SetStringField(TEXT("id"), TEXT("building-animation-yard"));
        Yard->SetNumberField(TEXT("storageCapacity"), 10000);
        TSharedRef<FJsonObject> Wallet = MakeShared<FJsonObject>();
        for (const TCHAR* Resource : {TEXT("twigs"),TEXT("pebbles"),TEXT("putty"),TEXT("goo")})
            Wallet->SetNumberField(Resource, 10000 * Fullness);
        Yard->SetObjectField(TEXT("resources"), Wallet);
        TArray<TSharedPtr<FJsonValue>> Buildings;
        for (int32 Type : {1,6,8,13,26})
        {
            TSharedRef<FJsonObject> Building = MakeShared<FJsonObject>();
            Building->SetStringField(TEXT("id"), FString::Printf(TEXT("animation_%d"), Type));
            Building->SetNumberField(TEXT("type"), Type); Building->SetNumberField(TEXT("level"), 1);
            Building->SetNumberField(TEXT("size"), 5); Building->SetNumberField(TEXT("maxHp"), 1000);
            Building->SetNumberField(TEXT("hp"), Type == 1 ? 1000 * CollectorHealthRatio : 1000);
            Building->SetNumberField(TEXT("x"), Type); Building->SetNumberField(TEXT("y"), 10);
            Building->SetNumberField(TEXT("readyAt"), 0); Building->SetBoolField(TEXT("working"), bWorking);
            Buildings.Add(MakeShared<FJsonValueObject>(Building));
        }
        if (bIncludeOppositeCollector)
        {
            TSharedRef<FJsonObject> Building = MakeShared<FJsonObject>();
            Building->SetStringField(TEXT("id"), TEXT("opposite_activity_collector"));
            Building->SetNumberField(TEXT("type"), 1); Building->SetNumberField(TEXT("level"), 1);
            Building->SetNumberField(TEXT("size"), 5); Building->SetNumberField(TEXT("maxHp"), 1000);
            Building->SetNumberField(TEXT("hp"), 1000);
            Building->SetNumberField(TEXT("x"), 30); Building->SetNumberField(TEXT("y"), 30);
            Building->SetNumberField(TEXT("readyAt"), 0); Building->SetBoolField(TEXT("working"), !bWorking);
            Buildings.Add(MakeShared<FJsonValueObject>(Building));
        }
        Yard->SetArrayField(TEXT("buildings"), Buildings);
        Yard->SetArrayField(TEXT("residentRoster"), TArray<TSharedPtr<FJsonValue>>());
        Root->SetObjectField(TEXT("yard"), Yard);
        FString Text; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)); return Text;
    }
    UMaterialInstanceDynamic* FindMaterial(ABYMBoard& Board, const UTexture2D* Texture,
        const UMaterialInstanceDynamic* ExcludedMaterial = nullptr)
    {
        TInlineComponentArray<UStaticMeshComponent*> Components(&Board);
        for (UStaticMeshComponent* Mesh : Components)
        {
            UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            if (Mesh->IsVisible() && Material && Material != ExcludedMaterial
                && Material->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == Texture) return Material;
        }
        return nullptr;
    }
    int32 ReadFrame(FAutomationTestBase& Test, const FFixture& Fixture, UMaterialInstanceDynamic& Material)
    {
        const double Width = Fixture.Atlas->GetNumberField(TEXT("frameWidth"));
        const double Height = Fixture.Atlas->GetNumberField(TEXT("frameHeight"));
        const double SheetWidth = Fixture.Atlas->GetNumberField(TEXT("textureWidth"));
        const double SheetHeight = Fixture.Atlas->GetNumberField(TEXT("textureHeight"));
        const int32 Columns = static_cast<int32>(Fixture.Atlas->GetNumberField(TEXT("columns")));
        const int32 Frames = static_cast<int32>(Fixture.Atlas->GetNumberField(TEXT("frames")));
        const FLinearColor Scale = Material.K2_GetVectorParameterValue(TEXT("UVScale"));
        const FLinearColor Offset = Material.K2_GetVectorParameterValue(TEXT("UVOffset"));
        if (!FMath::IsFinite(Offset.R) || !FMath::IsFinite(Offset.G)) { Test.AddError(TEXT("Building atlas UV offset became non-finite.")); return INDEX_NONE; }
        const int32 Column = FMath::RoundToInt((Offset.R * SheetWidth - .5) / Width);
        const int32 Row = FMath::RoundToInt((Offset.G * SheetHeight - .5) / Height);
        const int32 Frame = Row * Columns + Column;
        if (!Test.TestTrue(TEXT("Building animation samples one complete authored frame with protected edges"),
            Column >= 0 && Column < Columns && Row >= 0 && Frame >= 0 && Frame < Frames
            && FMath::IsNearlyEqual(static_cast<double>(Scale.R), (Width - 1.) / SheetWidth, 1.e-6)
            && FMath::IsNearlyEqual(static_cast<double>(Scale.G), (Height - 1.) / SheetHeight, 1.e-6)
            && FMath::IsNearlyEqual(static_cast<double>(Offset.R), (Column * Width + .5) / SheetWidth, 1.e-6)
            && FMath::IsNearlyEqual(static_cast<double>(Offset.G), (Row * Height + .5) / SheetHeight, 1.e-6))) return INDEX_NONE;
        return Frame;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBuildingActivityMaterialTest, "BackyardMMO.Presentation.BuildingActivityMaterials",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMBuildingActivityMaterialTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings;
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
        FString CatalogText; TSharedPtr<FJsonObject> Catalog;
        if (!Plugin.IsValid() || !FFileHelper::LoadFileToString(CatalogText, *(Plugin->GetContentDir() / TEXT("Data/catalog.json")))
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CatalogText), Catalog))
        { AddError(TEXT("Building animation test requires the bundled original catalog.")); return false; }
        TMap<int32, FFixture> Fixtures;
        TArray<UTexture*> Textures;
        UTexture2D* DamagedCollector = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* BuildingData = nullptr;
        if (!Catalog->TryGetArrayField(TEXT("buildings"), BuildingData)) return false;
        for (const auto& Value : *BuildingData)
        {
            const TSharedPtr<FJsonObject> Definition = Value->AsObject();
            const int32 Type = static_cast<int32>(Definition->GetNumberField(TEXT("id")));
            if (Type != 1 && Type != 6 && Type != 8 && Type != 13 && Type != 26) continue;
            const TSharedPtr<FJsonObject> Level = Definition->GetArrayField(TEXT("levels"))[0]->AsObject();
            FFixture Fixture; Fixture.Type = Type; Fixture.Atlas = Level->GetObjectField(TEXT("animation"));
            Fixture.Texture = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(Fixture.Atlas->GetStringField(TEXT("texture"))));
            if (!Fixture.Texture) { AddError(TEXT("Finish normal editor setup to import original building animation atlases before running this test.")); return false; }
            Textures.Add(Fixture.Texture); Fixtures.Add(Type, Fixture);
            if (Type == 1)
            {
                DamagedCollector = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(Level->GetObjectField(TEXT("damagedComposite"))->GetStringField(TEXT("texture"))));
                if (DamagedCollector) Textures.Add(DamagedCollector);
            }
        }
        if (!TestEqual(TEXT("All five activity/fill fixtures resolve"), Fixtures.Num(), 5)
            || !TestNotNull(TEXT("The original damaged collector image resolves"), DamagedCollector)) return false;
        FTextureCompilingManager::Get().FinishCompilation(Textures);
        const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("BYMBuildingAnimationTestWorld"));
        UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, Name, GetTransientPackage(), true);
        if (!TestNotNull(TEXT("Create an isolated editor world"), World)) return false;
        ON_SCOPE_EXIT { World->DestroyWorld(false); if (World->IsRooted()) World->RemoveFromRoot(); };
        FActorSpawnParameters Spawn; Spawn.ObjectFlags = RF_Transient;
        ABYMBoard* Board = World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(), FTransform::Identity, Spawn);
        if (!TestNotNull(TEXT("Spawn the actual board without Play or server state"), Board)) return false;
        // Bind the first collector before adding a second copy of its atlas.
        // Their independent MIDs identify them without relying on component order.
        Board->ApplySnapshot(MakeSnapshot(true, .5, 1., false));
        TMap<int32, UMaterialInstanceDynamic*> Materials;
        TMap<int32, TSet<int32>> Observed;
        for (const auto& Pair : Fixtures)
        {
            UMaterialInstanceDynamic* Material = FindMaterial(*Board, Pair.Value.Texture);
            if (!TestNotNull(TEXT("An activity building binds its actual imported atlas"), Material)) return false;
            Materials.Add(Pair.Key, Material);
        }
        Board->ApplySnapshot(MakeSnapshot(true, .5));
        const FFixture& CollectorFixture = Fixtures.FindChecked(1);
        UMaterialInstanceDynamic* OppositeMaterial = FindMaterial(*Board, CollectorFixture.Texture, Materials.FindChecked(1));
        if (!TestNotNull(TEXT("A second collector sharing the same atlas has its own material"), OppositeMaterial)
            || !TestTrue(TEXT("Shared texture assets never share mutable building material instances"), OppositeMaterial != Materials.FindChecked(1))) return false;
        const int32 OppositeHeldFrame = ReadFrame(*this, CollectorFixture, *OppositeMaterial);
        if (OppositeHeldFrame == INDEX_NONE) return false;
        for (int32 Tick = 0; Tick < 400; ++Tick)
        {
            Board->Tick(1.f / 40.f);
            if (Tick % 8 == 0) Board->ApplySnapshot(MakeSnapshot(true, .5));
            for (const auto& Pair : Fixtures)
            {
                UMaterialInstanceDynamic* Material = FindMaterial(*Board, Pair.Value.Texture, OppositeMaterial);
                if (!TestTrue(TEXT("Snapshot refresh retains the building material and animation clock"), Material && Materials.FindRef(Pair.Key) == Material)) return false;
                const int32 Frame = ReadFrame(*this, Pair.Value, *Material); if (Frame == INDEX_NONE) return false;
                Observed.FindOrAdd(Pair.Key).Add(Frame);
            }
            if (!TestTrue(TEXT("Snapshot refresh retains the independent second collector material"),
                FindMaterial(*Board, CollectorFixture.Texture, Materials.FindChecked(1)) == OppositeMaterial)
                || !TestEqual(TEXT("Inactive collector holds its pose while a shared-atlas neighbor advances"),
                    ReadFrame(*this, CollectorFixture, *OppositeMaterial), OppositeHeldFrame)) return false;
        }
        for (int32 Type : {1,8,13,26}) TestTrue(TEXT("Working producer/research machinery displays changing original frames"), Observed.FindRef(Type).Num() > 3);
        TestEqual(TEXT("Silo fullness is a stable selected pose, never an idle loop"), Observed.FindRef(6).Num(), 1);
        TestTrue(TEXT("Half-full silo uses source pose13"), Observed.FindRef(6).Contains(13));
        Board->ApplySnapshot(MakeSnapshot(false, .5));
        TMap<int32, int32> PausedFrames;
        for (const auto& Pair : Fixtures) PausedFrames.Add(Pair.Key, ReadFrame(*this, Pair.Value, *Materials.FindChecked(Pair.Key)));
        TestEqual(TEXT("Stopped Hatchery resets to original pose zero"), PausedFrames.FindRef(13), 0);
        TSet<int32> OppositeActiveFrames;
        for (int32 Tick = 0; Tick < 160; ++Tick)
        {
            Board->Tick(1.f / 40.f);
            if (Tick % 8 == 0) Board->ApplySnapshot(MakeSnapshot(false, .5));
            for (const auto& Pair : Fixtures)
                if (!TestEqual(TEXT("Inactive producers hold their displayed source frame"), ReadFrame(*this, Pair.Value, *Materials.FindChecked(Pair.Key)), PausedFrames.FindRef(Pair.Key))) return false;
            if (!TestTrue(TEXT("Switching activity preserves the second collector's independent material"),
                FindMaterial(*Board, CollectorFixture.Texture, Materials.FindChecked(1)) == OppositeMaterial)
                || !TestTrue(TEXT("The now-paused collector retains its original independent material"),
                    FindMaterial(*Board, CollectorFixture.Texture, OppositeMaterial) == Materials.FindChecked(1))) return false;
            const int32 Frame = ReadFrame(*this, CollectorFixture, *OppositeMaterial);
            if (Frame == INDEX_NONE) return false;
            OppositeActiveFrames.Add(Frame);
        }
        TestTrue(TEXT("Previously held collector advances original frames while its shared-atlas neighbor stays paused"), OppositeActiveFrames.Num() > 3);
        Board->ApplySnapshot(MakeSnapshot(false, 1.));
        Board->Tick(3.f / 40.f);
        TestEqual(TEXT("A full silo keeps the last complete frame inside the supplied strip"), ReadFrame(*this, Fixtures.FindChecked(6), *Materials.FindChecked(6)), 25);
        Board->ApplySnapshot(MakeSnapshot(false, 0.));
        Board->Tick(3.f / 40.f);
        TestEqual(TEXT("Empty silo returns to its original empty pose"), ReadFrame(*this, Fixtures.FindChecked(6), *Materials.FindChecked(6)), 0);
        Board->ApplySnapshot(MakeSnapshot(true, .5, .25));
        UMaterialInstanceDynamic* DamagedMaterial = FindMaterial(*Board, DamagedCollector);
        if (!TestNotNull(TEXT("Below-half-health collector uses its damaged source artwork"), DamagedMaterial)) return false;
        TestTrue(TEXT("Normal animation sheet does not cover the original damaged building"), FindMaterial(*Board, CollectorFixture.Texture, OppositeMaterial) == nullptr);
        Board->Tick(1.f);
        TestTrue(TEXT("Damaged static artwork has full-image UVs"), DamagedMaterial->K2_GetVectorParameterValue(TEXT("UVScale")).Equals(FLinearColor(1,1,0,0), .000001f));
        TestTrue(TEXT("Activity transitions introduce no presentation asset error"), Board->GetPresentationError().IsEmpty());
        return true;
    }
}
#endif
