#if WITH_DEV_AUTOMATION_TESTS
#include "BYMBoard.h"
#include "BYMSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace BYMWorkerMaterialTests
{
    FString Snapshot(double FacingX,double FacingY,bool bMoving=false,bool bOwn=true)
    {
        return FString::Printf(TEXT("{\"selfId\":\"%s\",\"now\":100,\"rules\":{\"baseYardCells\":40,\"cellSize\":100},")
            TEXT("\"yard\":{\"id\":\"worker-yard\",\"minX\":-3,\"minY\":-2,\"width\":55,\"height\":44,")
            TEXT("\"buildings\":[{\"id\":\"edge-decoration\",\"type\":55,\"level\":1,\"x\":-3,\"y\":0,\"size\":1,\"hp\":100,\"maxHp\":100}],\"residentRoster\":[],")
            TEXT("\"workers\":{\"total\":1,\"entries\":[{\"id\":\"worker-0\",\"x\":2,\"y\":2,\"facingX\":%.12f,\"facingY\":%.12f,")
            TEXT("\"state\":\"%s\",\"startedAt\":100,\"route\":%s}]},")
            TEXT("\"economy\":{\"mushrooms\":[{\"id\":\"mushroom-0\",\"x\":-5,\"y\":5,\"frame\":1,\"picking\":false}]}}}"),
            bOwn?TEXT("worker-yard"):TEXT("visitor"),FacingX,FacingY,bMoving?TEXT("walking"):TEXT("idle"),
            bMoving?TEXT("[{\"x\":2,\"y\":2,\"at\":0,\"hop\":false},{\"x\":4,\"y\":2,\"at\":2,\"hop\":false}]"):TEXT("[]"));
    }
    TArray<UMaterialInstanceDynamic*> Materials(ABYMBoard& Board,UTexture2D* Texture,bool& bCustomDepth)
    {
        TArray<UMaterialInstanceDynamic*> Found;
        TInlineComponentArray<UStaticMeshComponent*> Components(&Board);
        for(auto* Mesh:Components)
        {
            auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            if(Mesh->IsVisible()&&Material&&Material->K2_GetTextureParameterValue(TEXT("SpriteTexture"))==Texture)
            {Found.Add(Material);bCustomDepth=bCustomDepth&&Mesh->bRenderCustomDepth;}
        }
        return Found;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMWorkerMaterialsTest,"BackyardMMO.Presentation.WorkerMushroomAndYardMaterials",
        EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
    bool FBYMWorkerMaterialsTest::RunTest(const FString& Parameters)
    {
        auto* Settings=GetMutableDefault<UBYMSettings>();
        const bool Music=Settings->bMusicEnabled,Sounds=Settings->bCombatSoundsEnabled;
        Settings->bMusicEnabled=false;Settings->bCombatSoundsEnabled=false;
        ON_SCOPE_EXIT {Settings->bMusicEnabled=Music;Settings->bCombatSoundsEnabled=Sounds;};
        UTexture2D* Worker=LoadObject<UTexture2D>(nullptr,*ABYMBoard::AssetObjectPath(TEXT("monsters/worker.png")));
        UTexture2D* Top=LoadObject<UTexture2D>(nullptr,*ABYMBoard::AssetObjectPath(TEXT("source_client/shapes/1684.png")));
        UTexture2D* Shadow=LoadObject<UTexture2D>(nullptr,*ABYMBoard::AssetObjectPath(TEXT("derived/mushrooms/shadow_1.png")));
        UTexture2D* Line=LoadObject<UTexture2D>(nullptr,*ABYMBoard::AssetObjectPath(TEXT("derived/effects/source_line.png")));
        if(!Worker||!Top||!Shadow||!Line)
        {AddError(TEXT("Import the bundled original worker, mushroom and line artwork through normal editor setup first."));return false;}
        TArray<UTexture*> Textures{Worker,Top,Shadow,Line};FTextureCompilingManager::Get().FinishCompilation(Textures);
        const FName Name=MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("BYMWorkerMaterialWorld"));
        UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false,Name,GetTransientPackage(),true);
        if(!TestNotNull(TEXT("Create isolated presentation world"),World))return false;
        ON_SCOPE_EXIT {World->DestroyWorld(false);if(World->IsRooted())World->RemoveFromRoot();};
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
        auto* Board=World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(),FTransform::Identity,Spawn);
        if(!TestNotNull(TEXT("Spawn native Board"),Board))return false;
        bool CustomDepth=true;
        for(int32 Direction=0;Direction<30;++Direction)
        {
            const double Angle=(Direction+.5)*2.*PI/30.,X=FMath::Cos(Angle),Y=FMath::Sin(Angle);
            Board->ApplySnapshot(Snapshot(Y+X*.5,Y-X*.5));
            const auto Found=Materials(*Board,Worker,CustomDepth);
            if(!TestEqual(TEXT("Exactly one original worker sheet is visible"),Found.Num(),1))return false;
            const FLinearColor UV=Found[0]->K2_GetVectorParameterValue(TEXT("UVOffset"));
            const FLinearColor Scale=Found[0]->K2_GetVectorParameterValue(TEXT("UVScale"));
            // RoundToInt(double) returns int64 in UE 5.8; keep both assertion
            // operands int64 so Unreal's numeric TestEqual overload is unambiguous.
            const int64 SelectedDirection=FMath::RoundToInt((UV.R*810.-.5)/27.);
            TestEqual(TEXT("Facing selects one source cell"),SelectedDirection,static_cast<int64>(Direction));
            TestTrue(TEXT("Normal worker never samples boosted row"),FMath::IsNearlyEqual(static_cast<double>(UV.G),.5/54.,.00001));
            TestTrue(TEXT("UV extent remains inside one authored cell"),Scale.R<=27./810.&&Scale.G<=27./54.);
            Board->Tick(.2f);
            TestTrue(TEXT("A held source pose never cycles facing frames"),Found[0]->K2_GetVectorParameterValue(TEXT("UVOffset")).Equals(UV,.00001f));
        }
        TestEqual(TEXT("Mushroom top is its original variant"),Materials(*Board,Top,CustomDepth).Num(),1);
        TestEqual(TEXT("Mushroom has independent alpha shadow"),Materials(*Board,Shadow,CustomDepth).Num(),1);
        TestEqual(TEXT("Build-mode boundary has four source lines and four underlays"),Materials(*Board,Line,CustomDepth).Num(),8);
        TestEqual(TEXT("Mushrooms remain selectable outside buildable yard"),Board->GetBuildingAtCell(-5,5),FString(TEXT("mushroom-0")));
        TestEqual(TEXT("Expanded rectangle preserves width"),Board->GetYardBounds().Width(),55);
        TestEqual(TEXT("Expanded rectangle preserves height"),Board->GetYardBounds().Height(),44);
        TestTrue(TEXT("Noncombat decorations do not reject a legal deployment edge"),Board->CanDeployPreview(-3,0));
        TestTrue(TEXT("Expanded negative origin round-trips through projection"),Board->WorldToGrid(Board->GridToWorld(FVector2D(-3,-2))).Equals(FVector2D(-3,-2),.00001));
        Board->ApplySnapshot(Snapshot(1,0,true));Board->Tick(1.f);
        FVector Focus;TestTrue(TEXT("Worker focus resolves locally"),Board->GetWorkerFocusPosition(TEXT("worker-0"),Focus));
        TestTrue(TEXT("Focus tracks shared authoritative route clock"),Focus.Equals(Board->GridToWorld(FVector2D(3,2)),.001));
        Board->ApplySnapshot(Snapshot(1,0,false,false));
        TestEqual(TEXT("Source build border is hidden while visiting"),Materials(*Board,Line,CustomDepth).Num(),0);
        TestTrue(TEXT("Worker, mushroom and boundary primitives retain custom depth"),CustomDepth);
        TestTrue(TEXT("Presentation reports no missing artwork"),Board->GetPresentationError().IsEmpty());
        return true;
    }
}
#endif
