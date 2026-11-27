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
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace BYMBunkerMaterialTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    struct FSettingsGuard
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>();
        const bool Music = Settings->bMusicEnabled, Sounds = Settings->bCombatSoundsEnabled;
        FSettingsGuard() { Settings->bMusicEnabled = false; Settings->bCombatSoundsEnabled = false; }
        ~FSettingsGuard() { Settings->bMusicEnabled = Music; Settings->bCombatSoundsEnabled = Sounds; }
    };
    FString Snapshot(double DoorPhase, bool bOpening, double Health = 10000., bool bReturned = false, bool bBattle = true,
        const FString& DoorState = FString())
    {
        FObject Root = MakeShared<FJsonObject>(), Yard = MakeShared<FJsonObject>(), Rules = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("selfId"), TEXT("bunker-view")); Yard->SetStringField(TEXT("id"), TEXT("bunker-view"));
        Rules->SetNumberField(TEXT("yardCells"), 40); Rules->SetNumberField(TEXT("cellSize"), 100);
        Root->SetObjectField(TEXT("rules"), Rules);
        FObject Building = MakeShared<FJsonObject>();
        Building->SetStringField(TEXT("id"), TEXT("bunker")); Building->SetNumberField(TEXT("type"), 22);
        Building->SetNumberField(TEXT("level"), 1); Building->SetNumberField(TEXT("x"), 20); Building->SetNumberField(TEXT("y"), 20);
        Building->SetNumberField(TEXT("hp"), Health); Building->SetNumberField(TEXT("maxHp"), 10000);
        Building->SetNumberField(TEXT("size"), 6);
        Yard->SetArrayField(TEXT("buildings"), FArray{MakeShared<FJsonValueObject>(Building)});
        Yard->SetArrayField(TEXT("residentRoster"), FArray()); Root->SetObjectField(TEXT("yard"), Yard);
        if (bBattle)
        {
            FObject Battle = MakeShared<FJsonObject>(), Bunker = MakeShared<FJsonObject>();
            Battle->SetStringField(TEXT("id"), TEXT("bunker-material-battle")); Battle->SetStringField(TEXT("phase"), TEXT("active"));
            Battle->SetNumberField(TEXT("serverTime"), 100.);
            Bunker->SetStringField(TEXT("id"), TEXT("bunker")); Bunker->SetNumberField(TEXT("doorFrame"), DoorPhase);
            Bunker->SetBoolField(TEXT("doorOpening"), bOpening); Bunker->SetNumberField(TEXT("doorAt"), 100.);
            Bunker->SetStringField(TEXT("state"), DoorState.IsEmpty() ? (bOpening ? TEXT("opening") : TEXT("closing")) : DoorState);
            Battle->SetArrayField(TEXT("bunkers"), FArray{MakeShared<FJsonValueObject>(Bunker)});
            for (const TCHAR* Field : {TEXT("troops"), TEXT("defenders")})
            {
                const bool bDefender = FCString::Strcmp(Field, TEXT("defenders")) == 0;
                FObject Monster = MakeShared<FJsonObject>();
                // Equal test IDs intentionally verify side-specific visual keys.
                Monster->SetStringField(TEXT("id"), TEXT("same-id")); Monster->SetStringField(TEXT("type"), TEXT("C1"));
                Monster->SetNumberField(TEXT("x"), bDefender ? 22 : 4); Monster->SetNumberField(TEXT("y"), 22);
                Monster->SetNumberField(TEXT("hp"), 200); Monster->SetStringField(TEXT("state"), TEXT("moving"));
                Monster->SetNumberField(TEXT("facingX"), 1); Monster->SetNumberField(TEXT("facingY"), 1);
                Monster->SetNumberField(TEXT("attackSequence"), 0); Monster->SetBoolField(TEXT("returned"), bDefender && bReturned);
                Battle->SetArrayField(Field, FArray{MakeShared<FJsonValueObject>(Monster)});
            }
            Root->SetObjectField(TEXT("battle"), Battle);
        }
        FString Text; FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text;
    }
    TArray<UMaterialInstanceDynamic*> Materials(ABYMBoard& Board, UTexture2D* Texture, bool& bCustomDepth)
    {
        TArray<UMaterialInstanceDynamic*> Result;
        TInlineComponentArray<UStaticMeshComponent*> Components(&Board);
        for (UStaticMeshComponent* Mesh : Components)
        {
            UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            if (Mesh->IsVisible() && Material && Material->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == Texture)
            { Result.Add(Material); bCustomDepth = bCustomDepth && Mesh->bRenderCustomDepth; }
        }
        return Result;
    }
    int32 DoorFrame(UMaterialInstanceDynamic& Material)
    {
        const FLinearColor Offset = Material.K2_GetVectorParameterValue(TEXT("UVOffset"));
        return FMath::RoundToInt((Offset.R * 1104. - .5) / 138.) + 8 * FMath::RoundToInt((Offset.G * 200. - .5) / 100.);
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMBunkerMaterialsTest, "BackyardMMO.Presentation.BunkerMaterialsAndDefenders",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMBunkerMaterialsTest::RunTest(const FString& Parameters)
    {
        FSettingsGuard Settings;
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
        FString CatalogText; FObject Catalog;
        if (!Plugin.IsValid() || !FFileHelper::LoadFileToString(CatalogText, *(Plugin->GetContentDir() / TEXT("Data/catalog.json")))
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CatalogText), Catalog)) return false;
        FObject Level, Sprite;
        for (const auto& Value : Catalog->GetArrayField(TEXT("buildings")))
            if (Value->AsObject()->GetNumberField(TEXT("id")) == 22.) Level = Value->AsObject()->GetArrayField(TEXT("levels"))[0]->AsObject();
        for (const auto& Value : Catalog->GetArrayField(TEXT("monsters")))
            if (Value->AsObject()->GetStringField(TEXT("id")) == TEXT("C1")) Sprite = Value->AsObject()->GetObjectField(TEXT("sprite"));
        if (!Level.IsValid() || !Sprite.IsValid()) return false;
        UTexture2D* Door = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(Level->GetObjectField(TEXT("animation"))->GetStringField(TEXT("texture"))));
        UTexture2D* Damaged = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(Level->GetObjectField(TEXT("damagedComposite"))->GetStringField(TEXT("texture"))));
        UTexture2D* Destroyed = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(Level->GetObjectField(TEXT("destroyedComposite"))->GetStringField(TEXT("texture"))));
        UTexture2D* Pokey = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(Sprite->GetStringField(TEXT("texture"))));
        if (!Door || !Damaged || !Destroyed || !Pokey)
        { AddError(TEXT("Finish normal editor setup to import the bundled original Bunker and Pokey art before running this test.")); return false; }
        TArray<UTexture*> Textures{Door, Damaged, Destroyed, Pokey};
        FTextureCompilingManager::Get().FinishCompilation(Textures);
        const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("BYMBunkerMaterialWorld"));
        UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, WorldName, GetTransientPackage(), true);
        if (!TestNotNull(TEXT("Create isolated Bunker presentation world"), World)) return false;
        ON_SCOPE_EXIT { World->DestroyWorld(false); if (World->IsRooted()) World->RemoveFromRoot(); };
        FActorSpawnParameters Spawn; Spawn.ObjectFlags = RF_Transient;
        ABYMBoard* Board = World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(), FTransform::Identity, Spawn);
        if (!TestNotNull(TEXT("Spawn actual native Board"), Board)) return false;
        bool bCustomDepth = true;
        Board->ApplySnapshot(Snapshot(0., true));
        auto DoorMaterials = Materials(*Board, Door, bCustomDepth);
        if (!TestEqual(TEXT("Original Bunker atlas is bound"), DoorMaterials.Num(), 1)) return false;
        UMaterialInstanceDynamic* DoorMaterial = DoorMaterials[0];
        TestEqual(TEXT("Door starts at original closed cell"), DoorFrame(*DoorMaterial), 0);
        Board->Tick(.2f);
        TestEqual(TEXT("Opening displays the eighth authored cell"), DoorFrame(*DoorMaterial), 8);
        Board->ApplySnapshot(Snapshot(15., true));
        TestEqual(TEXT("Source phase15 clamps to valid cell14"), DoorFrame(*DoorMaterial), 14);
        Board->ApplySnapshot(Snapshot(15., false, 10000., false, true, TEXT("open"))); Board->Tick(.2f);
        TestEqual(TEXT("An open door stays open between snapshots"), DoorFrame(*DoorMaterial), 14);
        Board->ApplySnapshot(Snapshot(15., false, 10000., false, true, TEXT("open"))); Board->Tick(.2f);
        TestEqual(TEXT("Repeated open snapshots never create a closing flicker"), DoorFrame(*DoorMaterial), 14);
        Board->ApplySnapshot(Snapshot(15., false)); Board->Tick(.2f);
        TestEqual(TEXT("Closing traverses the original strip in reverse"), DoorFrame(*DoorMaterial), 7);
        auto Monsters = Materials(*Board, Pokey, bCustomDepth);
        if (!TestEqual(TEXT("Attacker and defender with equal test IDs remain separate sprites"), Monsters.Num(), 2)) return false;
        TestTrue(TEXT("The two sides never share a mutable sprite material"), Monsters[0] != Monsters[1]);
        Board->ApplySnapshot(Snapshot(7., false, 10000., true));
        TestEqual(TEXT("Returned defender disappears without removing the attacker"), Materials(*Board, Pokey, bCustomDepth).Num(), 1);
        Board->ApplySnapshot(Snapshot(15., true, 4999.));
        TestEqual(TEXT("Below half health uses original damaged art"), Materials(*Board, Damaged, bCustomDepth).Num(), 1);
        TestEqual(TEXT("Normal door atlas never covers damaged art"), Materials(*Board, Door, bCustomDepth).Num(), 0);
        Board->ApplySnapshot(Snapshot(15., true, 0.));
        TestEqual(TEXT("Destroyed bunker uses its original wreck image"), Materials(*Board, Destroyed, bCustomDepth).Num(), 1);
        Board->ApplySnapshot(Snapshot(0., false, 10000., false, false));
        TestEqual(TEXT("Leaving battle removes both combat sides"), Materials(*Board, Pokey, bCustomDepth).Num(), 0);
        TestTrue(TEXT("Bunker and all monster primitives retain custom depth"), bCustomDepth);
        TestTrue(TEXT("Door and defender transitions report no presentation errors"), Board->GetPresentationError().IsEmpty());
        return true;
    }
}
#endif
