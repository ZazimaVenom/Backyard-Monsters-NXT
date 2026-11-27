#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "InputCoreTypes.h"
#include "Widgets/SLeafWidget.h"

class FJsonObject;
struct FSlateBrush;

DECLARE_DELEGATE_OneParam(FBYMMapCellSelected, const FString&);
DECLARE_DELEGATE_ThreeParams(FBYMMapViewChanged, int32, int32, float);

/** The original Map Room 3 hex projection, drawn with the bundled source art. */
class SBYMWorldMap : public SLeafWidget
{
public:
    using FBrushResolver = TFunction<const FSlateBrush*(const FString&)>;
    SLATE_BEGIN_ARGS(SBYMWorldMap) : _CenterX(250), _CenterY(250), _Zoom(1.f) {}
        SLATE_ATTRIBUTE(TSharedPtr<FJsonObject>, MapData)
        SLATE_ATTRIBUTE(FString, SelectedCell)
        SLATE_ARGUMENT(FBrushResolver, BrushResolver)
        SLATE_ARGUMENT(FSlateFontInfo, LabelFont)
        SLATE_ARGUMENT(int32, CenterX)
        SLATE_ARGUMENT(int32, CenterY)
        SLATE_ARGUMENT(float, Zoom)
        SLATE_EVENT(FBYMMapCellSelected, OnCellSelected)
        SLATE_EVENT(FBYMMapViewChanged, OnViewChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    void CenterOn(int32 X, int32 Y);
    void SetZoom(float Value);
    float GetZoom() const { return Zoom; }
    int32 GetCenterX() const;
    int32 GetCenterY() const;
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual void Tick(const FGeometry& Geometry, double Time, float DeltaSeconds) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual void OnFocusLost(const FFocusEvent& Event) override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& DrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;

private:
    TAttribute<TSharedPtr<FJsonObject>> MapData;
    TAttribute<FString> SelectedCell;
    FBrushResolver BrushResolver;
    FSlateFontInfo LabelFont;
    FBYMMapCellSelected OnCellSelected;
    FBYMMapViewChanged OnViewChanged;
    TSharedPtr<FJsonObject> Artwork;
    mutable TMap<FString,TSharedPtr<FSlateBrush>> AnimationBrushes;
    FVector2D CenterPixels = FVector2D::ZeroVector;
    FVector2D PointerDown = FVector2D::ZeroVector;
    FVector2D PreviousPointer = FVector2D::ZeroVector;
    FString HoveredCell;
    float Zoom = 1.f;
    float ZoomFrom = 1.f, ZoomTarget = 1.f, ZoomElapsed = 1.f;
    double AnimationTime = 0.;
    float KeyScrollVelocity = 8.f;
    TSet<FKey> HeldKeys;
    bool bDragging = false;
    bool bMoved = false;
    int32 LastRequestedX = -1, LastRequestedY = -1;

    void ClampCenter();
    void NotifyViewChanged();
    FVector2D WorldFromPointer(const FGeometry& Geometry, const FVector2D& ScreenPosition) const;
    FString CellAt(const FVector2D& WorldPixels) const;
    TSharedPtr<FJsonObject> TileFor(int32 Altitude, int32 X, int32 Y) const;
};
