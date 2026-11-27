#if WITH_DEV_AUTOMATION_TESTS
#include "BYMBoard.h"
#include "BYMSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace BYMMapStrongholdPresentationTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;

    FString Snapshot(int32 BuildingType, const FString& Relationship, double Health,
        const TArray<FVector2D>& Emitters = {})
    {
        FObject Root = MakeShared<FJsonObject>(), Yard = MakeShared<FJsonObject>(), Rules = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("selfId"), TEXT("map-viewer")); Root->SetNumberField(TEXT("now"), 100.);
        Yard->SetStringField(TEXT("id"), TEXT("map-site")); Yard->SetStringField(TEXT("mapRelationship"), Relationship);
        Yard->SetNumberField(TEXT("width"), 50); Yard->SetNumberField(TEXT("height"), 40);
        Rules->SetNumberField(TEXT("baseYardCells"), 40); Rules->SetNumberField(TEXT("cellSize"), 100.);
        Root->SetObjectField(TEXT("rules"), Rules);
        FObject Building = MakeShared<FJsonObject>();
        Building->SetStringField(TEXT("id"), TEXT("map-core")); Building->SetNumberField(TEXT("type"), BuildingType);
        Building->SetNumberField(TEXT("level"), 1); Building->SetNumberField(TEXT("x"), 20.); Building->SetNumberField(TEXT("y"), 20.);
        Building->SetNumberField(TEXT("hp"), Health); Building->SetNumberField(TEXT("maxHp"), 10000.);
        Building->SetNumberField(TEXT("size"), 7); Building->SetBoolField(TEXT("working"), false);
        Yard->SetArrayField(TEXT("buildings"), FArray{MakeShared<FJsonValueObject>(Building)});
        Yard->SetArrayField(TEXT("residentRoster"), FArray()); Root->SetObjectField(TEXT("yard"), Yard);
        if (!Emitters.IsEmpty())
        {
            FObject Battle = MakeShared<FJsonObject>();
            Battle->SetStringField(TEXT("id"), TEXT("stronghold-lightning")); Battle->SetStringField(TEXT("phase"), TEXT("fighting"));
            Battle->SetNumberField(TEXT("serverTime"), 100.);
            FArray Events;
            for (int32 Index = 0; Index < Emitters.Num(); ++Index)
            {
                FObject Event = MakeShared<FJsonObject>();
                Event->SetStringField(TEXT("id"), FString::Printf(TEXT("source-emitter-%d"), Index));
                Event->SetStringField(TEXT("kind"), TEXT("defenseAttack")); Event->SetStringField(TEXT("actor"), TEXT("map-core"));
                Event->SetNumberField(TEXT("buildingType"), 138); Event->SetNumberField(TEXT("at"), 100.);
                Event->SetNumberField(TEXT("sourceX"), Emitters[Index].X); Event->SetNumberField(TEXT("sourceY"), Emitters[Index].Y);
                Event->SetNumberField(TEXT("targetX"), 25.); Event->SetNumberField(TEXT("targetY"), 28.);
                Events.Add(MakeShared<FJsonValueObject>(Event));
            }
            Battle->SetArrayField(TEXT("events"), Events); Root->SetObjectField(TEXT("battle"), Battle);
        }
        FString Text; FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text;
    }

    TArray<UStaticMeshComponent*> Planes(ABYMBoard& Board, UTexture2D* Texture)
    {
        TArray<UStaticMeshComponent*> Result;
        TInlineComponentArray<UStaticMeshComponent*> Components(&Board);
        for (UStaticMeshComponent* Mesh : Components)
        {
            auto* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            if (Mesh->IsVisible() && Material && Material->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == Texture)
                Result.Add(Mesh);
        }
        return Result;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapStrongholdMaterialsTest, "BackyardMMO.Presentation.MapRoomRelationshipAndStrongholdEmitters",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapStrongholdMaterialsTest::RunTest(const FString& Parameters)
    {
        auto* Settings = GetMutableDefault<UBYMSettings>();
        const bool Music = Settings->bMusicEnabled, Sounds = Settings->bCombatSoundsEnabled;
        Settings->bMusicEnabled = false; Settings->bCombatSoundsEnabled = false;
        ON_SCOPE_EXIT { Settings->bMusicEnabled = Music; Settings->bCombatSoundsEnabled = Sounds; };
        UTexture2D* OwnLight = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(TEXT("derived/buildings/animations/140_L1_relationship_self.png")));
        UTexture2D* EnemyLight = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(TEXT("derived/buildings/animations/140_L1_relationship_enemy.png")));
        UTexture2D* Damaged = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(TEXT("derived/buildings/composites/140_L1_damaged.png")));
        UTexture2D* Line = LoadObject<UTexture2D>(nullptr, *ABYMBoard::AssetObjectPath(TEXT("derived/effects/source_line.png")));
        if (!OwnLight || !EnemyLight || !Damaged || !Line)
        { AddError(TEXT("Complete normal editor setup to import the original Map Room relationship and lightning artwork.")); return false; }
        TArray<UTexture*> Textures{OwnLight, EnemyLight, Damaged, Line}; FTextureCompilingManager::Get().FinishCompilation(Textures);
        const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("BYMMapMaterialWorld"));
        UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, Name, GetTransientPackage(), true);
        if (!TestNotNull(TEXT("Create isolated Map Room presentation world"), World)) return false;
        ON_SCOPE_EXIT { World->DestroyWorld(false); if (World->IsRooted()) World->RemoveFromRoot(); };
        FActorSpawnParameters Spawn; Spawn.ObjectFlags = RF_Transient;
        ABYMBoard* Board = World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(), FTransform::Identity, Spawn);
        if (!TestNotNull(TEXT("Spawn actual Board implementation"), Board)) return false;

        Board->ApplySnapshot(Snapshot(140, TEXT("self"), 10000.));
        auto OwnPlanes = Planes(*Board, OwnLight);
        if (!TestEqual(TEXT("Owned resource outpost uses its original relationship atlas"), OwnPlanes.Num(), 1)) return false;
        auto* OwnMaterial = Cast<UMaterialInstanceDynamic>(OwnPlanes[0]->GetMaterial(0));
        const FLinearColor Initial = OwnMaterial->K2_GetVectorParameterValue(TEXT("UVOffset"));
        Board->Tick(.2f);
        const FLinearColor Advanced = OwnMaterial->K2_GetVectorParameterValue(TEXT("UVOffset"));
        TestFalse(TEXT("Relationship lights animate even without production work or residents"), Advanced.Equals(Initial, .000001f));
        Board->ApplySnapshot(Snapshot(140, TEXT("enemy"), 10000.));
        const auto EnemyPlanes = Planes(*Board, EnemyLight);
        if (!TestEqual(TEXT("Changed ownership replaces the relationship light texture"), EnemyPlanes.Num(), 1)) return false;
        auto* EnemyMaterial = Cast<UMaterialInstanceDynamic>(EnemyPlanes[0]->GetMaterial(0));
        TestTrue(TEXT("A relationship bitmap swap preserves the original animation phase"),
            EnemyMaterial->K2_GetVectorParameterValue(TEXT("UVOffset")).Equals(Advanced, .000001f));
        TestEqual(TEXT("Previous relationship texture is no longer visible"), Planes(*Board, OwnLight).Num(), 0);
        Board->ApplySnapshot(Snapshot(140, TEXT("enemy"), 4000.));
        TestEqual(TEXT("Damaged outposts use original damaged artwork"), Planes(*Board, Damaged).Num(), 1);
        TestEqual(TEXT("Intact light animation does not cover damaged artwork"), Planes(*Board, EnemyLight).Num(), 0);

        const TArray<FVector2D> Emitters{FVector2D(17, 18), FVector2D(19, 17), FVector2D(21, 18), FVector2D(19, 21)};
        Board->ApplySnapshot(Snapshot(138, TEXT("enemy"), 10000., Emitters));
        const auto Beams = Planes(*Board, Line);
        TestTrue(TEXT("Four independent source guns create visible lightning segments"), Beams.Num() >= 4);
        for (const FVector2D& Origin : Emitters)
        {
            const FVector Expected = Board->GridToWorld(Origin, 20.f);
            bool Found = false;
            for (const UStaticMeshComponent* Beam : Beams)
            {
                const FVector Half = Beam->GetRelativeRotation().Vector() * (Beam->GetRelativeScale3D().X * 50.);
                const FVector Start = Board->GetActorTransform().TransformPosition(Beam->GetRelativeLocation() - Half);
                if (Start.Equals(Expected, .1)) Found = true;
                TestTrue(TEXT("Original lightning primitives retain custom depth"), Beam->bRenderCustomDepth);
            }
            TestTrue(TEXT("The real host emitter origin is retained instead of the building centre"), Found);
        }
        TestTrue(TEXT("Map Room presentation reports no missing artwork"), Board->GetPresentationError().IsEmpty());
        return true;
    }
}
#endif
