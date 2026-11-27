#include "SBYMWorldMap.h"

#include "Dom/JsonObject.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"

namespace BYMMapDrawing
{
    constexpr double HexWidth=104.,HexHeight=68.,RowStride=50.;
    double Number(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double Default=0.)
    {double Value=0.;return Object&&Object->TryGetNumberField(Key,Value)&&FMath::IsFinite(Value)?Value:Default;}
    FString String(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
    {FString Value;if(Object)Object->TryGetStringField(Key,Value);return Value;}
    bool Bool(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
    {bool Value=false;return Object&&Object->TryGetBoolField(Key,Value)&&Value;}
    TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& Parent,const TCHAR* Key)
    {const TSharedPtr<FJsonObject>* Value=nullptr;return Parent&&Parent->TryGetObjectField(Key,Value)&&Value?*Value:nullptr;}
    const TArray<TSharedPtr<FJsonValue>>& Array(const TSharedPtr<FJsonObject>& Parent,const TCHAR* Key)
    {static const TArray<TSharedPtr<FJsonValue>> Empty;const TArray<TSharedPtr<FJsonValue>>* Value=nullptr;return Parent&&Parent->TryGetArrayField(Key,Value)&&Value?*Value:Empty;}
    FVector2D Origin(int32 X,int32 Y){return FVector2D(X*HexWidth+(Y&1)*HexWidth*.5,Y*RowStride);}
    bool InsideHex(const FVector2D& Point)
    {
        if(Point.X<0.||Point.X>HexWidth||Point.Y<0.||Point.Y>HexHeight)return false;
        const double Side=FMath::Abs(Point.X-52.)/52.;
        return Point.Y>=Side*17.&&Point.Y<=68.-Side*18.;
    }
}

void SBYMWorldMap::Construct(const FArguments& Args)
{
    MapData=Args._MapData;SelectedCell=Args._SelectedCell;BrushResolver=Args._BrushResolver;LabelFont=Args._LabelFont;
    OnCellSelected=Args._OnCellSelected;OnViewChanged=Args._OnViewChanged;
    Zoom=ZoomFrom=ZoomTarget=FMath::Clamp(Args._Zoom,.5f,1.f);
    CenterPixels=BYMMapDrawing::Origin(Args._CenterX,Args._CenterY)+FVector2D(52.,34.);
    SetClipping(EWidgetClipping::ClipToBounds);
    const TSharedPtr<IPlugin> Plugin=IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
    FString Text;
    if(Plugin&&FFileHelper::LoadFileToString(Text,*FPaths::Combine(Plugin->GetContentDir(),TEXT("Data/maproom-art.json"))))
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Artwork);
    ClampCenter();
}

FVector2D SBYMWorldMap::ComputeDesiredSize(float LayoutScaleMultiplier) const
{return FVector2D(748,450);}

int32 SBYMWorldMap::GetCenterY() const
{return FMath::Clamp(static_cast<int32>(FMath::RoundToInt((CenterPixels.Y-34.)/BYMMapDrawing::RowStride)),0,499);}
int32 SBYMWorldMap::GetCenterX() const
{return FMath::Clamp(static_cast<int32>(FMath::RoundToInt((CenterPixels.X-52.-(GetCenterY()&1)*52.)/104.)),0,499);}

void SBYMWorldMap::ClampCenter()
{
    const auto Data=MapData.Get();
    const double Width=FMath::Clamp(BYMMapDrawing::Number(Data,TEXT("width"),500.),1.,500.);
    const double Height=FMath::Clamp(BYMMapDrawing::Number(Data,TEXT("height"),500.),1.,500.);
    CenterPixels.X=FMath::Clamp(CenterPixels.X,52.,(Width-1.)*104.+104.);
    CenterPixels.Y=FMath::Clamp(CenterPixels.Y,34.,(Height-1.)*50.+34.);
}

void SBYMWorldMap::NotifyViewChanged()
{
    LastRequestedX=GetCenterX();LastRequestedY=GetCenterY();
    OnViewChanged.ExecuteIfBound(LastRequestedX,LastRequestedY,Zoom);
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SBYMWorldMap::CenterOn(int32 X,int32 Y)
{
    CenterPixels=BYMMapDrawing::Origin(FMath::Clamp(X,0,499),FMath::Clamp(Y,0,499))+FVector2D(52.,34.);
    ClampCenter();NotifyViewChanged();
}

void SBYMWorldMap::SetZoom(float Value)
{
    ZoomFrom=Zoom;ZoomTarget=FMath::Clamp(Value,.5f,1.f);ZoomElapsed=0.f;
    // Persist the intended scale if an ownership/page update rebuilds the
    // surrounding native panel while this one-second source tween is active.
    OnViewChanged.ExecuteIfBound(GetCenterX(),GetCenterY(),ZoomTarget);
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SBYMWorldMap::Tick(const FGeometry& Geometry,double Time,float DeltaSeconds)
{
    SLeafWidget::Tick(Geometry,Time,DeltaSeconds);
    const float Delta=FMath::Clamp(DeltaSeconds,0.f,.25f);AnimationTime=FMath::Fmod(Time,1000.);
    if(ZoomElapsed<1.f)
    {
        ZoomElapsed=FMath::Min(1.f,ZoomElapsed+Delta);
        const float T=ZoomElapsed,Ease=T<.5f?2.f*T*T:1.f-FMath::Square(-2.f*T+2.f)*.5f;
        Zoom=FMath::Lerp(ZoomFrom,ZoomTarget,Ease);
        if(ZoomElapsed>=1.f)NotifyViewChanged();
    }
    FVector2D Scroll=FVector2D::ZeroVector;
    if(HeldKeys.Contains(EKeys::Left)||HeldKeys.Contains(EKeys::A))Scroll.X-=1.;
    if(HeldKeys.Contains(EKeys::Right)||HeldKeys.Contains(EKeys::D))Scroll.X+=1.;
    if(HeldKeys.Contains(EKeys::Up)||HeldKeys.Contains(EKeys::W))Scroll.Y-=1.;
    if(HeldKeys.Contains(EKeys::Down)||HeldKeys.Contains(EKeys::S))Scroll.Y+=1.;
    if(!Scroll.IsNearlyZero())
    {
        // MapRoom3Window accelerates from8 to18 source pixels per40Hz tick.
        KeyScrollVelocity=FMath::Min(18.f,KeyScrollVelocity+Delta*10.f);
        CenterPixels+=Scroll*KeyScrollVelocity*(Delta*40.)/Zoom;ClampCenter();
        if(FMath::Abs(GetCenterX()-LastRequestedX)>=2||FMath::Abs(GetCenterY()-LastRequestedY)>=2)NotifyViewChanged();
    }
    else KeyScrollVelocity=8.f;
    Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SBYMWorldMap::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    const FKey Key=Event.GetKey();
    if(Key==EKeys::Left||Key==EKeys::Right||Key==EKeys::Up||Key==EKeys::Down||Key==EKeys::W||Key==EKeys::A||Key==EKeys::S||Key==EKeys::D)
    {HeldKeys.Add(Key);return FReply::Handled();}
    return SLeafWidget::OnKeyDown(Geometry,Event);
}

FReply SBYMWorldMap::OnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(HeldKeys.Remove(Event.GetKey())>0){NotifyViewChanged();return FReply::Handled();}
    return SLeafWidget::OnKeyUp(Geometry,Event);
}

void SBYMWorldMap::OnFocusLost(const FFocusEvent& Event)
{HeldKeys.Empty();KeyScrollVelocity=8.f;SLeafWidget::OnFocusLost(Event);}

FVector2D SBYMWorldMap::WorldFromPointer(const FGeometry& Geometry,const FVector2D& ScreenPosition) const
{return CenterPixels+(Geometry.AbsoluteToLocal(ScreenPosition)-FVector2D(Geometry.GetLocalSize())*.5)/Zoom;}

FString SBYMWorldMap::CellAt(const FVector2D& WorldPixels) const
{
    using namespace BYMMapDrawing;
    for(const auto& Value:Array(MapData.Get(),TEXT("cells")))
    {
        if(!Value||Value->Type!=EJson::Object)continue;
        const auto Cell=Value->AsObject();
        if(InsideHex(WorldPixels-Origin(static_cast<int32>(Number(Cell,TEXT("x"))),static_cast<int32>(Number(Cell,TEXT("y"))))))return String(Cell,TEXT("id"));
    }
    return FString();
}

TSharedPtr<FJsonObject> SBYMWorldMap::TileFor(int32 Altitude,int32 X,int32 Y) const
{
    using namespace BYMMapDrawing;
    const auto& Tiles=Array(Artwork,TEXT("tiles"));
    for(const auto& Value:Array(Artwork,TEXT("altitudeRanges")))
    {
        if(!Value||Value->Type!=EJson::Object)continue;
        const auto Range=Value->AsObject();
        if(Altitude<Number(Range,TEXT("start")))return nullptr;
        if(Altitude>Number(Range,TEXT("end")))continue;
        const auto& Choices=Array(Range,TEXT("tileIndices"));if(Choices.IsEmpty())return nullptr;
        uint32 Hash=static_cast<uint32>(Y*500+X);Hash^=Hash<<21;Hash^=Hash>>3;Hash^=Hash<<4;
        const int64 Signed=static_cast<int64>(static_cast<int32>(Hash));
        const int32 Choice=static_cast<int32>((Signed<0?-Signed:Signed)%Choices.Num());
        const int32 Index=static_cast<int32>(Choices[Choice]->AsNumber());
        return Tiles.IsValidIndex(Index)&&Tiles[Index]&&Tiles[Index]->Type==EJson::Object?Tiles[Index]->AsObject():nullptr;
    }
    return nullptr;
}

int32 SBYMWorldMap::OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& DrawElements,int32 LayerId,const FWidgetStyle& Style,bool bParentEnabled) const
{
    using namespace BYMMapDrawing;
    const FVector2D Size=Geometry.GetLocalSize();
    const auto Data=MapData.Get();
    const FLinearColor Tint=Style.GetColorAndOpacityTint();
    FSlateDrawElement::MakeBox(DrawElements,LayerId,Geometry.ToPaintGeometry(),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,FLinearColor(.10f,.19f,.22f)*Tint);
    auto DrawAsset=[&](const TSharedPtr<FJsonObject>& Asset,const FVector2D& Position,int32 Layer,const FLinearColor& Color=FLinearColor::White)
    {
        const FString Path=String(Asset,TEXT("path"));if(Path.IsEmpty()||!BrushResolver)return;
        const FSlateBrush* Brush=BrushResolver(Path);if(!Brush||!Brush->GetResourceObject())return;
        const FVector2D Extent(Number(Asset,TEXT("imageWidth")),Number(Asset,TEXT("imageHeight")));
        if(Extent.X<=0.||Extent.Y<=0.)return;
        FSlateDrawElement::MakeBox(DrawElements,Layer,Geometry.ToPaintGeometry(Extent*Zoom,FSlateLayoutTransform(Position)),Brush,ESlateDrawEffect::None,Color*Tint);
    };
    // The original water backdrop continues behind empty/unloaded cells.
    const auto Background=Object(Artwork,TEXT("background"));
    const double BackgroundWidth=Number(Background,TEXT("imageWidth")),BackgroundHeight=Number(Background,TEXT("imageHeight"));
    if(BackgroundWidth>0.&&BackgroundHeight>0.)
    {
        const double W=BackgroundWidth*Zoom,H=BackgroundHeight*Zoom;
        const double OffsetX=FMath::Fmod(-CenterPixels.X*Zoom+Size.X*.5,W),OffsetY=FMath::Fmod(-CenterPixels.Y*Zoom+Size.Y*.5,H);
        for(double Y=OffsetY-H;Y<Size.Y;Y+=H)for(double X=OffsetX-W;X<Size.X;X+=W)DrawAsset(Background,FVector2D(X,Y),LayerId+1);
    }
    auto DrawAnchored=[&](const TSharedPtr<FJsonObject>& Asset,const FVector2D& Position,int32 Layer)
    {if(Asset)DrawAsset(Asset,Position+FVector2D(Number(Asset,TEXT("offsetX")),Number(Asset,TEXT("offsetY")))*Zoom,Layer);};
    auto DrawSheetFrame=[&](const TSharedPtr<FJsonObject>& Asset,const FVector2D& Position,int32 Layer,int32 Frame,bool bVertical)
    {
        const FString Path=String(Asset,TEXT("path"));if(Path.IsEmpty()||!BrushResolver)return;
        const FSlateBrush* Base=BrushResolver(Path);if(!Base||!Base->GetResourceObject())return;
        const int32 Frames=FMath::Clamp(static_cast<int32>(Number(Asset,TEXT("frames"),40)),1,40);
        const double FrameWidth=Number(Asset,TEXT("frameWidth"),100),FrameHeight=Number(Asset,TEXT("frameHeight"),110);
        if(FrameWidth<=0.||FrameHeight<=0.)return;
        Frame=FMath::Clamp(Frame,0,Frames-1);
        // Each source sheet frame has its own stable brush and UV region.
        const FString Key=Path+(bVertical?TEXT(":v:"):TEXT(":h:"))+FString::FromInt(Frame);
        TSharedPtr<FSlateBrush>& Brush=AnimationBrushes.FindOrAdd(Key);
        if(!Brush)
        {
            Brush=MakeShared<FSlateBrush>(*Base);
            Brush->SetUVRegion(bVertical
                ?FBox2f(FVector2f(0.f,static_cast<float>(Frame)/Frames),FVector2f(1.f,static_cast<float>(Frame+1)/Frames))
                :FBox2f(FVector2f(static_cast<float>(Frame)/Frames,0.f),FVector2f(static_cast<float>(Frame+1)/Frames,1.f)));
            Brush->SetImageSize(FVector2D(FrameWidth,FrameHeight));
        }
        const FVector2D Offset(Number(Asset,TEXT("offsetX")),Number(Asset,TEXT("offsetY")));
        FSlateDrawElement::MakeBox(DrawElements,Layer,Geometry.ToPaintGeometry(FVector2D(FrameWidth,FrameHeight)*Zoom,FSlateLayoutTransform(Position+Offset*Zoom)),Brush.Get(),ESlateDrawEffect::None,Tint);
    };
    const auto Icons=Object(Artwork,TEXT("icons")),Overlays=Object(Artwork,TEXT("overlays"));
    const FString Self=String(Data,TEXT("selfId"));
    TArray<TSharedPtr<FJsonValue>> Cells=Array(Data,TEXT("cells"));
    Cells.Sort([](const TSharedPtr<FJsonValue>& A,const TSharedPtr<FJsonValue>& B)
    {
        const auto AO=A&&A->Type==EJson::Object?A->AsObject():nullptr,BO=B&&B->Type==EJson::Object?B->AsObject():nullptr;
        const double AY=Number(AO,TEXT("y")),BY=Number(BO,TEXT("y"));
        return AY==BY?Number(AO,TEXT("x"))<Number(BO,TEXT("x")):AY<BY;
    });
    // Source unblocked ground belongs to baseLayer. Plants at altitude51+
    // belong to the row-sorted tileLayer with cell icons, so foreground plants
    // can correctly overlap the preceding row.
    for(const auto& Value:Cells)
    {
        if(!Value||Value->Type!=EJson::Object)continue;const auto Cell=Value->AsObject();
        const int32 X=static_cast<int32>(Number(Cell,TEXT("x"))),Y=static_cast<int32>(Number(Cell,TEXT("y")));
        const FVector2D Position=(Origin(X,Y)-CenterPixels)*Zoom+Size*.5;
        const auto Tile=TileFor(static_cast<int32>(Number(Cell,TEXT("terrain"))),X,Y);
        if(Tile&&Number(Cell,TEXT("terrain"))<51.)DrawAnchored(Tile,Position,LayerId+2);
    }
    // Use the original rounded range path. Owned ranges stay visible; a
    // selected/hovered rival range is shown red without hiding distant terrain.
    auto DrawRange=[&](const TSharedPtr<FJsonObject>& Cell,bool bOwn)
    {
        const double Range=Number(Cell,TEXT("range"));if(Range<=0.)return;
        const int32 X=static_cast<int32>(Number(Cell,TEXT("x"))),Y=static_cast<int32>(Number(Cell,TEXT("y")));
        const FVector2D Center=(Origin(X,Y)+FVector2D(52,34)-CenterPixels)*Zoom+Size*.5;
        TArray<FVector2f> Points;
        auto Add=[&](const FVector2D& Point){Points.Add(FVector2f(Center+Point*Zoom));};
        if(Range<2.)
        {
            for(int32 I=0;I<=48;++I){const double A=2.*PI*I/48.;Add(FVector2D(FMath::Cos(A)*(52.+Range*104.),FMath::Sin(A)*(25.+Range*50.)));}
        }
        else
        {
            const double C=Range==2.?26.:52.,W=Range*52.+26.,H=Range*50.+25.;
            auto Curve=[&](const FVector2D& A,const FVector2D& Control,const FVector2D& B)
            {for(int32 I=1;I<=8;++I){const double T=I/8.;Add(A*FMath::Square(1.-T)+Control*(2.*T*(1.-T))+B*FMath::Square(T));}};
            Add(FVector2D(-2*W+C,-C));Add(FVector2D(-W-C,-H+C));
            Curve(FVector2D(-W-C,-H+C),FVector2D(-W,-H),FVector2D(-W+2*C,-H));
            Add(FVector2D(W-2*C,-H));Curve(FVector2D(W-2*C,-H),FVector2D(W,-H),FVector2D(W+C,-H+C));
            Add(FVector2D(2*W-C,-C));Curve(FVector2D(2*W-C,-C),FVector2D(2*W,0),FVector2D(2*W-C,C));
            Add(FVector2D(W+C,H-C));Curve(FVector2D(W+C,H-C),FVector2D(W,H),FVector2D(W-2*C,H));
            Add(FVector2D(-W+2*C,H));Curve(FVector2D(-W+2*C,H),FVector2D(-W,H),FVector2D(-W-C,H-C));
            Add(FVector2D(-2*W+C,C));Curve(FVector2D(-2*W+C,C),FVector2D(-2*W,0),FVector2D(-2*W+C,-C));
        }
        const FLinearColor Color=bOwn?FLinearColor(.22f,.62f,1.f,.55f):FLinearColor(1.f,.24f,.16f,.70f);
        FSlateDrawElement::MakeLines(DrawElements,LayerId+3,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color*Tint,true,6.f);
        FSlateDrawElement::MakeLines(DrawElements,LayerId+3,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(1.f,1.f,1.f,.8f)*Tint,true,1.f);
    };
    for(const auto& Value:Cells)if(Value&&Value->Type==EJson::Object&&Bool(Value->AsObject(),TEXT("owned")))DrawRange(Value->AsObject(),true);
    int32 RowLayer=LayerId+5;
    const int32 InfoLayer=LayerId+5+Cells.Num()*8+1;
    for(const auto& Value:Cells)
    {
        if(!Value||Value->Type!=EJson::Object)continue;const auto Cell=Value->AsObject();
        const int32 CellLayer=RowLayer;RowLayer+=8;
        const int32 X=static_cast<int32>(Number(Cell,TEXT("x"))),Y=static_cast<int32>(Number(Cell,TEXT("y")));
        const FVector2D Position=(Origin(X,Y)-CenterPixels)*Zoom+Size*.5;
        if(Position.X<-320.*Zoom||Position.Y<-160.*Zoom||Position.X>Size.X+320.*Zoom||Position.Y>Size.Y+320.*Zoom)continue;
        if(Number(Cell,TEXT("terrain"))>=51.)DrawAnchored(TileFor(static_cast<int32>(Number(Cell,TEXT("terrain"))),X,Y),Position,CellLayer);
        const int32 Kind=static_cast<int32>(Number(Cell,TEXT("kind")));
        const FString Id=String(Cell,TEXT("id"));
        if(Kind!=100)
        {
            const bool Own=Bool(Cell,TEXT("owned"))||(!Self.IsEmpty()&&String(Cell,TEXT("ownerId"))==Self);
            const FString HostRelationship=String(Cell,TEXT("relationship"));
            const TCHAR* Relationship=Own?TEXT("self"):HostRelationship==TEXT("ally")?TEXT("ally"):HostRelationship==TEXT("neutral")?TEXT("neutral"):TEXT("enemy");
            if(!Own&&(Id==HoveredCell||Id==SelectedCell.Get()))DrawRange(Cell,false);
            if(Kind!=104)DrawAnchored(Object(Overlays,Relationship),Position,LayerId+4);
            const FString Buff=String(Cell,TEXT("buffEffect"));
            if(Zoom>=1.f&&(Buff==TEXT("player")||Buff==TEXT("enemy")||Buff==TEXT("mixed")))
                // Source update increments before copying at the 40Hz display clock.
                DrawSheetFrame(Object(Object(Artwork,TEXT("strongholdEffects")),*Buff),Position,CellLayer+1,(static_cast<int32>(AnimationTime*40.)+1)%40,false);
            const bool FullyFortified=(Kind==101||Kind==102||Kind==103)&&Bool(Cell,TEXT("fullyFortified"));
            if(FullyFortified)DrawAnchored(Object(Icons,TEXT("fullyFortifiedBack")),Position,CellLayer+2);
            const TCHAR* IconKey=Kind==101?TEXT("playerBase"):Kind==102?TEXT("resourceCell"):Kind==103?TEXT("stronghold"):Kind==104?TEXT("fortification"):TEXT("wildMonsterBase");
            if(Kind==104&&Bool(Cell,TEXT("fortifiesParent")))
            {
                const int32 PX=static_cast<int32>(Number(Cell,TEXT("parentX"))),PY=static_cast<int32>(Number(Cell,TEXT("parentY")));
                if(PY==Y&&PX==X+1)IconKey=TEXT("fortificationEast");
                else if(PY==Y&&PX==X-1)IconKey=TEXT("fortificationWest");
                else if(PY==Y-1&&PX==X+(Y&1))IconKey=TEXT("fortificationNorthEast");
                else if(PY==Y-1&&PX==X-1+(Y&1))IconKey=TEXT("fortificationNorthWest");
                else if(PY==Y+1&&PX==X+(Y&1))IconKey=TEXT("fortificationSouthEast");
                else if(PY==Y+1&&PX==X-1+(Y&1))IconKey=TEXT("fortificationSouthWest");
            }
            DrawAnchored(Object(Icons,IconKey),Position,CellLayer+3);
            if(FullyFortified)DrawAnchored(Object(Icons,TEXT("fullyFortifiedFront")),Position,CellLayer+4);
            if(Kind==104&&!Bool(Cell,TEXT("isDestroyed")))DrawAnchored(Object(Object(Artwork,TEXT("fortificationLights")),Relationship),Position,CellLayer+4);
            if(Number(Cell,TEXT("shieldUntil"))>Number(Data,TEXT("now")))
            {
                const auto Shield=Object(Icons,TEXT("damageProtection"));
                DrawAsset(Shield,Position+FVector2D(Number(Shield,TEXT("offsetX")),Number(Shield,TEXT("offsetY")))*Zoom,CellLayer+5);
            }
            const double Damage=Number(Cell,TEXT("damagePercent"),Number(Cell,TEXT("destructionPercent")));
            if(Damage>0.)
            {
                const int32 Frame=static_cast<int32>(FMath::FloorToInt(17.*FMath::Clamp(Damage*.01,0.,.99)));
                DrawSheetFrame(Object(Artwork,TEXT("damageBar")),Position+FVector2D(32,0)*Zoom,CellLayer+5,Frame,true);
            }
            if(Zoom>=1.f&&Kind!=104)
            {
                FString Label=String(Cell,TEXT("name"));
                if(Label.IsEmpty())Label=String(Cell,TEXT("ownerName"));
                Label+=FString::Printf(TEXT(" (%d)"),static_cast<int32>(Number(Cell,TEXT("level"))));
                double LabelWidth=Label.Len()*7.;
                FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
                if(Renderer)
                {
                    const auto Measure=Renderer->GetFontMeasureService();
                    LabelWidth=Measure->Measure(FText::FromString(Label),LabelFont,1.f).X;
                    while(LabelWidth>156.&&Label.Len()>2)
                    {Label=Label.LeftChop(2)+TEXT("…");LabelWidth=Measure->Measure(FText::FromString(Label),LabelFont,1.f).X;}
                }
                // Source infoLayer is above row-sorted vegetation, centered over
                // 1.5hex widths and hidden while zoomed out.
                const FVector2D LabelPosition=Position+FVector2D(52.-LabelWidth*.5,48.)*Zoom;
                FSlateDrawElement::MakeText(DrawElements,InfoLayer,Geometry.ToPaintGeometry(FVector2D(156.*Zoom,20.),FSlateLayoutTransform(LabelPosition)),Label,LabelFont,ESlateDrawEffect::None,FLinearColor::White*Tint);
            }
        }
        if(Id==SelectedCell.Get()||Id==HoveredCell)
        {
            const FVector2D Corners[]={FVector2D(52,0),FVector2D(104,17),FVector2D(104,50),FVector2D(52,68),FVector2D(0,50),FVector2D(0,17),FVector2D(52,0)};
            TArray<FVector2f> Points;for(const FVector2D& Corner:Corners)Points.Add(FVector2f(Position+Corner*Zoom));
            FSlateDrawElement::MakeLines(DrawElements,CellLayer+7,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Id==SelectedCell.Get()?FLinearColor::White:FLinearColor(1.f,.85f,.2f),true,Id==SelectedCell.Get()?2.5f:1.25f);
        }
    }
    return InfoLayer;
}

FReply SBYMWorldMap::OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()!=EKeys::LeftMouseButton&&Event.GetEffectingButton()!=EKeys::RightMouseButton&&Event.GetEffectingButton()!=EKeys::MiddleMouseButton)return FReply::Unhandled();
    bDragging=true;bMoved=false;PointerDown=PreviousPointer=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
    return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this),EFocusCause::Mouse,false);
}

FReply SBYMWorldMap::OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(bDragging&&HasMouseCapture())
    {
        const FVector2D Pointer=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        bMoved=bMoved||(Pointer-PointerDown).SizeSquared()>16.;
        if(bMoved){CenterPixels-=(Pointer-PreviousPointer)/Zoom;ClampCenter();}
        PreviousPointer=Pointer;
        if(FMath::Abs(GetCenterX()-LastRequestedX)>=3||FMath::Abs(GetCenterY()-LastRequestedY)>=3)NotifyViewChanged();
        Invalidate(EInvalidateWidgetReason::Paint);return FReply::Handled();
    }
    const FString Hover=CellAt(WorldFromPointer(Geometry,Event.GetScreenSpacePosition()));
    if(Hover!=HoveredCell){HoveredCell=Hover;Invalidate(EInvalidateWidgetReason::Paint);}
    return FReply::Handled();
}

FReply SBYMWorldMap::OnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(!HasMouseCapture())return FReply::Unhandled();
    if(!bMoved&&Event.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        const FString Id=CellAt(WorldFromPointer(Geometry,Event.GetScreenSpacePosition()));
        if(!Id.IsEmpty())OnCellSelected.ExecuteIfBound(Id);
    }
    bDragging=false;NotifyViewChanged();return FReply::Handled().ReleaseMouseCapture();
}

FReply SBYMWorldMap::OnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    SetZoom(Event.GetWheelDelta()>0.f?1.f:.5f);
    return FReply::Handled();
}

void SBYMWorldMap::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{bDragging=false;bMoved=false;SLeafWidget::OnMouseCaptureLost(Event);}
