#include "BYMHUD.h"
#include "BYMBoard.h"
#include "BYMCameraPawn.h"
#include "BYMGameInstance.h"
#include "BYMPlayerController.h"
#include "BYMSettings.h"
#include "SBYMWorldMap.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "BYMUILayoutTestFixture.h"
#endif
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/TextLayout.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Math/TransformCalculus2D.h"
#include "Rendering/SlateRenderTransform.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtrTemplates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace BYMUI
{
    const FLinearColor Ink(.12f,.075f,.025f,1.f);
    const FLinearColor Muted(.30f,.24f,.14f,1.f);
    const FLinearColor Paper(.86f,.81f,.61f,1.f);
    const FLinearColor White=FLinearColor::White;
    const FLinearColor Gold(1.f,.83f,.27f,1.f);
    const FLinearColor Red(.65f,.035f,.015f,1.f);
    const FLinearColor Green(.12f,.36f,.025f,1.f);
    const FSlateBrush* WhiteBrush(){return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));}
    FSlateFontInfo Font(int32 Size,bool Heading=false)
    {
        const TSharedPtr<IPlugin> Plugin=IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
        if(!Plugin)return FCoreStyle::GetDefaultFontStyle(Heading?TEXT("Bold"):TEXT("Regular"),Size);
        FSlateFontInfo Result(FPaths::Combine(Plugin->GetContentDir(),Heading?TEXT("UI/Fonts/GROBOLD.ttf"):TEXT("UI/Fonts/Verdana.ttf")),Size);
        return Result;
    }
    FSlateFontInfo ButtonFont(int32 Size=13)
    {
        FSlateFontInfo Result=Font(Size,true);
        Result.OutlineSettings.OutlineSize=1;
        Result.OutlineSettings.OutlineColor=FLinearColor::Black;
        return Result;
    }
    TSharedPtr<FJsonObject> Parse(const FString& Text)
    {
        TSharedPtr<FJsonObject> Obj;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Obj);return Obj;
    }
    TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Key)
    {
        const TSharedPtr<FJsonObject>* Value=nullptr;
        return Obj&&Obj->TryGetObjectField(Key,Value)&&Value?*Value:nullptr;
    }
    FString String(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Key,const FString& Default=TEXT(""))
    {
        FString Value;return Obj&&Obj->TryGetStringField(Key,Value)?Value:Default;
    }
    double Number(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Key,double Default=0)
    {
        double Value=0;return Obj&&Obj->TryGetNumberField(Key,Value)?Value:Default;
    }
    bool Bool(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Key,bool Default=false)
    {
        bool Value=false;return Obj&&Obj->TryGetBoolField(Key,Value)?Value:Default;
    }
    TArray<TSharedPtr<FJsonValue>> Array(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Key)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        return Obj&&Obj->TryGetArrayField(Key,Values)&&Values?*Values:TArray<TSharedPtr<FJsonValue>>();
    }
    FString Json(const TSharedRef<FJsonObject>& Obj)
    {
        FString Text;FJsonSerializer::Serialize(Obj,TJsonWriterFactory<>::Create(&Text));return Text;
    }
    FString Count(double Value){return FText::AsNumber(static_cast<int64>(Value)).ToString();}
    FString Decimal(double Value)
    {return FText::AsNumber(Value).ToString();}
    FString PlainText(FString Value)
    {
        Value.ReplaceInline(TEXT("<br>"),TEXT("\n"),ESearchCase::IgnoreCase);
        Value.ReplaceInline(TEXT("<br/>"),TEXT("\n"),ESearchCase::IgnoreCase);
        Value.ReplaceInline(TEXT("<br />"),TEXT("\n"),ESearchCase::IgnoreCase);
        FString Result;bool InTag=false;
        for(const TCHAR C:Value){if(C==TEXT('<'))InTag=true;else if(C==TEXT('>'))InTag=false;else if(!InTag)Result.AppendChar(C);}
        Result.ReplaceInline(TEXT("&amp;"),TEXT("&"));Result.ReplaceInline(TEXT("&nbsp;"),TEXT(" "));
        Result.ReplaceInline(TEXT("&quot;"),TEXT("\""));Result.ReplaceInline(TEXT("&#39;"),TEXT("'"));
        return Result;
    }
    FString Duration(double Seconds)
    {
        const int64 S=FMath::Max<int64>(0,FMath::CeilToInt64(Seconds));
        if(S>=86400)return FString::Printf(TEXT("%lldd %lldh"),S/86400,(S/3600)%24);
        if(S>=3600)return FString::Printf(TEXT("%lldh %lldm"),S/3600,(S/60)%60);
        if(S>=60)return FString::Printf(TEXT("%lldm %llds"),S/60,S%60);
        return FString::Printf(TEXT("%llds"),S);
    }
    FString Cost(const TSharedPtr<FJsonObject>& Obj)
    {
        TArray<FString> Parts;
        const TCHAR* Keys[]={TEXT("twigs"),TEXT("pebbles"),TEXT("putty"),TEXT("goo")};
        const TCHAR* Names[]={TEXT("Twigs"),TEXT("Pebbles"),TEXT("Putty"),TEXT("Goo")};
        for(int32 I=0;I<4;++I)if(Number(Obj,Keys[I])>0)Parts.Add(Count(Number(Obj,Keys[I]))+TEXT(" ")+Names[I]);
        return Parts.Num()?FString::Join(Parts,TEXT("  /  ")):TEXT("No resource cost");
    }
}

struct FBYMUIArt
{
    TStrongObjectPtr<UTexture2D> Texture;
    FSlateBrush Brush;
    FBYMUIArt(UTexture2D* InTexture,const FVector2D& SourceSize):Texture(InTexture)
    {
        Brush=*BYMUI::WhiteBrush();
        if(InTexture)Brush.SetResourceObject(InTexture);
        Brush.DrawAs=ESlateBrushDrawType::Image;
        // Texture compilation may expose a temporary 4x4 resource. Source
        // dimensions are stable before import, during compilation and after it.
        Brush.ImageSize=SourceSize;
    }
};

/** Source artwork and layouts rendered by native Slate. Only the host changes game state. */
class SBYMRoot : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SBYMRoot){} SLATE_ARGUMENT(TWeakObjectPtr<ABYMPlayerController>,Controller) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        PC=Args._Controller;SetVisibility(EVisibility::SelfHitTestInvisible);PrepareStyles();BuildRoot();
    }
    virtual FReply OnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override
    {
        // A scroll box inside the dialog handles its wheel first. Unhandled wheel
        // events must not zoom the yard behind an open popup.
        return Popup.IsEmpty()?SCompoundWidget::OnMouseWheel(Geometry,Event):FReply::Handled();
    }
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Time,Delta);
        ViewportWidth=FMath::Max(1.f,static_cast<float>(Geometry.GetLocalSize().X));
        ViewportHeight=FMath::Max(1.f,static_cast<float>(Geometry.GetLocalSize().Y));
        if(!PC.IsValid())return;
        PC->bWorldMapOpen=Popup==TEXT("MapRoom")||Popup==TEXT("MapAttack")||Popup==TEXT("MapAbandon")||Popup==TEXT("MapRelocate");
        PC->bPointerOverGameUI=false;
        if(FSlateApplication::IsInitialized())
        {
            const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
            for(int32 I=BoardInputBlockers.Num()-1;I>=0;--I)
            {
                const TSharedPtr<SWidget> Panel=BoardInputBlockers[I].Pin();
                if(!Panel){BoardInputBlockers.RemoveAt(I);continue;}
                if(Panel->GetVisibility().IsVisible()&&Panel->GetCachedGeometry().IsUnderLocation(Pointer))
                    PC->bPointerOverGameUI=true;
            }
        }
        if(SeenSnapshot!=PC->SnapshotRevision)
        {
            const double PreviousPicked=BYMUI::Number(Shop(),TEXT("mushroomsPicked"),-1);
            const double PreviousShiny=BYMUI::Number(Shop(),TEXT("mushroomShinyEarned"),-1);
            SeenSnapshot=PC->SnapshotRevision;Snapshot=BYMUI::Parse(PC->LastSnapshot);
            SampleTime=BYMUI::Number(Snapshot,TEXT("now"),FDateTime::UtcNow().ToUnixTimestamp());SampleClock=FPlatformTime::Seconds();
            const double Picked=BYMUI::Number(Shop(),TEXT("mushroomsPicked"),-1),Earned=BYMUI::Number(Shop(),TEXT("mushroomShinyEarned"),-1);
            // Report only newly completed authoritative picks. Initial/rejoined
            // snapshots never replay old rewards, and newer actions/errors win.
            if(PreviousPicked>=0&&PreviousShiny>=0&&Picked>PreviousPicked&&Earned>=PreviousShiny
                &&!MushroomCommandMessage.IsEmpty()&&PC->StatusMessage==MushroomCommandMessage
                &&PC->LastCommandOperation==TEXT("clearmushroom")&&PC->bLastCommandSucceeded
                &&!PC->IsCommandPending()&&PC->PlacementMode.IsEmpty())
            {
                FString Message=Picked-PreviousPicked==1?TEXT("Mushroom cleared."):BYMUI::Count(Picked-PreviousPicked)+TEXT(" mushrooms cleared.");
                Message+=Earned>PreviousShiny?TEXT(" Found ")+BYMUI::Count(Earned-PreviousShiny)+TEXT(" Shiny."):TEXT(" No Shiny found this time.");
                PC->StatusMessage=Message;MushroomCommandMessage=Message;
            }
        }
        if(SeenLadder!=PC->LadderRevision)
        {
            SeenLadder=PC->LadderRevision;Ladder=BYMUI::Parse(PC->LastLadder);
            if(Popup==TEXT("Rivals"))bRefreshPopup=true;
        }
        if(SeenWorldMap!=PC->WorldMapRevision)
        {
            SeenWorldMap=PC->WorldMapRevision;
            const auto Response=BYMUI::Parse(PC->LastWorldMap);
            WorldMap=BYMUI::Object(Response,TEXT("map"));
            if(!WorldMap&&Response)
            {
                WorldMap=MakeShared<FJsonObject>();WorldMap->SetBoolField(TEXT("available"),false);
                WorldMap->SetStringField(TEXT("reason"),BYMUI::String(Response,TEXT("error")));
            }
            if(WorldMap&&!bMapCentered)
            {
                MapCenterX=static_cast<int32>(BYMUI::Number(WorldMap,TEXT("centerX"),BYMUI::Number(WorldMap,TEXT("homeX"),250)));
                MapCenterY=static_cast<int32>(BYMUI::Number(WorldMap,TEXT("centerY"),BYMUI::Number(WorldMap,TEXT("homeY"),250)));
                bMapCentered=true;
                if(MapCanvas)MapCanvas->CenterOn(MapCenterX,MapCenterY);
            }
            // A late reply from the previous page must not overwrite a newer
            // navigation request; only shrink the request if ownership pages fell.
            MapOwnedPage=FMath::Clamp(MapOwnedPage,0,FMath::Max(0,static_cast<int32>(BYMUI::Number(WorldMap,TEXT("ownedPages"),1))-1));
            FString Structure=(BYMUI::Bool(WorldMap,TEXT("available"))?TEXT("ready"):TEXT("locked"))+FString::FromInt(MapOwnedPage);
            for(const TCHAR* Key:{TEXT("ownedCells"),TEXT("bookmarks")})
                for(const auto& Value:BYMUI::Array(WorldMap,Key))if(Value&&Value->Type==EJson::Object)Structure+=TEXT("|")+BYMUI::String(Value->AsObject(),TEXT("id"));
            if(Structure!=SeenMapStructure){SeenMapStructure=Structure;if(Popup==TEXT("MapRoom"))bRefreshPopup=true;}
            if(MapCanvas)MapCanvas->Invalidate(EInvalidateWidgetReason::Paint);
        }
        if(Popup==TEXT("MapRoom")&&FPlatformTime::Seconds()>=NextMapRefresh)
        {NextMapRefresh=FPlatformTime::Seconds()+2.;RequestMap();}
        if(bGame!=PC->bAuthenticated){BuildRoot();}
        if(!bGame)return;
        const bool Attacking=IsAttacking();
        if(Attacking!=bWasAttacking)
        {
            bWasAttacking=Attacking;
            if(Attacking){Popup.Empty();PC->PlacementMode.Empty();}
            else if(BYMUI::Object(Snapshot,TEXT("lastBattle")))Popup=TEXT("BattleResult");
            bRefreshPopup=true;RefreshDeployment();
        }
        if(SeenCommand!=PC->CommandRevision)
        {
            SeenCommand=PC->CommandRevision;
            MushroomCommandMessage=PC->bLastCommandSucceeded&&PC->LastCommandOperation==TEXT("clearmushroom")?PC->StatusMessage:FString();
            if(PC->bLastCommandSucceeded&&PC->LastCommandOperation==TEXT("bunkerTransfer"))BunkerSelection.Reset();
            if(PC->bLastCommandSucceeded&&PC->LastCommandOperation==TEXT("bunkerDismiss")&&Popup==TEXT("DismissBunker"))
            {Popup=TEXT("Bunker");bRefreshPopup=true;}
            if(PC->bLastCommandSucceeded&&(PC->LastCommandOperation==TEXT("mapInspect")||PC->LastCommandOperation==TEXT("mapInvade")))ClosePopup();
            else if(PC->LastCommandOperation.StartsWith(TEXT("map")))
            {
                if(PC->bLastCommandSucceeded&&PC->LastCommandOperation==TEXT("mapRelocate"))
                {bMapCentered=false;MapCenterX=MapCenterY=-1;SelectedMapCellId.Empty();MapOwnedPage=0;}
                if(PC->bLastCommandSucceeded&&(Popup==TEXT("MapAttack")||Popup==TEXT("MapAbandon")||Popup==TEXT("MapRelocate")))ShowPopup(TEXT("MapRoom"));
                RequestMap();
            }
            // The snapshot is the authority for entering deployment mode. A clicked
            // invade button or a successful acknowledgement alone is insufficient.
            // Battle completion is presented from lastBattle in the same snapshot,
            // including an accepted retreat; do not dismiss that result on its ack.
        }
        if(SelectedBuilding!=PC->SelectedBuilding)
        {
            SelectedBuilding=PC->SelectedBuilding;
            if(!SelectedBuilding.IsEmpty()&&PC->PlacementMode.IsEmpty()&&!Attacking)
            {
                if(Mushroom(SelectedBuilding)){Popup=TEXT("Mushroom");bRefreshPopup=true;}
                else if(ViewingHome()&&BYMUI::Number(Building(SelectedBuilding),TEXT("type"))==22
                    &&BYMUI::Number(Building(SelectedBuilding),TEXT("level"))>0)ShowBunker(SelectedBuilding);
                else {Popup=TEXT("Building");bRefreshPopup=true;}
            }
        }
        if(Popup==TEXT("Mushroom")&&!Mushroom(SelectedBuilding))
        {SelectedBuilding.Empty();PC->SelectedBuilding.Empty();Popup.Empty();bRefreshPopup=true;}
        // Cancelling an unfinished structure removes it from the host snapshot.
        // Never leave controls bound to an identity that no longer exists.
        if((Popup==TEXT("Building")||Popup==TEXT("CancelBuilding"))&&!SelectedBuilding.IsEmpty()&&!Building(SelectedBuilding))
        {SelectedBuilding.Empty();PC->SelectedBuilding.Empty();ClosePopup();}
        if((Popup==TEXT("Bunker")||Popup==TEXT("DismissBunker"))&&(!ViewingHome()||!Bunker(SelectedBunker)))
        {SelectedBunker.Empty();BunkerSelection.Reset();ClosePopup();}
        const FString Structure=StructuralKey();
        if(Structure!=SeenStructure){SeenStructure=Structure;bRefreshPopup=true;RefreshDeployment();}
        // Numeric values and enabled states use live attributes. Do not recreate
        // buttons on the two-second snapshot timer while a mouse click is in flight.
        if(bRefreshPopup){bRefreshPopup=false;RefreshPopup();}
    }
private:
    TWeakObjectPtr<ABYMPlayerController> PC;
    TSharedPtr<FJsonObject> Snapshot,Ladder,WorldMap;
    TMap<FString,TSharedPtr<FBYMUIArt>> Art;
    TMap<FString,FVector2D> SourceImageSizes;
    bool bSourceImageSizesLoaded=false;
#if WITH_DEV_AUTOMATION_TESTS
    bool bCaptureLayoutProbes=false;
    TMap<FName,TSharedPtr<SWidget>> LayoutProbes;
    FString LayoutFixtureStatus;
#endif
    TArray<TWeakPtr<SWidget>> BoardInputBlockers;
    FButtonStyle StoneStyle,CardStyle,CloseStyle,WorkerStyle;
    TSharedPtr<SBox> PopupHost,DeploymentHost,MonsterDetailsHost;
    TSharedPtr<SEditableTextBox> UsernameBox,PasswordBox,AddressBox;
    TSharedPtr<SEditableTextBox> MapJumpX,MapJumpY;
    TSharedPtr<SBYMWorldMap> MapCanvas;
    FString SelectedMapCellId,SeenMapStructure;
    FString MapConfirmedCell,MapConfirmedOwner;
    int64 MapConfirmedRevision=0;
    TSharedPtr<FJsonObject> MapConfirmedCost;
    FString MapConfirmedTitle;
    int32 MapCenterX=-1,MapCenterY=-1,MapListTab=0,MapOwnedPage=0;
    float MapZoom=1.f;
    bool bMapCentered=false;
    double NextMapRefresh=0;
    FString Popup,SelectedBuilding,SelectedHatchery,SeenStructure,InspectedMonster=TEXT("C1"),SelectedProduct=TEXT("BEW");
    FString SpeedupBuilding,SpeedupCode=TEXT("SP1");
    FString MushroomCommandMessage;
    FString CancelBuildingId,CancelBuildingJob,CancelBuildingKind;
    FString SelectedBunker,DismissBunkerId,DismissBunkerType;
    TMap<FString,int32> BunkerSelection;
    int32 DismissBunkerExpectedStored=0;
    int32 BuildGroup=1,BuildPage=0,Blueprint=0,LadderPage=0,DecorationCategory=0;
    int32 ArmyTab=0,LockerPage=1,StoreCategory=0;
    int32 SeenSnapshot=-1,SeenLadder=-1,SeenCommand=-1,SeenWorldMap=-1;
    bool bGame=false,bCreateAccount=false,bWasAttacking=false,bRefreshPopup=false;
    double SampleTime=0,SampleClock=0;
    float ViewportWidth=1280.f,ViewportHeight=720.f;
    double ServerNow()const{return SampleTime+FMath::Max(0.0,FPlatformTime::Seconds()-SampleClock);}
    TSharedPtr<FJsonObject> Yard()const{return BYMUI::Object(Snapshot,TEXT("yard"));}
    TSharedPtr<FJsonObject> ViewedMapCell()const{return BYMUI::Object(Yard(),TEXT("mapCell"));}
    bool ViewingOwnedMapSite()const{return BYMUI::Bool(ViewedMapCell(),TEXT("owned"))&&BYMUI::Number(ViewedMapCell(),TEXT("kind"))!=101;}
    TSharedPtr<FJsonObject> Home()const
    {const auto H=BYMUI::Object(Snapshot,TEXT("home"));return H?H:Yard();}
    TSharedPtr<FJsonObject> Shop()const{return BYMUI::Object(Home(),TEXT("shop"));}
    TSharedPtr<FJsonObject> Product(const FString& Code)const
    {
        for(const auto& V:BYMUI::Array(Shop(),TEXT("products")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("code"))==Code)return V->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> SpeedupJob(const FString& Id)const
    {
        for(const auto& V:BYMUI::Array(Shop(),TEXT("speedups")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("building"))==Id)return V->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> SpeedupOption(const FString& Id,const FString& Code)const
    {
        for(const auto& V:BYMUI::Array(SpeedupJob(Id),TEXT("options")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("code"))==Code)return V->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> Decoration(int32 Type)const
    {
        for(const auto& V:BYMUI::Array(Shop(),TEXT("decorations")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::Number(V->AsObject(),TEXT("type"))==Type)return V->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> Mushroom(const FString& Id)const
    {
        if(ViewingHome())
            for(const auto& V:BYMUI::Array(Shop(),TEXT("mushrooms")))
                if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("id"))==Id)return V->AsObject();
        const auto Economy=BYMUI::Object(Yard(),TEXT("economy"));
        const auto Mushrooms=Yard()&&Yard()->HasField(TEXT("mushrooms"))?BYMUI::Array(Yard(),TEXT("mushrooms")):BYMUI::Array(Economy,TEXT("mushrooms"));
        for(const auto& V:Mushrooms)
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("id"))==Id)return V->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> WorkerAt(int32 Index)const
    {
        TArray<TSharedPtr<FJsonObject>> Entries;
        for(const auto& V:BYMUI::Array(BYMUI::Object(Home(),TEXT("workers")),TEXT("entries")))
            if(V.IsValid()&&V->Type==EJson::Object)Entries.Add(V->AsObject());
        Entries.Sort([](const TSharedPtr<FJsonObject>& A,const TSharedPtr<FJsonObject>& B)
        {
            const bool ActiveA=BYMUI::String(A,TEXT("state"))!=TEXT("idle"),ActiveB=BYMUI::String(B,TEXT("state"))!=TEXT("idle");
            return ActiveA!=ActiveB?ActiveA:BYMUI::String(A,TEXT("id"))<BYMUI::String(B,TEXT("id"));
        });
        return Entries.IsValidIndex(Index)?Entries[Index]:nullptr;
    }
    TSharedPtr<FJsonObject> Hatchery(const FString& Id)const
    {
        for(const auto& Value:BYMUI::Array(Home(),TEXT("hatcheries")))
            if(Value.IsValid()&&Value->Type==EJson::Object&&BYMUI::String(Value->AsObject(),TEXT("building"))==Id)return Value->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> SelectedHatcheryState()const{return Hatchery(SelectedHatchery);}
    TSharedPtr<FJsonObject> Bunker(const FString& Id)const
    {
        for(const auto& Value:BYMUI::Array(Home(),TEXT("bunkers")))
            if(Value.IsValid()&&Value->Type==EJson::Object&&BYMUI::String(Value->AsObject(),TEXT("building"))==Id)return Value->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> BunkerOption(const FString& BuildingId,const FString& Type)const
    {
        for(const auto& Value:BYMUI::Array(Bunker(BuildingId),TEXT("options")))
            if(Value.IsValid()&&Value->Type==EJson::Object&&BYMUI::String(Value->AsObject(),TEXT("type"))==Type)return Value->AsObject();
        return nullptr;
    }
    TSharedPtr<FJsonObject> HatcheryTrainOption(const FString& Type)const
    {return BYMUI::Object(BYMUI::Object(SelectedHatcheryState(),TEXT("trainOptions")),*Type);}
    void ResolveHatcherySelection()
    {
        if(SelectedHatcheryState())return;
        SelectedHatchery.Empty();
        for(const auto& Value:BYMUI::Array(Home(),TEXT("hatcheries")))
            if(Value.IsValid()&&Value->Type==EJson::Object)
            {SelectedHatchery=BYMUI::String(Value->AsObject(),TEXT("building"));break;}
    }
    TSharedPtr<FJsonObject> Experience()const{return BYMUI::Object(Yard(),TEXT("experience"));}
    TSharedPtr<FJsonObject> Battle()const{return BYMUI::Object(Snapshot,TEXT("battle"));}
    bool ViewingHome()const{return Yard().IsValid()&&BYMUI::String(Yard(),TEXT("id"))==BYMUI::String(Snapshot,TEXT("selfId"));}
    bool IsAttacking()const{return Battle().IsValid()&&BYMUI::String(Battle(),TEXT("attacker"))==BYMUI::String(Snapshot,TEXT("selfId"));}
    bool Idle()const{return PC.IsValid()&&!PC->IsCommandPending();}
    TSharedPtr<FJsonObject> BuildOption(int32 Type)const{return BYMUI::Object(BYMUI::Object(Snapshot,TEXT("buildOptions")),*FString::FromInt(Type));}
    TSharedPtr<FJsonObject> MonsterOption(const FString& Id)const
    {return BYMUI::Object(BYMUI::Object(Snapshot,TEXT("monsterOptions")),*Id);}
    TSharedPtr<FJsonObject> MonsterStats(const FString& Id,bool Next=false)const
    {return BYMUI::Object(MonsterOption(Id),Next?TEXT("nextStats"):TEXT("currentStats"));}
    FString MonsterArt(const TSharedPtr<FJsonObject>& M,bool Portrait)const
    {
        FString Path=Portrait?BYMUI::String(M,TEXT("portraitTexture")):FString();
        if(Path.IsEmpty())Path=BYMUI::String(M,TEXT("mediumTexture"));
        if(Path.IsEmpty())Path=BYMUI::String(M,TEXT("menuTexture"));
        if(Path.IsEmpty())Path=BYMUI::String(M,TEXT("texture"));
        return Path;
    }
    TSharedPtr<FJsonObject> MonsterDefinition(const FString& Id)const
    {
        for(const auto& V:BYMUI::Array(BYMUI::Object(Snapshot,TEXT("catalog")),TEXT("monsters")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("id"))==Id)return V->AsObject();
        return nullptr;
    }
    TArray<TSharedPtr<FJsonObject>> MonsterEntries()const
    {
        TArray<TSharedPtr<FJsonObject>> Result;
        for(const auto& V:BYMUI::Array(BYMUI::Object(Snapshot,TEXT("catalog")),TEXT("monsters")))
        {
            if(!V.IsValid()||V->Type!=EJson::Object)continue;
            const auto M=V->AsObject();
            // Original pickers hide event-blocked and dependent split entries.
            // Existing worlds may already own an event monster: retain its
            // host-authorized Hatchery production, without exposing new unlocks.
            const auto Option=MonsterOption(BYMUI::String(M,TEXT("id")));
            if(BYMUI::Bool(M,TEXT("sourceFake")))continue;
            if(BYMUI::Bool(M,TEXT("sourceBlocked"))&&!(ArmyTab==0&&BYMUI::Bool(Option,TEXT("unlocked"))))continue;
            if(Option)Result.Add(M);
        }
        Result.Sort([](const TSharedPtr<FJsonObject>& A,const TSharedPtr<FJsonObject>& B)
        {
            const double PageA=BYMUI::Number(A,TEXT("sourcePage"),1),PageB=BYMUI::Number(B,TEXT("sourcePage"),1);
            if(PageA!=PageB)return PageA<PageB;
            return BYMUI::Number(A,TEXT("sourceOrder"),FCString::Atoi(*BYMUI::String(A,TEXT("id")).Mid(1)))
                <BYMUI::Number(B,TEXT("sourceOrder"),FCString::Atoi(*BYMUI::String(B,TEXT("id")).Mid(1)));
        });
        return Result;
    }
    TSharedPtr<FJsonObject> Definition(int32 Type)const
    {
        for(const auto& V:BYMUI::Array(BYMUI::Object(Snapshot,TEXT("catalog")),TEXT("buildings")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::Number(V->AsObject(),TEXT("id"))==Type)return V->AsObject();
        return nullptr;
    }
    FString MonsterName(const FString& Id)const
    {
        for(const auto& V:BYMUI::Array(BYMUI::Object(Snapshot,TEXT("catalog")),TEXT("monsters")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("id"))==Id)return BYMUI::String(V->AsObject(),TEXT("name"),Id);
        return Id;
    }
    TSharedPtr<FJsonObject> Building(const FString& Id)const
    {
        for(const auto& V:BYMUI::Array(Yard(),TEXT("buildings")))
            if(V.IsValid()&&V->Type==EJson::Object&&BYMUI::String(V->AsObject(),TEXT("id"))==Id)return V->AsObject();
        return nullptr;
    }
    FString StructuralKey()const
    {
        FString Key=BYMUI::String(Yard(),TEXT("id"));
        for(const auto& V:BYMUI::Array(Yard(),TEXT("buildings")))
        {const auto B=V->AsObject();Key+=BYMUI::String(B,TEXT("id"))+TEXT(":")+BYMUI::Count(BYMUI::Number(B,TEXT("level")));}
        Key+=FString::FromInt(BYMUI::Array(BYMUI::Object(Snapshot,TEXT("catalog")),TEXT("buildings")).Num());
        // Rebuild only when the roster changes. Levels, prices, ready counts and
        // progression timers are live attributes, including the selected detail.
        for(const auto& M:MonsterEntries())Key+=TEXT("|")+BYMUI::String(M,TEXT("id"));
        // Refresh the displayed queue only on identity changes. Countdowns and
        // counts remain live. Other Hatcheries must not rebuild a clicked row,
        // and hatching during an invasion must not recreate the deployment UI.
        if(Popup==TEXT("Army")&&ArmyTab==0)
            for(const auto& Value:BYMUI::Array(Home(),TEXT("hatcheries")))
            {
                if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
                const auto H=Value->AsObject();const FString BuildingId=BYMUI::String(H,TEXT("building"));
                Key+=TEXT("|h:")+BuildingId+TEXT(":")+BYMUI::Count(BYMUI::Number(H,TEXT("level")));
                if(BuildingId!=SelectedHatchery)continue;
                Key+=TEXT(":a:")+BYMUI::String(BYMUI::Object(H,TEXT("active")),TEXT("id"));
                for(const auto& Item:BYMUI::Array(H,TEXT("queue")))
                    if(Item.IsValid()&&Item->Type==EJson::Object)Key+=TEXT(":q:")+BYMUI::String(Item->AsObject(),TEXT("id"));
            }
        // Rebuild the two Bunker lists only when a species enters/leaves a
        // list. Counts, selected quantities and fees are live attributes.
        if(Popup==TEXT("Bunker"))
        {
            Key+=TEXT("|bunker:")+SelectedBunker;
            for(const auto& Value:BYMUI::Array(Bunker(SelectedBunker),TEXT("options")))
                if(Value.IsValid()&&Value->Type==EJson::Object)
                {
                    const auto Option=Value->AsObject();const FString Type=BYMUI::String(Option,TEXT("type"));
                    if(BYMUI::Number(Option,TEXT("ready"))>0||BunkerSelectedCount(Type)>0)Key+=TEXT(":ready:")+Type;
                    if(BYMUI::Number(Option,TEXT("stored"))>0)Key+=TEXT(":stored:")+Type;
                }
        }
        if(Popup==TEXT("Store"))
        {
            for(const auto& V:BYMUI::Array(Shop(),TEXT("products")))if(V.IsValid()&&V->Type==EJson::Object)
            {
                const auto P=V->AsObject();Key+=TEXT("|shop:")+BYMUI::String(P,TEXT("code"))+TEXT(":" )
                    +BYMUI::Count(BYMUI::Number(P,TEXT("owned")))+TEXT(":" )+BYMUI::Count(BYMUI::Number(P,TEXT("price")));
            }
            for(const auto& V:BYMUI::Array(Shop(),TEXT("speedups")))if(V.IsValid()&&V->Type==EJson::Object)
            {
                const auto J=V->AsObject();Key+=TEXT("|speed:")+BYMUI::String(J,TEXT("building"))+TEXT(":")
                    +BYMUI::String(J,TEXT("job"))+TEXT(":")+BYMUI::Count(BYMUI::Number(J,TEXT("revision")));
            }
        }
        return Key;
    }
    FVector2D SourceImageSize(const FString& Relative)
    {
        if(!bSourceImageSizesLoaded)
        {
            bSourceImageSizesLoaded=true;
            const TSharedPtr<IPlugin> Plugin=IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
            FString ManifestText;
            if(Plugin&&FFileHelper::LoadFileToString(ManifestText,*FPaths::Combine(Plugin->GetContentDir(),TEXT("Data/asset-manifest.json"))))
            {
                const auto Manifest=BYMUI::Parse(ManifestText);
                for(const auto& Value:BYMUI::Array(Manifest,TEXT("assets")))
                {
                    if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
                    const auto Asset=Value->AsObject();const FString Source=BYMUI::String(Asset,TEXT("source"));
                    const double Width=BYMUI::Number(Asset,TEXT("width")),Height=BYMUI::Number(Asset,TEXT("height"));
                    if(!Source.IsEmpty()&&FMath::IsFinite(Width)&&FMath::IsFinite(Height)&&Width>0&&Height>0&&Width<=65536&&Height<=65536)
                        SourceImageSizes.Add(Source,FVector2D(Width,Height));
                }
            }
        }
        const FVector2D* Found=SourceImageSizes.Find(Relative);
        return Found?*Found:FVector2D(1,1);
    }
    const FSlateBrush* ImageBrush(const FString& Relative)
    {
        TSharedPtr<FBYMUIArt>& Entry=Art.FindOrAdd(Relative);
        if(!Entry)Entry=MakeShared<FBYMUIArt>(nullptr,SourceImageSize(Relative));
        if(!Entry->Texture.IsValid())
            if(UTexture2D* Texture=LoadObject<UTexture2D>(nullptr,*ABYMBoard::AssetObjectPath(Relative)))
            {Entry->Texture.Reset(Texture);Entry->Brush.SetResourceObject(Texture);}
        // Never replace this allocation: Slate widgets retain its brush address.
        return &Entry->Brush;
    }
    void RecordLayoutProbe(const FName& Name,const TSharedRef<SWidget>& Widget)
    {
#if WITH_DEV_AUTOMATION_TESTS
        if(bCaptureLayoutProbes)LayoutProbes.Add(Name,Widget);
#else
        (void)Name;(void)Widget;
#endif
    }
    FSlateBrush BoxBrush(const FString& Relative,FMargin Margin)
    {FSlateBrush Result=*ImageBrush(Relative);Result.DrawAs=ESlateBrushDrawType::Box;Result.Margin=Margin;return Result;}
    void PrepareStyles()
    {
        // StoneButton.as uses these original images and white Grobold text with a
        // black outline. Explicit brushes remove editor-style dark/disabled tint.
        FSlateBrush Normal=BoxBrush(TEXT("ui/stone3.png"),FMargin(10.f/79.f,10.f/36.f,10.f/79.f,10.f/36.f));
        FSlateBrush Hover=Normal;Hover.TintColor=FSlateColor(FLinearColor(1.12f,1.12f,1.06f));
        FSlateBrush Pressed=Normal;Pressed.TintColor=FSlateColor(FLinearColor(.78f,.78f,.74f));
        StoneStyle.SetNormal(Normal).SetHovered(Hover).SetPressed(Pressed).SetDisabled(Normal)
            .SetNormalPadding(FMargin(10,7)).SetPressedPadding(FMargin(10,8,10,6))
            .SetNormalForeground(FSlateColor(FLinearColor::White)).SetHoveredForeground(FSlateColor(FLinearColor::White))
            .SetPressedForeground(FSlateColor(FLinearColor::White)).SetDisabledForeground(FSlateColor(FLinearColor(.62f,.62f,.58f)));
        FSlateBrush Card=BoxBrush(TEXT("source_client/shapes/1.png"),FMargin(.04f));
        FSlateBrush CardHover=Card;CardHover.TintColor=FSlateColor(FLinearColor(1.05f,1.04f,.94f));
        CardStyle.SetNormal(Card).SetHovered(CardHover).SetPressed(Card).SetDisabled(Card)
            .SetNormalPadding(FMargin(5)).SetPressedPadding(FMargin(5));
        const FSlateBrush Close=*ImageBrush(TEXT("alliances/redx.png"));
        CloseStyle.SetNormal(Close).SetHovered(Close).SetPressed(Close).SetDisabled(Close)
            .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
        WorkerStyle.SetNormal(Empty).SetHovered(Empty).SetPressed(Empty).SetDisabled(Empty)
            .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    }
    TSharedRef<SWidget> Text(const FString& Value,int32 Size=12,FLinearColor Color=BYMUI::Ink,bool Heading=false,float WrapWidth=0)
    {return SNew(STextBlock).Text(FText::FromString(Value)).Font(BYMUI::Font(Size,Heading)).ColorAndOpacity(Color).AutoWrapText(false).WrapTextAt(FMath::Max(0.f,WrapWidth)).WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
    TSharedRef<SWidget> LiveText(TFunction<FString()> Value,int32 Size=12,FLinearColor Color=BYMUI::Ink,bool Heading=false,float WrapWidth=0)
    {return SNew(STextBlock).Text_Lambda([Value](){return FText::FromString(Value());}).Font(BYMUI::Font(Size,Heading)).ColorAndOpacity(Color).AutoWrapText(false).WrapTextAt(FMath::Max(0.f,WrapWidth)).WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
    TSharedRef<SWidget> Image(const FString& Relative,float Width,float Height)
    {
        const FVector2D SourceSize=SourceImageSize(Relative);
        const double Scale=FMath::Min(Width/SourceSize.X,Height/SourceSize.Y);
        TSharedRef<SImage> Picture=SNew(SImage).Image(ImageBrush(Relative));
        if(Relative==TEXT("ui/title.png"))RecordLayoutProbe(TEXT("Logo"),Picture);
        return SNew(SBox).WidthOverride(Width).HeightOverride(Height).HAlign(HAlign_Center).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
            [SNew(SBox).WidthOverride(static_cast<float>(SourceSize.X*Scale)).HeightOverride(static_cast<float>(SourceSize.Y*Scale))[Picture]];
    }
    TSharedRef<SWidget> Button(const FString& Label,TFunction<void()> Action,TFunction<bool()> Enabled=TFunction<bool()>())
    {
        return SNew(SButton).ButtonStyle(&StoneStyle).ContentPadding(FMargin(10,7)).IsFocusable(false)
            .IsEnabled_Lambda([this,Enabled](){return Idle()&&(!Enabled||Enabled());})
            .OnClicked_Lambda([Action](){Action();if(FSlateApplication::IsInitialized())FSlateApplication::Get().SetAllUserFocusToGameViewport();return FReply::Handled();})
            [SNew(STextBlock).Text(FText::FromString(Label)).Font(BYMUI::ButtonFont()).ColorAndOpacity(FLinearColor::White).Justification(ETextJustify::Center)];
    }
    TSharedRef<SWidget> Wood(const TSharedRef<SWidget>& Content,FMargin Padding=FMargin(10))
    {
        // Keep a stable member brush; Slate retains pointers instead of brush copies.
        ImageBrush(TEXT("ui/wood1.png"));
        auto* Found=Art.Find(TEXT("ui/wood1.png"));
        if(Found){(*Found)->Brush.DrawAs=ESlateBrushDrawType::Box;(*Found)->Brush.Margin=FMargin(15.f/276.f,15.f/54.f,15.f/276.f,15.f/54.f);}
        const TSharedRef<SBorder> Panel=SNew(SBorder).BorderImage(ImageBrush(TEXT("ui/wood1.png"))).Padding(Padding)
            .OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
            .OnMouseButtonUp_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
            .OnMouseDoubleClick_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})[Content];
        BoardInputBlockers.Add(TWeakPtr<SWidget>(StaticCastSharedRef<SWidget>(Panel)));
        return Panel;
    }
    TSharedRef<SWidget> Frame(const TSharedRef<SWidget>& Content,float Width,float Height)
    {
        const FString Prefix=TEXT("source_client/images/");
        TSharedRef<SOverlay> Layers=SNew(SOverlay);
        Layers->AddSlot().Padding(12,11,12,17)[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/1.png"))).Visibility(EVisibility::HitTestInvisible)];
        const TSharedRef<SBox> BodyBox=SNew(SBox).WidthOverride(Width-38).HeightOverride(Height-32)
            .HAlign(HAlign_Fill).VAlign(VAlign_Fill).Clipping(EWidgetClipping::ClipToBounds)[Content];
        RecordLayoutProbe(TEXT("Body"),BodyBox);
        Layers->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill).Padding(31,27,31,33)[BodyBox];
        auto Edge=[this,&Layers,&Prefix](const TCHAR* Name,EHorizontalAlignment H,EVerticalAlignment V,float W,float D,FMargin Padding)
        {Layers->AddSlot().HAlign(H).VAlign(V).Padding(Padding)[SNew(SBox).WidthOverride(W).HeightOverride(D).Visibility(EVisibility::HitTestInvisible)[SNew(SImage).Image(ImageBrush(Prefix+Name))]];};
        Edge(TEXT("2681_frame1_filler_top_frame1_filler_top.png"),HAlign_Left,VAlign_Top,Width-153,21,FMargin(54,6,0,0));
        Edge(TEXT("2678_frame1_filler_bottom_frame1_filler_bottom.png"),HAlign_Left,VAlign_Bottom,Width-100,17,FMargin(62,0,0,10));
        Edge(TEXT("2675_frame1_filler_left_frame1_filler_left.png"),HAlign_Left,VAlign_Top,16,Height-219,FMargin(8,183,0,0));
        Edge(TEXT("2677_frame1_filler_right_frame1_filler_right.png"),HAlign_Right,VAlign_Top,20,Height-158,FMargin(0,50,6,0));
        Edge(TEXT("2671_frame1_bottom_left_frame1_bottom_left.png"),HAlign_Left,VAlign_Bottom,93,64,FMargin(4,0,0,2));
        Edge(TEXT("2676_frame1_bottom_right_frame1_bottom_right.png"),HAlign_Right,VAlign_Bottom,112,158,FMargin(0,0,0,5));
        Edge(TEXT("2683_frame1_top_left_frame1_top_left.png"),HAlign_Left,VAlign_Top,54,182,FMargin(0));
        Edge(TEXT("2673_frame1_top_right_frame1_top_right.png"),HAlign_Right,VAlign_Top,123,58,FMargin(0,3,2,0));
        Edge(TEXT("2680_frame1_top_middle_frame1_top_middle.png"),HAlign_Center,VAlign_Top,280,31,FMargin(0));
        Edge(TEXT("2672_frame1_bottom_middle_frame1_bottom_middle.png"),HAlign_Center,VAlign_Bottom,432,27,FMargin(0,0,0,4));
        const TSharedRef<SBox> Panel=SNew(SBox).WidthOverride(Width+24).HeightOverride(Height+28)
            [SNew(SBorder).BorderImage(BYMUI::WhiteBrush()).BorderBackgroundColor(FLinearColor::Transparent).Padding(0)
                .OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
                .OnMouseButtonUp_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
                .OnMouseDoubleClick_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})[Layers]];
        BoardInputBlockers.Add(TWeakPtr<SWidget>(StaticCastSharedRef<SWidget>(Panel)));
        RecordLayoutProbe(TEXT("Frame"),Panel);
        return Panel;
    }
    TSharedRef<SVerticalBox> DialogBody(const FString& Title)
    {
        TSharedRef<SVerticalBox> Body=SNew(SVerticalBox);
        const TSharedRef<SWidget> Heading=Text(Title,23,BYMUI::Ink,true);
        RecordLayoutProbe(TEXT("Title"),Heading);
        Body->AddSlot().AutoHeight().Padding(5,8,5,12)
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Heading]
            +SHorizontalBox::Slot().AutoWidth()
                [SNew(SBox).WidthOverride(27).HeightOverride(27)
                    [SNew(SButton).ButtonStyle(&CloseStyle).ContentPadding(0).IsFocusable(false).ToolTipText(FText::FromString(TEXT("Close")))
                        .OnClicked_Lambda([this](){ClosePopup();return FReply::Handled();})[SNew(SBox)]]]];
        return Body;
    }
    void Command(const FString& Operation,const TSharedRef<FJsonObject>& Request)
    {if(PC.IsValid())PC->SendCommand(Operation,BYMUI::Json(Request));}
    void BuildingCommand(const FString& Operation,const FString& Id)
    {TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetStringField(TEXT("building"),Id);Command(Operation,R);}
    void MonsterCommand(const FString& Operation,const FString& Id)
    {TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetStringField(TEXT("type"),Id);Command(Operation,R);}
    void ShowArmy(int32 Tab,const FString& Id=TEXT(""),const FString& HatcheryId=TEXT(""))
    {
        ArmyTab=FMath::Clamp(Tab,0,2);
        if(!Id.IsEmpty())InspectedMonster=Id;
        if(!HatcheryId.IsEmpty())SelectedHatchery=HatcheryId;
        ResolveHatcherySelection();
        LockerPage=FMath::Max(1,static_cast<int32>(BYMUI::Number(MonsterDefinition(InspectedMonster),TEXT("sourcePage"),1)));
        ShowPopup(TEXT("Army"));
    }
    void ShowPopup(const FString& Name)
    {
        Popup=Name;bRefreshPopup=true;
        if(PC.IsValid())
        {
            PC->PlacementMode.Empty();
            PC->bWorldMapOpen=Name==TEXT("MapRoom")||Name==TEXT("MapAttack")||Name==TEXT("MapAbandon")||Name==TEXT("MapRelocate");
            if(Name==TEXT("Rivals"))PC->RequestLadder(LadderPage);
            if(Name==TEXT("MapRoom")){RequestMap();NextMapRefresh=FPlatformTime::Seconds()+2.;}
        }
    }
    void ShowBunker(const FString& Id)
    {
        if(SelectedBunker!=Id)BunkerSelection.Reset();
        SelectedBunker=Id;ShowPopup(TEXT("Bunker"));
    }
    void ClosePopup(){Popup.Empty();bRefreshPopup=true;if(PC.IsValid())PC->bWorldMapOpen=false;}
    void BuildRoot()
    {
        bGame=PC.IsValid()&&PC->bAuthenticated;
        if(bGame)BuildGame();else BuildLogin();
    }
    void Connect(bool Host)
    {
        if(!PC.IsValid()||!UsernameBox||!PasswordBox)return;
        FString Password=PasswordBox->GetText().ToString();const FString Username=UsernameBox->GetText().ToString();
        if(PC->IsMenuWorld())
        {
            UBYMGameInstance* GI=Cast<UBYMGameInstance>(PC->GetGameInstance());
            if(GI)
            {
                FString Error;const bool Ok=Host?GI->HostAndPlay(Username,Password,bCreateAccount,Error):GI->JoinHost(AddressBox->GetText().ToString(),Username,Password,bCreateAccount,Error);
                PC->StatusMessage=Ok?TEXT("Opening your yard..."):Error;PC->bConnecting=Ok;
            }
        }
        else PC->SubmitCredentials(Username,Password,bCreateAccount);
        PasswordBox->SetText(FText::GetEmpty());
        if(!Password.IsEmpty())FMemory::Memzero(Password.GetCharArray().GetData(),Password.GetCharArray().Num()*sizeof(TCHAR));Password.Empty();
    }
    FString StatusText()const
    {
        if(PC.IsValid())return PC->StatusMessage;
#if WITH_DEV_AUTOMATION_TESTS
        return LayoutFixtureStatus;
#else
        return FString();
#endif
    }
    void BuildLogin(bool bForceMenu=false)
    {
        using namespace BYMUI;
        const bool Menu=bForceMenu||(PC.IsValid()&&PC->IsMenuWorld());
        // A fixed design canvas is scaled once by the outer ScaleBox. Labels
        // have intrinsic widths; only prose has an explicit wrapping width.
        TSharedRef<SVerticalBox> Form=SNew(SVerticalBox);
        Form->AddSlot().AutoHeight().HAlign(HAlign_Center)[Image(TEXT("ui/title.png"),360,111)];
        const TSharedRef<STextBlock> Title=SNew(STextBlock).Text(FText::FromString(TEXT("BACKYARD MONSTERS NXT")))
            .Font(Font(16,true)).ColorAndOpacity(Ink).AutoWrapText(false).Justification(ETextJustify::Center);
        RecordLayoutProbe(TEXT("Title"),Title);
        Form->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(0,0,0,12)[Title];
        if(Menu)
        {
            Form->AddSlot().AutoHeight()[Text(TEXT("Host address"))];
            Form->AddSlot().AutoHeight().Padding(0,4,0,10)[SAssignNew(AddressBox,SEditableTextBox).Text(FText::FromString(GetDefault<UBYMSettings>()->HostAddress)).Font(Font(13)).Padding(8)];
        }
        Form->AddSlot().AutoHeight()[Text(TEXT("Username"))];
        Form->AddSlot().AutoHeight().Padding(0,4,0,10)[SAssignNew(UsernameBox,SEditableTextBox).Font(Font(13)).Padding(8)];
        Form->AddSlot().AutoHeight()[Text(TEXT("Password"))];
        Form->AddSlot().AutoHeight().Padding(0,4,0,10)[SAssignNew(PasswordBox,SEditableTextBox).IsPassword(true).Font(Font(13)).Padding(8)];
        const TSharedRef<SWidget> Caption=Text(TEXT("Create account on this host"));
        RecordLayoutProbe(TEXT("Caption"),Caption);
        Form->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(0,0,0,6)
            [SNew(SCheckBox).HAlign(HAlign_Fill).IsChecked_Lambda([this](){return bCreateAccount?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bCreateAccount=State==ECheckBoxState::Checked;})[Caption]];
        const TSharedRef<SWidget> Status=LiveText([this](){return StatusText();},11,Red,false,460);
        const TSharedRef<SScrollBox> StatusScroll=SNew(SScrollBox)+SScrollBox::Slot().Padding(0,4,8,4)[Status];
        RecordLayoutProbe(TEXT("Status"),StatusScroll);RecordLayoutProbe(TEXT("Paragraph"),Status);
        // Error/status text can be arbitrarily long. It scrolls inside the
        // remaining height and cannot push the sign-in actions out of the frame.
        Form->AddSlot().FillHeight(1).Padding(0,0,0,8)[StatusScroll];
        TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox);
        if(Menu)
        {
            Actions->AddSlot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Host & Play"),[this](){Connect(true);},[this](){return PC.IsValid()&&!PC->bConnecting;})];
            Actions->AddSlot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Join"),[this](){Connect(false);},[this](){return PC.IsValid()&&!PC->bConnecting;})];
        }
        else Actions->AddSlot().FillWidth(1)[Button(TEXT("Sign in"),[this](){Connect(false);},[this](){return PC.IsValid()&&!PC->bConnecting;})];
        RecordLayoutProbe(TEXT("Actions"),Actions);
        Form->AddSlot().AutoHeight()[Actions];
        Form->AddSlot().AutoHeight().Padding(0,8,0,0)[Text(TEXT("Return to the same host to continue your yard."),10,Muted,false,492)];
        if(!Menu)Form->AddSlot().AutoHeight().Padding(0,8,0,0)[Button(TEXT("Disconnect"),[this](){if(PC.IsValid())PC->LeaveServer();})];
        ChildSlot[SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            +SOverlay::Slot().Padding(28)[SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Frame(Form,530,Menu?610:560)]]];
    }
    FString ExperienceTooltip()const
    {
        using namespace BYMUI;const auto XP=Experience();
        const int32 Level=FMath::Max(1,static_cast<int32>(Number(XP,TEXT("level"),1)));
        FString Result=String(Yard(),TEXT("name"),TEXT("Backyard Monsters"))+TEXT(" - Level ")+FString::FromInt(Level)
            +TEXT("\nExperience: ")+Count(Number(XP,TEXT("points")));
        if(Bool(XP,TEXT("atMaxLevel")))Result+=TEXT("\nMaximum player level reached");
        else Result+=TEXT("\n")+Count(Number(XP,TEXT("needed")))+TEXT(" to level ")+FString::FromInt(Level+1);
        return Result;
    }
    TSharedRef<SWidget> ExperienceHUD()
    {
        using namespace BYMUI;
        // assets.swf PointsBar_882: original background, cream fill and star.
        // The 10,12 canvas offset contains the star's rotated source bounds.
        // Shape584's local registration is (-18.95,-20.2), not its PNG corner.
        const FSlateRenderTransform StarTransform(FMatrix2x2(.91650390625f,-.39532470703125f,.39532470703125f,.91650390625f),FVector2f(0.f,0.f));
        return SNew(SBox).WidthOverride(237).HeightOverride(58)
            .ToolTipText_Lambda([this](){return FText::FromString(ExperienceTooltip());})
            [SNew(SOverlay)
                +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(10,12,0,0)
                    [SNew(SBox).WidthOverride(217).HeightOverride(33)[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/582.png"))).Visibility(EVisibility::HitTestInvisible)]]
                +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24.3f,14.05f,0,0)
                    [SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(static_cast<float>(200.*FMath::Clamp(BYMUI::Number(Experience(),TEXT("progress")),0.,1.)));})
                        .HeightOverride(27.948074f).Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/553.png")))]]
                +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(46,21.55f,0,0)
                    [SNew(SBox).WidthOverride(168).HeightOverride(20).Clipping(EWidgetClipping::ClipToBounds)
                        [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(BYMUI::String(Yard(),TEXT("name"),TEXT("Backyard Monsters")));})
                            .Font(ButtonFont(12)).ColorAndOpacity(White)]]
                +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(7.05f,8.8f,0,0)
                    [SNew(SBox).WidthOverride(42).HeightOverride(42).Visibility(EVisibility::HitTestInvisible)
                        .RenderTransform(TOptional<FSlateRenderTransform>(StarTransform)).RenderTransformPivot(FVector2D(18.95/42.,20.2/42.))
                        [SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/584.png")))]]
                +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(9,22,0,0)
                    [SNew(SBox).WidthOverride(34).HeightOverride(20).HAlign(HAlign_Center).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
                        [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(BYMUI::Count(FMath::Max(1.,BYMUI::Number(Experience(),TEXT("level"),1))));})
                            .Font(ButtonFont(12)).ColorAndOpacity(White)]]];
    }
    TSharedRef<SWidget> ResourceHUD()
    {
        using namespace BYMUI;
        TSharedRef<SVerticalBox> Bars=SNew(SVerticalBox);
        Bars->AddSlot().AutoHeight().Padding(0,0,0,3)[ExperienceHUD()];
        const TCHAR* Keys[]={TEXT("twigs"),TEXT("pebbles"),TEXT("putty"),TEXT("goo")};
        const TCHAR* Names[]={TEXT("Twigs"),TEXT("Pebbles"),TEXT("Putty"),TEXT("Goo")};
        for(int32 I=0;I<4;++I)
        {
            const FString Key=Keys[I];const FString Label=Names[I];
            Bars->AddSlot().AutoHeight().Padding(0,-5,0,0)
                [SNew(SBox).WidthOverride(146).HeightOverride(42).ToolTipText_Lambda([this,Key,Label](){return FText::FromString(Label+TEXT(": ")+Count(Number(Object(Home(),TEXT("resources")),*Key))+TEXT(" / ")+Count(Number(Home(),TEXT("storageCapacity"))));})
                    [SNew(SOverlay)+SOverlay::Slot()[SNew(SImage).Image(ImageBrush(TEXT("derived/ui/original/resource_")+Key+TEXT(".png"))).Visibility(EVisibility::HitTestInvisible)]
                        +SOverlay::Slot().Padding(39,14,5,0)
                            [SNew(STextBlock).Text_Lambda([this,Key](){return FText::FromString(Count(Number(Object(Home(),TEXT("resources")),*Key)));})
                                .Font(ButtonFont(12)).ColorAndOpacity(White)]]];
        }
        Bars->AddSlot().AutoHeight().Padding(8,3,0,0)[LiveText([this](){return TEXT("RP ")+Count(Number(Home(),TEXT("rp")))+TEXT("   Town Hall ")+Count(Number(Home(),TEXT("townHallLevel"),1));},10,White,true)];
        const TSharedRef<SBorder> Panel=SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor::Transparent).Padding(0)
            .OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
            .OnMouseButtonUp_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
            .OnMouseDoubleClick_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})[Bars];
        BoardInputBlockers.Add(TWeakPtr<SWidget>(StaticCastSharedRef<SWidget>(Panel)));
        return Panel;
    }
    TSharedRef<SWidget> BattleHeader()
    {
        using namespace BYMUI;
        TSharedRef<SVerticalBox> Lines=SNew(SVerticalBox);
        const TSharedRef<SWidget> Title=LiveText([this](){return IsAttacking()?TEXT("ATTACK IN PROGRESS"):Battle()?TEXT("UNDER ATTACK"):ViewingHome()?TEXT("YOUR YARD"):ViewingOwnedMapSite()?TEXT("YOUR TERRITORY"):TEXT("SCOUTING");},13,White,true);
        const TSharedRef<SWidget> Caption=LiveText([this](){return IsAttacking()?(String(Battle(),TEXT("phase"))==TEXT("deploying")?TEXT("Choose monsters to deploy"):TEXT("Your monsters are attacking")):FString();},10,White,false,207);
        RecordLayoutProbe(TEXT("Title"),Title);RecordLayoutProbe(TEXT("Caption"),Caption);
        Lines->AddSlot().AutoHeight()[Title];
        Lines->AddSlot().AutoHeight().Padding(0,4,0,0)[Caption];
        Lines->AddSlot().AutoHeight().Padding(0,4,0,0)[LiveText([this]()
        {
            if(Battle())return TEXT("Time left: ")+Duration(Number(Battle(),TEXT("endsAt"))-ServerNow());
            const double Shield=Number(Yard(),TEXT("shieldUntil"))-ServerNow();return Shield>0?TEXT("Protected: ")+Duration(Shield):TEXT("No damage protection");
        },11,Gold)];
        Lines->AddSlot().AutoHeight().Padding(0,4,0,0)
            [SNew(SBox).Visibility_Lambda([this](){return Battle()&&!String(Battle(),TEXT("mapCellId")).IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
                [LiveText([this](){return TEXT("Damage: ")+Count(Number(Battle(),TEXT("destructionPercent")))+TEXT("% / 90%");},11,Gold,true)]];
        Lines->AddSlot().AutoHeight().Padding(0,6,0,0)
            [SNew(SBox).Visibility_Lambda([this](){return !ViewingHome()&&!ViewingOwnedMapSite()&&!IsAttacking()?EVisibility::Visible:EVisibility::Collapsed;})
                [Button(TEXT("Attack!"),[this]()
                {
                    const auto Cell=ViewedMapCell();
                    if((Cell&&Number(Cell,TEXT("kind"))!=101)||Bool(Cell,TEXT("requiresMapAttack"))||Bool(Object(Snapshot,TEXT("rules")),TEXT("mapRoomParticipant")))
                    {
                        if(Cell&&Bool(Cell,TEXT("canAttack")))ConfirmMapAction(TEXT("MapAttack"),Cell);
                        else {if(PC.IsValid())PC->StatusMessage=String(Cell,TEXT("reason"),TEXT("Open the Map Room to review this target."));FocusMapCell(Cell);ShowPopup(TEXT("MapRoom"));}
                        return;
                    }
                    TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetStringField(TEXT("target"),String(Yard(),TEXT("id")));Command(TEXT("invade"),R);
                },[this](){return Yard().IsValid()&&!Battle().IsValid()&&Number(Yard(),TEXT("shieldUntil"))<=ServerNow();})]];
        return Wood(Lines,FMargin(14,10));
    }
    FString WorkerTooltip(int32 Index)const
    {
        using namespace BYMUI;const auto Worker=WorkerAt(Index);
        if(!Worker)return TEXT("Hire an extra worker");
        const FString State=String(Worker,TEXT("state"));
        if(State==TEXT("idle"))return TEXT("Idle");
        FString Label=State==TEXT("walking")?TEXT("Walking"):State==TEXT("repairing")?TEXT("Waiting for repairs")
            :State==TEXT("damaged")?TEXT("Waiting - repair the building")
            :String(Worker,TEXT("kind"))==TEXT("upgrade")?TEXT("Upgrading")
            :String(Worker,TEXT("kind"))==TEXT("mushroom")?TEXT("Clearing mushroom"):TEXT("Building");
        const auto Task=Building(String(Worker,TEXT("building")));
        if(Task)Label+=TEXT("\n")+String(Task,TEXT("name"));
        const double End=Number(Worker,State==TEXT("walking")?TEXT("arrivalAt"):TEXT("readyAt"));
        const double Remaining=End>0?FMath::Max(0.,End-ServerNow()):Number(Worker,TEXT("remaining"));
        if(Remaining>0)Label+=TEXT("\n")+Duration(Remaining);
        return Label;
    }
    TSharedRef<SWidget> WorkerHUD()
    {
        // UI_WORKERS uses the original 45px-spaced idle/busy/HIRE icons. The
        // host assigns jobs; clicking an owned icon only focuses its worker.
        ImageBrush(TEXT("source_client/shapes/2068.png"));ImageBrush(TEXT("source_client/shapes/2069.png"));
        ImageBrush(TEXT("source_client/shapes/2073.png"));ImageBrush(TEXT("source_client/shapes/2074.png"));
        ImageBrush(TEXT("source_client/shapes/130.png"));
        TSharedRef<SVerticalBox> Icons=SNew(SVerticalBox);
        for(int32 Index=0;Index<5;++Index)
        {
            const TSharedRef<SWidget> Icon=SNew(SBox).WidthOverride(46).HeightOverride(43)
                [SNew(SOverlay)
                    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(0,1,0,0)
                        [SNew(SBox).WidthOverride(46).HeightOverride(41)[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/2068.png")))]]
                    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(1,0,0,0)
                        [SNew(SBox).WidthOverride(43).HeightOverride(43)[SNew(SImage).Image_Lambda([this,Index]()
                        {
                            const auto Worker=WorkerAt(Index);
                            return ImageBrush(!Worker?TEXT("source_client/shapes/2074.png")
                                :BYMUI::String(Worker,TEXT("state"))==TEXT("idle")?TEXT("source_client/shapes/2069.png"):TEXT("source_client/shapes/2073.png"));
                        })]]
                    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(11,8,0,0)
                        [SNew(SBox).WidthOverride(28).HeightOverride(28).Visibility_Lambda([this,Index](){return WorkerAt(Index)?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                            [SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/130.png")))]]
                    +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [SNew(SBox).Visibility_Lambda([this,Index](){return WorkerAt(Index)?EVisibility::Collapsed:EVisibility::HitTestInvisible;})[Text(TEXT("HIRE"),9,BYMUI::Ink,true)]]];
            const TSharedRef<SButton> WorkerButton=SNew(SButton).ButtonStyle(&WorkerStyle).ContentPadding(0).IsFocusable(false)
                    .ToolTipText_Lambda([this,Index](){return FText::FromString(WorkerTooltip(Index));})
                    .IsEnabled_Lambda([this](){return Idle()&&ViewingHome();})
                    .OnClicked_Lambda([this,Index]()
                    {
                        const auto Worker=WorkerAt(Index);
                        if(!Worker){StoreCategory=0;SelectedProduct=TEXT("BEW");ShowPopup(TEXT("Store"));}
                        else if(PC.IsValid())if(ABYMCameraPawn* Camera=Cast<ABYMCameraPawn>(PC->GetPawn()))Camera->FocusOnWorker(BYMUI::String(Worker,TEXT("id")));
                        return FReply::Handled();
                    })[Icon];
            Icons->AddSlot().AutoHeight().Padding(0,0,0,2)[WorkerButton];
            RecordLayoutProbe(FName(*FString::Printf(TEXT("Worker%d"),Index)),WorkerButton);
        }
        const TSharedRef<SBox> Panel=SNew(SBox).Visibility_Lambda([this]()
            {return ViewingHome()&&!Battle()&&BYMUI::Object(Home(),TEXT("workers"))?EVisibility::Visible:EVisibility::Collapsed;})[Icons];
        BoardInputBlockers.Add(TWeakPtr<SWidget>(StaticCastSharedRef<SWidget>(Panel)));RecordLayoutProbe(TEXT("Strip"),Panel);return Panel;
    }
    TSharedRef<SWidget> StoreIcon(const FString& Code)
    {
        TSharedRef<SOverlay> Layers=SNew(SOverlay);
        Layers->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(1,1,0,0)
            [SNew(SBox).WidthOverride(71).HeightOverride(71)[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/364.png")))]];
        // Source store_icon frame8 (BIP) has no foreground illustration.
        FString Picture;
        if(Code==TEXT("BEW"))Picture=TEXT("source_client/shapes/377.png");
        else if(Code==TEXT("ENL"))Picture=TEXT("source_client/shapes/379.png");
        else if(Code==TEXT("SP1"))Picture=TEXT("source_client/shapes/369.png");
        else if(Code==TEXT("SP2"))Picture=TEXT("source_client/shapes/371.png");
        else if(Code==TEXT("SP3"))Picture=TEXT("source_client/shapes/373.png");
        else if(Code==TEXT("SP4"))Picture=TEXT("source_client/shapes/375.png");
        if(!Picture.IsEmpty())
            Layers->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(1,Code==TEXT("ENL")?2:1,0,0)
                [SNew(SBox).WidthOverride(71).HeightOverride(71)[SNew(SImage).Image(ImageBrush(Picture))]];
        Layers->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
            [SNew(SBox).WidthOverride(72).HeightOverride(72)[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/367.png")))]];
        return SNew(SBox).WidthOverride(72).HeightOverride(73).Visibility(EVisibility::HitTestInvisible)[Layers];
    }
    TSharedRef<SWidget> ShinyHUD()
    {
        // UI_TOP.R5 uses the source Shiny gem and resource-bar background.
        // Its native button opens the supported store, never a payment page.
        const TSharedRef<SButton> Panel=SNew(SButton).ButtonStyle(&WorkerStyle).ContentPadding(0).IsFocusable(false)
            .ToolTipText(FText::FromString(TEXT("Shiny - open the General Store")))
            .IsEnabled_Lambda([this](){return Idle()&&ViewingHome()&&!Battle();})
            .OnClicked_Lambda([this](){ShowPopup(TEXT("Store"));return FReply::Handled();})
            [SNew(SBox).WidthOverride(144).HeightOverride(41)
                [SNew(SOverlay)
                    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8,2,0,0)
                        [SNew(SBox).WidthOverride(125).HeightOverride(34)[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/575.png")))]]
                    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
                        [SNew(SBox).WidthOverride(40).HeightOverride(41)[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/517.png")))]]
                    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(35,12,0,0)
                        [SNew(SBox).WidthOverride(95).HeightOverride(20).Clipping(EWidgetClipping::ClipToBounds)
                            [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(BYMUI::Count(BYMUI::Number(Shop(),TEXT("shiny"))));})
                                .Font(BYMUI::ButtonFont(11)).ColorAndOpacity(BYMUI::White)]]]];
        BoardInputBlockers.Add(TWeakPtr<SWidget>(StaticCastSharedRef<SWidget>(Panel)));return Panel;
    }
    TSharedRef<SWidget> StoreDialog()
    {
        using namespace BYMUI;
        if(StoreCategory==2&&Shop()&&Shop()->HasField(TEXT("speedups")))return SpeedupStoreDialog();
        const auto Products=Array(Shop(),TEXT("products"));
        if(!Product(TEXT("BIP")))StoreCategory=0;
        if(!Product(SelectedProduct)||(SelectedProduct==TEXT("BIP"))!=(StoreCategory==1))
            SelectedProduct=StoreCategory==1?TEXT("BIP"):TEXT("BEW");
        TSharedRef<SVerticalBox> Body=DialogBody(TEXT("General Store"));
        Body->AddSlot().AutoHeight().Padding(0,0,0,10)[StoreTabs()];
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("Shiny balance"),13,Ink,true)]
                +SHorizontalBox::Slot().AutoWidth()[Image(TEXT("source_client/shapes/517.png"),24,25)]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7,0,0,0)
                    [LiveText([this](){return Count(Number(Shop(),TEXT("shiny")))+TEXT(" Shiny");},13,Ink,true)]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)[LiveText([this]()
        {
            const auto W=Object(Home(),TEXT("workers"));
            FString Label=TEXT("Workers: ")+Count(Number(W,TEXT("available")))+TEXT(" / ")+Count(Number(W,TEXT("total")))+TEXT(" available");
            if(Number(W,TEXT("legacyBusy"))>0)Label+=TEXT("   Earlier jobs finishing: ")+Count(Number(W,TEXT("legacyBusy")));
            return Label;
        },11,Muted,false,720)];
        TSharedRef<SScrollBox> Items=SNew(SScrollBox);
        for(const auto& V:Products)
        {
            if(!V.IsValid()||V->Type!=EJson::Object)continue;
            const auto P=V->AsObject();const FString Code=String(P,TEXT("code"));
            if((Code==TEXT("BIP"))!=(StoreCategory==1))continue;
            Items->AddSlot().Padding(0,0,4,8)
                [SNew(SBorder).BorderImage(WhiteBrush()).Padding(2).BorderBackgroundColor(SelectedProduct==Code?Gold:Paper)
                    [SNew(SButton).ButtonStyle(&CardStyle).ContentPadding(10).IsFocusable(false)
                        .OnClicked_Lambda([this,Code](){SelectedProduct=Code;bRefreshPopup=true;return FReply::Handled();})
                        [SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,15,0)[StoreIcon(Code)]
                            +SHorizontalBox::Slot().FillWidth(1)
                                [SNew(SVerticalBox)
                                    +SVerticalBox::Slot().AutoHeight()[Text(String(P,TEXT("name")),14,Ink,true,570)]
                                    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Text(PlainText(String(P,TEXT("description"))),11,Muted,false,570)]
                                    +SVerticalBox::Slot().AutoHeight()[LiveText([this,Code]()
                                    {
                                        const auto Item=Product(Code);
                                        return TEXT("Purchased: ")+Count(Number(Item,TEXT("owned")))+TEXT(" / ")+Count(Number(Item,TEXT("max")));
                                    },10,Ink,false,570)]]]]];
        }
        if(Products.IsEmpty())Items->AddSlot()[Text(TEXT("The host is loading the General Store."),12,Muted,false,710)];
        Body->AddSlot().FillHeight(1)[Items];
        const TSharedRef<SWidget> Status=LiveText([this]()
        {
            if(!ViewingHome())return FString(TEXT("Return to your yard to use the General Store."));
            if(!Bool(Shop(),TEXT("storeAvailable")))return String(Shop(),TEXT("storeReason"));
            return String(Product(SelectedProduct),TEXT("reason"));
        },11,Red,false,710);
        const TSharedRef<SScrollBox> StatusScroll=SNew(SScrollBox)+SScrollBox::Slot()[Status];
        Body->AddSlot().AutoHeight().Padding(0,8)[SNew(SBox).MaxDesiredHeight(58)[StatusScroll]];
        const auto Selected=Product(SelectedProduct);const FString Code=SelectedProduct;
        const int32 ExpectedLevel=static_cast<int32>(Number(Selected,TEXT("owned")));
        const int64 Quote=static_cast<int64>(Number(Selected,TEXT("price")));
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)[Text(String(Selected,TEXT("name"))+TEXT(" - ")+Count(Quote)+TEXT(" Shiny"),13,Ink,true,720)];
        TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox);
        Actions->AddSlot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Decorations"),[this](){BuildGroup=4;BuildPage=0;ShowPopup(TEXT("Construction"));})];
        Actions->AddSlot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Buy"),[this,Code,ExpectedLevel]()
        {
            TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetStringField(TEXT("code"),Code);R->SetNumberField(TEXT("expectedLevel"),ExpectedLevel);Command(TEXT("shopBuy"),R);
        },[this,Code,ExpectedLevel,Quote]()
        {
            const auto Current=Product(Code);
            return ViewingHome()&&!Battle()&&Bool(Current,TEXT("canBuy"))&&Number(Current,TEXT("owned"))==ExpectedLevel&&Number(Current,TEXT("price"))==Quote;
        })];
        Body->AddSlot().AutoHeight()[Actions];
        RecordLayoutProbe(TEXT("Actions"),Actions);RecordLayoutProbe(TEXT("Status"),StatusScroll);
        return Frame(Body,780,600);
    }
    TSharedRef<SWidget> StoreTabs()
    {
        const TSharedRef<SHorizontalBox> Tabs=SNew(SHorizontalBox);
        const TCHAR* Names[]={TEXT("Construction"),TEXT("Resources"),TEXT("Speed Ups")};
        for(int32 Index=0;Index<3;++Index)
        {
            if(Index==1&&!Product(TEXT("BIP")))continue;
            if(Index==2&&(!Shop()||!Shop()->HasField(TEXT("speedups"))))continue;
            Tabs->AddSlot().FillWidth(1).Padding(3,0)[Button((StoreCategory==Index?TEXT("[ "):TEXT(""))+FString(Names[Index])+(StoreCategory==Index?TEXT(" ]"):TEXT("")),
                [this,Index](){StoreCategory=Index;bRefreshPopup=true;})];
        }
        return Tabs;
    }
    TSharedRef<SWidget> SpeedupStoreDialog()
    {
        using namespace BYMUI;const auto Jobs=Array(Shop(),TEXT("speedups"));
        if(!SpeedupJob(SpeedupBuilding))
        {
            SpeedupBuilding.Empty();
            if(Jobs.Num()>0&&Jobs[0].IsValid()&&Jobs[0]->Type==EJson::Object)SpeedupBuilding=String(Jobs[0]->AsObject(),TEXT("building"));
        }
        if(!SpeedupOption(SpeedupBuilding,SpeedupCode))SpeedupCode=TEXT("SP1");
        TSharedRef<SVerticalBox> Body=DialogBody(TEXT("General Store"));
        Body->AddSlot().AutoHeight().Padding(0,0,0,10)[StoreTabs()];
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)[LiveText([this](){return TEXT("Shiny balance: ")+Count(Number(Shop(),TEXT("shiny")));},13,Ink,true,720)];
        TSharedRef<SScrollBox> Targets=SNew(SScrollBox);
        for(const auto& V:Jobs)
        {
            if(!V.IsValid()||V->Type!=EJson::Object)continue;const auto J=V->AsObject();const FString Id=String(J,TEXT("building"));
            Targets->AddSlot().Padding(0,0,4,4)
                [SNew(SButton).ButtonStyle(&CardStyle).ContentPadding(7).IsFocusable(false)
                    .OnClicked_Lambda([this,Id](){SpeedupBuilding=Id;bRefreshPopup=true;return FReply::Handled();})
                    [LiveText([this,Id]()
                    {
                        const auto Current=SpeedupJob(Id);
                        return (SpeedupBuilding==Id?TEXT("[ "):TEXT(""))+String(Current,TEXT("name"))
                            +TEXT(" - ")+Duration(Number(Current,TEXT("remaining")))+(SpeedupBuilding==Id?TEXT(" ]"):TEXT(""));
                    },12,Ink,true,690)]];
        }
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(SBox).MaxDesiredHeight(100)[Targets]];
        TSharedRef<SScrollBox> Options=SNew(SScrollBox);
        for(const auto& V:Array(SpeedupJob(SpeedupBuilding),TEXT("options")))
        {
            if(!V.IsValid()||V->Type!=EJson::Object)continue;const auto Option=V->AsObject();const FString Code=String(Option,TEXT("code")),TargetId=SpeedupBuilding;
            Options->AddSlot().Padding(0,0,4,6)
                [SNew(SBorder).BorderImage(WhiteBrush()).Padding(2).BorderBackgroundColor(SpeedupCode==Code?Gold:Paper)
                    [SNew(SButton).ButtonStyle(&CardStyle).ContentPadding(8).IsFocusable(false)
                        .OnClicked_Lambda([this,Code](){SpeedupCode=Code;bRefreshPopup=true;return FReply::Handled();})
                        [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(0,0,14,0)[StoreIcon(Code)]
                            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                                [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Text(String(Option,TEXT("name")),14,Ink,true,570)]
                                    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[LiveText([this,TargetId,Code](){return Count(Number(SpeedupOption(TargetId,Code),TEXT("price")))+TEXT(" Shiny");},12,Ink,true,570)]
                                    +SVerticalBox::Slot().AutoHeight()[LiveText([this,TargetId,Code](){return String(SpeedupOption(TargetId,Code),TEXT("reason"));},11,Muted,false,570)]]]]];
        }
        if(Jobs.IsEmpty())Options->AddSlot()[Text(TEXT("Select a building with construction or an upgrade in progress to use a speed-up."),12,Muted,false,710)];
        Body->AddSlot().FillHeight(1)[Options];
        const auto Job=SpeedupJob(SpeedupBuilding),Option=SpeedupOption(SpeedupBuilding,SpeedupCode);
        const FString BuildingId=SpeedupBuilding,Code=SpeedupCode,JobId=String(Job,TEXT("job"));
        const int32 Revision=static_cast<int32>(Number(Job,TEXT("revision")));
        const int64 Quote=static_cast<int64>(Number(Option,TEXT("price")));
        const TSharedRef<SWidget> Message=LiveText([this,BuildingId,Code]()
        {
            if(!ViewingHome())return FString(TEXT("Return to your yard to use a speed-up."));
            if(!SpeedupJob(BuildingId))return FString(TEXT("This work has finished or changed."));
            return String(SpeedupOption(BuildingId,Code),TEXT("reason"));
        },11,Red,false,710);
        const TSharedRef<SScrollBox> Status=SNew(SScrollBox)+SScrollBox::Slot()[Message];
        Body->AddSlot().AutoHeight().Padding(0,8)[SNew(SBox).MaxDesiredHeight(48)[Status]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)[LiveText([this,BuildingId,Code]()
            {const auto Current=SpeedupOption(BuildingId,Code);return String(Current,TEXT("name"))+TEXT(" - ")+Count(Number(Current,TEXT("price")))+TEXT(" Shiny");},13,Ink,true,720)];
        const TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Go back"),[this](){StoreCategory=0;bRefreshPopup=true;})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Use speed-up"),[this,BuildingId,JobId,Code,Revision,Quote]()
            {
                TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetStringField(TEXT("building"),BuildingId);R->SetStringField(TEXT("job"),JobId);
                R->SetStringField(TEXT("code"),Code);R->SetNumberField(TEXT("revision"),Revision);R->SetNumberField(TEXT("maxPrice"),Quote);Command(TEXT("shopSpeedup"),R);
            },[this,BuildingId,JobId,Code,Revision,Quote]()
            {
                const auto CurrentJob=SpeedupJob(BuildingId),CurrentOption=SpeedupOption(BuildingId,Code);
                return ViewingHome()&&!Battle()&&Bool(CurrentOption,TEXT("canBuy"))&&String(CurrentJob,TEXT("job"))==JobId
                    &&Number(CurrentJob,TEXT("revision"))==Revision&&Number(CurrentOption,TEXT("price"))<=Quote;
            })];
        Body->AddSlot().AutoHeight()[Actions];RecordLayoutProbe(TEXT("Actions"),Actions);RecordLayoutProbe(TEXT("Status"),Status);
        return Frame(Body,780,650);
    }
    TSharedRef<SWidget> MushroomDialog()
    {
        using namespace BYMUI;const FString Id=SelectedBuilding;
        TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Mushroom"));
        TSharedRef<SVerticalBox> Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().HAlign(HAlign_Center)[Image(TEXT("buildingthumbs/7.png"),110,100)];
        Details->AddSlot().AutoHeight().Padding(0,8)[Text(TEXT("Send an available worker to clear this mushroom. You may find Shiny underneath."),12,Ink,false,480)];
        const TSharedRef<SWidget> Status=LiveText([this,Id]()
        {
            const auto M=Mushroom(Id);
            if(!M)return FString(TEXT("This mushroom has already been cleared."));
            if(!ViewingHome())return FString(TEXT("Clear mushrooms in your own yard."));
            if(Bool(M,TEXT("picking")))return FString(TEXT("Your worker is clearing this mushroom."));
            const FString Reason=String(M,TEXT("reason"));
            return !Reason.IsEmpty()?Reason:Bool(Shop(),TEXT("canClearMushrooms"))?FString(TEXT("No resource cost.")):String(Shop(),TEXT("clearReason"));
        },11,Muted,false,480);
        Details->AddSlot().AutoHeight().Padding(0,8)[Status];
        const TSharedRef<SScrollBox> Scroll=SNew(SScrollBox)+SScrollBox::Slot()[Details];
        Body->AddSlot().FillHeight(1)[Scroll];
        TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox);
        Actions->AddSlot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Go back"),[this](){ClosePopup();})];
        Actions->AddSlot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Clear"),[this,Id]()
        {TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetStringField(TEXT("id"),Id);Command(TEXT("clearmushroom"),R);},[this,Id]()
        {
            const auto M=Mushroom(Id);
            return ViewingHome()&&!Battle()&&M.IsValid()&&!Bool(M,TEXT("picking"))&&Bool(M,TEXT("canClear"),Bool(Shop(),TEXT("canClearMushrooms")));
        })];
        Body->AddSlot().AutoHeight()[Actions];RecordLayoutProbe(TEXT("Actions"),Actions);RecordLayoutProbe(TEXT("Status"),Scroll);
        return Frame(Body,540,390);
    }
    TSharedRef<SWidget> HUDStatus()
    {
        const TSharedRef<SWidget> Paragraph=LiveText([this](){return StatusText();},11,BYMUI::White,false,700);
        const TSharedRef<SScrollBox> Status=SNew(SScrollBox)+SScrollBox::Slot().Padding(0,0,8,0)[Paragraph];
        const TSharedRef<SVerticalBox> Body=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SBox).MaxDesiredHeight(64)[Status]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                [SNew(SBox).Visibility_Lambda([this](){return PC.IsValid()&&!PC->PlacementMode.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
                    [Button(TEXT("Cancel placement"),[this](){if(PC.IsValid())PC->PlacementMode.Empty();})]];
        const TSharedRef<SWidget> Panel=Wood(Body,FMargin(12,8));
        RecordLayoutProbe(TEXT("Status"),Status);RecordLayoutProbe(TEXT("Paragraph"),Paragraph);
        RecordLayoutProbe(TEXT("StatusPanel"),Panel);
        return SNew(SBox).WidthOverride(760)[Panel];
    }
    void BuildGame()
    {
        using namespace BYMUI;
        TSharedRef<SHorizontalBox> Dock=SNew(SHorizontalBox);
        Dock->AddSlot().AutoWidth().Padding(0,0,3,0)[Button(TEXT("Buildings"),[this](){ShowPopup(TEXT("Construction"));},[this](){return ViewingHome()&&!Battle();})];
        Dock->AddSlot().AutoWidth().Padding(0,0,3,0)[Button(TEXT("Monsters"),[this](){ShowArmy(0);},[this](){return ViewingHome()&&!Battle();})];
        Dock->AddSlot().AutoWidth().Padding(0,0,3,0)[Button(TEXT("Map Room"),[this](){ShowPopup(TEXT("MapRoom"));},[this](){return !Battle();})];
        Dock->AddSlot().AutoWidth()[Button(TEXT("Rankings"),[this](){ShowPopup(TEXT("Rivals"));},[this](){return !Battle();})];
        RecordLayoutProbe(TEXT("Actions"),Dock);
        TSharedRef<SHorizontalBox> Utility=SNew(SHorizontalBox);
        Utility->AddSlot().AutoWidth().Padding(0,0,4,0)
            [SNew(SButton).ButtonStyle(&StoneStyle).IsFocusable(false).IsEnabled_Lambda([this](){return Idle();}).OnClicked_Lambda([this](){if(PC.IsValid())PC->ReturnHome();return FReply::Handled();})
                [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(IsAttacking()?TEXT("End attack"):TEXT("Home"));}).Font(ButtonFont()).ColorAndOpacity(White)]];
        Utility->AddSlot().AutoWidth().Padding(0,0,4,0)[Button(TEXT("Collect"),[this](){Command(TEXT("collect"),MakeShared<FJsonObject>());},[this](){return ViewingHome()&&!Battle();})];
        Utility->AddSlot().AutoWidth()[Button(TEXT("Menu"),[this](){ShowPopup(TEXT("Menu"));})];
        ChildSlot[SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8,6,0,0)
                [SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(FMath::Min(237.f,ViewportWidth*.44f));})
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[ResourceHUD()]]]
            +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0,10,12,0)[SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(FMath::Min(235.f,ViewportWidth*.44f));})
                    .HeightOverride_Lambda([this](){return FOptionalSize(FMath::Max(1.f,ViewportHeight-90.f));})
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly).HAlign(HAlign_Right).VAlign(VAlign_Top)[SNew(SBox).WidthOverride(235)
                        [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[BattleHeader()]
                            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,6,0,0)[ShinyHUD()]
                            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,12,0,0)[WorkerHUD()]]]]]
            +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(8,244,0,145)[SAssignNew(DeploymentHost,SBox).WidthOverride(210)]
            +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(10,0,0,11)
                [SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(FMath::Min(260.f,ViewportWidth*.36f));}).HeightOverride(54)
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Wood(Utility,FMargin(10,9))]]]
            +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0,0,10,11)
                [SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(FMath::Min(470.f,ViewportWidth*.56f));}).HeightOverride(54)
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Wood(Dock,FMargin(10,9))]]]
            +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(16,0,16,74)
                [SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(FMath::Clamp(ViewportWidth-32.f,1.f,760.f));})
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[HUDStatus()]]]
            +SOverlay::Slot().Padding(24,34,24,130)
                [SAssignNew(PopupHost,SBox).Visibility(EVisibility::Collapsed)]];
        SeenStructure.Empty();RefreshDeployment();RefreshPopup();
    }
    void RefreshPopup()
    {
        if(!PopupHost)return;
        if(Popup.IsEmpty()){PopupHost->SetContent(SNullWidget::NullWidget);PopupHost->SetVisibility(EVisibility::Collapsed);return;}
        TSharedRef<SWidget> Content=SNew(SBox);
        if(Popup==TEXT("Construction"))Content=Construction();
        else if(Popup==TEXT("Blueprint"))Content=BlueprintDialog();
        else if(Popup==TEXT("Army"))Content=ArmyDialog();
        else if(Popup==TEXT("Rivals"))Content=RivalsDialog();
        else if(Popup==TEXT("MapRoom"))Content=MapRoomDialog();
        else if(Popup==TEXT("MapAttack")||Popup==TEXT("MapAbandon")||Popup==TEXT("MapRelocate"))Content=MapConfirmationDialog();
        else if(Popup==TEXT("Building"))Content=BuildingDialog();
        else if(Popup==TEXT("CancelBuilding"))Content=CancelBuildingDialog();
        else if(Popup==TEXT("Bunker"))Content=BunkerDialog();
        else if(Popup==TEXT("DismissBunker"))Content=DismissBunkerDialog();
        else if(Popup==TEXT("Store"))Content=StoreDialog();
        else if(Popup==TEXT("Mushroom"))Content=MushroomDialog();
        else if(Popup==TEXT("BattleResult"))Content=BattleResultDialog();
        else Content=MenuDialog();
        PopupHost->SetContent(SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly).Visibility(EVisibility::SelfHitTestInvisible)[Content]);
        PopupHost->SetVisibility(EVisibility::SelfHitTestInvisible);
        if(Popup==TEXT("MapRoom")&&MapCanvas&&PC.IsValid()&&FSlateApplication::IsInitialized())
            FSlateApplication::Get().SetKeyboardFocus(MapCanvas,EFocusCause::SetDirectly);
    }
    TSharedRef<SWidget> Construction()
    {
        using namespace BYMUI;
        TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Buildings"));TSharedRef<SHorizontalBox> Tabs=SNew(SHorizontalBox);
        const TCHAR* Names[]={TEXT("Resources"),TEXT("Buildings"),TEXT("Defensive"),TEXT("Decorations")};
        for(int32 I=0;I<4;++I)
        {
            const int32 Group=I+1;
            Tabs->AddSlot().FillWidth(1).Padding(2,0)[Button((BuildGroup==Group?TEXT("[ "):TEXT(""))+FString(Names[I])+(BuildGroup==Group?TEXT(" ]"):TEXT("")),[this,Group](){BuildGroup=Group;BuildPage=0;bRefreshPopup=true;})];
        }
        Body->AddSlot().AutoHeight().Padding(0,0,0,12)[Tabs];
        const TCHAR* DecorationCategories[]={TEXT("War"),TEXT("Nature"),TEXT("Leisure"),TEXT("Flags"),TEXT("Special")};
        if(BuildGroup==4)
        {
            // BUILDINGSPOPUP.SwitchB retains these five original subgroups.
            TSharedRef<SHorizontalBox> Subcategories=SNew(SHorizontalBox);
            for(int32 Index=0;Index<5;++Index)
                Subcategories->AddSlot().FillWidth(1).Padding(2,0)[Button((DecorationCategory==Index?TEXT("[ "):TEXT(""))+FString(DecorationCategories[Index])+(DecorationCategory==Index?TEXT(" ]"):TEXT("")),
                    [this,Index](){DecorationCategory=Index;BuildPage=0;bRefreshPopup=true;})];
            Body->AddSlot().AutoHeight().Padding(0,0,0,10)[Subcategories];
        }
        TArray<TSharedPtr<FJsonObject>> Entries;
        for(const auto& V:Array(Object(Snapshot,TEXT("catalog")),TEXT("buildings")))
        {
            const auto B=V->AsObject();if(Number(B,TEXT("group"))!=BuildGroup)continue;
            if(BuildGroup==4)
            {
                const auto Offer=Decoration(static_cast<int32>(Number(B,TEXT("id"))));
                if(!Offer||String(Offer,TEXT("category"))!=DecorationCategories[DecorationCategory])continue;
            }
            Entries.Add(B);
        }
        BuildPage=FMath::Clamp(BuildPage,0,FMath::Max(0,(Entries.Num()-1)/10));
        TSharedRef<SUniformGridPanel> Grid=SNew(SUniformGridPanel).SlotPadding(FMargin(5));
        for(int32 I=BuildPage*10;I<FMath::Min(Entries.Num(),BuildPage*10+10);++I)
        {
            const auto B=Entries[I];const int32 Type=Number(B,TEXT("id"));
            const FString MenuArt=String(B,TEXT("menuTexture"),String(B,TEXT("thumbnailTexture"),String(B,TEXT("texture"))));
            TSharedRef<SOverlay> Card=SNew(SOverlay);
            Card->AddSlot()[SNew(SImage).Image(ImageBrush(MenuArt)).Visibility(EVisibility::HitTestInvisible)];
            Card->AddSlot().VAlign(VAlign_Top).Padding(3,5,3,0)[Text(String(B,TEXT("name")),11,Ink,false,114)];
            Card->AddSlot().VAlign(VAlign_Bottom).Padding(3,0,3,7)
                [SNew(STextBlock).Text_Lambda([this,Type]()
                    {
                        const auto D=Decoration(Type),O=D?D:BuildOption(Type);
                        return FText::FromString(D?Count(Number(D,TEXT("price")))+TEXT(" Shiny")
                            :Count(Number(O,TEXT("owned")))+TEXT(" / ")+Count(Number(O,TEXT("limit"))));
                    }).Font(Font(11)).ColorAndOpacity_Lambda([this,Type]()
                    {
                        const auto D=Decoration(Type),O=D?D:BuildOption(Type);
                        if(D)return Bool(D,TEXT("canBuy"))?Ink:Red;
                        return Number(O,TEXT("owned"))>=Number(O,TEXT("limit"))?Red:Ink;
                    })];
            Grid->AddSlot((I%10)%5,(I%10)/5)
                [SNew(SBox).WidthOverride(120).HeightOverride(160)
                    [SNew(SButton).ButtonStyle(&CardStyle).ContentPadding(0).IsFocusable(false)
                        .ToolTipText_Lambda([this,Type](){const auto D=Decoration(Type);return FText::FromString(String(D?D:BuildOption(Type),TEXT("reason"),TEXT("View building details")));})
                        .OnClicked_Lambda([this,Type](){Blueprint=Type;Popup=TEXT("Blueprint");bRefreshPopup=true;return FReply::Handled();})[Card]]];
        }
        Body->AddSlot().FillHeight(1)[Grid];
        if(!Entries.Num())Body->AddSlot().AutoHeight().Padding(0,10)[Text(TEXT("No buildings available in this category."),12,Muted)];
        Body->AddSlot().AutoHeight().Padding(0,8,0,0)
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Previous"),[this](){--BuildPage;bRefreshPopup=true;},[this](){return BuildPage>0;})]
                +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("Page %d / %d"),BuildPage+1,FMath::Max(1,FMath::DivideAndRoundUp(Entries.Num(),10))),11)]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Next"),[this](){++BuildPage;bRefreshPopup=true;},[this,Entries](){return (BuildPage+1)*10<Entries.Num();})]];
        return Frame(Body,762,BuildGroup==4?574:522);
    }
    TSharedRef<SWidget> BlueprintDialog()
    {
        using namespace BYMUI;const auto B=Definition(Blueprint);const int32 Type=Blueprint;
        if(Decoration(Type))return DecorationDialog(Type);
        TSharedRef<SVerticalBox> Body=DialogBody(String(B,TEXT("name"),TEXT("Building")));
        TSharedRef<SVerticalBox> Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().HAlign(HAlign_Center)[Image(String(B,TEXT("texture")),190,160)];
        Details->AddSlot().AutoHeight().Padding(4,10)[Text(String(B,TEXT("description")),12,BYMUI::Ink,false,492)];
        Details->AddSlot().AutoHeight().Padding(4,5)[LiveText([this,Type](){const auto O=BuildOption(Type);return TEXT("Cost: ")+Cost(Object(O,TEXT("costs")));},12,BYMUI::Ink,false,492)];
        Details->AddSlot().AutoHeight().Padding(4,5)[LiveText([this,Type](){const auto O=BuildOption(Type);return TEXT("Build time: ")+Duration(Number(O,TEXT("time")))+TEXT("   Built: ")+Count(Number(O,TEXT("owned")))+TEXT(" / ")+Count(Number(O,TEXT("limit")));},12,BYMUI::Ink,false,492)];
        Details->AddSlot().AutoHeight().Padding(4,5)[Text(String(B,TEXT("requirementsText")),11,Muted,false,492)];
        Details->AddSlot().AutoHeight().Padding(4,7)[LiveText([this,Type](){return String(BuildOption(Type),TEXT("reason"));},12,Red,false,492)];
        Body->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Details]];
        Body->AddSlot().AutoHeight()
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Back"),[this](){ShowPopup(TEXT("Construction"));})]
                +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Build"),[this,Type]()
                {
                    if(!PC.IsValid())return;PC->SelectedBuildingType=Type;PC->PlacementMode=TEXT("build");
                    PC->StatusMessage=TEXT("Choose an empty location for ")+String(Definition(Type),TEXT("name"))+TEXT(". Green footprint means the position is clear. Escape cancels.");ClosePopup();
                },[this,Type](){return ViewingHome()&&Bool(BuildOption(Type),TEXT("canBuild"));})]];
        return Frame(Body,560,520);
    }
    TSharedRef<SWidget> DecorationDialog(int32 Type)
    {
        using namespace BYMUI;const auto Offer=Decoration(Type),D=Definition(Type);
        TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Decoration"));
        TSharedRef<SVerticalBox> Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().HAlign(HAlign_Center)[Image(String(Offer,TEXT("texture"),String(D,TEXT("texture"))),190,160)];
        Details->AddSlot().AutoHeight().Padding(0,8)[Text(String(Offer,TEXT("name")),16,Ink,true,490)];
        Details->AddSlot().AutoHeight().Padding(0,5)[Text(PlainText(String(D,TEXT("description"))),12,Muted,false,490)];
        Details->AddSlot().AutoHeight().Padding(0,6)[LiveText([this,Type]()
        {
            const auto Item=Decoration(Type);const double Limit=Number(Item,TEXT("limit"));
            return TEXT("Cost: ")+Count(Number(Item,TEXT("price")))+TEXT(" Shiny\nOwned: ")+Count(Number(Item,TEXT("owned")))
                +(Limit>0?TEXT(" / ")+Count(Limit):FString());
        },12,Ink,true,490)];
        const TSharedRef<SWidget> Status=LiveText([this,Type](){return String(Decoration(Type),TEXT("reason"));},11,Red,false,490);
        Details->AddSlot().AutoHeight().Padding(0,8)[Status];
        const TSharedRef<SScrollBox> Scroll=SNew(SScrollBox)+SScrollBox::Slot()[Details];
        Body->AddSlot().FillHeight(1)[Scroll];
        const TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Back"),[this](){BuildGroup=4;ShowPopup(TEXT("Construction"));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Place"),[this,Type]()
            {
                if(!PC.IsValid())return;PC->SelectedBuildingType=Type;PC->PlacementMode=TEXT("decorate");
                PC->StatusMessage=TEXT("Choose a clear position for ")+String(Decoration(Type),TEXT("name"))+TEXT(". Shiny is spent only when placement succeeds.");ClosePopup();
            },[this,Type](){return ViewingHome()&&!Battle()&&Bool(Decoration(Type),TEXT("canBuy"));})];
        Body->AddSlot().AutoHeight()[Actions];RecordLayoutProbe(TEXT("Actions"),Actions);RecordLayoutProbe(TEXT("Status"),Scroll);
        return Frame(Body,560,520);
    }
    FString CollectorStatus(const FString& Id)const
    {
        using namespace BYMUI;const auto Item=Building(Id);const FString State=String(Item,TEXT("productionState"));
        if(State==TEXT("full"))return TEXT("Collector full - collect resources to restart production.");
        if(State==TEXT("damaged"))return TEXT("Production paused - repair to at least half health.");
        if(State==TEXT("upgrading"))return TEXT("Production paused while upgrading.");
        if(State==TEXT("constructing"))return TEXT("Production begins after construction finishes.");
        if(State==TEXT("producing"))
        {
            const double Remaining=FMath::Max(0.,Number(Item,TEXT("productionRemaining"))-(ServerNow()-SampleTime));
            return TEXT("Next batch: ")+Duration(Remaining)+TEXT("   ")+Count(Number(Item,TEXT("productionBatch")))
                +TEXT(" every ")+Duration(Number(Item,TEXT("productionCycleSeconds")));
        }
        return TEXT("Production is paused.");
    }
    FString BuildingWorkStatus(const FString& Id)const
    {
        using namespace BYMUI;const auto Item=Building(Id);const FString Kind=String(Item,TEXT("jobKind"),TEXT("none"));
        if(Kind==TEXT("none"))return FString();
        const FString Label=Kind==TEXT("upgrade")?TEXT("Upgrade to level ")+Count(Number(Item,TEXT("targetLevel"))):TEXT("Construction");
        const FString Pause=String(Item,TEXT("workPausedReason"));
        if(Pause==TEXT("worker_walking"))return Label+TEXT(" - your worker is on the way.\nWork remaining: ")+Duration(Number(Item,TEXT("workRemaining")));
        if(!Pause.IsEmpty())
            return Label+TEXT(" paused - ")+(Pause==TEXT("repairing")?TEXT("repairs must finish first."):TEXT("repair this building to full health."))
                +TEXT("\nWork remaining: ")+Duration(Number(Item,TEXT("workRemaining")));
        const double Deadline=Number(Item,TEXT("workReadyAt"));
        const double Remaining=Deadline>0?FMath::Max(0.,Deadline-ServerNow()):Number(Item,TEXT("workRemaining"));
        return Remaining>0?Label+TEXT(" finishes in ")+Duration(Remaining):Label+TEXT(" is finishing...");
    }
    FString BuildingRepairStatus(const FString& Id)const
    {
        using namespace BYMUI;const auto Item=Building(Id);
        if(!Bool(Item,TEXT("repairing")))return FString();
        const double Deadline=Number(Item,TEXT("repairReadyAt"));
        const double Remaining=Deadline>0?FMath::Max(0.,Deadline-ServerNow()):Number(Item,TEXT("repairRemaining"));
        return Remaining>0?TEXT("Repair to full health: ")+Duration(Remaining):TEXT("Finishing repair...");
    }
    bool CancellationStillCurrent()const
    {
        const auto Item=Building(CancelBuildingId);
        return ViewingHome()&&Item.IsValid()&&!CancelBuildingJob.IsEmpty()
            &&BYMUI::String(Item,TEXT("jobId"))==CancelBuildingJob&&BYMUI::String(Item,TEXT("jobKind"))==CancelBuildingKind;
    }
    void ConfirmBuildingCancellation(const FString& Id)
    {
        const auto Item=Building(Id);const FString Kind=BYMUI::String(Item,TEXT("jobKind"));
        const FString Job=BYMUI::String(Item,TEXT("jobId"));
        if(Job.IsEmpty()||(Kind!=TEXT("construction")&&Kind!=TEXT("upgrade")))return;
        CancelBuildingId=Id;CancelBuildingJob=Job;CancelBuildingKind=Kind;
        ShowPopup(TEXT("CancelBuilding"));
    }
    TSharedRef<SWidget> CancelBuildingDialog()
    {
        using namespace BYMUI;const bool bCancelConstruction=CancelBuildingKind==TEXT("construction");
        TSharedRef<SVerticalBox> Body=DialogBody(bCancelConstruction?TEXT("Cancel construction?"):TEXT("Cancel upgrade?"));
        TSharedRef<SVerticalBox> Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().Padding(4,8)[Text(bCancelConstruction
            ?TEXT("The unfinished building will be removed. Its construction progress will be lost.")
            :TEXT("The building will keep its current level. Progress toward the next level will be lost."),12,Ink,false,492)];
        Details->AddSlot().AutoHeight().Padding(4,10)[LiveText([this]()
        {
            if(!CancellationStillCurrent())return FString(TEXT("This job has changed or finished. Return to the building to see its current state."));
            const auto Refund=BYMUI::Object(Building(CancelBuildingId),TEXT("cancelRefund"));
            const double Total=BYMUI::Number(Refund,TEXT("twigs"))+BYMUI::Number(Refund,TEXT("pebbles"))
                +BYMUI::Number(Refund,TEXT("putty"))+BYMUI::Number(Refund,TEXT("goo"));
            return Total>0?TEXT("Refund: ")+BYMUI::Cost(Refund):FString(TEXT("Refund: 0 - no resources can be returned."));
        },12,Green,true,492)];
        Details->AddSlot().AutoHeight().Padding(4,4)[Text(TEXT("Refunds are limited by the free space in your resource storage."),11,Muted,false,492)];
        Details->AddSlot().AutoHeight().Padding(4,8)[LiveText([this](){return CancellationStillCurrent()?BYMUI::String(Building(CancelBuildingId),TEXT("cancelReason")):FString();},11,Red,false,492)];
        Body->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Details]];
        Body->AddSlot().AutoHeight().Padding(0,10)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Go back"),[this](){ShowPopup(TEXT("Building"));})]
                +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(bCancelConstruction?TEXT("Cancel construction"):TEXT("Cancel upgrade"),[this]()
                {
                    // A job identity captured when the confirmation opened prevents
                    // this request from cancelling replacement work after a refresh.
                    TSharedRef<FJsonObject> Request=MakeShared<FJsonObject>();
                    Request->SetStringField(TEXT("building"),CancelBuildingId);Request->SetStringField(TEXT("job"),CancelBuildingJob);
                    Command(CancelBuildingKind==TEXT("construction")?TEXT("cancelConstruction"):TEXT("cancelUpgrade"),Request);
                    ShowPopup(TEXT("Building"));
                },[this]()
                {
                    return CancellationStillCurrent()&&!Battle()&&BYMUI::Bool(Building(CancelBuildingId),
                        CancelBuildingKind==TEXT("construction")?TEXT("canCancelConstruction"):TEXT("canCancelUpgrade"));
                })]];
        return Frame(Body,560,390);
    }
    TSharedRef<SWidget> BuildingDialog()
    {
        using namespace BYMUI;const FString Id=SelectedBuilding;const auto B=Building(Id);
        TSharedRef<SVerticalBox> Body=DialogBody(String(B,TEXT("name"),TEXT("Building")));
        TSharedRef<SVerticalBox> Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().HAlign(HAlign_Center)[Image(String(B,TEXT("texture")),180,145)];
        Details->AddSlot().AutoHeight().Padding(4,7)[LiveText([this,Id](){const auto Item=Building(Id);return TEXT("Level ")+Count(Number(Item,TEXT("level")))+TEXT("   Health ")+Count(Number(Item,TEXT("hp")))+TEXT(" / ")+Count(Number(Item,TEXT("maxHp")));},12,BYMUI::Ink,false,492)];
        Details->AddSlot().AutoHeight().Padding(4,5)
            [SNew(SBox).Visibility_Lambda([this,Id](){return String(Building(Id),TEXT("jobKind"),TEXT("none"))!=TEXT("none")?EVisibility::Visible:EVisibility::Collapsed;})
                [LiveText([this,Id](){return BuildingWorkStatus(Id);},12,Muted,false,492)]];
        Details->AddSlot().AutoHeight().Padding(4,5)
            [SNew(SBox).Visibility_Lambda([this,Id](){return Bool(Building(Id),TEXT("repairing"))?EVisibility::Visible:EVisibility::Collapsed;})
                [LiveText([this,Id](){return BuildingRepairStatus(Id);},12,Green,false,492)]];
        if(ViewingHome())
        {
            if(Number(B,TEXT("type"))>=1&&Number(B,TEXT("type"))<=4)
            {
                Details->AddSlot().AutoHeight().Padding(4,4)[LiveText([this,Id]()
                {
                    const auto Item=Building(Id);
                    return TEXT("Stored: ")+Count(Number(Item,TEXT("stored")))+TEXT(" / ")+Count(Number(Item,TEXT("collectorCapacity")));
                },12,Ink,true,492)];
                Details->AddSlot().AutoHeight().Padding(4,3)[LiveText([this,Id](){return CollectorStatus(Id);},11,Muted,false,492)];
                Details->AddSlot().AutoHeight().Padding(0,5)[Button(TEXT("Collect resources"),[this,Id](){BuildingCommand(TEXT("collect"),Id);},
                    [this,Id](){return !Battle()&&Bool(Building(Id),TEXT("canCollect"));})];
                Details->AddSlot().AutoHeight().Padding(4,2)[LiveText([this,Id](){return String(Building(Id),TEXT("collectReason"));},10,Red,false,492)];
            }
            TSharedRef<SVerticalBox> Upgrade=SNew(SVerticalBox);
            Upgrade->AddSlot().AutoHeight().Padding(4,5)[LiveText([this,Id]()
            {const auto Item=Building(Id);return Bool(Item,TEXT("upgradeAvailable"))?TEXT("Upgrade: ")+Cost(Object(Item,TEXT("upgradeCosts")))+TEXT("   ")+Duration(Number(Item,TEXT("upgradeSeconds"))):TEXT("Maximum level reached");},11,BYMUI::Ink,false,492)];
            Upgrade->AddSlot().AutoHeight().Padding(4,4)[LiveText([this,Id](){return String(Building(Id),TEXT("upgradeReason"));},11,Red,false,492)];
            Upgrade->AddSlot().AutoHeight().Padding(0,7)[Button(TEXT("Upgrade"),[this,Id](){BuildingCommand(TEXT("upgrade"),Id);},[this,Id](){return !Battle()&&Bool(Building(Id),TEXT("canUpgrade"));})];
            Details->AddSlot().AutoHeight()
                [SNew(SBox).Visibility_Lambda([this,Id](){return String(Building(Id),TEXT("jobKind"),TEXT("none"))==TEXT("none")?EVisibility::Visible:EVisibility::Collapsed;})[Upgrade]];
            if(Number(B,TEXT("type"))==11)Details->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Open Map Room"),[this](){ShowPopup(TEXT("MapRoom"));})];
            if(Number(B,TEXT("type"))==12)Details->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Open General Store"),[this](){ShowPopup(TEXT("Store"));})];
            Details->AddSlot().AutoHeight().Padding(0,3)
                [SNew(SBox).Visibility_Lambda([this,Id](){return SpeedupJob(Id)?EVisibility::Visible:EVisibility::Collapsed;})
                    [Button(TEXT("Speed up work"),[this,Id](){SpeedupBuilding=Id;StoreCategory=2;ShowPopup(TEXT("Store"));})]];
            if(Number(B,TEXT("type"))==13||Number(B,TEXT("type"))==15)
                Details->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Open Hatchery"),[this,Id](){ShowArmy(0,TEXT(""),BYMUI::Number(Building(Id),TEXT("type"))==13?Id:FString());})];
            if(Number(B,TEXT("type"))==8)
                Details->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Unlock monsters"),[this](){ShowArmy(1);})];
            if(Number(B,TEXT("type"))==26)
                Details->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Research monsters"),[this](){ShowArmy(2);})];
            if(Number(B,TEXT("type"))==22)
                Details->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Manage Monster Bunker"),[this,Id](){ShowBunker(Id);},
                    [this,Id](){return Number(Building(Id),TEXT("level"))>0&&Bunker(Id).IsValid();})];
            Details->AddSlot().AutoHeight().Padding(0,4)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                        [SNew(SBox).ToolTipText_Lambda([this,Id](){return FText::FromString(String(Building(Id),TEXT("repairReason")));})
                            [Button(TEXT("Repair"),[this,Id](){BuildingCommand(TEXT("repair"),Id);},[this,Id](){return !Battle()&&Bool(Building(Id),TEXT("canRepair"));})]]
                    +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)
                        [SNew(SBox).ToolTipText_Lambda([this,Id](){return FText::FromString(String(Building(Id),TEXT("moveReason")));})
                            [Button(TEXT("Move"),[this,Id](){if(PC.IsValid()){PC->SelectedBuilding=Id;PC->PlacementMode=TEXT("move");PC->StatusMessage=TEXT("Choose a clear position for this building.");ClosePopup();}},[this,Id](){return !Battle()&&Bool(Building(Id),TEXT("canMove"));})]]];
            Details->AddSlot().AutoHeight().Padding(0,4)
                [SNew(SBox).Visibility_Lambda([this,Id](){return String(Building(Id),TEXT("jobKind"))==TEXT("construction")?EVisibility::Visible:EVisibility::Collapsed;})
                    .ToolTipText_Lambda([this,Id](){return FText::FromString(String(Building(Id),TEXT("cancelReason")));})
                    [Button(TEXT("Cancel construction"),[this,Id](){ConfirmBuildingCancellation(Id);},[this,Id](){return !Battle()&&Bool(Building(Id),TEXT("canCancelConstruction"));})]];
            Details->AddSlot().AutoHeight().Padding(0,4)
                [SNew(SBox).Visibility_Lambda([this,Id](){return String(Building(Id),TEXT("jobKind"))==TEXT("upgrade")?EVisibility::Visible:EVisibility::Collapsed;})
                    .ToolTipText_Lambda([this,Id](){return FText::FromString(String(Building(Id),TEXT("cancelReason")));})
                    [Button(TEXT("Cancel upgrade"),[this,Id](){ConfirmBuildingCancellation(Id);},[this,Id](){return !Battle()&&Bool(Building(Id),TEXT("canCancelUpgrade"));})]];
        }
        else if(ViewingOwnedMapSite())
        {
            Details->AddSlot().AutoHeight().Padding(4,8)[Text(TEXT("This captured territory keeps its original buildings. You can repair and reposition its defenses; its central structure stays fixed."),11,Muted,false,492)];
            Details->AddSlot().AutoHeight().Padding(0,6)[Button(TEXT("Repair territory"),[this]()
            {
                const auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("cell"),String(ViewedMapCell(),TEXT("id")));
                Command(TEXT("mapRepair"),Request);
            },[this](){return !Battle()&&Bool(ViewedMapCell(),TEXT("canRepair"));})];
            Details->AddSlot().AutoHeight().Padding(0,6)[Button(TEXT("Move building"),[this,Id]()
            {
                if(!PC.IsValid())return;PC->SelectedBuilding=Id;PC->PlacementMode=TEXT("move");
                PC->StatusMessage=TEXT("Choose a clear position inside this territory.");ClosePopup();
            },[this,Id](){return !Battle()&&Bool(Building(Id),TEXT("canMove"));})];
            Details->AddSlot().AutoHeight().Padding(4,4)[LiveText([this,Id](){return String(Building(Id),TEXT("moveReason"));},11,Red,false,492)];
            Details->AddSlot().AutoHeight().Padding(0,6)[Button(TEXT("Open Map Room"),[this](){ShowPopup(TEXT("MapRoom"));})];
        }
        Details->AddSlot().AutoHeight().Padding(4,8)[Text(String(B,TEXT("description")),11,Muted,false,492)];
        Body->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Details]];
        const bool Collector=Number(B,TEXT("type"))>=1&&Number(B,TEXT("type"))<=4;
        return Frame(Body,560,Collector&&ViewingHome()?690:610);
    }
    int32 BunkerSelectedCount(const FString& Type)const{return BunkerSelection.FindRef(Type);}
    int64 BunkerSelectionCost()const
    {
        int64 Total=0;
        for(const TPair<FString,int32>& Entry:BunkerSelection)
        {
            const int64 Goo=static_cast<int64>(BYMUI::Number(BunkerOption(SelectedBunker,Entry.Key),TEXT("sourceGoo")));
            // MONSTERBUNKERPOPUP.GetCost truncates each species' batch before
            // Transfer adds the batches; do not round each individual monster.
            if(Entry.Value>0)Total+=Goo*Entry.Value/2;
        }
        return Total;
    }
    int64 BunkerSelectionHousing()const
    {
        int64 Total=0;
        for(const TPair<FString,int32>& Entry:BunkerSelection)
            if(Entry.Value>0)Total+=static_cast<int64>(BYMUI::Number(BunkerOption(SelectedBunker,Entry.Key),TEXT("housing")))*Entry.Value;
        return Total;
    }
    FString BunkerSelectionReason()const
    {
        using namespace BYMUI;const auto State=Bunker(SelectedBunker);
        if(!ViewingHome()||!State)return TEXT("Manage a Monster Bunker in your own yard.");
        if(!Bool(State,TEXT("canManage")))return String(State,TEXT("reason"),TEXT("This Bunker cannot be managed right now."));
        int32 Selected=0;
        for(const TPair<FString,int32>& Entry:BunkerSelection)if(Entry.Value>0)
        {
            const auto Option=BunkerOption(SelectedBunker,Entry.Key);Selected+=Entry.Value;
            if(!Option||!Bool(Option,TEXT("canTransfer")))return MonsterName(Entry.Key)+TEXT(": ")+String(Option,TEXT("transferReason"),TEXT("This monster is no longer available."));
            if(Entry.Value>Number(Option,TEXT("ready"))||Entry.Value>Number(Option,TEXT("maxTransfer")))
                return TEXT("Reduce the selected ")+MonsterName(Entry.Key)+TEXT(" quantity, or clear the selection, to match the available monsters, Putty and Bunker space.");
        }
        if(Selected==0)return TEXT("Choose monsters from Housing with the + buttons.");
        if(Number(State,TEXT("used"))+BunkerSelectionHousing()>Number(State,TEXT("capacity")))return TEXT("The selected monsters exceed the Bunker's remaining capacity.");
        if(BunkerSelectionCost()>Number(Object(Home(),TEXT("resources")),TEXT("putty")))return TEXT("Not enough Putty to transfer the selected monsters.");
        return FString();
    }
    bool CanSelectBunkerMonster(const FString& Type)const
    {
        using namespace BYMUI;const auto State=Bunker(SelectedBunker),Option=BunkerOption(SelectedBunker,Type);
        if(!ViewingHome()||!Bool(State,TEXT("canManage"))||!Bool(Option,TEXT("canTransfer")))return false;
        const int32 Selected=BunkerSelectedCount(Type);
        if(Selected>=Number(Option,TEXT("ready"))||Selected>=Number(Option,TEXT("maxTransfer")))return false;
        const int64 Goo=static_cast<int64>(Number(Option,TEXT("sourceGoo")));
        const int64 ExtraCost=Goo*(Selected+1)/2-Goo*Selected/2;
        return Number(State,TEXT("used"))+BunkerSelectionHousing()+Number(Option,TEXT("housing"))<=Number(State,TEXT("capacity"))
            &&BunkerSelectionCost()+ExtraCost<=Number(Object(Home(),TEXT("resources")),TEXT("putty"));
    }
    TSharedRef<SWidget> BunkerSelectionButton(const FString& Type,bool Add)
    {
        return SNew(SBox).WidthOverride(22).HeightOverride(26)
            [SNew(SButton).ButtonStyle(&StoneStyle).ContentPadding(0).IsFocusable(false)
                .ToolTipText(FText::FromString(Add?TEXT("Select one monster for transfer"):TEXT("Remove one monster from the selection")))
                .IsEnabled_Lambda([this,Type,Add](){return Idle()&&(Add?CanSelectBunkerMonster(Type):BunkerSelectedCount(Type)>0);})
                .OnClicked_Lambda([this,Type,Add]()
                {
                    if(Add)
                    {if(CanSelectBunkerMonster(Type))++BunkerSelection.FindOrAdd(Type);}
                    else if(BunkerSelectedCount(Type)>1)--BunkerSelection.FindOrAdd(Type);
                    else BunkerSelection.Remove(Type);
                    return FReply::Handled();
                })
                [SNew(STextBlock).Text(FText::FromString(Add?TEXT("+"):TEXT("-"))).Font(BYMUI::ButtonFont(11))
                    .ColorAndOpacity(BYMUI::White).Justification(ETextJustify::Center)]];
    }
    TSharedRef<SWidget> BunkerReadyRow(const FString& Type)
    {
        using namespace BYMUI;const FString Id=SelectedBunker;
        const auto Monster=MonsterDefinition(Type);
        return SNew(SBox).HeightOverride(44)
            [SNew(SOverlay)
                +SOverlay::Slot()[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/2578.png"))).Visibility(EVisibility::HitTestInvisible)]
                +SOverlay::Slot().Padding(4,2)
                    [SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                            [Image(String(Monster,TEXT("menuTexture"),MonsterArt(Monster,false)),34,36)]
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight()[Text(MonsterName(Type),10,Ink,true)]
                                +SVerticalBox::Slot().AutoHeight()
                                    [SNew(SHorizontalBox)
                                        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
                                            [SNew(STextBlock).Font(Font(9)).Text_Lambda([this,Id,Type]()
                                            {
                                                const double Available=FMath::Max(0.,Number(BunkerOption(Id,Type),TEXT("ready"))-BunkerSelectedCount(Type));
                                                return FText::FromString(TEXT("Ready: ")+Count(Available));
                                            }).ColorAndOpacity_Lambda([this,Id,Type]()
                                                {return Number(BunkerOption(Id,Type),TEXT("ready"))<=BunkerSelectedCount(Type)?Red:Ink;})]
                                        +SHorizontalBox::Slot().AutoWidth()
                                            [SNew(STextBlock).Font(Font(9)).Text_Lambda([this,Type]()
                                                {return FText::FromString(TEXT("Selected: ")+Count(BunkerSelectedCount(Type)));})
                                                .ColorAndOpacity_Lambda([this,Type](){return BunkerSelectedCount(Type)>0?Red:FLinearColor(.55f,.55f,.55f);})]]]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,2,0)[BunkerSelectionButton(Type,false)]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BunkerSelectionButton(Type,true)]]];
    }
    bool BunkerDismissStillCurrent()const
    {
        const auto Option=BunkerOption(DismissBunkerId,DismissBunkerType);
        return ViewingHome()&&SelectedBunker==DismissBunkerId&&Option.IsValid()&&DismissBunkerExpectedStored>0
            &&BYMUI::Number(Option,TEXT("stored"))==DismissBunkerExpectedStored;
    }
    void ConfirmBunkerDismiss(const FString& Type)
    {
        const auto Option=BunkerOption(SelectedBunker,Type);
        if(!ViewingHome()||!BYMUI::Bool(Option,TEXT("canDismiss")))return;
        DismissBunkerId=SelectedBunker;DismissBunkerType=Type;
        DismissBunkerExpectedStored=static_cast<int32>(BYMUI::Number(Option,TEXT("stored")));
        ShowPopup(TEXT("DismissBunker"));
    }
    TSharedRef<SWidget> BunkerStoredRow(const FString& Type)
    {
        using namespace BYMUI;const FString Id=SelectedBunker;const auto Monster=MonsterDefinition(Type);
        return SNew(SBox).HeightOverride(44)
            [SNew(SOverlay)
                +SOverlay::Slot()[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/2583.png"))).Visibility(EVisibility::HitTestInvisible)]
                +SOverlay::Slot().Padding(4,2)
                    [SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                            [Image(String(Monster,TEXT("menuTexture"),MonsterArt(Monster,false)),34,36)]
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight()[Text(MonsterName(Type),10,Ink,true)]
                                +SVerticalBox::Slot().AutoHeight()[LiveText([this,Id,Type]()
                                    {return TEXT("Stored: ")+Count(Number(BunkerOption(Id,Type),TEXT("stored")));},10,Muted)]]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3,0,0,0)
                            [SNew(SBox).WidthOverride(56).HeightOverride(28)
                                [SNew(SButton).ButtonStyle(&StoneStyle).ContentPadding(2).IsFocusable(false)
                                    .ToolTipText_Lambda([this,Id,Type]()
                                    {
                                        const auto Option=BunkerOption(Id,Type);
                                        return FText::FromString(Bool(Option,TEXT("canDismiss"))?FString(TEXT("Remove one monster permanently. No refund or return to Housing.")):String(Option,TEXT("dismissReason")));
                                    })
                                    .IsEnabled_Lambda([this,Id,Type](){return Idle()&&ViewingHome()&&Bool(BunkerOption(Id,Type),TEXT("canDismiss"));})
                                    .OnClicked_Lambda([this,Type](){ConfirmBunkerDismiss(Type);return FReply::Handled();})
                                    [SNew(STextBlock).Text(FText::FromString(TEXT("Remove"))).Font(ButtonFont(9)).ColorAndOpacity(White).Justification(ETextJustify::Center)]]]]];
    }
    TSharedRef<SWidget> BunkerCapacityBar()
    {
        // MONSTERBUNKERPOPUP.mcStorage uses the original background/bars/frame
        // at 0.3550415 horizontal scale (535 source pixels -> about 190).
        using namespace BYMUI;
        TSharedRef<SOverlay> Layers=SNew(SOverlay);
        Layers->AddSlot()[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/236.png"))).Visibility(EVisibility::HitTestInvisible)];
        for(int32 Layer=0;Layer<2;++Layer)
        {
            const bool IncludeSelection=Layer==0;
            Layers->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Center)
                [SNew(SBox).HeightOverride(30).Visibility(EVisibility::HitTestInvisible)
                    .WidthOverride_Lambda([this,IncludeSelection]()
                    {
                        const auto State=Bunker(SelectedBunker);const double Capacity=BYMUI::Number(State,TEXT("capacity"));
                        const double Used=BYMUI::Number(State,TEXT("used"))+(IncludeSelection?BunkerSelectionHousing():0);
                        return FOptionalSize(static_cast<float>(Capacity>0?190.*FMath::Clamp(Used/Capacity,0.,1.):0.));
                    })
                    [SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/1253.png")))
                        .ColorAndOpacity(FLinearColor(1.f,1.f,1.f,IncludeSelection?.4f:1.f))]];
        }
        Layers->AddSlot()[SNew(SImage).Image(ImageBrush(TEXT("source_client/shapes/1620.png"))).Visibility(EVisibility::HitTestInvisible)];
        Layers->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [LiveText([this]()
            {
                const auto State=Bunker(SelectedBunker);
                return Count(Number(State,TEXT("used"))+BunkerSelectionHousing())+TEXT(" / ")+Count(Number(State,TEXT("capacity")));
            },10,Ink,true)];
        return SNew(SBox).WidthOverride(190).HeightOverride(32).Clipping(EWidgetClipping::ClipToBounds)[Layers];
    }
    TSharedRef<SWidget> BunkerDialog()
    {
        using namespace BYMUI;const FString Id=SelectedBunker;
        TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Monster Bunker"));
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("Housing"),15,Ink,true)]
                +SHorizontalBox::Slot().AutoWidth().Padding(12,0).VAlign(VAlign_Center)[Text(TEXT("Capacity"),11,Ink,true)]
                +SHorizontalBox::Slot().AutoWidth()[BunkerCapacityBar()]];
        TSharedRef<SScrollBox> Ready=SNew(SScrollBox),Stored=SNew(SScrollBox);
        int32 ReadySpecies=0,StoredSpecies=0;
        TArray<TSharedPtr<FJsonObject>> Options;
        for(const auto& Value:Array(Bunker(Id),TEXT("options")))
            if(Value.IsValid()&&Value->Type==EJson::Object)Options.Add(Value->AsObject());
        Options.Sort([](const TSharedPtr<FJsonObject>& A,const TSharedPtr<FJsonObject>& B)
            {return FCString::Atoi(*BYMUI::String(A,TEXT("type")).Mid(1))<FCString::Atoi(*BYMUI::String(B,TEXT("type")).Mid(1));});
        for(const auto& Option:Options)
        {
            const FString Type=String(Option,TEXT("type"));
            // A depleted species stays visible while it has a local selection,
            // so its minus button can correct stale quantities without silently
            // rewriting an in-flight transfer or requiring a complete reset.
            if(Number(Option,TEXT("ready"))>0||BunkerSelectedCount(Type)>0){Ready->AddSlot().Padding(0,0,3,4)[BunkerReadyRow(Type)];++ReadySpecies;}
            if(Number(Option,TEXT("stored"))>0){Stored->AddSlot().Padding(0,0,3,4)[BunkerStoredRow(Type)];++StoredSpecies;}
        }
        if(ReadySpecies==0)Ready->AddSlot().Padding(10,25)[Text(TEXT("No eligible monsters in Housing. Train monsters in your Hatchery first."),12,Muted,false,240)];
        if(StoredSpecies==0)Stored->AddSlot().Padding(10,25)[Text(TEXT("No monsters stored. Select monsters from Housing and transfer them into this Bunker."),12,Muted,false,234)];
        TSharedRef<SVerticalBox> Transfer=SNew(SVerticalBox);
        Transfer->AddSlot().AutoHeight().Padding(3,0,3,7)
            [LiveText([this](){const int64 CostValue=BunkerSelectionCost();return CostValue>0?Count(CostValue)+TEXT("\nPutty"):FString(TEXT("Transfer"));},12,Ink,true)];
        Transfer->AddSlot().AutoHeight()[Button(TEXT(">>"),[this]()
        {
            if(!BunkerSelectionReason().IsEmpty())return;
            TSharedRef<FJsonObject> Request=MakeShared<FJsonObject>(),Selection=MakeShared<FJsonObject>();
            Request->SetStringField(TEXT("building"),SelectedBunker);
            for(const TPair<FString,int32>& Entry:BunkerSelection)if(Entry.Value>0)Selection->SetNumberField(Entry.Key,Entry.Value);
            Request->SetObjectField(TEXT("selection"),Selection);Command(TEXT("bunkerTransfer"),Request);
            // Clear only after the host accepts. A failed or delayed request
            // leaves the visible selection available for correction/retry.
        },[this](){return BunkerSelectionReason().IsEmpty();})];
        Transfer->AddSlot().AutoHeight().Padding(0,8,0,0)[Button(TEXT("Clear"),[this](){BunkerSelection.Reset();},[this](){return BunkerSelection.Num()>0;})];
        Body->AddSlot().FillHeight(1)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(284)[Ready]]
                +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(10,0)[SNew(SBox).WidthOverride(84)[Transfer]]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(278)[Stored]]];
        Body->AddSlot().AutoHeight().Padding(0,10,0,4)[LiveText([this]()
        {
            const FString Reason=BunkerSelectionReason();
            return Reason.IsEmpty()?TEXT("Ready to transfer. These monsters will defend this Bunker during an invasion."):Reason;
        },11,Muted,false,700)];
        Body->AddSlot().AutoHeight().Padding(0,3,0,8)[Text(TEXT("Transferred monsters leave Housing. Removing a stored monster is permanent and gives no refund."),10,Muted,false,700)];
        Body->AddSlot().AutoHeight()
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Building details"),[this,Id](){SelectedBuilding=Id;if(PC.IsValid())PC->SelectedBuilding=Id;ShowPopup(TEXT("Building"));})]
                +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Open Hatchery"),[this](){ShowArmy(0);})]];
        return Frame(Body,750,560);
    }
    TSharedRef<SWidget> DismissBunkerDialog()
    {
        using namespace BYMUI;TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Remove monster?"));
        TSharedRef<SVerticalBox> Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().Padding(3,12)[Text(TEXT("Permanently remove one ")+MonsterName(DismissBunkerType)+TEXT(" from this Bunker?"),14,Ink,true,492)];
        Details->AddSlot().AutoHeight().Padding(3,6)[Text(TEXT("The monster will not return to Housing. No Putty, Goo or other resources are refunded."),12,Muted,false,492)];
        Details->AddSlot().AutoHeight().Padding(3,10)[LiveText([this]()
        {
            if(!BunkerDismissStillCurrent())return FString(TEXT("This garrison has changed. Go back and select the monster again."));
            return String(BunkerOption(DismissBunkerId,DismissBunkerType),TEXT("dismissReason"));
        },11,Red,false,492)];
        Body->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Details]];
        Body->AddSlot().AutoHeight()
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Go back"),[this](){ShowPopup(TEXT("Bunker"));})]
                +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Remove one"),[this]()
                {
                    if(!BunkerDismissStillCurrent())return;
                    TSharedRef<FJsonObject> Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("building"),DismissBunkerId);
                    Request->SetStringField(TEXT("type"),DismissBunkerType);Request->SetNumberField(TEXT("count"),1);
                    Request->SetNumberField(TEXT("expectedStored"),DismissBunkerExpectedStored);Command(TEXT("bunkerDismiss"),Request);
                },[this](){return BunkerDismissStillCurrent()&&Bool(BunkerOption(DismissBunkerId,DismissBunkerType),TEXT("canDismiss"));})]];
        return Frame(Body,560,340);
    }
    FString MonsterState(const FString& Id)const
    {
        using namespace BYMUI;const auto O=MonsterOption(Id);
        const double Unlock=Number(O,TEXT("unlockReadyAt"));
        const double Research=Number(O,TEXT("researchReadyAt"));
        if(Unlock>0)return Unlock>ServerNow()?TEXT("Unlocking: ")+Duration(Unlock-ServerNow()):TEXT("Finishing unlock...");
        if(Research>0)return TEXT("Level ")+Count(Number(O,TEXT("level")))+TEXT(" - ")
            +(Research>ServerNow()?TEXT("researching: ")+Duration(Research-ServerNow()):TEXT("finishing research..."));
        return Bool(O,TEXT("unlocked"))?TEXT("Unlocked - Level ")+Count(Number(O,TEXT("level"))):TEXT("Locked");
    }
    FString MonsterStatValue(const FString& Id,const FString& Key,bool Next=false)const
    {
        using namespace BYMUI;const auto Stats=MonsterStats(Id,Next);
        if(!Stats||!Stats->HasField(*Key))return TEXT("-");
        const double Value=Number(Stats,*Key);
        if(Key==TEXT("time"))return Duration(Value);
        return Key==TEXT("speed")?Decimal(Value):Count(Value);
    }
    FString TrainingReason(const FString& Id)const
    {
        using namespace BYMUI;
        if(!SelectedHatcheryState())return TEXT("Complete a Hatchery to begin hatching monsters.");
        const auto O=HatcheryTrainOption(Id);
        if(!Bool(O,TEXT("canTrain")))return String(O,TEXT("reason"),TEXT("This Hatchery cannot queue this monster."));
        const int32 N=PC.IsValid()?PC->SelectedMonsterCount:1;
        const int32 Max=static_cast<int32>(Number(O,TEXT("maxTrainCount")));
        return N>Max?TEXT("This Hatchery can currently queue up to ")+Count(Max)+TEXT(" of this monster."):FString();
    }
    TSharedRef<SWidget> MonsterStatsTable(const FString& Id,bool Compare)
    {
        using namespace BYMUI;TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
        if(Compare)
            Rows->AddSlot().AutoHeight().Padding(0,0,0,5)
                [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Text(TEXT("Monster stats"),11,Ink,true)]
                    +SHorizontalBox::Slot().FillWidth(.8f)[Text(TEXT("Before"),11,Ink,true)]
                    +SHorizontalBox::Slot().FillWidth(.8f)[Text(TEXT("After"),11,Green,true)]];
        const TCHAR* Keys[]={TEXT("hp"),TEXT("damage"),TEXT("speed"),TEXT("goo"),TEXT("housing"),TEXT("time")};
        const TCHAR* Labels[]={TEXT("Health"),TEXT("Damage"),TEXT("Speed"),TEXT("Goo cost"),TEXT("Housing"),TEXT("Hatch time")};
        for(int32 I=0;I<6;++I)
        {
            const FString Key=Keys[I];TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox);
            Row->AddSlot().FillWidth(1)[Text(Labels[I],11,Muted)];
            Row->AddSlot().FillWidth(.8f)[LiveText([this,Id,Key](){return MonsterStatValue(Id,Key);},11,Ink)];
            if(Compare)Row->AddSlot().FillWidth(.8f)[LiveText([this,Id,Key](){return MonsterStatValue(Id,Key,true);},11,Green)];
            Rows->AddSlot().AutoHeight().Padding(0,3)[Row];
        }
        return Rows;
    }
    TSharedRef<SWidget> MonsterDetails()
    {
        using namespace BYMUI;const FString Id=InspectedMonster;const auto M=MonsterDefinition(Id);
        const float DetailWrap=ArmyTab==0?840.f:ArmyTab==1?550.f:780.f;
        TSharedRef<SVerticalBox> Body=SNew(SVerticalBox);
        if(!M){Body->AddSlot().AutoHeight()[Text(TEXT("Monster information is loading..."),12,BYMUI::Ink,false,DetailWrap)];return Body;}
        Body->AddSlot().AutoHeight().Padding(0,0,0,6)
            [LiveText([this,Id](){return MonsterName(Id)+TEXT("   ")+MonsterState(Id);},16,Ink,true,DetailWrap)];
        TSharedRef<SHorizontalBox> Info=SNew(SHorizontalBox);
        const FString Portrait=MonsterArt(M,true);
        Info->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0,0,14,0)[Image(Portrait,165,150)];
        Info->AddSlot().FillWidth(1).VAlign(VAlign_Top)[MonsterStatsTable(Id,ArmyTab==2)];
        Body->AddSlot().AutoHeight()[Info];
        Body->AddSlot().AutoHeight().Padding(0,7)[Text(PlainText(String(M,TEXT("descriptionPlain"),String(M,TEXT("description")))),10,Muted,false,DetailWrap)];
        if(ArmyTab==0)
        {
            Body->AddSlot().AutoHeight().Padding(0,0,0,5)[LiveText([this,Id]()
            {
                const auto Stats=MonsterStats(Id);const int32 N=PC.IsValid()?PC->SelectedMonsterCount:1;
                return TEXT("Ready: ")+Count(Number(Object(Home(),TEXT("army")),*Id))+TEXT("   Batch: ")
                    +Count(Number(Stats,TEXT("goo"))*N)+TEXT(" Goo / ")+Count(Number(Stats,TEXT("housing"))*N)
                    +TEXT(" housing / ")+Duration(Number(Stats,TEXT("time"))*N);
            },11,Green,true,DetailWrap)];
            TSharedRef<SHorizontalBox> Train=SNew(SHorizontalBox);
            Train->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)[Text(TEXT("Quantity"),11)];
            Train->AddSlot().AutoWidth().Padding(0,0,10,0)
                [SNew(SBox).WidthOverride(72)[SNew(SSpinBox<int32>).MinValue(1).MaxValue(100)
                    .Value_Lambda([this](){return PC.IsValid()?PC->SelectedMonsterCount:1;})
                    .OnValueChanged_Lambda([this](int32 N){if(PC.IsValid())PC->SelectedMonsterCount=N;})]];
            Train->AddSlot().FillWidth(1)[Button(TEXT("Train"),[this,Id]()
            {
                if(!PC.IsValid())return;TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();
                R->SetStringField(TEXT("type"),Id);R->SetNumberField(TEXT("count"),PC->SelectedMonsterCount);
                R->SetStringField(TEXT("building"),SelectedHatchery);Command(TEXT("train"),R);
            },[this,Id](){return ViewingHome()&&!Battle()&&BYMUI::Bool(HatcheryTrainOption(Id),TEXT("canTrain"))&&TrainingReason(Id).IsEmpty();})];
            Train->AddSlot().AutoWidth().Padding(8,0,0,0)
                [SNew(SBox).Visibility_Lambda([this,Id](){return Bool(MonsterOption(Id),TEXT("unlocked"))?EVisibility::Collapsed:EVisibility::Visible;})
                    [Button(TEXT("Open Locker"),[this,Id](){ShowArmy(1,Id);})]];
            Body->AddSlot().AutoHeight()[Train];
            Body->AddSlot().AutoHeight().Padding(0,5)[LiveText([this,Id](){return TrainingReason(Id);},11,Red,false,DetailWrap)];
        }
        else
        {
            const bool Research=ArmyTab==2;
            Body->AddSlot().AutoHeight().Padding(0,0,0,5)[LiveText([this,Id,Research]()
            {
                const auto O=MonsterOption(Id);
                if(Research&&!Bool(O,TEXT("unlocked")))return FString(TEXT("Unlock this monster in the Monster Locker first."));
                if(Research&&Number(O,TEXT("level"))>=Number(O,TEXT("maxLevel"),1))return FString(TEXT("Maximum monster level reached."));
                if(!Research&&Bool(O,TEXT("unlocked")))return FString(TEXT("This monster is unlocked and can be trained in your Hatchery."));
                FString Result=Count(Number(O,Research?TEXT("researchCost"):TEXT("unlockCost")))+TEXT(" Putty   ")
                    +Duration(Number(O,Research?TEXT("researchSeconds"):TEXT("unlockSeconds")));
                const double Required=Number(O,Research?TEXT("requiredAcademyLevel"):TEXT("requiredLockerLevel"));
                if(Required>0)Result+=Research?TEXT("   Academy Level ")+Count(Required):TEXT("   Locker Level ")+Count(Required);
                return Result;
            },11,Ink,true,DetailWrap)];
            Body->AddSlot().AutoHeight().Padding(0,3)[LiveText([this,Id,Research]()
            {
                const auto O=MonsterOption(Id);const double End=Number(O,Research?TEXT("researchReadyAt"):TEXT("unlockReadyAt"));
                if(End>0)return End>ServerNow()?TEXT("Completes in ")+Duration(End-ServerNow()):FString(TEXT("Finishing..."));
                return String(O,Research?TEXT("researchReason"):TEXT("unlockReason"));
            },11,Red,false,DetailWrap)];
            TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox);
            Actions->AddSlot().FillWidth(1)
                [SNew(SBox).Visibility_Lambda([this,Id,Research]()
                    {
                        const auto O=MonsterOption(Id);
                        return Number(O,Research?TEXT("researchReadyAt"):TEXT("unlockReadyAt"))>0
                            ||(Research?!Bool(O,TEXT("unlocked")):Bool(O,TEXT("unlocked")))?EVisibility::Collapsed:EVisibility::Visible;
                    })
                    [Button(Research?TEXT("Start research"):TEXT("Unlock monster"),[this,Id,Research](){MonsterCommand(Research?TEXT("research"):TEXT("unlock"),Id);},
                        [this,Id,Research](){return ViewingHome()&&!Battle()&&Bool(MonsterOption(Id),Research?TEXT("canResearch"):TEXT("canUnlock"));})]];
            Actions->AddSlot().FillWidth(1)
                [SNew(SBox).Visibility_Lambda([this,Id,Research](){return Number(MonsterOption(Id),Research?TEXT("researchReadyAt"):TEXT("unlockReadyAt"))>0?EVisibility::Visible:EVisibility::Collapsed;})
                    [Button(Research?TEXT("Cancel research"):TEXT("Cancel unlock"),[this,Id,Research](){MonsterCommand(Research?TEXT("cancelResearch"):TEXT("cancelUnlock"),Id);},
                        [this,Id,Research](){return ViewingHome()&&!Battle()&&Number(MonsterOption(Id),Research?TEXT("researchReadyAt"):TEXT("unlockReadyAt"))>ServerNow();})]];
            Body->AddSlot().AutoHeight().Padding(0,5)[Actions];
            Body->AddSlot().AutoHeight()
                [SNew(SBox).Visibility_Lambda([this,Id,Research]()
                    {return (Research?!Bool(MonsterOption(Id),TEXT("unlocked")):Bool(MonsterOption(Id),TEXT("unlocked")))?EVisibility::Visible:EVisibility::Collapsed;})
                    [Button(Research?TEXT("Open Monster Locker"):TEXT("Open Hatchery"),[this,Id,Research](){ShowArmy(Research?1:0,Id);})]];
            Body->AddSlot().AutoHeight()[LiveText([this,Id,Research]()
            {
                return Number(MonsterOption(Id),Research?TEXT("researchReadyAt"):TEXT("unlockReadyAt"))>0
                    ?FString(TEXT("Canceling returns the Putty spent, up to your storage capacity. Progress is lost.")):FString();
            },10,Muted,false,DetailWrap)];
        }
        return SNew(SScrollBox)+SScrollBox::Slot()[Body];
    }
    void SelectMonster(const FString& Id)
    {
        InspectedMonster=Id;
        if(MonsterDetailsHost)MonsterDetailsHost->SetContent(MonsterDetails());
    }
    FString MonsterWorkSummary()const
    {
        using namespace BYMUI;TArray<FString> Work;
        const auto Unlock=Object(Home(),TEXT("monsterUnlock"));
        const FString UnlockType=String(Unlock,TEXT("type"));
        if(!UnlockType.IsEmpty())
        {
            const double Remaining=Number(Unlock,TEXT("readyAt"))-ServerNow();
            Work.Add(TEXT("Unlocking ")+MonsterName(UnlockType)+TEXT(": ")+(Remaining>0?Duration(Remaining):TEXT("finishing...")));
        }
        for(const auto& V:Array(Home(),TEXT("monsterResearch")))
        {
            if(!V.IsValid()||V->Type!=EJson::Object)continue;
            const auto Q=V->AsObject();const double Remaining=Number(Q,TEXT("readyAt"))-ServerNow();
            Work.Add(TEXT("Researching ")+MonsterName(String(Q,TEXT("type")))+TEXT(" Level ")+Count(Number(Q,TEXT("toLevel")))
                +TEXT(": ")+(Remaining>0?Duration(Remaining):TEXT("finishing...")));
        }
        return Work.IsEmpty()?TEXT("Unlock new monsters in the Locker and improve their levels at the Academy."):FString::Join(Work,TEXT("\n"));
    }
    FString HatcheryTitle(const FString& Id)const
    {
        using namespace BYMUI;int32 Index=0;
        for(const auto& Value:Array(Home(),TEXT("hatcheries")))
        {
            if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
            ++Index;const auto H=Value->AsObject();
            if(String(H,TEXT("building"))==Id)
                return TEXT("Hatchery ")+Count(Index)+TEXT(" - Level ")+Count(Number(H,TEXT("level")));
        }
        return TEXT("Hatchery");
    }
    FString HatcheryStatus(const FString& Id)const
    {
        using namespace BYMUI;const auto H=Hatchery(Id);
        if(!H)return TEXT("Complete a Hatchery to begin production.");
        const FString State=String(H,TEXT("state"));
        if(State==TEXT("housing_full"))return TEXT("Hatched monster is waiting for room in Housing.");
        const FString Reason=String(H,TEXT("pauseReason"));
        if(!Reason.IsEmpty())return Reason;
        if(State==TEXT("hatching"))return TEXT("Hatching");
        if(State==TEXT("idle"))return TEXT("Ready to hatch monsters");
        if(State==TEXT("damaged"))return TEXT("Paused - repair this Hatchery to at least half health.");
        if(State==TEXT("upgrading"))return TEXT("Paused while this Hatchery upgrades.");
        if(State==TEXT("construction"))return TEXT("This Hatchery is still under construction.");
        if(State==TEXT("repairing"))return TEXT("Hatching is paused while repairs restore this Hatchery.");
        return TEXT("Waiting for the next host update");
    }
    TSharedPtr<FJsonObject> HatcheryEntry(const FString& BuildingId,const FString& EntryId,bool Active)const
    {
        using namespace BYMUI;const auto H=Hatchery(BuildingId);
        if(Active)
        {
            const auto A=Object(H,TEXT("active"));
            return String(A,TEXT("id"))==EntryId?A:nullptr;
        }
        for(const auto& Value:Array(H,TEXT("queue")))
            if(Value.IsValid()&&Value->Type==EJson::Object&&String(Value->AsObject(),TEXT("id"))==EntryId)return Value->AsObject();
        return nullptr;
    }
    void CancelHatcheryEntry(const FString& BuildingId,const FString& EntryId,bool Active)
    {
        const auto Entry=HatcheryEntry(BuildingId,EntryId,Active);
        if(!Entry)return;
        TSharedRef<FJsonObject> Request=MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("building"),BuildingId);Request->SetStringField(TEXT("entry"),EntryId);
        Request->SetNumberField(TEXT("expectedCount"),BYMUI::Number(Entry,TEXT("count"),Active?1:0));
        Command(TEXT("cancelTraining"),Request);
    }
    TSharedRef<SWidget> HatcherySelector()
    {
        using namespace BYMUI;TSharedRef<SHorizontalBox> Choices=SNew(SHorizontalBox);
        for(const auto& Value:Array(Home(),TEXT("hatcheries")))
        {
            if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
            const FString Id=String(Value->AsObject(),TEXT("building"));
            if(Id.IsEmpty())continue;
            Choices->AddSlot().FillWidth(1).Padding(2,0)
                [Button((SelectedHatchery==Id?TEXT("[ "):TEXT(""))+HatcheryTitle(Id)+(SelectedHatchery==Id?TEXT(" ]"):TEXT("")),
                    [this,Id](){SelectedHatchery=Id;bRefreshPopup=true;})];
        }
        return SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Choices]
            +SVerticalBox::Slot().AutoHeight().Padding(3,4,3,7)
                [LiveText([this](){return HatcheryStatus(SelectedHatchery);},10,Muted,false,840)];
    }
    TSharedRef<SWidget> HatcheryQueueCard(const FString& BuildingId,const TSharedPtr<FJsonObject>& Entry,bool Active)
    {
        using namespace BYMUI;const FString EntryId=String(Entry,TEXT("id")),Type=String(Entry,TEXT("type"));
        TSharedRef<SVerticalBox> Card=SNew(SVerticalBox);
        Card->AddSlot().AutoHeight().HAlign(HAlign_Center)
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)[Image(MonsterArt(MonsterDefinition(Type),false),35,32)]
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [LiveText([this,BuildingId,EntryId,Type,Active]()
                    {
                        const auto Item=HatcheryEntry(BuildingId,EntryId,Active);
                        return MonsterName(Type)+(Active?FString():TEXT(" x")+Count(Number(Item,TEXT("count"))));
                    },10,Ink,true,100)]];
        Card->AddSlot().AutoHeight().Padding(0,3)[LiveText([this,BuildingId,EntryId,Active]()
        {
            const auto Item=HatcheryEntry(BuildingId,EntryId,Active);
            if(!Item)return FString(TEXT("Updating..."));
            if(!Active)return FString(TEXT("Waiting"));
            const auto H=Hatchery(BuildingId);const FString State=String(H,TEXT("state"));
            if(State==TEXT("housing_full"))return FString(TEXT("Ready - Housing full"));
            const double End=Number(Item,TEXT("readyAt"));
            const double Remaining=End>0?FMath::Max(0.,End-ServerNow()):Number(Item,TEXT("remaining"));
            return End>0?(Remaining>0?Duration(Remaining):FString(TEXT("Finishing..."))):TEXT("Paused: ")+Duration(Remaining);
        },10,Active?Green:Muted,false,140)];
        Card->AddSlot().AutoHeight()
            [Button(Active?TEXT("Cancel"):TEXT("Remove 1"),[this,BuildingId,EntryId,Active](){CancelHatcheryEntry(BuildingId,EntryId,Active);},
                [this,BuildingId,EntryId,Active](){return ViewingHome()&&!Battle()&&!BYMUI::Bool(Home(),TEXT("underAttack"))&&HatcheryEntry(BuildingId,EntryId,Active).IsValid();})];
        Card->AddSlot().AutoHeight().Padding(0,3,0,0)[LiveText([this,BuildingId,EntryId,Active]()
        {return TEXT("Refund: ")+Count(Number(HatcheryEntry(BuildingId,EntryId,Active),TEXT("refundGoo")))+TEXT(" Goo");},9,Muted,false,140)];
        return SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Active?FLinearColor(.77f,.77f,.52f,1):Paper).Padding(7)
            [SNew(SBox).WidthOverride(140)[Card]];
    }
    TSharedRef<SWidget> HatcheryQueue()
    {
        using namespace BYMUI;const FString Id=SelectedHatchery;const auto H=SelectedHatcheryState();
        TSharedRef<SVerticalBox> Queue=SNew(SVerticalBox);
        Queue->AddSlot().AutoHeight().Padding(0,0,0,4)[LiveText([this,Id]()
        {
            const auto Current=Hatchery(Id);
            return TEXT("Hatching queue   ")+Count(Array(Current,TEXT("queue")).Num())+TEXT(" / ")
                +Count(Number(Current,TEXT("queueCapacity")))+TEXT(" waiting groups   (up to ")
                +Count(Number(Current,TEXT("groupCapacity"),20))+TEXT(" per group)");
        },11,Ink,true,840)];
        const auto Active=Object(H,TEXT("active"));const auto Waiting=Array(H,TEXT("queue"));
        if(String(Active,TEXT("id")).IsEmpty()&&Waiting.IsEmpty())
            Queue->AddSlot().AutoHeight().Padding(0,2)[Text(TEXT("No monsters queued. Choose an unlocked monster and Train. Housing is checked when each monster finishes hatching."),10,Muted,false,840)];
        else
        {
            TSharedRef<SHorizontalBox> Cards=SNew(SHorizontalBox);
            if(!String(Active,TEXT("id")).IsEmpty())Cards->AddSlot().AutoWidth().Padding(0,0,6,0)[HatcheryQueueCard(Id,Active,true)];
            for(const auto& Value:Waiting)
                if(Value.IsValid()&&Value->Type==EJson::Object)Cards->AddSlot().AutoWidth().Padding(0,0,6,0)[HatcheryQueueCard(Id,Value->AsObject(),false)];
            Queue->AddSlot().AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal)+SScrollBox::Slot()[Cards]];
            Queue->AddSlot().AutoHeight().Padding(0,3)[Text(TEXT("Cancel returns one monster's current Goo cost, limited by available storage."),9,Muted,false,840)];
        }
        return Queue;
    }

    TSharedRef<SWidget> ArmyDialog()
    {
        using namespace BYMUI;
        const float BodyWrap=ArmyTab==0?840.f:780.f;
        ResolveHatcherySelection();
        const auto Entries=MonsterEntries();
        if(!Entries.IsEmpty()&&!Entries.ContainsByPredicate([this](const TSharedPtr<FJsonObject>& M){return BYMUI::String(M,TEXT("id"))==InspectedMonster;}))
            InspectedMonster=String(Entries[0],TEXT("id"));
        const TCHAR* Titles[]={TEXT("Hatchery"),TEXT("Monster Locker"),TEXT("Monster Academy")};
        TSharedRef<SVerticalBox> Body=DialogBody(Titles[ArmyTab]);
        TSharedRef<SHorizontalBox> Tabs=SNew(SHorizontalBox);
        for(int32 I=0;I<3;++I)
            Tabs->AddSlot().FillWidth(1).Padding(2,0)
                [Button((ArmyTab==I?TEXT("[ "):TEXT(""))+FString(Titles[I])+(ArmyTab==I?TEXT(" ]"):TEXT("")),[this,I](){ShowArmy(I);})];
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)[Tabs];
        Body->AddSlot().AutoHeight().Padding(3,0,3,8)[LiveText([this]()
        {
            return TEXT("Housing: ")+Count(Number(Home(),TEXT("housingUsed")))+TEXT(" / ")+Count(Number(Home(),TEXT("housingCapacity")))
                +TEXT("   Queued housing: ")+Count(Number(Home(),TEXT("housingQueued")))+TEXT(" (not reserved)");
        },11,Ink,true,BodyWrap)];
        if(Entries.IsEmpty())
        {
            Body->AddSlot().FillHeight(1)[Text(TEXT("Monster information is loading. Your yard must be connected before training or research can begin."),12,Ink,false,780)];
            return Frame(Body,840,720);
        }
        if(ArmyTab==0)
        {
            Body->AddSlot().AutoHeight()[HatcherySelector()];
            // The source Hatchery arranges monster icons in nine columns, above
            // the selected portrait and production statistics.
            TSharedRef<SUniformGridPanel> Icons=SNew(SUniformGridPanel).SlotPadding(FMargin(3));
            for(int32 I=0;I<Entries.Num();++I)
            {
                const auto M=Entries[I];const FString Id=String(M,TEXT("id"));
                Icons->AddSlot(I%9,I/9)
                    [SNew(SBorder).BorderImage(WhiteBrush()).Padding(2)
                        .BorderBackgroundColor_Lambda([this,Id](){return InspectedMonster==Id?Gold:Paper;})
                        [SNew(SButton).ButtonStyle(&CardStyle).ContentPadding(1).IsFocusable(false)
                            .ToolTipText_Lambda([this,Id](){return FText::FromString(MonsterName(Id)+TEXT("\n")+MonsterState(Id));})
                            .OnClicked_Lambda([this,Id](){SelectMonster(Id);return FReply::Handled();})
                            [SNew(SBox).WidthOverride(65).HeightOverride(50)
                                [SNew(SImage).Image(ImageBrush(MonsterArt(M,false)))
                                    .ColorAndOpacity_Lambda([this,Id](){return Bool(MonsterOption(Id),TEXT("unlocked"))?White:FLinearColor(.48f,.48f,.48f,1.f);})]]]];
            }
            Body->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,8)[Icons];
            Body->AddSlot().FillHeight(1)[SAssignNew(MonsterDetailsHost,SBox)[MonsterDetails()]];
            Body->AddSlot().AutoHeight().Padding(0,8,0,0)[HatcheryQueue()];
        }
        else if(ArmyTab==1)
        {
            // Locker pages and their source ordering are retained instead of
            // flattening every unlock into the Hatchery's production list.
            int32 Pages=1;for(const auto& M:Entries)Pages=FMath::Max(Pages,static_cast<int32>(Number(M,TEXT("sourcePage"),1)));
            LockerPage=FMath::Clamp(LockerPage,1,Pages);
            TSharedRef<SVerticalBox> List=SNew(SVerticalBox);
            bool FoundOnPage=false;
            for(const auto& M:Entries)if(Number(M,TEXT("sourcePage"),1)==LockerPage&&String(M,TEXT("id"))==InspectedMonster)FoundOnPage=true;
            if(!FoundOnPage)for(const auto& M:Entries)if(Number(M,TEXT("sourcePage"),1)==LockerPage){InspectedMonster=String(M,TEXT("id"));break;}
            for(const auto& M:Entries)
            {
                if(Number(M,TEXT("sourcePage"),1)!=LockerPage)continue;
                const FString Id=String(M,TEXT("id"));
                List->AddSlot().AutoHeight().Padding(0,0,0,7)
                    [SNew(SBorder).BorderImage(WhiteBrush()).Padding(2)
                        .BorderBackgroundColor_Lambda([this,Id](){return InspectedMonster==Id?Gold:Paper;})
                        [SNew(SButton).ButtonStyle(&CardStyle).ContentPadding(6).IsFocusable(false)
                            .OnClicked_Lambda([this,Id](){SelectMonster(Id);return FReply::Handled();})
                            [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Text(String(M,TEXT("name")),12,Ink,true,190)]
                                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[LiveText([this,Id](){return MonsterState(Id);},10,Muted,false,190)]]]];
            }
            List->AddSlot().FillHeight(1)[SNew(SBox)];
            List->AddSlot().AutoHeight()[LiveText([this](){return TEXT("Locker page ")+Count(LockerPage);},10,Muted)];
            List->AddSlot().AutoHeight().Padding(0,5)
                [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)
                    [Button(TEXT("Previous"),[this](){--LockerPage;bRefreshPopup=true;},[this](){return LockerPage>1;})]
                    +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Next"),[this](){++LockerPage;bRefreshPopup=true;},[this,Pages](){return LockerPage<Pages;})]];
            Body->AddSlot().FillHeight(1)
                [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)[SNew(SBox).WidthOverride(210)[List]]
                    +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(MonsterDetailsHost,SBox)[MonsterDetails()]]];
        }
        else
        {
            auto ChangeMonster=[this](int32 Step)
            {
                const auto Items=MonsterEntries();if(Items.IsEmpty())return;
                int32 Index=Items.IndexOfByPredicate([this](const TSharedPtr<FJsonObject>& M){return BYMUI::String(M,TEXT("id"))==InspectedMonster;});
                Index=FMath::Clamp(Index+Step,0,Items.Num()-1);SelectMonster(BYMUI::String(Items[Index],TEXT("id")));
            };
            Body->AddSlot().AutoHeight().Padding(0,0,0,10)
                [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Previous monster"),[ChangeMonster](){ChangeMonster(-1);},[this,Entries](){return InspectedMonster!=String(Entries[0],TEXT("id"));})]
                    +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[Text(TEXT("Compare levels before you research"),11,Muted,false,310)]
                    +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Next monster"),[ChangeMonster](){ChangeMonster(1);},[this,Entries](){return InspectedMonster!=String(Entries.Last(),TEXT("id"));})]];
            Body->AddSlot().FillHeight(1)[SAssignNew(MonsterDetailsHost,SBox)[MonsterDetails()]];
        }
        Body->AddSlot().AutoHeight().Padding(0,8,0,0)
            [SNew(SBox).MaxDesiredHeight(48)[SNew(SScrollBox)+SScrollBox::Slot()
                [LiveText([this](){return PC.IsValid()&&PC->IsCommandPending()?FString(TEXT("Working...")):StatusText();},10,Red,false,BodyWrap)]]];
        if(ArmyTab!=0)Body->AddSlot().AutoHeight().Padding(0,6,0,0)
            [SNew(SBox).MaxDesiredHeight(48)[SNew(SScrollBox)+SScrollBox::Slot()[LiveText([this](){return MonsterWorkSummary();},10,Muted,false,BodyWrap)]]];
        return Frame(Body,ArmyTab==0?900:840,ArmyTab==0?820:720);
    }
    void RefreshDeployment()
    {
        if(!DeploymentHost)return;
        if(!IsAttacking()){DeploymentHost->SetContent(SNullWidget::NullWidget);DeploymentHost->SetVisibility(EVisibility::Collapsed);return;}
        using namespace BYMUI;TSharedRef<SVerticalBox> Tray=SNew(SVerticalBox);
        Tray->AddSlot().AutoHeight()[Text(TEXT("Flinger"),17,White,true)];
        Tray->AddSlot().AutoHeight().Padding(0,4,0,7)[LiveText([this](){return Count(Number(Battle(),TEXT("flingerUsed")))+TEXT(" / ")+Count(Number(Battle(),TEXT("flingerCapacity")))+TEXT(" capacity");},11,Gold)];
        Tray->AddSlot().AutoHeight().Padding(0,0,0,8)[Text(TEXT("Choose a monster, then click a clear point along the yard edge."),10,White,false,190)];
        TSharedRef<SScrollBox> Scroll=SNew(SScrollBox);
        for(const auto& V:Array(Object(Snapshot,TEXT("catalog")),TEXT("monsters")))
        {
            const auto M=V->AsObject();const FString Id=String(M,TEXT("id")),Name=String(M,TEXT("name"));
            Scroll->AddSlot().Padding(0,0,0,5)
                [SNew(SButton).ButtonStyle(&StoneStyle).IsFocusable(false)
                    .IsEnabled_Lambda([this,Id](){return Idle()&&IsAttacking()&&Bool(Battle(),TEXT("canDeploy"))&&Number(Object(Home(),TEXT("army")),*Id)>0&&Number(MonsterStats(Id),TEXT("housing"),1)<=Number(Battle(),TEXT("flingerRemaining"));})
                    .OnClicked_Lambda([this,Id,Name](){if(PC.IsValid()){PC->SelectedMonsterType=Id;PC->SelectedMonsterCount=1;PC->PlacementMode=TEXT("deploy");PC->StatusMessage=TEXT("Fling ")+Name+TEXT(": click the deployment band around the yard.");}return FReply::Handled();})
                    [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[Image(String(M,TEXT("menuTexture"),String(M,TEXT("texture"))),42,40)]
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [SNew(STextBlock).Text_Lambda([this,Id,Name](){return FText::FromString(Name+TEXT(" L")+Count(Number(MonsterOption(Id),TEXT("level"),1))+TEXT("  x")+Count(Number(Object(Home(),TEXT("army")),*Id)));}).Font(ButtonFont(11)).ColorAndOpacity(White).AutoWrapText(false).WrapTextAt(110)]]];
        }
        Tray->AddSlot().FillHeight(1)[Scroll];
        DeploymentHost->SetContent(Wood(Tray,FMargin(10)));DeploymentHost->SetVisibility(EVisibility::Visible);
    }
    void RequestMap()
    {if(PC.IsValid())PC->RequestWorldMap(MapCenterX,MapCenterY,15,SelectedMapCellId,MapOwnedPage);}
    TSharedPtr<FJsonObject> MapCell()const
    {
        const auto Selected=BYMUI::Object(WorldMap,TEXT("selected"));
        if(Selected&&BYMUI::String(Selected,TEXT("id"))==SelectedMapCellId)return Selected;
        for(const auto& Value:BYMUI::Array(WorldMap,TEXT("cells")))
            if(Value&&Value->Type==EJson::Object&&BYMUI::String(Value->AsObject(),TEXT("id"))==SelectedMapCellId)return Value->AsObject();
        return nullptr;
    }
    FString MapCellKind(const TSharedPtr<FJsonObject>& Cell)const
    {
        switch(static_cast<int32>(BYMUI::Number(Cell,TEXT("kind"))))
        {
            case 101:return TEXT("Backyard");
            case 102:return TEXT("Resource Outpost");
            case 103:return TEXT("Stronghold");
            case 104:return TEXT("Fortification");
            case 1:return TEXT("Wild Monster Outpost");
            default:return TEXT("Open terrain");
        }
    }
    FString MapCellTitle(const TSharedPtr<FJsonObject>& Cell)const
    {
        if(!Cell)return TEXT("Select a cell");
        const FString Name=BYMUI::String(Cell,TEXT("name"));
        if(!Name.IsEmpty())return Name;
        const FString Owner=BYMUI::String(Cell,TEXT("ownerName"));
        return BYMUI::Number(Cell,TEXT("kind"))==101&&!Owner.IsEmpty()?Owner:MapCellKind(Cell);
    }
    void SelectMapCell(const FString& Id)
    {SelectedMapCellId=Id;RequestMap();}
    void FocusMapCell(const TSharedPtr<FJsonObject>& Cell)
    {
        if(!Cell)return;
        SelectedMapCellId=BYMUI::String(Cell,TEXT("id"));
        MapCenterX=static_cast<int32>(BYMUI::Number(Cell,TEXT("x")));
        MapCenterY=static_cast<int32>(BYMUI::Number(Cell,TEXT("y")));
        bMapCentered=true;if(MapCanvas)MapCanvas->CenterOn(MapCenterX,MapCenterY);
        RequestMap();bRefreshPopup=true;
    }
    void MapCommand(const FString& Operation)
    {
        const auto Cell=MapCell();if(!Cell)return;
        const auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("cell"),BYMUI::String(Cell,TEXT("id")));
        if(Operation==TEXT("mapBookmark"))Request->SetBoolField(TEXT("enabled"),!BYMUI::Bool(Cell,TEXT("bookmarked")));
        Request->SetNumberField(TEXT("expectedRevision"),BYMUI::Number(Cell,TEXT("attackRevision")));
        Command(Operation,Request);
    }
    void ConfirmMapAction(const FString& Name,const TSharedPtr<FJsonObject>& ViewedCell=nullptr)
    {
        if(Name==TEXT("MapRelocate"))
        {
            MapConfirmedCell=BYMUI::String(WorldMap,TEXT("homeCellId"));MapConfirmedTitle=TEXT("Your backyard");
            ShowPopup(Name);return;
        }
        const auto Cell=ViewedCell?ViewedCell:MapCell();if(!Cell)return;
        SelectedMapCellId=BYMUI::String(Cell,TEXT("id"));
        MapConfirmedTitle=MapCellTitle(Cell);
        MapConfirmedCell=BYMUI::String(Cell,TEXT("id"));MapConfirmedOwner=BYMUI::String(Cell,TEXT("ownerId"));
        MapConfirmedRevision=static_cast<int64>(BYMUI::Number(Cell,TEXT("attackRevision")));
        MapConfirmedCost=BYMUI::Object(Cell,Name==TEXT("MapRelocate")?TEXT("relocateCost"):TEXT("attackCost"));
        ShowPopup(Name);
    }
    TSharedRef<SWidget> MapConfirmationDialog()
    {
        using namespace BYMUI;const FString Action=Popup;
        const bool Abandon=Action==TEXT("MapAbandon"),Relocate=Action==TEXT("MapRelocate");
        const auto Body=DialogBody(Abandon?TEXT("Abandon territory?"):Relocate?TEXT("Relocate your backyard?"):TEXT("Attack this yard?"));
        const auto Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().Padding(4,10)[Text(MapConfirmedTitle,18,Ink,true,490)];
        if(Abandon)
            Details->AddSlot().AutoHeight().Padding(4,8)[Text(TEXT("This territory will no longer belong to you. Its range, production and bonuses will be lost. You will have to conquer it again to reclaim it."),12,Muted,false,490)];
        else if(Relocate)
            Details->AddSlot().AutoHeight().Padding(4,8)[Text(TEXT("Relocate your backyard to a random available location on this host's world map. There is no relocation fee. Your captured Resource Outposts and Strongholds stay where they are; the old surrounding Fortifications are reset. The host chooses a valid destination before moving your backyard."),12,Muted,false,490)];
        else
        {
            Details->AddSlot().AutoHeight().Padding(4,8)[Text(TEXT("Attack cost"),14,Ink,true)];
            Details->AddSlot().AutoHeight().Padding(4,4)[Text(Cost(MapConfirmedCost),12,Ink,false,490)];
            Details->AddSlot().AutoHeight().Padding(4,8)[Text(TEXT("Deploy your trained army and deal at least 90% damage to non-wall buildings. Resource Outposts, Strongholds and Fortifications are captured; Wild Monster Outposts are destroyed. Main backyards stay with their owner."),12,Muted,false,490)];
        }
        Details->AddSlot().AutoHeight().Padding(4,8)[LiveText([this](){return StatusText();},11,Red,false,490)];
        Body->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Details]];
        const auto Actions=SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Go back"),[this](){ShowPopup(TEXT("MapRoom"));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(Abandon?TEXT("Abandon"):Relocate?TEXT("Relocate"):TEXT("Attack"),[this,Abandon,Relocate]()
            {
                const auto Request=MakeShared<FJsonObject>();
                if(Relocate)
                {
                    Request->SetStringField(TEXT("mode"),TEXT("random"));Request->SetStringField(TEXT("expectedHome"),MapConfirmedCell);
                    Command(TEXT("mapRelocate"),Request);return;
                }
                Request->SetStringField(TEXT("cell"),MapConfirmedCell);
                Request->SetStringField(TEXT("expectedOwner"),MapConfirmedOwner);Request->SetNumberField(TEXT("expectedRevision"),MapConfirmedRevision);
                Request->SetObjectField(TEXT("maxCost"),MapConfirmedCost?MapConfirmedCost.ToSharedRef():MakeShared<FJsonObject>());
                Command(Abandon?TEXT("mapAbandon"):TEXT("mapInvade"),Request);
            })];
        Body->AddSlot().AutoHeight()[Actions];RecordLayoutProbe(TEXT("Actions"),Actions);
        return Frame(Body,550,440);
    }
    TSharedRef<SWidget> MapRoomDialog()
    {
        using namespace BYMUI;
        const auto Body=DialogBody(TEXT("Map Room"));
        const auto Navigation=SNew(SHorizontalBox);
        Navigation->AddSlot().AutoWidth().Padding(0,0,6,0)[Button(TEXT("Find Base"),[this]()
        {
            MapCenterX=static_cast<int32>(Number(WorldMap,TEXT("homeX"),250));MapCenterY=static_cast<int32>(Number(WorldMap,TEXT("homeY"),250));
            SelectedMapCellId=String(WorldMap,TEXT("homeCellId"));bMapCentered=true;
            if(MapCanvas)MapCanvas->CenterOn(MapCenterX,MapCenterY);RequestMap();bRefreshPopup=true;
        })];
        Navigation->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3,0)[Text(TEXT("X"),11)];
        Navigation->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(58)[SAssignNew(MapJumpX,SEditableTextBox).Text(FText::AsNumber(FMath::Max(0,MapCenterX))).Font(Font(11))]];
        Navigation->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6,0,3,0)[Text(TEXT("Y"),11)];
        Navigation->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(58)[SAssignNew(MapJumpY,SEditableTextBox).Text(FText::AsNumber(FMath::Max(0,MapCenterY))).Font(Font(11))]];
        Navigation->AddSlot().AutoWidth().Padding(6,0)[Button(TEXT("Jump"),[this]()
        {
            auto Coordinate=[](const FString& Text,int32& Value)
            {
                const FString Digits=Text.TrimStartAndEnd();if(Digits.IsEmpty()||Digits.Len()>3)return false;
                for(const TCHAR Digit:Digits)if(Digit<TEXT('0')||Digit>TEXT('9'))return false;
                Value=FCString::Atoi(*Digits);return Value<500;
            };
            int32 X=0,Y=0;
            if(!MapJumpX||!MapJumpY||!Coordinate(MapJumpX->GetText().ToString(),X)||!Coordinate(MapJumpY->GetText().ToString(),Y))
            {if(PC.IsValid())PC->StatusMessage=TEXT("Enter map coordinates from 0 to 499.");return;}
            MapCenterX=X;MapCenterY=Y;bMapCentered=true;SelectedMapCellId.Empty();
            if(MapCanvas)MapCanvas->CenterOn(X,Y);RequestMap();
        })];
        Navigation->AddSlot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [LiveText([this](){return FString::Printf(TEXT("%d, %d"),FMath::Max(0,MapCenterX),FMath::Max(0,MapCenterY));},12,Ink,true)];
        Navigation->AddSlot().AutoWidth().Padding(4,0)[SNew(SButton).ButtonStyle(&StoneStyle).IsFocusable(false).ContentPadding(4)
            .ToolTipText(FText::FromString(TEXT("Zoom out"))).OnClicked_Lambda([this](){if(MapCanvas)MapCanvas->SetZoom(.5f);return FReply::Handled();})
            [Image(TEXT("worldmap/hud/options/button_zoom_out.png"),26,26)]];
        Navigation->AddSlot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).ButtonStyle(&StoneStyle).IsFocusable(false).ContentPadding(4)
            .ToolTipText(FText::FromString(TEXT("Zoom in"))).OnClicked_Lambda([this](){if(MapCanvas)MapCanvas->SetZoom(1.f);return FReply::Handled();})
            [Image(TEXT("worldmap/hud/options/button_zoom_in.png"),26,26)]];
        Navigation->AddSlot().AutoWidth()[Button(TEXT("Refresh"),[this](){RequestMap();})];
        Body->AddSlot().AutoHeight().Padding(0,0,0,8)[Navigation];
        FSlateFontInfo MapLabelFont=Font(10);MapLabelFont.OutlineSettings.OutlineSize=1;MapLabelFont.OutlineSettings.OutlineColor=FLinearColor::Black;
        const auto Canvas=SAssignNew(MapCanvas,SBYMWorldMap)
            .MapData_Lambda([this](){return WorldMap;}).SelectedCell_Lambda([this](){return SelectedMapCellId;})
            .BrushResolver([this](const FString& Path){return ImageBrush(Path);}).LabelFont(MapLabelFont)
            .CenterX(FMath::Max(0,MapCenterX)).CenterY(FMath::Max(0,MapCenterY)).Zoom(MapZoom)
            .OnCellSelected_Lambda([this](const FString& Id){SelectMapCell(Id);})
            .OnViewChanged_Lambda([this](int32 X,int32 Y,float Zoom){MapCenterX=X;MapCenterY=Y;MapZoom=Zoom;bMapCentered=true;RequestMap();});
        RecordLayoutProbe(TEXT("MapCanvas"),Canvas);
        const auto MapPanel=SNew(SOverlay)
            +SOverlay::Slot()[Canvas]
            +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24)
                [SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(.90f,.85f,.67f,.96f)).Padding(18)
                    .Visibility_Lambda([this](){return Bool(WorldMap,TEXT("available"))?EVisibility::Collapsed:EVisibility::Visible;})
                    [LiveText([this](){const FString Reason=String(WorldMap,TEXT("reason"));return Reason.IsEmpty()?FString(TEXT("Loading the world map…")):Reason;},14,Ink,true,540)]];
        const auto Selected=SNew(SVerticalBox);
        Selected->AddSlot().AutoHeight().Padding(2,0,2,6)[LiveText([this](){return MapCellTitle(MapCell());},17,Ink,true,246)];
        Selected->AddSlot().AutoHeight().Padding(2,0,2,6)[LiveText([this]()
        {
            const auto Cell=MapCell();if(!Cell)return FString(TEXT("Click a backyard, outpost or stronghold to see its details. Drag the map to explore; use the wheel to zoom."));
            FString Result=MapCellKind(Cell)+FString::Printf(TEXT("  (%d, %d)"),static_cast<int32>(Number(Cell,TEXT("x"))),static_cast<int32>(Number(Cell,TEXT("y"))));
            if(Number(Cell,TEXT("level"))>0)Result+=TEXT("\nLevel ")+Count(Number(Cell,TEXT("level")));
            const FString Owner=String(Cell,TEXT("ownerName"));if(!Owner.IsEmpty())Result+=TEXT("\nOwner: ")+Owner;
            if(Number(Cell,TEXT("range"))>0)Result+=TEXT("\nRange: ")+Count(Number(Cell,TEXT("range")))+TEXT(" cells");
            if(Number(Cell,TEXT("productionPerSecond"))>0)Result+=TEXT("\nProduction: ")+Count(Number(Cell,TEXT("productionPerSecond"))*60)+TEXT(" of each resource / minute");
            if(Number(Cell,TEXT("resourceCapacity"))>0)Result+=TEXT("\nCapacity: ")+Count(Number(Cell,TEXT("resourceCapacity")))+TEXT(" per resource");
            if(Number(Cell,TEXT("monsterBonus"))>0)Result+=TEXT("\nMonster damage: +")+Count(Number(Cell,TEXT("monsterBonus")))+TEXT("%");
            if(Number(Cell,TEXT("towerBonus"))>0)Result+=TEXT("\nTower damage: +")+Count(Number(Cell,TEXT("towerBonus")))+TEXT("%");
            if(Number(Cell,TEXT("damageReduction"))>0)Result+=TEXT("\nFortification: ")+Count(Number(Cell,TEXT("damageReduction")))+TEXT("% damage reduction");
            if(Number(Cell,TEXT("damagePercent"))>0)Result+=TEXT("\nDamage: ")+Count(Number(Cell,TEXT("damagePercent")))+TEXT("%");
            if(Number(Cell,TEXT("distanceBeyondRange"))>0)Result+=TEXT("\nBeyond your range: ")+Count(Number(Cell,TEXT("distanceBeyondRange")))+TEXT(" cells");
            const double Shield=Number(Cell,TEXT("shieldUntil"))-ServerNow();if(Shield>0)Result+=TEXT("\nProtected for ")+Duration(Shield);
            const double Regrow=Number(Cell,TEXT("destroyedUntil"))-ServerNow();if(Regrow>0)Result+=TEXT("\nReturns in ")+Duration(Regrow);
            return Result;
        },11,Muted,false,246)];
        Selected->AddSlot().AutoHeight().Padding(2,4)[LiveText([this](){return String(MapCell(),TEXT("captureRule"));},11,Ink,false,246)];
        Selected->AddSlot().AutoHeight().Padding(2,4)[LiveText([this](){const auto Cell=MapCell();return String(Cell,Bool(Cell,TEXT("owned"))?TEXT("manageReason"):TEXT("reason"));},11,Red,false,246)];
        Selected->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)[Button(TEXT("Scout / Enter"),[this](){MapCommand(TEXT("mapInspect"));},[this](){return Bool(MapCell(),TEXT("canScout"))||Bool(MapCell(),TEXT("canEnter"));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(3,0,0,0)[Button(TEXT("Attack"),[this](){ConfirmMapAction(TEXT("MapAttack"));},[this](){return Bool(MapCell(),TEXT("canAttack"));})]];
        Selected->AddSlot().AutoHeight().Padding(0,3)[SNew(SButton).ButtonStyle(&StoneStyle).IsFocusable(false).IsEnabled_Lambda([this](){const int32 Kind=static_cast<int32>(Number(MapCell(),TEXT("kind")));return Idle()&&Bool(WorldMap,TEXT("available"))&&(Kind==1||Kind==101||Kind==102||Kind==103||Kind==104);})
            .OnClicked_Lambda([this](){MapCommand(TEXT("mapBookmark"));return FReply::Handled();})
            [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Bool(MapCell(),TEXT("bookmarked"))?TEXT("Remove bookmark"):TEXT("Bookmark"));}).Font(ButtonFont()).ColorAndOpacity(White)]];
        Selected->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Repair territory"),[this](){MapCommand(TEXT("mapRepair"));},[this](){return Bool(MapCell(),TEXT("canRepair"));})];
        Selected->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("Abandon"),[this](){ConfirmMapAction(TEXT("MapAbandon"));},[this](){return Bool(MapCell(),TEXT("canAbandon"));})];
        const auto Sidebar=SNew(SVerticalBox);
        const auto SelectedScroll=SNew(SScrollBox)+SScrollBox::Slot().Padding(0,0,8,0)[Selected];
        Sidebar->AddSlot().FillHeight(.68f)[SelectedScroll];
        Sidebar->AddSlot().AutoHeight().Padding(0,8,0,5)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)[Button(MapListTab==0?TEXT("[ My territory ]"):TEXT("My territory"),[this](){MapListTab=0;bRefreshPopup=true;})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(3,0,0,0)[Button(MapListTab==1?TEXT("[ Bookmarks ]"):TEXT("Bookmarks"),[this](){MapListTab=1;bRefreshPopup=true;})]];
        const auto Places=SNew(SScrollBox);
        for(const auto& Value:Array(WorldMap,MapListTab==0?TEXT("ownedCells"):TEXT("bookmarks")))
        {
            if(!Value||Value->Type!=EJson::Object)continue;const auto Cell=Value->AsObject();
            const FString Label=MapCellTitle(Cell)+FString::Printf(TEXT("  %d,%d"),static_cast<int32>(Number(Cell,TEXT("x"))),static_cast<int32>(Number(Cell,TEXT("y"))));
            Places->AddSlot().Padding(0,2,7,2)[SNew(SButton).ButtonStyle(&CardStyle).IsFocusable(false).OnClicked_Lambda([this,Cell](){FocusMapCell(Cell);return FReply::Handled();})[Text(Label,11,Ink,false,237)]];
        }
        if(Array(WorldMap,MapListTab==0?TEXT("ownedCells"):TEXT("bookmarks")).IsEmpty())
            Places->AddSlot()[Text(MapListTab==0?TEXT("Captured territories appear here."):TEXT("Select a cell and choose Bookmark."),11,Muted,false,246)];
        Sidebar->AddSlot().FillHeight(.32f)[Places];
        Sidebar->AddSlot().AutoHeight().Padding(0,5,0,0)
            [SNew(SBox).Visibility_Lambda([this](){return MapListTab==0&&Number(WorldMap,TEXT("ownedPages"),1)>1?EVisibility::Visible:EVisibility::Collapsed;})
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("<"),[this](){MapOwnedPage=FMath::Max(0,MapOwnedPage-1);RequestMap();},[this](){return MapOwnedPage>0;})]
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).HAlign(HAlign_Center)[LiveText([this](){return Count(MapOwnedPage+1)+TEXT(" / ")+Count(Number(WorldMap,TEXT("ownedPages"),1));},10)]
                    +SHorizontalBox::Slot().AutoWidth()[Button(TEXT(">"),[this](){++MapOwnedPage;RequestMap();},[this](){return MapOwnedPage+1<Number(WorldMap,TEXT("ownedPages"),1);})]]];
        RecordLayoutProbe(TEXT("MapDetails"),SelectedScroll);RecordLayoutProbe(TEXT("MapPlaces"),Places);
        const auto Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[MapPanel]
            +SHorizontalBox::Slot().AutoWidth().Padding(12,0,0,0)[SNew(SBox).WidthOverride(276)[Sidebar]];
        Body->AddSlot().FillHeight(1)[Content];
        const auto Footer=SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("Drag to move • Wheel to zoom • Click a cell to select"),10,Muted,false,640)]
            +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[SNew(SBox).ToolTipText_Lambda([this](){return FText::FromString(String(WorldMap,TEXT("relocateReason")));})
                [Button(TEXT("Relocate backyard"),[this](){ConfirmMapAction(TEXT("MapRelocate"));},[this](){return Bool(WorldMap,TEXT("canRelocate"));})]]
            +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[Button(TEXT("Rankings"),[this](){ShowPopup(TEXT("Rivals"));})]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Enter Base"),[this](){if(PC.IsValid())PC->ReturnHome();ClosePopup();})];
        Body->AddSlot().AutoHeight().Padding(0,8,0,0)[Footer];RecordLayoutProbe(TEXT("Actions"),Footer);
        RecordLayoutProbe(TEXT("MapNavigation"),Navigation);
        return Frame(Body,1120,660);
    }
    TSharedRef<SWidget> RivalsDialog()
    {
        using namespace BYMUI;TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Rankings"));
        Body->AddSlot().AutoHeight().Padding(0,0,0,10)[Text(TEXT("Scout a rival's yard, then choose Attack. Players and rival bots compete for the same RP ladder."),11,Muted,false,700)];
        TSharedRef<SScrollBox> Scroll=SNew(SScrollBox);
        for(const auto& V:Array(Ladder,TEXT("entries")))
        {
            const auto E=V->AsObject();const FString Id=String(E,TEXT("id"));const bool Self=Id==String(Snapshot,TEXT("selfId"));
            Scroll->AddSlot().Padding(0,0,0,6)
                [SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(.80f,.75f,.56f)).Padding(10)
                    [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0).VAlign(VAlign_Center)[Text(TEXT("#")+Count(Number(E,TEXT("rank"))),15,Ink,true)]
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Text(String(E,TEXT("name")),14,Ink,true,300)]
                                +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Text(Self?TEXT("Your yard"):Bool(E,TEXT("bot"))?TEXT("Rival bot"):TEXT("Player"),10,Muted)]
                                +SVerticalBox::Slot().AutoHeight()[LiveText([this,E](){const double T=Number(E,TEXT("shieldUntil"))-ServerNow();return T>0?TEXT("Protected for ")+Duration(T):TEXT("Open to invasion");},10,Muted)]]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0)[Text(Count(Number(E,TEXT("rp")))+TEXT(" RP"),12,Ink,true)]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(Self?TEXT("Home"):TEXT("Scout"),[this,Id](){if(PC.IsValid()){PC->ViewYard(Id);ClosePopup();}})]]];
        }
        if(!Array(Ladder,TEXT("entries")).Num())Scroll->AddSlot()[Text(TEXT("Loading rivals..."),13)];
        Body->AddSlot().FillHeight(1)[Scroll];
        Body->AddSlot().AutoHeight().Padding(0,10,0,0)
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Previous"),[this](){LadderPage=FMath::Max(0,LadderPage-1);if(PC.IsValid())PC->RequestLadder(LadderPage);},[this](){return LadderPage>0;})]
                +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[Text(TEXT("Page ")+Count(Number(Ladder,TEXT("page"))+1)+TEXT(" / ")+Count(Number(Ladder,TEXT("totalPages"),1)),11)]
                +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[Button(TEXT("Refresh"),[this](){if(PC.IsValid())PC->RequestLadder(LadderPage);})]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Next"),[this](){++LadderPage;if(PC.IsValid())PC->RequestLadder(LadderPage);},[this](){return LadderPage+1<Number(Ladder,TEXT("totalPages"),1);})]];
        return Frame(Body,740,540);
    }
    TSharedRef<SWidget> BattleResultDialog()
    {
        using namespace BYMUI;const auto Result=Object(Snapshot,TEXT("lastBattle"));
        const bool Contested=Bool(Result,TEXT("contested"));const bool Won=Bool(Result,TEXT("won"));
        const bool MapBattle=!String(Result,TEXT("mapCellId")).IsEmpty();
        TSharedRef<SVerticalBox> Body=DialogBody(Contested?(Won?TEXT("Victory!"):TEXT("Defeat")):TEXT("Attack ended"));
        TSharedRef<SVerticalBox> Details=SNew(SVerticalBox);
        Details->AddSlot().AutoHeight().Padding(5,12)[Text(String(Result,TEXT("reason")),15,Ink,true,482)];
        if(MapBattle)
        {
            const FString Territory=String(Result,TEXT("territoryName"),TEXT("Territory"));
            const FString Outcome=Bool(Result,TEXT("captured"))?Territory+TEXT(" captured. Its range and benefits now belong to you.")
                :Number(Result,TEXT("territoryKind"))==1&&Won?Territory+TEXT(" destroyed. The wild outpost returns after three days.")
                :Number(Result,TEXT("territoryKind"))==101?TEXT("Backyards remain with their owner."):TEXT("Territory was not captured.");
            Details->AddSlot().AutoHeight().Padding(5,8)[Text(Outcome,12,Ink,true,482)];
        }
        const auto Loot=Object(Result,TEXT("loot"));
        Details->AddSlot().AutoHeight().Padding(5,10)[Text(TEXT("Resources captured"),15,Ink,true,482)];
        const TCHAR* Keys[]={TEXT("twigs"),TEXT("pebbles"),TEXT("putty"),TEXT("goo")};
        const TCHAR* Names[]={TEXT("Twigs"),TEXT("Pebbles"),TEXT("Putty"),TEXT("Goo")};
        for(int32 I=0;I<4;++I)Details->AddSlot().AutoHeight().Padding(10,4)[Text(FString(Names[I])+TEXT(": ")+Count(Number(Loot,Keys[I])),13,BYMUI::Ink,false,482)];
        Details->AddSlot().AutoHeight().Padding(5,12)[Text(TEXT("Your ranking: ")+Count(Number(Home(),TEXT("rp")))+TEXT(" RP"),14,Ink,true,482)];
        Details->AddSlot().AutoHeight()[Text(Contested?(MapBattle?TEXT("The host has updated the world map and battle result."):TEXT("The defending yard now has damage protection.")):TEXT("No combat took place. No RP was awarded."),11,Muted,false,482)];
        Body->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Details]];
        const auto Actions=SNew(SHorizontalBox);
        Actions->AddSlot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Return home"),[this](){if(PC.IsValid())PC->ReturnHome();ClosePopup();})];
        if(MapBattle&&Bool(Result,TEXT("captured")))
        {
            const FString CellId=String(Result,TEXT("mapCellId"));
            Actions->AddSlot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("View territory"),[this,CellId]()
            {const auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("cell"),CellId);Command(TEXT("mapInspect"),Request);})];
        }
        Body->AddSlot().AutoHeight()[Actions];
        return Frame(Body,550,470);
    }
    TSharedRef<SWidget> MenuDialog()
    {
        TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Backyard Monsters NXT"));
        Body->AddSlot().AutoHeight().Padding(0,12)[Text(TEXT("WASD or arrow keys: pan\nMouse wheel: zoom\nRight or middle drag: pan\nEscape: cancel placement"),13)];
        Body->AddSlot().AutoHeight().Padding(0,10)[Button(TEXT("Return to yard"),[this](){if(PC.IsValid())PC->ReturnHome();ClosePopup();})];
        Body->AddSlot().AutoHeight().Padding(0,4)[Button(TEXT("General Store"),[this](){ShowPopup(TEXT("Store"));},[this](){return ViewingHome()&&!Battle();})];
        Body->AddSlot().AutoHeight().Padding(0,10)[Button(PC.IsValid()&&PC->IsHost()?TEXT("Stop host and return to menu"):TEXT("Log out"),[this](){if(PC.IsValid())PC->LeaveServer();})];
        Body->AddSlot().FillHeight(1)[Text(TEXT("Your progress is kept by the host."),11,BYMUI::Muted)];
        return Frame(Body,520,390);
    }
#if WITH_DEV_AUTOMATION_TESTS
public:
    void PrepareMapLayoutFixture(bool bLongText,bool bLocked)
    {
        using namespace BYMUI;
        PrepareEconomyLayoutFixture(false);MapCenterX=250;MapCenterY=250;MapZoom=.75f;bMapCentered=true;
        WorldMap=MakeShared<FJsonObject>();WorldMap->SetBoolField(TEXT("available"),!bLocked);
        WorldMap->SetStringField(TEXT("selfId"),TEXT("fixture"));WorldMap->SetNumberField(TEXT("width"),500);WorldMap->SetNumberField(TEXT("height"),500);
        WorldMap->SetNumberField(TEXT("homeX"),250);WorldMap->SetNumberField(TEXT("homeY"),250);WorldMap->SetNumberField(TEXT("now"),100);
        WorldMap->SetStringField(TEXT("homeCellId"),TEXT("cell-250-250"));
        WorldMap->SetStringField(TEXT("reason"),bLocked?TEXT("Complete your Map Room to explore the world map. ")+FString::ChrN(160,TEXT('W')):FString());
        TArray<TSharedPtr<FJsonValue>> Cells,Owned,Bookmarks;
        for(int32 Y=241;Y<=259;++Y)for(int32 X=241;X<=259;++X)
        {
            const auto Cell=MakeShared<FJsonObject>();Cell->SetStringField(TEXT("id"),FString::Printf(TEXT("cell-%d-%d"),X,Y));
            Cell->SetNumberField(TEXT("x"),X);Cell->SetNumberField(TEXT("y"),Y);Cell->SetNumberField(TEXT("kind"),100);Cell->SetNumberField(TEXT("terrain"),47+(X+Y)%22);
            if(X==250&&Y==250)
            {
                Cell->SetNumberField(TEXT("kind"),101);Cell->SetStringField(TEXT("ownerId"),TEXT("fixture"));Cell->SetStringField(TEXT("ownerName"),TEXT("Your backyard"));
                Cell->SetBoolField(TEXT("owned"),true);Cell->SetNumberField(TEXT("range"),10);Owned.Add(MakeShared<FJsonValueObject>(Cell));
            }
            if(X==253&&Y==250)
            {
                Cell->SetNumberField(TEXT("kind"),103);Cell->SetNumberField(TEXT("level"),40);Cell->SetNumberField(TEXT("range"),15);
                Cell->SetStringField(TEXT("name"),bLongText?TEXT("An exceptionally long localized stronghold name which must wrap inside the details panel"):TEXT("Stronghold"));
                Cell->SetStringField(TEXT("ownerId"),TEXT("rival"));Cell->SetStringField(TEXT("ownerName"),bLongText?FString::ChrN(120,TEXT('W')):TEXT("Rival bot"));
                Cell->SetNumberField(TEXT("monsterBonus"),20);Cell->SetNumberField(TEXT("towerBonus"),20);Cell->SetNumberField(TEXT("damageReduction"),35);
                Cell->SetNumberField(TEXT("attackRevision"),7);Cell->SetBoolField(TEXT("canAttack"),true);Cell->SetBoolField(TEXT("canScout"),true);
                Cell->SetObjectField(TEXT("attackCost"),Parse(TEXT("{\"twigs\":10000,\"pebbles\":10000,\"putty\":10000,\"goo\":10000}")));
                Cell->SetStringField(TEXT("reason"),bLongText?FString::ChrN(160,TEXT('W')):TEXT("Capture this stronghold to extend your territory."));
                SelectedMapCellId=String(Cell,TEXT("id"));WorldMap->SetObjectField(TEXT("selected"),Cell);Bookmarks.Add(MakeShared<FJsonValueObject>(Cell));
            }
            if(X==248&&Y==249){Cell->SetNumberField(TEXT("kind"),102);Cell->SetNumberField(TEXT("level"),20);}
            if(X==252&&Y==250){Cell->SetNumberField(TEXT("kind"),104);Cell->SetNumberField(TEXT("level"),30);}
            Cells.Add(MakeShared<FJsonValueObject>(Cell));
        }
        WorldMap->SetArrayField(TEXT("cells"),Cells);WorldMap->SetArrayField(TEXT("ownedCells"),Owned);WorldMap->SetArrayField(TEXT("bookmarks"),Bookmarks);
    }
    void PrepareEconomyLayoutFixture(bool bLongText)
    {
        Snapshot=BYMUI::Parse(TEXT("{\"selfId\":\"fixture\",\"home\":{},\"yard\":{},\"catalog\":{\"buildings\":[{\"id\":28,\"name\":\"American Flag\",\"description\":\"Decorate your yard with an original flag.\",\"texture\":\"buildingthumbs/28.png\"}]}}"));
        const auto H=BYMUI::Object(Snapshot,TEXT("home"));H->SetStringField(TEXT("id"),TEXT("fixture"));
        H->SetObjectField(TEXT("workers"),BYMUI::Parse(TEXT("{\"total\":2,\"busy\":1,\"available\":1,\"max\":5,\"entries\":[{\"id\":\"w1\",\"state\":\"working\",\"kind\":\"construction\",\"remaining\":120},{\"id\":\"w2\",\"state\":\"idle\"}]}")));
        const auto ShopData=BYMUI::Parse(TEXT("{\"shiny\":1500,\"storeAvailable\":true,\"canClearMushrooms\":true,\"products\":[{\"code\":\"BEW\",\"name\":\"1 Extra Worker\",\"description\":\"One extra worker to help you build and upgrade your village.\",\"price\":250,\"owned\":0,\"max\":4,\"canBuy\":true},{\"code\":\"ENL\",\"name\":\"More Yardage\",\"description\":\"Increase the size of your yard by 10% to make room for more stuff.\",\"price\":50,\"owned\":0,\"max\":6,\"canBuy\":true},{\"code\":\"BIP\",\"name\":\"Improved packing skills\",\"description\":\"Improves the capacity of all resources by 10%.\",\"price\":50,\"owned\":0,\"max\":10,\"canBuy\":true}],\"decorations\":[{\"type\":28,\"name\":\"American Flag\",\"price\":25,\"texture\":\"buildingthumbs/28.png\",\"owned\":0,\"limit\":0,\"canBuy\":true}]}"));
        if(bLongText)
        {
            const FString LongName=TEXT("A long localized original store product name that must remain readable within the selected purchase panel");
            const FString Token=FString::ChrN(160,TEXT('W'));
            const auto First=BYMUI::Array(ShopData,TEXT("products"))[0]->AsObject();
            First->SetStringField(TEXT("name"),LongName);
            First->SetStringField(TEXT("description"),Token+TEXT("\nAn extended product description wraps inside the scrolling list without moving the purchase actions outside the frame."));
            ShopData->SetBoolField(TEXT("storeAvailable"),false);ShopData->SetStringField(TEXT("storeReason"),Token+TEXT("\nComplete and repair your General Store before purchasing this item."));
        }
        H->SetObjectField(TEXT("shop"),ShopData);Snapshot->SetObjectField(TEXT("yard"),H);
        const auto SpeedData=BYMUI::Parse(TEXT("{\"speedups\":[{\"building\":\"b1\",\"job\":\"job1\",\"name\":\"General Store construction\",\"remaining\":180,\"revision\":1,\"options\":[{\"code\":\"SP1\",\"name\":\"Close enough\",\"price\":0,\"canBuy\":true},{\"code\":\"SP2\",\"name\":\"Reduce by 1 hour\",\"price\":20,\"canBuy\":false,\"reason\":\"Less than one hour of work remains.\"},{\"code\":\"SP3\",\"name\":\"Reduce by 2 hours\",\"price\":40,\"canBuy\":false,\"reason\":\"Less than two hours of work remains.\"},{\"code\":\"SP4\",\"name\":\"FINISH NOW!\",\"price\":1,\"canBuy\":false,\"reason\":\"Use the free speed-up for the final five minutes.\"}]}]}"));
        ShopData->SetArrayField(TEXT("speedups"),BYMUI::Array(SpeedData,TEXT("speedups")));
        H->SetObjectField(TEXT("economy"),BYMUI::Parse(TEXT("{\"mushrooms\":[{\"id\":\"m1\",\"x\":12,\"y\":16,\"frame\":0,\"picking\":false,\"canClear\":true}]}")));
        SelectedBuilding=TEXT("m1");
    }
    BYMUIAutomation::FFixture MakeLayoutFixture(FName Variant)
    {
        // Tests instantiate these production builders without a controller or
        // connection. Probes refer to the actual leaf/panel widgets, not copies.
        bCaptureLayoutProbes=true;LayoutProbes.Reset();BoardInputBlockers.Reset();
        TSharedPtr<FBYMUIArt> PreviousTitle;
        LayoutFixtureStatus=TEXT("Select a building, or choose a construction or deployment tool.");
        if(Variant==TEXT("LoginLongStatus"))
        {
            LayoutFixtureStatus=TEXT("Unable to connect to this host. Check its address and try again.\n")+FString::ChrN(180,TEXT('W'));
            for(int32 I=0;I<24;++I)LayoutFixtureStatus+=TEXT("\nThe host returned additional connection details. Your sign-in actions remain available below.");
        }
        if(Variant==TEXT("ColdAssets"))
        {
            PreviousTitle=Art.FindRef(TEXT("ui/title.png"));
            UTexture2D* ColdTexture=UTexture2D::CreateTransient(4,4);
            // Construct the real brush while its resource is 4x4. Retain the
            // previous brush until BuildLogin replaces the previous widget tree.
            Art.Add(TEXT("ui/title.png"),MakeShared<FBYMUIArt>(ColdTexture,SourceImageSize(TEXT("ui/title.png"))));
        }
        if(Variant==TEXT("MapRoom")||Variant==TEXT("MapRoomLongText")||Variant==TEXT("MapRoomLocked")||Variant==TEXT("MapRoomPaged")||Variant==TEXT("MapAttack"))
        {
            PrepareMapLayoutFixture(Variant==TEXT("MapRoomLongText"),Variant==TEXT("MapRoomLocked"));
            if(Variant==TEXT("MapRoomPaged"))
            {MapOwnedPage=1;WorldMap->SetNumberField(TEXT("ownedPage"),1);WorldMap->SetNumberField(TEXT("ownedPages"),3);WorldMap->SetNumberField(TEXT("ownedTotal"),121);}
            Popup=TEXT("MapRoom");
            if(Variant==TEXT("MapAttack"))ConfirmMapAction(TEXT("MapAttack"));
            const TSharedRef<SWidget> Content=Variant==TEXT("MapAttack")?MapConfirmationDialog():MapRoomDialog();
            ChildSlot[SNew(SOverlay)+SOverlay::Slot().Padding(20)
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Content]]];
        }
        else if(Variant==TEXT("HUDWorkers"))
        {
            PrepareEconomyLayoutFixture(false);bGame=true;BuildGame();
        }
        else if(Variant==TEXT("Store")||Variant==TEXT("StoreLongText")||Variant==TEXT("StoreSpeedups")||Variant==TEXT("Workers")||Variant==TEXT("Mushroom")||Variant==TEXT("Decoration"))
        {
            PrepareEconomyLayoutFixture(Variant==TEXT("StoreLongText"));
            if(Variant==TEXT("StoreSpeedups"))StoreCategory=2;
            TSharedRef<SWidget> Content=SNullWidget::NullWidget;
            if(Variant==TEXT("Workers"))Content=WorkerHUD();
            else if(Variant==TEXT("Mushroom"))Content=MushroomDialog();
            else if(Variant==TEXT("Decoration"))Content=DecorationDialog(28);
            else Content=StoreDialog();
            ChildSlot[SNew(SOverlay)+SOverlay::Slot().Padding(28)
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Content]]];
        }
        else if(Variant==TEXT("HUD"))
        {
            Snapshot=BYMUI::Parse(TEXT("{\"selfId\":\"fixture\",\"home\":{\"id\":\"fixture\"},\"yard\":{\"id\":\"fixture\",\"name\":\"Fixture yard\"},\"battle\":{\"attacker\":\"fixture\",\"phase\":\"deploying\"}}"));
            bGame=true;BuildGame();
        }
        else if(Variant==TEXT("SharedDialog"))
        {
            const TSharedRef<SVerticalBox> Body=DialogBody(TEXT("Monster Bunker"));
            const TSharedRef<SWidget> Caption=Text(TEXT("Transfer monsters from Housing"),12,BYMUI::Ink,true);
            const TSharedRef<SWidget> Picture=Image(TEXT("ui/title.png"),360,111);
            const TSharedRef<SWidget> Paragraph=Text(TEXT("Choose monsters from Housing and transfer them into this Bunker. Transferred monsters defend your yard during an invasion. Removing a stored monster is permanent and gives no refund."),12,BYMUI::Muted,false,680);
            const TSharedRef<SScrollBox> Status=SNew(SScrollBox)+SScrollBox::Slot()[Paragraph];
            const TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Button(TEXT("Go back"),[](){})]
                +SHorizontalBox::Slot().FillWidth(1).Padding(5,0,0,0)[Button(TEXT("Transfer"),[](){})];
            Body->AddSlot().AutoHeight()[Caption];Body->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0,10)[Picture];
            Body->AddSlot().FillHeight(1).Padding(0,8)[Status];Body->AddSlot().AutoHeight()[Actions];
            RecordLayoutProbe(TEXT("Caption"),Caption);RecordLayoutProbe(TEXT("Image"),Picture);
            RecordLayoutProbe(TEXT("Paragraph"),Paragraph);RecordLayoutProbe(TEXT("Status"),Status);RecordLayoutProbe(TEXT("Actions"),Actions);
            ChildSlot[SNew(SOverlay)+SOverlay::Slot().Padding(28)
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Frame(Body,750,560)]]];
        }
        else BuildLogin(true);
        bCaptureLayoutProbes=false;
        BYMUIAutomation::FFixture Result;Result.OwnerLifetime=AsShared();Result.Root=AsShared();Result.Probes=LayoutProbes;return Result;
    }
#endif
};

#if WITH_DEV_AUTOMATION_TESTS
BYMUIAutomation::FFixture BYMUIAutomation::BuildLayoutFixture(FName Variant)
{
    const TSharedRef<SBYMRoot> Root=SNew(SBYMRoot);
    return Root->MakeLayoutFixture(Variant);
}
#endif

void ABYMHUD::BeginPlay()
{
    Super::BeginPlay();ABYMPlayerController* PC=Cast<ABYMPlayerController>(GetOwningPlayerController());
    if(!PC||!PC->IsLocalController()||!GEngine||!GEngine->GameViewport)return;
    SAssignNew(RootWidget,SBYMRoot).Controller(PC);GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(),20);
}
void ABYMHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if(auto* PC=Cast<ABYMPlayerController>(GetOwningPlayerController()))
    {PC->bWorldMapOpen=false;PC->bPointerOverGameUI=false;}
    if(RootWidget.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
    RootWidget.Reset();Super::EndPlay(EndPlayReason);
}
