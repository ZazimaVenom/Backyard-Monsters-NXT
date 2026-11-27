#include "BYMCameraPawn.h"
#include "BYMBoard.h"
#include "BYMPlayerController.h"
#include "BYMSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

namespace
{
    bool PointerOnBoardPlane(const APlayerController* PC,const ABYMBoard* Board,FVector& Hit)
    {
        if(!PC) return false;
        FVector Origin,Direction;
        if(!PC->DeprojectMousePositionToWorld(Origin,Direction)) return false;
        const FVector Normal=Board?Board->GetActorUpVector():FVector::UpVector;
        const FVector PlaneOrigin=Board?Board->GetActorLocation():FVector::ZeroVector;
        const double Denominator=FVector::DotProduct(Direction,Normal);
        if(FMath::Abs(Denominator)<KINDA_SMALL_NUMBER) return false;
        const double Distance=FVector::DotProduct(PlaneOrigin-Origin,Normal)/Denominator;
        if(Distance<0) return false;
        Hit=Origin+Direction*Distance;
        return true;
    }
}

ABYMCameraPawn::ABYMCameraPawn()
{
    PrimaryActorTick.bCanEverTick=true;
    bReplicates=true;
    bOnlyRelevantToOwner=true;
    SetReplicateMovement(false);
    CameraRoot=CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot"));SetRootComponent(CameraRoot);
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));Camera->SetupAttachment(CameraRoot);
    Camera->ProjectionMode=ECameraProjectionMode::Orthographic;
    Camera->OrthoWidth=6200;
    Camera->SetRelativeLocation(FVector(0,0,10000));
    Camera->SetRelativeRotation(FRotator(-90,-90,0));
    // Keep the flat yard inside the explicit clip range at every zoom level.
    Camera->bAutoCalculateOrthoPlanes=false;
    Camera->bUpdateOrthoPlanes=false;
    Camera->OrthoNearClipPlane=1;
    Camera->OrthoFarClipPlane=30000;
    Camera->bConstrainAspectRatio=false;
    Camera->PostProcessBlendWeight=1.f;
    Camera->PostProcessSettings.bOverride_AutoExposureMethod=true;
    Camera->PostProcessSettings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    // Color artwork uses fixed exposure, independent of shutter, ISO and aperture.
    Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias=true;
    Camera->PostProcessSettings.AutoExposureBias=0;
}
void ABYMCameraPawn::BeginPlay()
{
    Super::BeginPlay();
    for(TActorIterator<ABYMBoard> It(GetWorld());It;++It){Board=*It;break;}
    ResetCamera();
}
void ABYMCameraPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ABYMCameraPawn::ClickBoard);
    Input->BindKey(EKeys::MiddleMouseButton,IE_Pressed,this,&ABYMCameraPawn::BeginDrag);
    Input->BindKey(EKeys::MiddleMouseButton,IE_Released,this,&ABYMCameraPawn::EndDrag);
    Input->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&ABYMCameraPawn::BeginDrag);
    Input->BindKey(EKeys::RightMouseButton,IE_Released,this,&ABYMCameraPawn::EndDrag);
    Input->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&ABYMCameraPawn::ZoomIn);
    Input->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&ABYMCameraPawn::ZoomOut);
    Input->BindKey(EKeys::Home,IE_Pressed,this,&ABYMCameraPawn::ResetCamera);
    Input->BindKey(EKeys::Escape,IE_Pressed,this,&ABYMCameraPawn::CancelTool);
}
void ABYMCameraPawn::ResetCamera()
{
    if(const auto* PC=Cast<ABYMPlayerController>(GetController());PC&&PC->bWorldMapOpen){ClampCamera();return;}
    const auto* S=GetDefault<UBYMSettings>();
    KnownCells=Board.IsValid()?Board->GetGridCells():FMath::Clamp(S->YardCells,40,100);
    KnownCellUnits=Board.IsValid()?Board->GetCellSize():S->CellSize;
    const FIntRect Bounds=Board.IsValid()?Board->GetYardBounds():FIntRect(0,0,KnownCells,KnownCells);
    const FVector2D Center((Bounds.Min.X+Bounds.Max.X)*.5,(Bounds.Min.Y+Bounds.Max.Y)*.5);
    SetActorLocation(Board.IsValid()?Board->GridToWorld(Center):FVector(Center.X*KnownCellUnits,Center.Y*KnownCellUnits,0));
    Camera->OrthoWidth=FMath::Clamp(S->CameraWidth,1000.f,20000.f);
    ClampCamera();
}
void ABYMCameraPawn::ZoomIn(){if(const auto* PC=Cast<ABYMPlayerController>(GetController());PC&&PC->bWorldMapOpen)return;Camera->OrthoWidth=FMath::Max(1000.f,Camera->OrthoWidth*.88f);ClampCamera();}
void ABYMCameraPawn::ZoomOut(){if(const auto* PC=Cast<ABYMPlayerController>(GetController());PC&&PC->bWorldMapOpen)return;Camera->OrthoWidth=FMath::Min(15000.f,Camera->OrthoWidth/ .88f);ClampCamera();}
bool ABYMCameraPawn::FocusOnWorker(const FString& WorkerId)
{
    if(const auto* PC=Cast<ABYMPlayerController>(GetController());PC&&PC->bWorldMapOpen)return false;
    FVector Position;
    if(!Board.IsValid()||!Board->GetWorkerFocusPosition(WorkerId,Position))return false;
    bDragging=false;SetActorLocation(Position);ClampCamera();return true;
}
void ABYMCameraPawn::BeginDrag()
{
    if(const auto* PC=Cast<ABYMPlayerController>(GetController());PC&&PC->bWorldMapOpen)return;
    if(auto* PC=Cast<APlayerController>(GetController())) {float X=0,Y=0;bDragging=PC->GetMousePosition(X,Y);LastPointer=FVector2D(X,Y);}
}
void ABYMCameraPawn::EndDrag(){bDragging=false;}
void ABYMCameraPawn::CancelTool(){if(auto* PC=Cast<ABYMPlayerController>(GetController()))PC->PlacementMode.Empty();}
bool ABYMCameraPawn::PointerToCell(int32& X,int32& Y) const
{
    const auto* PC=Cast<ABYMPlayerController>(GetController()); if(!PC||PC->bPointerOverGameUI||PC->bWorldMapOpen) return false;
    FVector Hit;
    if(!PointerOnBoardPlane(PC,Board.Get(),Hit)) return false;
    const auto* S=GetDefault<UBYMSettings>();
    const float Cell=Board.IsValid()?Board->GetCellSize():S->CellSize;
    const int32 Cells=Board.IsValid()?Board->GetGridCells():S->YardCells;
    const FVector2D GridPoint=Board.IsValid()?Board->WorldToGrid(Hit):FVector2D(Hit.X/Cell,Hit.Y/Cell);
    X=FMath::FloorToInt(GridPoint.X+1.e-6);Y=FMath::FloorToInt(GridPoint.Y+1.e-6);
    const FIntRect Bounds=Board.IsValid()?Board->GetYardBounds():FIntRect(0,0,Cells,Cells);
    return X>=Bounds.Min.X&&Y>=Bounds.Min.Y&&X<Bounds.Max.X&&Y<Bounds.Max.Y;
}
void ABYMCameraPawn::ClickBoard()
{
    auto* PC=Cast<ABYMPlayerController>(GetController());
    if(!PC||!PC->bAuthenticated||PC->bPointerOverGameUI||PC->bWorldMapOpen) return;
    if(PC->PlacementMode.IsEmpty()&&Board.IsValid())
    {
        FVector Hit;
        if(!PointerOnBoardPlane(PC,Board.Get(),Hit)) return;
        // Original building artwork extends above its logical footprint. Pick
        // its visible sprite before rejecting cells outside the playable grid.
        const FVector2D GridPoint=Board->WorldToGrid(Hit);
        PC->HandleBoardClick(FMath::FloorToInt(GridPoint.X+1.e-6),FMath::FloorToInt(GridPoint.Y+1.e-6),Board->GetBuildingAtWorld(Hit));
        return;
    }
    int32 X,Y;if(!PointerToCell(X,Y)) return;
    PC->HandleBoardClick(X,Y,Board.IsValid()?Board->GetBuildingAtCell(X,Y):FString());
}
void ABYMCameraPawn::ConstrainViewToTerrain(FVector& Position,float& OrthoWidth,float AspectRatio,
    float YardExtent,float TerrainMargin)
{
    const double Aspect=FMath::IsFinite(AspectRatio)&&AspectRatio>0.f?AspectRatio:16.0/9.0;
    const double Extent=FMath::Max(1.0,static_cast<double>(YardExtent));
    const double Margin=FMath::Max(1.0,static_cast<double>(TerrainMargin));
    // Leave one world unit inside the mesh edge so numerical rounding never reveals it.
    const double GroundMin=-Margin+1.0;
    const double GroundMax=Extent+Margin-1.0;
    const double Span=GroundMax-GroundMin;
    const double RequestedWidth=FMath::IsFinite(OrthoWidth)&&OrthoWidth>0.f?OrthoWidth:1000.0;
    // Extremely tall windows need a smaller width to keep their vertical field of view
    // inside the ground. Ordinary landscape/portrait views retain the requested zoom.
    const double WidthLimit=Span*FMath::Min(1.0,Aspect);
    OrthoWidth=static_cast<float>(FMath::Min(RequestedWidth,WidthLimit));
    const double HalfWidth=OrthoWidth*.5;
    const double HalfHeight=HalfWidth/Aspect;
    // The original isometric yard spans [-.5,1.5] extents horizontally and
    // [0,1] vertically. Retain a .2-extent pan margin around that diamond.
    const double MinX=FMath::Max(-Extent*.7,GroundMin+HalfWidth);
    const double MaxX=FMath::Min(Extent*1.7,GroundMax-HalfWidth);
    const double MinY=FMath::Max(-Extent*.2,GroundMin+HalfHeight);
    const double MaxY=FMath::Min(Extent*1.2,GroundMax-HalfHeight);
    Position.X=MinX<=MaxX?FMath::Clamp(Position.X,MinX,MaxX):Extent*.5;
    Position.Y=MinY<=MaxY?FMath::Clamp(Position.Y,MinY,MaxY):Extent*.5;
    Position.Z=0;
}

void ABYMCameraPawn::ConstrainViewToGroundBounds(FVector& Position,float& OrthoWidth,float AspectRatio,
    const FVector2D& Min,const FVector2D& Max,float TerrainMargin,float PanMargin)
{
    if(!FMath::IsFinite(OrthoWidth)||OrthoWidth<=0)OrthoWidth=1000;
    const double SafeAspect=FMath::IsFinite(AspectRatio)&&AspectRatio>0?AspectRatio:16./9.;
    const FVector2D GroundMin=Min-FVector2D(TerrainMargin-1,TerrainMargin-1),GroundMax=Max+FVector2D(TerrainMargin-1,TerrainMargin-1);
    OrthoWidth=FMath::Min(OrthoWidth,static_cast<float>(FMath::Min(GroundMax.X-GroundMin.X,(GroundMax.Y-GroundMin.Y)*SafeAspect)));
    const double HalfW=OrthoWidth*.5,HalfH=HalfW/SafeAspect;
    // The source's mushroom ring extends200px outside the yard. Allow it
    // to be inspected, while the whole viewport stays on tiled terrain.
    const double Ring=FMath::Max(0.f,PanMargin);
    const double MinX=FMath::Max(Min.X-Ring,GroundMin.X+HalfW),MaxX=FMath::Min(Max.X+Ring,GroundMax.X-HalfW);
    const double MinY=FMath::Max(Min.Y-Ring,GroundMin.Y+HalfH),MaxY=FMath::Min(Max.Y+Ring,GroundMax.Y-HalfH);
    Position.X=MinX<=MaxX?FMath::Clamp(Position.X,MinX,MaxX):(Min.X+Max.X)*.5;
    Position.Y=MinY<=MaxY?FMath::Clamp(Position.Y,MinY,MaxY):(Min.Y+Max.Y)*.5;Position.Z=0;
}

void ABYMCameraPawn::ClampCamera()
{
    const auto* S=GetDefault<UBYMSettings>();
    const float Cell=Board.IsValid()?Board->GetCellSize():FMath::Clamp(S->CellSize,20.f,1000.f);
    const float Edge=(Board.IsValid()?Board->GetGridCells():FMath::Clamp(S->YardCells,40,100))*Cell;
    const float Margin=Board.IsValid()?Board->GetTerrainMargin():ABYMBoard::CalculateTerrainMargin(Cell);
    float Aspect=Camera->AspectRatio;
    if(const auto* PC=Cast<APlayerController>(GetController()))
    {
        int32 Width=0,Height=0;
        PC->GetViewportSize(Width,Height);
        if(Width>0&&Height>0) Aspect=static_cast<float>(Width)/Height;
    }
    FVector Pos=GetActorLocation();
    if(Board.IsValid())
    {
        FVector2D Min,Max;Board->GetTerrainWorldBounds(Min,Max);
        ConstrainViewToGroundBounds(Pos,Camera->OrthoWidth,Aspect,Min,Max,Margin,Cell*20.f);
    }
    else ConstrainViewToTerrain(Pos,Camera->OrthoWidth,Aspect,Edge,Margin);
    SetActorLocation(Pos);
}
void ABYMCameraPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* PC=Cast<ABYMPlayerController>(GetController());if(!PC||!PC->IsLocalController()) return;
    if(!Board.IsValid()) for(TActorIterator<ABYMBoard> It(GetWorld());It;++It){Board=*It;break;}
    if(!PC->bAuthenticated){ClampCamera();return;}
    // The map is a separate navigation surface. Keep terrain clamping active,
    // but do not pan/zoom or move the yard preview behind its modal window.
    if(PC->bWorldMapOpen){bDragging=false;ClampCamera();if(Board.IsValid())Board->ClearHover();return;}
    if(Board.IsValid()&&(KnownCells!=Board->GetGridCells()||!FMath::IsNearlyEqual(KnownCellUnits,Board->GetCellSize())))ResetCamera();
    FVector Move=FVector::ZeroVector;
    if(PC->IsInputKeyDown(EKeys::A)||PC->IsInputKeyDown(EKeys::Left))Move.X-=1;
    if(PC->IsInputKeyDown(EKeys::D)||PC->IsInputKeyDown(EKeys::Right))Move.X+=1;
    if(PC->IsInputKeyDown(EKeys::W)||PC->IsInputKeyDown(EKeys::Up))Move.Y-=1;
    if(PC->IsInputKeyDown(EKeys::S)||PC->IsInputKeyDown(EKeys::Down))Move.Y+=1;
    if(!Move.IsNearlyZero())AddActorWorldOffset(Move.GetSafeNormal()*Camera->OrthoWidth*.7f*DeltaSeconds);
    if(bDragging)
    {
        float MouseX=0,MouseY=0;
        if(PC->GetMousePosition(MouseX,MouseY))
        {
            const FVector2D Mouse(MouseX,MouseY);
            int32 Width,Height;PC->GetViewportSize(Width,Height);
            const FVector2D Delta=(Mouse-LastPointer)*Camera->OrthoWidth/FMath::Max(Width,1);
            AddActorWorldOffset(FVector(-Delta.X,-Delta.Y,0));LastPointer=Mouse;
        }
    }
    ClampCamera();
    if(Board.IsValid())
    {
        int32 X,Y;
        if(!PC->PlacementMode.IsEmpty()&&PointerToCell(X,Y))
        {
            int32 Size=1;
            if(PC->PlacementMode==TEXT("build")||PC->PlacementMode==TEXT("decorate")) Size=Board->GetFootprint(PC->SelectedBuildingType);
            else if(PC->PlacementMode==TEXT("move")) Size=Board->GetExistingFootprint(PC->SelectedBuilding);
            // Deployment permits the outermost cells and destroyed buildings;
            // construction has a separate inset and occupied-footprint policy.
            const bool bValid=PC->PlacementMode==TEXT("deploy")?Board->CanDeployPreview(X,Y,PC->SelectedMonsterCount)
                :Board->CanPlacePreview(X,Y,Size,PC->PlacementMode==TEXT("move")?PC->SelectedBuilding:FString());
            Board->SetHoveredCell(X,Y,Size,bValid,
                PC->PlacementMode==TEXT("build")||PC->PlacementMode==TEXT("decorate")?PC->SelectedBuildingType:0,
                PC->PlacementMode==TEXT("move")?PC->SelectedBuilding:FString(),
                PC->PlacementMode==TEXT("deploy"));
        }
        else Board->ClearHover();
    }
}
