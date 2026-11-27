#if WITH_DEV_AUTOMATION_TESTS

#include "BYMBoard.h"
#include "BYMCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMPresentationTest, "BackyardMMO.Presentation.EditorTerrainAndCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMPresentationTest::RunTest(const FString& Parameters)
{
    // Exercise actual imported content without opening a map or starting gameplay.
    UTexture2D* Grass = LoadObject<UTexture2D>(nullptr,
        *ABYMBoard::AssetObjectPath(TEXT("derived/terrain/grass_blended_tile.png")));
    UMaterialInterface* SpriteMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/BackyardMMO/Materials/M_Sprite.M_Sprite"));
    UMaterialInterface* AlphaMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/BackyardMMO/Materials/M_SpriteAlpha.M_SpriteAlpha"));
    if (!Grass || !SpriteMaterial || !AlphaMaterial)
    {
        AddError(TEXT("Presentation test requires imported grass, M_Sprite and M_SpriteAlpha. Finish the plugin's editor setup before running this test."));
        return false;
    }
    TestEqual(TEXT("MAPBG terrain retains its original composite width"), Grass->GetSizeX(), 1000);
    TestEqual(TEXT("MAPBG terrain retains its original composite height"), Grass->GetSizeY(), 500);
    TestTrue(TEXT("Terrain repeats along both source-image axes"), Grass->AddressX == TA_Wrap && Grass->AddressY == TA_Wrap);

    const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
        TEXT("BYMPresentationTestWorld"));
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, WorldName, GetTransientPackage(), true);
    if (!TestNotNull(TEXT("Create an isolated transient editor world"), World)) return false;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        if (World->IsRooted()) World->RemoveFromRoot();
    };

    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags = RF_Transient;
    ABYMBoard* Board = World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(), FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Spawn the board without entering Play"), Board)) return false;
    TestFalse(TEXT("Editor preview does not require BeginPlay"), Board->HasActorBegunPlay());
    TestTrue(TEXT("Imported presentation assets resolve"), Board->GetPresentationError().IsEmpty());
    TestTrue(TEXT("Ground keeps its opaque masked surface"), SpriteMaterial->GetBlendMode() == BLEND_Masked);
    TestTrue(TEXT("Original soft sprite alpha is not converted to opaque black"), AlphaMaterial->GetBlendMode() == BLEND_Translucent);
    for (const FVector2D Grid : { FVector2D(0,0), FVector2D(40,0), FVector2D(0,40),
        FVector2D(40,40), FVector2D(3.5,7.25), FVector2D(-2,45) })
    {
        const FVector WorldPoint = Board->GridToWorld(Grid);
        const FVector2D Restored = Board->WorldToGrid(WorldPoint);
        TestTrue(TEXT("Isometric pointer inverse restores the logical grid coordinate"), Restored.Equals(Grid, 0.000001));
    }
    const FVector RightCorner = Board->GridToWorld(FVector2D(Board->GetGridCells(),0));
    const FVector LeftCorner = Board->GridToWorld(FVector2D(0,Board->GetGridCells()));
    TestTrue(TEXT("Original 2:1 isometric footprint keeps opposite corners level"),
        FMath::IsNearlyEqual(RightCorner.Y, LeftCorner.Y)
        && FMath::IsNearlyEqual(RightCorner.X - LeftCorner.X, 2.0 * Board->GetGridCells() * Board->GetCellSize()));
    TestFalse(TEXT("Construction preview rejects the host's reserved outer edge"), Board->CanPlacePreview(0,1,5));
    TestFalse(TEXT("Construction preview rejects a footprint touching the far edge"), Board->CanPlacePreview(Board->GetGridCells()-5,1,5));
    TestTrue(TEXT("Construction preview accepts an unobstructed inset footprint"), Board->CanPlacePreview(1,1,5));
    TestTrue(TEXT("Deployment has its own valid outermost-cell rule"), Board->CanDeployPreview(0,Board->GetGridCells()/2));
    TestFalse(TEXT("Deployment preview rejects the centre of the enemy yard"), Board->CanDeployPreview(Board->GetGridCells()/2,Board->GetGridCells()/2));

    TInlineComponentArray<UStaticMeshComponent*> Planes(Board);
    UStaticMeshComponent* Terrain = nullptr;
    UMaterialInstanceDynamic* TerrainMaterial = nullptr;
    for (UStaticMeshComponent* Plane : Planes)
    {
        UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Plane->GetMaterial(0));
        if (Material && Material->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == Grass)
        {
            Terrain = Plane;
            TerrainMaterial = Material;
            break;
        }
    }
    if (!TestNotNull(TEXT("Editor board has a terrain plane bound to the imported grass"), Terrain)) return false;
    TestTrue(TEXT("Terrain is registered and visible before Play"), Terrain->IsRegistered() && Terrain->IsVisible());
    TestNotNull(TEXT("Terrain has its render mesh"), Terrain->GetStaticMesh().Get());
    const FLinearColor UVScale = TerrainMaterial->K2_GetVectorParameterValue(TEXT("UVScale"));
    const FLinearColor UVOffset = TerrainMaterial->K2_GetVectorParameterValue(TEXT("UVOffset"));
    const float PixelUnits = Board->GetCellSize() / 20.f;
    const FVector TerrainSize = Terrain->GetRelativeScale3D() * 100.f;
    TestTrue(TEXT("Terrain preserves source-pixel scale instead of stretching to the yard"),
        FMath::IsNearlyEqual(UVScale.R, static_cast<float>(TerrainSize.X) / (1000.f * PixelUnits))
        && FMath::IsNearlyEqual(UVScale.G, static_cast<float>(TerrainSize.Y) / (500.f * PixelUnits)));
    const FVector TerrainOrigin=Terrain->GetRelativeLocation()-TerrainSize*.5;
    TestTrue(TEXT("Terrain keeps a stable world-zero phase beyond the playable area"),
        FMath::IsNearlyEqual(UVOffset.R, static_cast<float>(TerrainOrigin.X) / (1000.f * PixelUnits))
        && FMath::IsNearlyEqual(UVOffset.G, static_cast<float>(TerrainOrigin.Y) / (500.f * PixelUnits)));
    TestTrue(TEXT("Terrain continues beyond maximum camera pan and zoom"),
        TerrainSize.X >= Board->GetGridCells() * Board->GetCellSize() + 40000.f);

    UTexture2D* Outline = LoadObject<UTexture2D>(nullptr,
        *ABYMBoard::AssetObjectPath(TEXT("derived/ui/placement_diamond_outline.png")));
    if (!TestNotNull(TEXT("Placement uses the imported transparent outline"), Outline)) return false;
    Board->SetHoveredCell(2, 3, 5, true, 1);
    UTexture2D* GhostTexture = LoadObject<UTexture2D>(nullptr,
        *ABYMBoard::AssetObjectPath(TEXT("derived/buildings/composites/1_L1_normal.png")));
    TInlineComponentArray<UStaticMeshComponent*> HoverPlanes(Board);
    bool bFoundOutline = false;
    bool bFoundBuildingPreview = false;
    for (UStaticMeshComponent* Plane : HoverPlanes)
    {
        UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Plane->GetMaterial(0));
        if (Material && Material->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == Outline)
        {
            bFoundOutline = bFoundOutline || (Plane->IsVisible()
                && FMath::IsNearlyEqual(Plane->GetRelativeScale3D().X * 100.0, 10.0 * Board->GetCellSize())
                && FMath::IsNearlyEqual(Plane->GetRelativeScale3D().Y * 100.0, 5.0 * Board->GetCellSize())
                && Plane->GetComponentLocation().Equals(Board->GridToWorld(FVector2D(4.5,5.5),31.4f),0.001));
        }
        if (Material && GhostTexture && Material->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == GhostTexture)
        {
            bFoundBuildingPreview = Plane->IsVisible() && Material->GetBlendMode() == BLEND_Translucent
                && Plane->TranslucencySortPriority > Terrain->TranslucencySortPriority;
        }
    }
    TestTrue(TEXT("Placement marker uses the footprint outline at its actual grid size"), bFoundOutline);
    TestTrue(TEXT("Placement shows the selected original building artwork above terrain"), bFoundBuildingPreview);
    Board->ClearHover();

    const FVector GroundPoint = Terrain->GetComponentLocation();
    TestTrue(TEXT("Terrain stays on the intended flat ground plane"), FMath::IsNearlyEqual(GroundPoint.Z, -10.0));
    const FTransform CameraTransform(FRotator::ZeroRotator, FVector(GroundPoint.X, GroundPoint.Y, 0.0));
    ABYMCameraPawn* Pawn = World->SpawnActor<ABYMCameraPawn>(ABYMCameraPawn::StaticClass(), CameraTransform, Spawn);
    if (!TestNotNull(TEXT("Spawn the top-down camera"), Pawn)) return false;
    UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>();
    if (!TestNotNull(TEXT("Camera pawn supplies its view component"), Camera)) return false;
    FMinimalViewInfo View;
    Camera->GetCameraView(0.f, View);
    const FVector Forward = View.Rotation.Vector();
    const double GroundDepth = FVector::DotProduct(GroundPoint - View.Location, Forward);
    TestTrue(TEXT("Camera provides an orthographic view"), View.ProjectionMode == ECameraProjectionMode::Orthographic);
    TestTrue(TEXT("Camera looks straight down onto the board"), Forward.Z < -0.999);
    TestFalse(TEXT("Zoom cannot automatically replace the explicit clip planes"), View.bAutoCalculateOrthoPlanes);
    TestFalse(TEXT("View origin correction cannot move the explicit clip planes"), View.bUpdateOrthoPlanes);
    TestTrue(TEXT("Ground lies inside the actual camera near/far interval"),
        GroundDepth > View.OrthoNearClipPlane && GroundDepth < View.OrthoFarClipPlane);
    TestTrue(TEXT("Camera applies its presentation exposure settings"), View.PostProcessBlendWeight > 0.f);
    TestTrue(TEXT("Camera overrides manual exposure"), View.PostProcessSettings.bOverride_AutoExposureMethod
        && View.PostProcessSettings.AutoExposureMethod == EAutoExposureMethod::AEM_Manual);
    TestTrue(TEXT("Camera explicitly excludes physical exposure from the artwork"),
        View.PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure
        && !View.PostProcessSettings.AutoExposureApplyPhysicalCameraExposure);

    // Exercise the same bounds calculation used after pan, zoom and window resizing.
    // Check every viewport corner, not just the camera centre, including a very tall
    // window that must reduce zoom to remain over terrain.
    for (const float Cell : { 20.f, 100.f, 1000.f })
    {
        for (const int32 Cells : { 40, 100 })
        {
            const float Edge = Cells * Cell;
            const float GroundMargin = ABYMBoard::CalculateTerrainMargin(Cell);
            for (const float Aspect : { 16.f / 9.f, 32.f / 9.f, 9.f / 16.f, .02f })
            {
                for (const float RequestedZoom : { 15000.f, 20000.f })
                {
                    for (const float PanX : { -Edge, 2.f * Edge })
                    {
                        for (const float PanY : { -Edge, 2.f * Edge })
                        {
                            FVector Position(PanX, PanY, 0);
                            float Width = RequestedZoom;
                            ABYMCameraPawn::ConstrainViewToTerrain(Position, Width, Aspect, Edge, GroundMargin);
                            const double HalfWidth = Width * .5;
                            const double HalfHeight = HalfWidth / Aspect;
                            const bool bInside = Position.X - HalfWidth > -GroundMargin
                                && Position.X + HalfWidth < Edge + GroundMargin
                                && Position.Y - HalfHeight > -GroundMargin
                                && Position.Y + HalfHeight < Edge + GroundMargin;
                            TestTrue(FString::Printf(TEXT("All viewport corners remain on terrain: cells=%d cell=%g aspect=%g zoom=%g pan=(%g,%g)"),
                                Cells, Cell, Aspect, RequestedZoom, PanX, PanY), bInside);
                            TestTrue(TEXT("Terrain bounds preserve the yard's normal pan range"),
                                Position.X >= -.7 * Edge && Position.X <= 1.7 * Edge
                                && Position.Y >= -.2 * Edge && Position.Y <= 1.2 * Edge);
                            TestTrue(TEXT("Viewport fitting never zooms farther out"), Width > 0.f && Width <= RequestedZoom);
                        }
                    }
                }
            }
        }
    }
    // Current centered rectangular expansions can cross negative coordinates.
    // This is the exact helper used by ClampCamera, including the mushroom ring.
    for(const float Aspect:{.02f,9.f/16.f,16.f/9.f,8.f})
        for(const FVector2D Min:{FVector2D(-3000,-200),FVector2D(-9200,-3500)})
            for(const FVector2D Span:{FVector2D(9000,4500),FVector2D(16000,8000)})
                for(const float Pan:{-1000000.f,1000000.f})
                {
                    const FVector2D Max=Min+Span;FVector Position(Pan,-Pan,0);float Width=20000;
                    ABYMCameraPawn::ConstrainViewToGroundBounds(Position,Width,Aspect,Min,Max,20000,2000);
                    TestTrue(TEXT("Every rectangular viewport corner stays on the terrain"),
                        Position.X-Width*.5>Min.X-20000&&Position.X+Width*.5<Max.X+20000
                        &&Position.Y-Width*.5/Aspect>Min.Y-20000&&Position.Y+Width*.5/Aspect<Max.Y+20000);
                    TestTrue(TEXT("Rectangular viewport fitting preserves a positive bounded zoom"),Width>0&&Width<=20000);
                }
    return true;
}

#endif
