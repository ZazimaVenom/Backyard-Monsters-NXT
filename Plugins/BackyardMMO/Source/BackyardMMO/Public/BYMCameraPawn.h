#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "BYMCameraPawn.generated.h"
class UCameraComponent;
class ABYMBoard;
UCLASS()
class BACKYARDMMO_API ABYMCameraPawn : public APawn
{
    GENERATED_BODY()
public:
    ABYMCameraPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    bool FocusOnWorker(const FString& WorkerId);
    /** Keep the entire orthographic viewport within the repeated terrain, at any aspect ratio. */
    static void ConstrainViewToTerrain(FVector& Position, float& OrthoWidth, float AspectRatio,
        float YardExtent, float TerrainMargin);
    static void ConstrainViewToGroundBounds(FVector& Position,float& OrthoWidth,float AspectRatio,
        const FVector2D& YardMin,const FVector2D& YardMax,float TerrainMargin,float PanMargin);
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> CameraRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<ABYMBoard> Board;
    FVector2D LastPointer=FVector2D::ZeroVector;
    bool bDragging=false;
    int32 KnownCells=0;
    float KnownCellUnits=0;
    void ClickBoard();
    void CancelTool();
    void BeginDrag();
    void EndDrag();
    void ZoomIn();
    void ZoomOut();
    void ResetCamera();
    bool PointerToCell(int32& X,int32& Y) const;
    void ClampCamera();
};
