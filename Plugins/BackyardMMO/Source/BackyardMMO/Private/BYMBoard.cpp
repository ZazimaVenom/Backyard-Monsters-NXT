#include "BYMBoard.h"
#include "BYMSettings.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    double Number(const TSharedPtr<FJsonObject>& J,const TCHAR* K,double Default=0)
    {
        double V=Default; if(J.IsValid()) J->TryGetNumberField(K,V); return V;
    }
    FString String(const TSharedPtr<FJsonObject>& J,const TCHAR* K,const FString& Default=TEXT(""))
    {
        FString V=Default; if(J.IsValid()) J->TryGetStringField(K,V); return V;
    }
    TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& J,const TCHAR* K)
    {
        const TSharedPtr<FJsonObject>* O=nullptr; return J.IsValid()&&J->TryGetObjectField(K,O)?*O:nullptr;
    }
    bool Boolean(const TSharedPtr<FJsonObject>& J,const TCHAR* K,bool Default=false)
    {
        bool V=Default;if(J.IsValid())J->TryGetBoolField(K,V);return V;
    }
    int32 SpriteSortPriority(double Height)
    {
        // The render proxy stores this as int16. The previous Z*100000
        // overflowed, including the cursor, and could sort it under the yard.
        return FMath::Clamp(FMath::RoundToInt(Height*1000.),-32768,32767);
    }
    float SourceBounceOut(float T)
    {
        // The original deployment TweenLite uses Bounce.easeOut, from -90px.
        constexpr float Scale=7.5625f,Step=2.75f;
        if(T<1.f/Step)return Scale*T*T;
        if(T<2.f/Step){T-=1.5f/Step;return Scale*T*T+.75f;}
        if(T<2.5f/Step){T-=2.25f/Step;return Scale*T*T+.9375f;}
        T-=2.625f/Step;return Scale*T*T+.984375f;
    }
    FString SafeComponent(const FString& In)
    {
        FString Result;
        for(TCHAR Ch:In)
            Result.AppendChar(((Ch>=TEXT('A')&&Ch<=TEXT('Z'))||(Ch>=TEXT('a')&&Ch<=TEXT('z'))||(Ch>=TEXT('0')&&Ch<=TEXT('9'))||Ch==TEXT('_'))?Ch:TEXT('_'));
        if(Result.IsEmpty()) Result=TEXT("Asset");
        if(Result[0]>=TEXT('0')&&Result[0]<=TEXT('9')) Result=TEXT("T_")+Result;
        return Result;
    }
}

ABYMBoard::ABYMBoard()
{
    PrimaryActorTick.bCanEverTick=true;
    bReplicates=false;
    BoardRoot=CreateDefaultSubobject<USceneComponent>(TEXT("BoardRoot"));
    SetRootComponent(BoardRoot);
    Music=CreateDefaultSubobject<UAudioComponent>(TEXT("Music"));
    Music->SetupAttachment(BoardRoot);
    Music->bAutoActivate=false;
    Music->bAllowSpatialization=false;
    Music->bIsUISound=true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
    PlaneMesh=Mesh.Object;
}

FString ABYMBoard::AssetObjectPath(const FString& RelativePath)
{
    FString Path=RelativePath;
    Path.ReplaceInline(TEXT("\\"),TEXT("/"));
    if(Path.Contains(TEXT(".."))||Path.StartsWith(TEXT("/"))||Path.Contains(TEXT(":"))) return FString();
    TArray<FString> Parts; Path.ParseIntoArray(Parts,TEXT("/"),true);
    if(Parts.IsEmpty()) return FString();
    Parts.Last()=FPaths::GetBaseFilename(Parts.Last());
    FString Result=TEXT("/BackyardMMO/Art");
    for(const FString& Part:Parts) Result+=TEXT("/")+SafeComponent(Part);
    return Result+TEXT(".")+SafeComponent(Parts.Last());
}

void ABYMBoard::InitializePresentation()
{
    ProjectionBaseCells=FMath::Clamp(GetDefault<UBYMSettings>()->YardCells,40,100);
    YardBounds=FIntRect(0,0,FMath::CeilToInt(ProjectionBaseCells*1.25),ProjectionBaseCells);
    GridCells=YardBounds.Width();
    CellUnits=FMath::Clamp(GetDefault<UBYMSettings>()->CellSize,20.f,1000.f);
    SpriteMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/BackyardMMO/Materials/M_Sprite.M_Sprite"));
    AlphaSpriteMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/BackyardMMO/Materials/M_SpriteAlpha.M_SpriteAlpha"));
    if(!PlaneMesh) ReportPresentationError(TEXT("The board plane mesh could not be loaded."));
    if(!SpriteMaterial) ReportPresentationError(TEXT("The board sprite material could not be loaded."));
    if(!AlphaSpriteMaterial) ReportPresentationError(TEXT("The original-art alpha material could not be loaded."));
    if(GetNetMode()!=NM_DedicatedServer)
    {
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
        FString Json;TSharedPtr<FJsonObject> Catalog;
        if(Plugin.IsValid()&&FFileHelper::LoadFileToString(Json,*(Plugin->GetContentDir()/TEXT("Data/catalog.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Catalog)&&Catalog.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
            if(Catalog->TryGetArrayField(TEXT("buildings"),Items))for(const auto& Value:*Items)
                if(Value.IsValid()&&Value->Type==EJson::Object){const auto O=Value->AsObject();BuildingCatalog.Add(static_cast<int32>(Number(O,TEXT("id"))),O);}
            if(Catalog->TryGetArrayField(TEXT("monsters"),Items))for(const auto& Value:*Items)
                if(Value.IsValid()&&Value->Type==EJson::Object){const auto O=Value->AsObject();MonsterCatalog.Add(String(O,TEXT("id")),O);}
            WorkerSprite=Object(Object(Catalog,TEXT("sprites")),TEXT("worker"));
            CombatEffects=Object(Catalog,TEXT("effects"));
            CombatSounds=Object(Catalog,TEXT("combatSounds"));
        }
        else ReportPresentationError(TEXT("The local artwork catalog is missing or invalid."));
    }
    if(GetNetMode()!=NM_DedicatedServer) MakeGround();
}

void ABYMBoard::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
#if WITH_EDITOR
    // Display the actual terrain in an opened map, without starting accounts or simulation.
    if(!IsTemplate() && GetWorld() && GetWorld()->WorldType==EWorldType::Editor && !bBuildingEditorPreview)
    {
        TGuardValue<bool> Guard(bBuildingEditorPreview,true);
        InitializePresentation();
    }
#endif
}

void ABYMBoard::ReportPresentationError(const FString& Error)
{
    if(!PresentationError.IsEmpty()) return;
    PresentationError=Error;
    UE_LOG(LogTemp,Error,TEXT("Backyard Monsters NXT artwork: %s"),*Error);
}

void ABYMBoard::BeginPlay()
{
    Super::BeginPlay();
    InitializePresentation();
    if(GetNetMode()!=NM_DedicatedServer)
    {
        MakeGround();
        auto LoadMusic=[](const FString& Relative)->USoundWave*
        {
            USoundWave* Wave=LoadObject<USoundWave>(nullptr,*ABYMBoard::AssetObjectPath(Relative));
            if(Wave) Wave->bLooping=true;
            return Wave;
        };
        BuildingMusic=LoadMusic(TEXT("sounds/music/Music_Building.wav"));
        BattleMusic=LoadMusic(TEXT("sounds/music/Music_Attack.wav"));
        DefenseMusic=LoadMusic(TEXT("sounds/music/Music_UnderAttack.wav"));
        SetMusicMode(TEXT("building"));
    }
}

UStaticMeshComponent* ABYMBoard::MakePlane(const FString& Key)
{
    if(FBYMSpriteVisual* Existing=Sprites.Find(Key)) return Existing->Mesh.Get();
    // Ground is opaque. Original artwork needs true alpha to retain antialiased
    // edges and the source's partially transparent multiply-shadow conversion.
    UMaterialInterface* BaseMaterial=Key==TEXT("ground_field")?SpriteMaterial.Get():AlphaSpriteMaterial.Get();
    if(!PlaneMesh||!BaseMaterial)
    {
        if(!bLoggedMissingMaterial)
        {
            UE_LOG(LogTemp,Error,TEXT("Backyard Monsters NXT: generated sprite material is missing. Run Backyard Monsters NXT > Setup All before cooking."));
            bLoggedMissingMaterial=true;
        }
        return nullptr;
    }
    // Editor previews must not be saved into the map or copied into PIE.
    UStaticMeshComponent* Mesh=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient|RF_DuplicateTransient);
    Mesh->SetupAttachment(BoardRoot);
    Mesh->SetStaticMesh(PlaneMesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->SetRenderCustomDepth(true);
    Mesh->SetCustomDepthStencilValue(1);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->RegisterComponent();
    if(GetWorld() && GetWorld()->IsGameWorld()) AddInstanceComponent(Mesh);
    UMaterialInstanceDynamic* MID=UMaterialInstanceDynamic::Create(BaseMaterial,this);
    MID->SetVectorParameterValue(TEXT("UVScale"),FLinearColor(1,1,0,0));
    MID->SetVectorParameterValue(TEXT("UVOffset"),FLinearColor(0,0,0,0));
    MID->SetVectorParameterValue(TEXT("SpriteTint"),FLinearColor::White);
    Mesh->SetMaterial(0,MID);
    FBYMSpriteVisual Entry; Entry.Mesh=Mesh; Entry.Material=MID;
    Sprites.Add(Key,Entry); VisualComponents.Add(Mesh);
    return Mesh;
}

void ABYMBoard::PutSprite(const FString& Key,const FString& Texture,const FVector& Location,const FVector2D& Size,const FLinearColor& Tint,bool bSmooth)
{
    UStaticMeshComponent* Mesh=MakePlane(Key); if(!Mesh) return;
    FBYMSpriteVisual& Entry=Sprites.FindChecked(Key);
    if(Entry.TexturePath!=Texture)
    {
        UTexture2D* Tex=nullptr;
        if(const TObjectPtr<UTexture2D>* Found=TextureCache.Find(Texture)) Tex=Found->Get();
        else
        {
            FString Path=AssetObjectPath(Texture);
            if(!Path.IsEmpty()) Tex=LoadObject<UTexture2D>(nullptr,*Path);
            // An unavailable original image is shown as a missing visual, never a fabricated replacement.
            TextureCache.Add(Texture,Tex);
        }
        Entry.TexturePath=Texture;
        // A reused effect/building may change its texture without changing its
        // descriptor pointer. The old sheet layout must not survive that swap.
        Entry.bAtlasReady=false;
        Entry.AtlasTexturePath.Empty();
        if(!Tex) ReportPresentationError(TEXT("Artwork texture could not be loaded: ")+Texture);
        Mesh->SetVisibility(Entry.bPresentationVisible&&Tex!=nullptr);
        if(Tex&&Entry.Material.IsValid()) Entry.Material->SetTextureParameterValue(TEXT("SpriteTexture"),Tex);
    }
    const bool bNewTarget=!Entry.Target.Equals(Location,.001f);
    if(bNewTarget||!Entry.bInitialized)
    {
        Entry.InterpolationStart=Entry.BasePosition;
        Entry.InterpolationElapsed=0.f;
        Entry.InterpolationDuration=SnapshotInterval;
    }
    Entry.Target=Location;
    Entry.bMoving=bSmooth;
    if(!bSmooth||!Entry.bInitialized)
    {
        Entry.BasePosition=Location;
        Entry.InterpolationStart=Location;
        Mesh->SetRelativeLocation(Location);
    }
    Entry.bInitialized=true;
    Mesh->SetRelativeScale3D(FVector(Size.X/100.f,Size.Y/100.f,1));
    // All sprites are flat planes. Sort by their source-derived ground depth,
    // not radial camera distance, so overlapping alpha artwork remains stable.
    Mesh->SetTranslucentSortPriority(SpriteSortPriority(Location.Z));
    if(Entry.Material.IsValid()) Entry.Material->SetVectorParameterValue(TEXT("SpriteTint"),Tint);
}

float ABYMBoard::CalculateTerrainMargin(float CellSize)
{
    return FMath::Max(8192.f*FMath::Clamp(CellSize,20.f,1000.f)/20.f,20000.f);
}

FVector ABYMBoard::GridToLocal(const FVector2D& GridPoint,float Height) const
{
    // Original GRID.ToISO projects ground coordinates, not the already-rendered
    // artwork. Keep the yard centre unchanged for existing saves and cameras.
    return FVector((GridPoint.X-GridPoint.Y)*CellUnits+ProjectionBaseCells*CellUnits*.5,
        (GridPoint.X+GridPoint.Y)*CellUnits*.5,Height);
}

FVector ABYMBoard::GridToWorld(const FVector2D& GridPoint,float Height) const
{
    return GetActorTransform().TransformPosition(GridToLocal(GridPoint,Height));
}

FVector2D ABYMBoard::WorldToGrid(const FVector& WorldPoint) const
{
    const FVector Local=GetActorTransform().InverseTransformPosition(WorldPoint);
    const double ProjectedX=(Local.X-ProjectionBaseCells*CellUnits*.5)/CellUnits;
    const double ProjectedY=Local.Y/CellUnits;
    return FVector2D(ProjectedY+ProjectedX*.5,ProjectedY-ProjectedX*.5);
}

void ABYMBoard::PutBuildingSprite(const FString& Key,const TSharedPtr<FJsonObject>& Building,
    const FLinearColor& Tint,bool bPreview)
{
    const int32 Type=static_cast<int32>(Number(Building,TEXT("type")));
    const auto Definition=BuildingCatalog.FindRef(Type);
    FString Texture=String(Building,TEXT("texture"),String(Definition,TEXT("texture")));
    const int32 Level=FMath::Max(1,static_cast<int32>(Number(Building,TEXT("level"),1)));
    const TArray<TSharedPtr<FJsonValue>>* Levels=nullptr;
    TSharedPtr<FJsonObject> LevelDefinition;
    if(Definition.IsValid()&&Definition->TryGetArrayField(TEXT("levels"),Levels)&&Levels->IsValidIndex(Level-1))
    {
        LevelDefinition=(*Levels)[Level-1]->AsObject();
        Texture=String(LevelDefinition,TEXT("texture"),Texture);
    }
    const double HP=Number(Building,TEXT("hp"),1);
    const double MaxHP=FMath::Max(1.,Number(Building,TEXT("maxHp"),HP));
    TSharedPtr<FJsonObject> Composite=Object(LevelDefinition,
        HP<=0?TEXT("destroyedComposite"):(HP<MaxHP*.5?TEXT("damagedComposite"):TEXT("composite")));
    if(!Composite.IsValid()) Composite=Object(LevelDefinition,TEXT("composite"));
    Texture=Composite.IsValid()?String(Composite,TEXT("texture"),Texture):String(LevelDefinition,TEXT("compositeTexture"),Texture);
    TSharedPtr<FJsonObject> Animation=HP>0&&!bPreview?Object(LevelDefinition,TEXT("animation")):nullptr;
    if(Type==140&&HP>=MaxHP*.5&&HP>0&&!bPreview)
    {
        const auto Relationships=Object(LevelDefinition,TEXT("mapRoomRelationshipAnimations"));
        const FString Relationship=String(LastYard,TEXT("mapRelationship"),TEXT("neutral"));
        Animation=Object(Relationships,*Relationship);
        if(!Animation.IsValid())Animation=Object(Relationships,TEXT("neutral"));
    }
    if(Animation.IsValid()&&HP<MaxHP*.5)
    {
        const auto Damaged=Object(Animation,TEXT("damaged"));
        // A damaged building must use its original damaged art. Never cover it
        // with the normal tower's animation when no damaged atlas exists.
        Animation=Damaged;
    }
    if(Animation.IsValid())Texture=String(Animation,TEXT("texture"),Texture);
    const float Footprint=static_cast<float>(Number(Building,TEXT("size"),
        FMath::Max(1.,Number(Definition,TEXT("size"),100)/20.)));
    const double X=Number(Building,TEXT("x")),Y=Number(Building,TEXT("y"));
    UTexture2D* Image=TextureCache.FindRef(Texture).Get();
    if(!Image)
    {
        const FString Path=AssetObjectPath(Texture);
        if(!Path.IsEmpty()) Image=LoadObject<UTexture2D>(nullptr,*Path);
        if(Image) TextureCache.Add(Texture,Image);
    }
    const float PixelUnits=CellUnits/20.f;
    const auto Geometry=Animation.IsValid()?Animation:Composite;
    const float Width=Animation.IsValid()?Number(Animation,TEXT("frameWidth"))*PixelUnits:(Image?Image->GetSizeX()*PixelUnits:Footprint*CellUnits);
    const float Height=Animation.IsValid()?Number(Animation,TEXT("frameHeight"))*PixelUnits:(Image?Image->GetSizeY()*PixelUnits:Footprint*CellUnits);
    const double OffsetX=Geometry.IsValid()?Width*.5-Number(Geometry,TEXT("pivotX"))*PixelUnits:0.;
    const double OffsetY=Geometry.IsValid()?Height*.5-Number(Geometry,TEXT("pivotY"))*PixelUnits:0.;
    // BFOUNDATION.Setup anchors the image at ToISO(saved X,Y), the back corner
    // of its footprint. Composite offsets are already screen-space pixels.
    // Source depth sorting uses projected Y plus half the footprint, then X.
    const float Depth=static_cast<float>((X+Y+Footprint)*.5+(X-Y)*.0005);
    const FVector Anchor=GridToLocal(FVector2D(X,Y),(bPreview?28.f:10.f)+Depth*.02f);
    PutSprite(Key,Texture,Anchor+FVector(OffsetX,OffsetY,0),FVector2D(Width,Height),Tint);
    SetAtlas(Key,Animation);
    if(FBYMSpriteVisual* Visual=Sprites.Find(Key))
    {
        Visual->RequestedState=TEXT("idle");
        Visual->ActivitySeed=GetTypeHash(Key);
        Visual->bActivityWorking=Boolean(Building,TEXT("working"))
            ||(Type==140&&HP>=MaxHP*.5&&HP>0&&Animation.IsValid());
        if(String(Animation,TEXT("mode"))==TEXT("resource_fill"))
        {
            const auto Wallet=Object(LastYard,TEXT("resources"));
            const double Total=Number(Wallet,TEXT("twigs"))+Number(Wallet,TEXT("pebbles"))
                +Number(Wallet,TEXT("putty"))+Number(Wallet,TEXT("goo"));
            const double Capacity=Number(LastYard,TEXT("storageCapacity"))*4.;
            const double Ratio=Capacity>0.?FMath::Clamp(Total/Capacity,0.,1.):0.;
            Visual->FillFrame=FMath::FloorToInt(Number(Animation,TEXT("fillFrameScale"),26)*Ratio);
        }
        UpdateSpriteFrame(*Visual,false);
    }
}

void ABYMBoard::MakeGround()
{
    if(bGroundCreated||!SpriteMaterial) return;
    bGroundCreated=true;
    const FVector A=GridToLocal(FVector2D(YardBounds.Min.X,YardBounds.Max.Y),0);
    const FVector B=GridToLocal(FVector2D(YardBounds.Max.X,YardBounds.Min.Y),0);
    const double MinY=GridToLocal(FVector2D(YardBounds.Min.X,YardBounds.Min.Y),0).Y;
    const double MaxY=GridToLocal(FVector2D(YardBounds.Max.X,YardBounds.Max.Y),0).Y;
    // MAPBG composes a 1000 x 500 image; MAP repeats it without stretching its
    // already-projected pixels. Use the same 20-source-pixels-per-cell scale as
    // buildings and continue the terrain beyond the playable grid, like MAP's
    // 8192-pixel outer-ground frame. The world-unit minimum covers camera zoom.
    const float PixelUnits=CellUnits/20.f;
    const float Margin=GetTerrainMargin();
    const FVector2D GroundSize(B.X-A.X+2.f*Margin,MaxY-MinY+2.f*Margin);
    const FString Texture=TEXT("derived/terrain/grass_blended_tile.png");
    PutSprite(TEXT("ground_field"),Texture,FVector((A.X+B.X)*.5,(MinY+MaxY)*.5,-10),GroundSize,FLinearColor::White);
    FBYMSpriteVisual* Ground=Sprites.Find(TEXT("ground_field"));
    UTexture2D* Tile=TextureCache.FindRef(Texture).Get();
    if(Ground&&Ground->Material.IsValid()&&Tile&&Tile->GetSizeX()>0&&Tile->GetSizeY()>0)
    {
        const float TileWidth=Tile->GetSizeX()*PixelUnits;
        const float TileHeight=Tile->GetSizeY()*PixelUnits;
        Ground->Material->SetVectorParameterValue(TEXT("UVScale"),FLinearColor(GroundSize.X/TileWidth,GroundSize.Y/TileHeight,0,0));
        // Keep world zero at the same texture phase when server grid settings change.
        Ground->Material->SetVectorParameterValue(TEXT("UVOffset"),FLinearColor((A.X-Margin)/TileWidth,(MinY-Margin)/TileHeight,0,0));
    }
}

void ABYMBoard::SetAtlas(const FString& Key,const TSharedPtr<FJsonObject>& Data)
{
    FBYMSpriteVisual* Visual=Sprites.Find(Key); if(!Visual||!Visual->Material.IsValid()) return;
    if(Visual->bAtlasReady&&Visual->Atlas==Data&&Visual->AtlasTexturePath==Visual->TexturePath)return;
    Visual->PendingAtlas=Data;
    Visual->bAtlasReady=false;
    TryConfigureAtlas(*Visual,Data);
}

bool ABYMBoard::TryConfigureAtlas(FBYMSpriteVisual& Visual,TSharedPtr<FJsonObject> Data)
{
    if(!Visual.Material.IsValid())return false;
    // OutpostDefender swaps only its relationship light bitmap. Its source
    // frame number continues when ownership changes; do not restart that cycle.
    const bool bKeepRelationshipClock=String(Visual.Atlas,TEXT("activityGate"))==TEXT("relationship_light")
        &&String(Data,TEXT("activityGate"))==TEXT("relationship_light")
        &&static_cast<double>(Visual.Frames)==Number(Data,TEXT("frames"));
    // Never commit a descriptor pointer before initialization succeeds. An
    // asynchronously compiling texture can initially expose a default resource;
    // caching those dimensions used to permanently compress several poses into
    // one resident's quad while later residents initialized correctly.
    if(Data.IsValid())
    {
        UTexture2D* Texture=TextureCache.FindRef(Visual.TexturePath).Get();
        bool bTextureReady=Texture!=nullptr;
#if WITH_EDITOR
        // Async texture-compilation placeholders and this API are editor-only.
        // Retain the retry in the editor without referencing it in cooked games.
        if(bTextureReady)bTextureReady=!Texture->IsDefaultTexture();
#endif
        if(!bTextureReady)
        {
            if(Visual.Mesh.IsValid())Visual.Mesh->SetVisibility(false);
            return false;
        }
        // Exact original PNG dimensions also preserve trailing sheet padding.
        // GetSize is a compatibility fallback only, after the resource is ready.
        const double SheetWidth=Number(Data,TEXT("textureWidth"),Texture->GetSizeX());
        const double SheetHeight=Number(Data,TEXT("textureHeight"),Texture->GetSizeY());
        const auto ReadCount=[&Data](const TCHAR* Field,int32& Count)
        {
            const double Value=Number(Data,Field,0);
            if(!FMath::IsFinite(Value)||Value<0||Value>65536.)return false;
            if(Value!=static_cast<int32>(Value))return false;
            Count=static_cast<int32>(Value);return true;
        };
        int32 Columns=0,Rows=0,Frames=0,Directions=0;
        BYMSpriteAtlas::FLayout Layout;
        if(!ReadCount(TEXT("columns"),Columns)||!ReadCount(TEXT("rows"),Rows)
            ||!ReadCount(TEXT("frames"),Frames)||!ReadCount(TEXT("directions"),Directions)
            ||!BYMSpriteAtlas::MakeLayout(Number(Data,TEXT("frameWidth")),Number(Data,TEXT("frameHeight")),
                SheetWidth,SheetHeight,Columns,Rows,Frames,Layout))
        {
            if(Visual.Mesh.IsValid())Visual.Mesh->SetVisibility(false);
            ReportPresentationError(TEXT("Invalid sprite frame layout: ")+Visual.TexturePath);
            return false;
        }
        Visual.AtlasLayout=Layout;
        Visual.Columns=Layout.Columns;Visual.Rows=Layout.Rows;Visual.Frames=Layout.Frames;
        Visual.Directions=FMath::Clamp(Directions>0?Directions:Layout.Columns,1,Layout.Columns);
        Visual.bDirectional=String(Data,TEXT("layout")).Contains(TEXT("facing"))||Directions>1;
        Visual.FacingOffset=Number(Data,TEXT("sourceFacingOffsetDegrees"),0);
        Visual.FPS=FMath::Clamp(static_cast<float>(Number(Data,TEXT("fps"),12)),0.f,72.f);
        Visual.Material->SetVectorParameterValue(TEXT("UVScale"),FLinearColor(Layout.Scale.X,Layout.Scale.Y,0,0));
        Visual.Material->SetVectorParameterValue(TEXT("UVOffset"),FLinearColor(Layout.Inset.X,Layout.Inset.Y,0,0));
    }
    else
    {
        Visual.AtlasLayout=BYMSpriteAtlas::FLayout();
        Visual.Columns=1;Visual.Rows=1;Visual.Frames=1;Visual.Directions=1;
        Visual.bDirectional=false;Visual.FacingOffset=0;
        Visual.Material->SetVectorParameterValue(TEXT("UVScale"),FLinearColor(1,1,0,0));
        Visual.Material->SetVectorParameterValue(TEXT("UVOffset"),FLinearColor(0,0,0,0));
    }
    Visual.Atlas=Data;
    if(!bKeepRelationshipClock)Visual.ActivityClock=BYMBuildingAnimation::FClock();
    Visual.FillDisplayTick=-1;
    Visual.DisplayedFillFrame=0;
    Visual.ActivityFrames.Empty();
    const TArray<TSharedPtr<FJsonValue>>* SourceCycle=nullptr;
    if(Data.IsValid()&&Data->TryGetArrayField(TEXT("sourceFrameCycle"),SourceCycle))
        for(const auto& FrameValue:*SourceCycle)
            if(FrameValue.IsValid()&&FrameValue->Type==EJson::Number)
                Visual.ActivityFrames.Add(FMath::Clamp(static_cast<int32>(FrameValue->AsNumber()),0,Visual.Frames-1));
    Visual.PendingAtlas.Reset();
    Visual.AtlasTexturePath=Visual.TexturePath;
    Visual.bAtlasReady=true;
    Visual.DisplayedState.Empty();Visual.AnimationFrames.Empty();
    if(Visual.Mesh.IsValid())Visual.Mesh->SetVisibility(Visual.bPresentationVisible&&TextureCache.FindRef(Visual.TexturePath)!=nullptr);
    return true;
}

void ABYMBoard::SetAnimation(FBYMSpriteVisual& Visual,const FString& State)
{
    if(Visual.DisplayedState==State)return;
    const FString PreviousState=Visual.DisplayedState;
    Visual.DisplayedState=State;
    Visual.AnimationStartedAt=Visual.bMonster&&State==TEXT("attack")?Visual.AttackStartedAt:AnimationClock;
    Visual.AnimationFrames.Empty();
    Visual.LoopStartFrame=0;
    Visual.bLoop=true;
    const auto Animation=Object(Object(Visual.Atlas,TEXT("animations")),*State);
    const TArray<TSharedPtr<FJsonValue>>* Frames=nullptr;
    if(Visual.bMonster)
    {
        // The original sheets have explicit state rows. Facing columns are
        // never played as an animation; many source monsters have one pose.
        if(Animation.IsValid()&&Animation->TryGetArrayField(TEXT("rows"),Frames))
            for(const auto& V:*Frames)if(V.IsValid()&&V->Type==EJson::Number)
                Visual.AnimationFrames.Add(FMath::Clamp(static_cast<int32>(V->AsNumber()),0,Visual.Rows-1));
        if(Visual.AnimationFrames.IsEmpty())Visual.AnimationFrames.Add(0);
        Visual.FPS=FMath::Clamp(static_cast<float>(Number(Animation,TEXT("fps"),Number(Visual.Atlas,TEXT("fps"),0))),0.f,72.f);
        Visual.bLoop=Boolean(Animation,TEXT("loop"),true);
    }
    else if(Visual.Atlas.IsValid())
    {
        const bool bFiring=String(Visual.Atlas,TEXT("mode"))==TEXT("firing");
        const bool bCooling=bFiring&&(State==TEXT("cool")||(State==TEXT("idle")&&PreviousState==TEXT("attack")));
        const bool bCharging=bFiring&&State==TEXT("charge");
        if(bCooling)
            for(int32 Frame=FMath::Clamp(Visual.CoolingStartFrame,0,Visual.Frames-1);Frame<Visual.Frames;++Frame)
                Visual.AnimationFrames.Add(Frame);
        const TCHAR* Field=bCharging?TEXT("startFrames"):(State==TEXT("attack")?TEXT("attackFrames"):TEXT("idleFrames"));
        if(Visual.Atlas->TryGetArrayField(Field,Frames))
            for(const auto& V:*Frames)if(V.IsValid()&&V->Type==EJson::Number)
                Visual.AnimationFrames.Add(FMath::Clamp(static_cast<int32>(V->AsNumber()),0,Visual.Columns*Visual.Rows-1));
        if(Visual.AnimationFrames.IsEmpty())
        {
            if(bFiring)Visual.AnimationFrames.Add(0);
            else for(int32 Frame=0;Frame<Visual.Frames;++Frame)Visual.AnimationFrames.Add(Frame);
        }
        Visual.FPS=FMath::Clamp(static_cast<float>(Number(Visual.Atlas,bCooling?TEXT("endFps"):TEXT("fps"),12)),0.f,72.f);
        Visual.bLoop=!bCooling&&!bCharging&&Boolean(Visual.Atlas,TEXT("loop"),true);
    }
}

void ABYMBoard::UpdateSpriteFrame(FBYMSpriteVisual& Visual,bool bTravelling)
{
    if(!Visual.Material.IsValid())return;
    if(!Visual.bAtlasReady&&Visual.PendingAtlas.IsValid())TryConfigureAtlas(Visual,Visual.PendingAtlas);
    if(!Visual.bAtlasReady||!Visual.Atlas.IsValid())return;
    const FString AtlasMode=String(Visual.Atlas,TEXT("mode"));
    if(AtlasMode==TEXT("bunker_door"))
    {
        const double Phase=BYMBuildingAnimation::BunkerDoorPhase(Visual.BunkerDoorPhase,
            FMath::Max(0.,BunkerClock-Visual.BunkerDoorClock),Visual.bBunkerDoorOpening,Visual.bBunkerDoorMoving);
        const int32 Frame=BYMBuildingAnimation::BunkerDoorFrame(Phase,Visual.Frames);
        const FVector2D Offset=BYMSpriteAtlas::FrameOffset(Visual.AtlasLayout,Frame%Visual.Columns,Frame/Visual.Columns);
        Visual.Material->SetVectorParameterValue(TEXT("UVOffset"),FLinearColor(Offset.X,Offset.Y,0,0));
        return;
    }
    if(AtlasMode==TEXT("activity")||AtlasMode==TEXT("resource_fill"))
    {
        int32 Frame=Visual.FillFrame;
        if(AtlasMode==TEXT("activity"))
        {
            const int32 CycleLength=FMath::Max(1,Visual.ActivityFrames.Num());
            const bool bRandom=Boolean(Visual.Atlas,TEXT("sourceRandomStart"));
            const int32 StartFrames=FMath::Clamp(static_cast<int32>(Number(Visual.Atlas,TEXT("sourceRandomStartFrames"),FMath::Max(1,CycleLength-2))),1,CycleLength);
            const int32 PhaseCount=FMath::Clamp(static_cast<int32>(Number(Visual.Atlas,TEXT("sourceDisplayPhaseCount"),1)),1,40);
            const int32 Ordinal=BYMBuildingAnimation::SelectFrame(Visual.ActivityClock,AnimationClock,
                Number(Visual.Atlas,TEXT("sourceDisplayHz"),40),
                static_cast<int32>(Number(Visual.Atlas,TEXT("fastTickDivisor"),3)),
                static_cast<int32>(Number(Visual.Atlas,TEXT("slowTickDivisor"),10)),CycleLength,
                Visual.bActivityWorking,bHasBattle||!Residents.IsEmpty(),
                String(Visual.Atlas,TEXT("pausedFrameBehavior"))==TEXT("reset"),
                bRandom?static_cast<int32>(Visual.ActivitySeed%StartFrames):0,
                static_cast<int32>(Visual.ActivitySeed%PhaseCount));
            Frame=Visual.ActivityFrames.IsValidIndex(Ordinal)?Visual.ActivityFrames[Ordinal]:0;
        }
        else
        {
            const double DisplayHz=FMath::Clamp(Number(Visual.Atlas,TEXT("sourceDisplayHz"),40),1.,120.);
            const int64 FillTick=static_cast<int64>(FMath::Max(0.,static_cast<double>(AnimationClock))*DisplayHz);
            const int64 RefreshTicks=FMath::Clamp(static_cast<int64>(Number(Visual.Atlas,TEXT("refreshDisplayTicks"),3)),static_cast<int64>(1),static_cast<int64>(120));
            if(Visual.FillDisplayTick<0||FillTick<Visual.FillDisplayTick||FillTick/RefreshTicks>Visual.FillDisplayTick/RefreshTicks)
            {
                Visual.DisplayedFillFrame=Visual.FillFrame;
                Visual.FillDisplayTick=FillTick;
            }
            Frame=Visual.DisplayedFillFrame;
        }
        Frame=FMath::Clamp(Frame,0,Visual.Frames-1);
        const FVector2D Offset=BYMSpriteAtlas::FrameOffset(Visual.AtlasLayout,Frame%Visual.Columns,Frame/Visual.Columns);
        Visual.Material->SetVectorParameterValue(TEXT("UVOffset"),FLinearColor(Offset.X,Offset.Y,0,0));
        return;
    }
    if(Visual.bMonster)
    {
        // MonsterBase.render uses screen/isometric _xd/_yd, while BTOWER.Rotate
        // uses logical coordinates. Do not infer facing from interpolated mesh
        // locations: pivots and snapshot latency would turn sprites incorrectly.
        const FVector2D Heading(Visual.LogicalFacing.X-Visual.LogicalFacing.Y,
            (Visual.LogicalFacing.X+Visual.LogicalFacing.Y)*.5);
        const double Angle=FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Heading.Y,Heading.X))+Visual.FacingOffset+360.,360.);
        Visual.Facing=FMath::FloorToInt(Angle/360.*Visual.Directions)%Visual.Directions;
    }
    FString State=Visual.RequestedState;
    if(Visual.DeathStartedAt>=0)State=TEXT("death");
    else if(Visual.bMonster&&!Visual.bAmbient)
    {
        // Finish the bounded interpolation before switching to the attack pose.
        // Stale 'moving' snapshots must not leave a stopped sprite walking forever.
        if(bTravelling)State=TEXT("walk");
        else if(State==TEXT("walk"))State=TEXT("idle");
    }
    SetAnimation(Visual,State);
    if(Visual.EffectDuration>0.f)
    {
        // Delayed texture readiness must not restart a projectile's timeline
        // or discard smoke's lifetime-selected frame rate.
        Visual.AnimationStartedAt=Visual.EffectStartedAt;
        if(String(Visual.Atlas,TEXT("animationMode"))==TEXT("particle_life_selects_frame"))
            Visual.FPS=Visual.Frames/Visual.EffectDuration;
    }
    const int32 Count=Visual.AnimationFrames.Num();
    const int32 Tick=FMath::Max(0,FMath::FloorToInt((AnimationClock-Visual.AnimationStartedAt)*Visual.FPS));
    const int32 LoopStart=FMath::Clamp(Visual.LoopStartFrame,0,FMath::Max(0,Count-1));
    const int32 Index=Count>0?(Visual.bLoop&&Tick>=Count?LoopStart+(Tick-LoopStart)%(Count-LoopStart):FMath::Min(Tick,Count-1)):0;
    int32 Column=0,Row=0;
    if(Visual.bMonster)
    {
        Column=FMath::Clamp(Visual.Facing,0,Visual.Columns-1);
        Row=Count>0?Visual.AnimationFrames[Index]:0;
    }
    else
    {
        const int32 Frame=String(Visual.Atlas,TEXT("mode"))==TEXT("directional")
            ?FMath::Clamp(Visual.Facing,0,Visual.Columns*Visual.Rows-1)
            :(Count>0?Visual.AnimationFrames[Index]:0);
        Column=Frame%Visual.Columns;Row=Frame/Visual.Columns;
    }
    const FVector2D Offset=BYMSpriteAtlas::FrameOffset(Visual.AtlasLayout,Column,Row);
    Visual.Material->SetVectorParameterValue(TEXT("UVOffset"),FLinearColor(Offset.X,Offset.Y,0,0));
}

void ABYMBoard::PutMonsterSprite(const FString& Key,const FString& Type,const FVector2D& Position,
    const FVector2D& Facing,const FString& State,bool bSmooth,bool bAmbient)
{
    const auto Definition=MonsterCatalog.FindRef(Type);if(!Definition.IsValid())return;
    const auto Sprite=Object(Definition,TEXT("sprite"));
    const float Width=Number(Sprite,TEXT("frameWidth"),Number(Definition,TEXT("frameWidth"),32));
    const float Height=Number(Sprite,TEXT("frameHeight"),Number(Definition,TEXT("frameHeight"),32));
    const float PixelUnits=CellUnits/20.f;
    const FVector Offset((Width*.5-Number(Sprite,TEXT("pivotX"),Width*.5))*PixelUnits,
        (Height*.5-Number(Sprite,TEXT("pivotY"),Height*.5))*PixelUnits,0);
    const float Depth=(Position.X+Position.Y)*.5f+(Position.X-Position.Y)*.0005f;
    PutSprite(Key,String(Definition,TEXT("texture")),GridToLocal(Position,10.01f+Depth*.02f)+Offset,
        FVector2D(Width,Height)*PixelUnits,FLinearColor::White,bSmooth);
    SetAtlas(Key,Sprite.IsValid()?Sprite:Definition);
    if(FBYMSpriteVisual* Visual=Sprites.Find(Key))
    {
        Visual->bMonster=true;Visual->bAmbient=bAmbient;Visual->RequestedState=State;
        const auto Hover=Object(Object(Definition,TEXT("presentation")),TEXT("hover"));
        Visual->HoverAmplitude=Number(Hover,TEXT("amplitudePixels"));
        Visual->HoverAngularSpeed=Number(Hover,TEXT("angularSpeedRadiansPerSecond"));
        if(!Facing.IsNearlyZero())Visual->LogicalFacing=Facing.GetSafeNormal();
        UpdateSpriteFrame(*Visual,false);
    }
}

void ABYMBoard::UpdateResidents(const TSharedPtr<FJsonObject>& Yard)
{
    // A roster belongs to the displayed yard, never the visiting player's home.
    // Discard walkers during combat instead of retaining a hidden previous army
    // that can reappear while battle/view snapshots are changing.
    const FString RosterYard=String(Yard,TEXT("residentYardId"),String(Yard,TEXT("id")));
    if(bHasBattle||CurrentYard.IsEmpty()||RosterYard!=CurrentYard)
    {
        TArray<FString> ResidentKeys;Residents.GetKeys(ResidentKeys);
        for(const FString& Key:ResidentKeys)ReleaseSprite(Key);
        Residents.Empty();
        return;
    }
    // Keep pathfinding out of the snapshot hot path unless the yard layout
    // actually changed. Ready-army changes only reconcile the bounded roster.
    TArray<FString> BuildingIds;Buildings.GetKeys(BuildingIds);BuildingIds.Sort();
    FString Layout=FString::Printf(TEXT("%d:%d:%d:%d"),YardBounds.Min.X,YardBounds.Min.Y,YardBounds.Max.X,YardBounds.Max.Y);
    TArray<FIntRect> Obstacles;
    for(const FString& Id:BuildingIds)
    {
        const auto B=Buildings.FindRef(Id);
        const double X=Number(B,TEXT("x")),Y=Number(B,TEXT("y"));
        const int32 Type=Number(B,TEXT("type"));
        Layout+=FString::Printf(TEXT("|%s:%d:%.3f:%.3f:%.3f"),*Id,Type,X,Y,Number(B,TEXT("size")));
        const auto Def=BuildingCatalog.FindRef(Type);
        const TArray<TSharedPtr<FJsonValue>>* Rectangles=nullptr;
        bool bAdded=false;
        if(Def.IsValid()&&Def->TryGetArrayField(TEXT("collisionFootprints"),Rectangles))
            for(const auto& Value:*Rectangles)
            {
                if(!Value.IsValid()||Value->Type!=EJson::Array)continue;
                const auto& R=Value->AsArray();if(R.Num()!=4)continue;
                const double RX=R[0]->AsNumber(),RY=R[1]->AsNumber();
                const double Left=X+(RY+RX*.5)/20.,Top=Y+(RY-RX*.5)/20.;
                const double Width=R[2]->AsNumber()/20.,Height=R[3]->AsNumber()/20.;
                if(Width<=0||Height<=0)continue;
                Obstacles.Add(FIntRect(FMath::FloorToInt(Left),FMath::FloorToInt(Top),
                    FMath::CeilToInt(Left+Width),FMath::CeilToInt(Top+Height)));bAdded=true;
            }
        if(!bAdded)
        {
            const int32 Size=FMath::Max(1,static_cast<int32>(Number(B,TEXT("size"),1)));
            Obstacles.Add(FIntRect(FMath::FloorToInt(X),FMath::FloorToInt(Y),FMath::CeilToInt(X+Size),FMath::CeilToInt(Y+Size)));
        }
    }
    if(ResidentLayout!=Layout)
    {
        ResidentLayout=Layout;ResidentGrid.Reset(YardBounds,Obstacles);
        for(auto& Pair:Residents)BYMYardResidents::RevalidateWalker(Pair.Value.Walker,ResidentGrid);
    }
    const int32 Limit=FMath::Clamp(GetDefault<UBYMSettings>()->MaxVisibleYardMonsters,0,BYMYardResidents::MaximumResidents);
    TSet<FString> Seen;
    const TArray<TSharedPtr<FJsonValue>>* Roster=nullptr;
    // This is the server's public presentation roster, never a fabricated army.
    if(Yard.IsValid()&&Yard->TryGetArrayField(TEXT("residentRoster"),Roster))
        for(const auto& Value:*Roster)
        {
            if(Seen.Num()>=Limit)break;
            if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
            const auto Resident=Value->AsObject();
            const FString Id=String(Resident,TEXT("id")),Type=String(Resident,TEXT("type"));
            if(Id.IsEmpty()||!MonsterCatalog.Contains(Type))continue;
            // Different yards often own C1:0, C1:1 etc. Keep their visual
            // identity separate even across rapid scouting/snapshot changes.
            const FString Key=TEXT("r_")+CurrentYard+TEXT(":")+Id;Seen.Add(Key);
            FBYMResidentVisual* Existing=Residents.Find(Key);
            if(Existing&&Existing->Type!=Type)
            {
                ReleaseSprite(Key);Residents.Remove(Key);Existing=nullptr;
            }
            if(!Existing)
            {
                FBYMResidentVisual NewResident;NewResident.Type=Type;
                BYMYardResidents::InitializeWalker(NewResident.Walker,ResidentGrid,GetTypeHash(CurrentYard+TEXT(":")+Id));
                Residents.Add(Key,MoveTemp(NewResident));Existing=Residents.Find(Key);
            }
            if(Existing&&Existing->Walker.bInitialized)
                PutMonsterSprite(Key,Existing->Type,Existing->Walker.Position,Existing->Walker.Facing,
                    Existing->Walker.bMoving?TEXT("walk"):TEXT("idle"),false,true);
            if(FBYMSpriteVisual* Visual=Sprites.Find(Key))if(Visual->Mesh.IsValid())
            {
                Visual->bPresentationVisible=!bHasBattle&&Existing&&Existing->Walker.bInitialized;
                Visual->Mesh->SetVisibility(Visual->bPresentationVisible&&Visual->bAtlasReady&&TextureCache.FindRef(Visual->TexturePath)!=nullptr);
            }
        }
    TArray<FString> Keys;Residents.GetKeys(Keys);
    for(const FString& Key:Keys)if(!Seen.Contains(Key)){Residents.Remove(Key);ReleaseSprite(Key);}
}

void ABYMBoard::GetTerrainWorldBounds(FVector2D& OutMin,FVector2D& OutMax) const
{
    OutMin=FVector2D(TNumericLimits<double>::Max(),TNumericLimits<double>::Max());
    OutMax=-OutMin;
    const FVector2D Corners[]={FVector2D(YardBounds.Min.X,YardBounds.Min.Y),FVector2D(YardBounds.Max.X,YardBounds.Min.Y),
        FVector2D(YardBounds.Max.X,YardBounds.Max.Y),FVector2D(YardBounds.Min.X,YardBounds.Max.Y)};
    for(const FVector2D& Corner:Corners)
    {
        const FVector P=GridToWorld(Corner);
        OutMin.X=FMath::Min(OutMin.X,P.X);OutMin.Y=FMath::Min(OutMin.Y,P.Y);
        OutMax.X=FMath::Max(OutMax.X,P.X);OutMax.Y=FMath::Max(OutMax.Y,P.Y);
    }
}

void ABYMBoard::DrawYardBoundary()
{
    // MAP.Edge draws the true yard rectangle with a two-pixel, half-alpha
    // white line in build mode. A narrow dark underlay keeps it legible on
    // the original bright grass. Construction retains its host-validated inset.
    const double L=YardBounds.Min.X,T=YardBounds.Min.Y,R=YardBounds.Max.X,B=YardBounds.Max.Y;
    const FVector2D Corners[]={FVector2D(L,T),FVector2D(R,T),FVector2D(R,B),FVector2D(L,B)};
    for(int32 Edge=0;Edge<4;++Edge)for(int32 Layer=0;Layer<2;++Layer)
    {
        const FString Key=FString::Printf(TEXT("yard_boundary_%d_%d"),Edge,Layer);
        if(!bShowYardBoundary){ReleaseSprite(Key);continue;}
        const FVector A=GridToLocal(Corners[Edge],1.f+Layer*.01f),Z=GridToLocal(Corners[(Edge+1)%4],1.f+Layer*.01f);
        PutSprite(Key,TEXT("derived/effects/source_line.png"),(A+Z)*.5,
            FVector2D((Z-A).Size2D(),CellUnits*(Layer==0?.16:.1)),
            Layer==0?FLinearColor(0,0,0,.35f):FLinearColor(1,1,1,.5f));
        if(auto* V=Sprites.Find(Key))if(V->Mesh.IsValid())
            V->Mesh->SetRelativeRotation(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2((Z-A).Y,(Z-A).X)),0));
    }
}

bool ABYMBoard::GetWorkerFocusPosition(const FString& WorkerId,FVector& OutWorldPosition) const
{
    const auto* Worker=Workers.Find(WorkerId);if(!Worker)return false;
    FVector2D Position=Worker->Position;
    if(!Worker->Route.IsEmpty())Position=BYMWorkerMovement::SampleRoute(Worker->Route,
        WorkerServerTime+BunkerClock-WorkerSnapshotClock-Worker->StartedAt).Position;
    OutWorldPosition=GridToWorld(Position);return true;
}

void ABYMBoard::UpdateWorkers(const TSharedPtr<FJsonObject>& Yard,double ServerTime)
{
    WorkerServerTime=ServerTime;WorkerSnapshotClock=BunkerClock;
    const auto Data=Object(Yard,TEXT("workers"));
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;TSet<FString> Seen;
    if(Data.IsValid()&&Data->TryGetArrayField(TEXT("entries"),Entries))
        for(const auto& Value:*Entries)
        {
            if(Seen.Num()>=2048)break;
            if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
            const auto Entry=Value->AsObject();const FString Id=String(Entry,TEXT("id"));if(Id.IsEmpty())continue;
            FBYMWorkerVisual Worker;Worker.Position=FVector2D(Number(Entry,TEXT("x")),Number(Entry,TEXT("y")));
            Worker.Facing=FVector2D(Number(Entry,TEXT("facingX")),Number(Entry,TEXT("facingY"),1));
            Worker.State=String(Entry,TEXT("state"));Worker.StartedAt=Number(Entry,TEXT("startedAt"));
            const TArray<TSharedPtr<FJsonValue>>* Route=nullptr;
            if(Entry->TryGetArrayField(TEXT("route"),Route)&&Route->Num()<=4096)
                for(const auto& Point:*Route)
                {
                    if(!Point.IsValid()||Point->Type!=EJson::Object)continue;
                    const auto P=Point->AsObject();BYMWorkerMovement::FRoutePoint RP;
                    RP.Position=FVector2D(Number(P,TEXT("x")),Number(P,TEXT("y")));
                    RP.AtSeconds=Number(P,TEXT("at"));RP.bHop=Boolean(P,TEXT("hop"));Worker.Route.Add(RP);
                }
            Workers.Add(Id,MoveTemp(Worker));Seen.Add(Id);
        }
    TArray<FString> Keys;Workers.GetKeys(Keys);
    for(const FString& Id:Keys)if(!Seen.Contains(Id)){Workers.Remove(Id);ReleaseSprite(TEXT("w_")+Id);}
    PresentWorkers();
}

void ABYMBoard::PresentWorkers()
{
    if(!WorkerSprite.IsValid())return;
    const float PixelUnits=CellUnits/20.f;
    const float Width=Number(WorkerSprite,TEXT("frameWidth"),27),Height=Number(WorkerSprite,TEXT("frameHeight"),27);
    TArray<FString> WorkerIds;Workers.GetKeys(WorkerIds);
    WorkerIds.Sort([this](const FString& A,const FString& B)
    {
        const bool ActiveA=Workers.FindChecked(A).State!=TEXT("idle"),ActiveB=Workers.FindChecked(B).State!=TEXT("idle");
        return ActiveA!=ActiveB?ActiveA:A<B;
    });
    int32 VisibleCount=0;
    for(const FString& Id:WorkerIds)
    {
        if(VisibleCount++>=5){ReleaseSprite(TEXT("w_")+Id);continue;}
        auto& Worker=Workers.FindChecked(Id);double Hop=0.;
        if(!Worker.Route.IsEmpty())
        {
            const auto Sample=BYMWorkerMovement::SampleRoute(Worker.Route,
                WorkerServerTime+BunkerClock-WorkerSnapshotClock-Worker.StartedAt);
            Worker.Position=Sample.Position;Worker.Facing=Sample.Facing;Hop=Sample.HopHeightPixels;
        }
        const FVector2D P=Worker.Position;const FString Key=TEXT("w_")+Id;
        const float Depth=(P.X+P.Y)*.5f+(P.X-P.Y)*.0005f;
        const FVector Offset((Width*.5-Number(WorkerSprite,TEXT("pivotX"),9))*PixelUnits,
            (Height*.5-Number(WorkerSprite,TEXT("pivotY"),19)-Hop)*PixelUnits,0);
        PutSprite(Key,String(WorkerSprite,TEXT("texture")),GridToLocal(P,10.01f+Depth*.02f)+Offset,
            FVector2D(Width,Height)*PixelUnits,FLinearColor::White);
        SetAtlas(Key,WorkerSprite);
        if(auto* V=Sprites.Find(Key))
        {
            // Shared atlas validation preserves all thirty direction columns.
            // This is the source's normal pose row, never a made-up walk loop.
            V->bMonster=true;V->bAmbient=true;V->LogicalFacing=Worker.Facing;V->RequestedState=TEXT("idle");
            V->bPresentationVisible=!bHasBattle;
            if(V->Mesh.IsValid())V->Mesh->SetVisibility(V->bPresentationVisible&&V->bAtlasReady&&TextureCache.FindRef(V->TexturePath)!=nullptr);
            UpdateSpriteFrame(*V,false);
        }
    }
}

void ABYMBoard::UpdateMushrooms(const TSharedPtr<FJsonObject>& Yard,double ServerTime)
{
    (void)ServerTime;
    const auto Economy=Object(Yard,TEXT("economy"));const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
    TSet<FString> Seen;
    if(Economy.IsValid()&&Economy->TryGetArrayField(TEXT("mushrooms"),Entries))
        for(const auto& Value:*Entries)
        {
            if(Seen.Num()>=100)break;
            if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
            const auto M=Value->AsObject();const FString Id=String(M,TEXT("id"));if(Id.IsEmpty())continue;
            Seen.Add(Id);Mushrooms.Add(Id,M);
        }
    TArray<FString> Keys;Mushrooms.GetKeys(Keys);
    for(const FString& Id:Keys)if(!Seen.Contains(Id))
    {Mushrooms.Remove(Id);ReleaseSprite(TEXT("shroom_top_")+Id);ReleaseSprite(TEXT("shroom_shadow_")+Id);}
    PresentMushrooms();
}

void ABYMBoard::PresentMushrooms()
{
    const auto Presentation=Object(BuildingCatalog.FindRef(7),TEXT("mushroomPresentation"));
    const TArray<TSharedPtr<FJsonValue>>* Variants=nullptr;
    if(!Presentation.IsValid()||!Presentation->TryGetArrayField(TEXT("variants"),Variants))return;
    const double Now=WorkerServerTime+BunkerClock-WorkerSnapshotClock;const float PixelUnits=CellUnits/20.f;
    for(const auto& Pair:Mushrooms)
    {
        const auto M=Pair.Value;const int32 Frame=FMath::Clamp(static_cast<int32>(Number(M,TEXT("frame"),1)),1,5)-1;
        if(!Variants->IsValidIndex(Frame))continue;
        const auto Variant=(*Variants)[Frame]->AsObject();const FVector2D Position(Number(M,TEXT("x")),Number(M,TEXT("y")));
        const double Finish=Number(M,TEXT("pickFinishAt"));
        const bool bShake=Boolean(M,TEXT("picking"))&&Finish>Now&&Now>=Finish-1.55;
        // Original calls jitter every second display tick. A stable local hash
        // avoids changing poses whenever a network snapshot is repeated.
        const uint32 Tick=bShake?static_cast<uint32>(FMath::Clamp(Now-(Finish-1.55),0.,1.55)*20.):0;
        FRandomStream Random(static_cast<int32>(GetTypeHash(Pair.Key)^Tick));
        const FVector2D Jitter(bShake?Random.FRandRange(-1.f,1.f):0.,bShake?Random.FRandRange(-1.f,1.f):0.);
        for(int32 Layer=0;Layer<2;++Layer)
        {
            const TCHAR* Name=Layer==0?TEXT("shadow"):TEXT("top");const auto Art=Object(Variant,Name);
            const double W=Number(Art,TEXT("pixelWidth")),H=Number(Art,TEXT("pixelHeight"));
            const double Strength=Layer==0?1.:2.;
            const float Depth=(Position.X+Position.Y)*.5f+(Position.X-Position.Y)*.0005f;
            const FVector Offset((W*.5-Number(Art,TEXT("pivotX"))+Jitter.X*Strength)*PixelUnits,
                (H*.5-Number(Art,TEXT("pivotY"))+Jitter.Y*Strength)*PixelUnits,0);
            PutSprite(TEXT("shroom_")+FString(Name)+TEXT("_")+Pair.Key,String(Art,TEXT("texture")),
                GridToLocal(Position,Layer==0?0.f:10.f+Depth*.02f)+Offset,FVector2D(W,H)*PixelUnits,FLinearColor::White);
        }
    }
}

void ABYMBoard::ApplySnapshot(const FString& Json)
{
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid()) return;
    TSharedPtr<FJsonObject> Yard=Object(Root,TEXT("yard")); if(!Yard.IsValid()) return;
    const auto Rules=Object(Root,TEXT("rules"));
    const int32 BaseCells=FMath::Clamp(static_cast<int32>(Number(Rules,TEXT("baseYardCells"),ProjectionBaseCells)),40,100);
    const auto Economy=Object(Yard,TEXT("economy"));
    const auto BoundsData=Yard->HasField(TEXT("minX"))?Yard:Economy;
    const int32 MinX=Number(BoundsData,TEXT("minX")),MinY=Number(BoundsData,TEXT("minY"));
    const int32 Width=FMath::Clamp(static_cast<int32>(Number(BoundsData,TEXT("width"),BaseCells*1.25)),3,256);
    const int32 Height=FMath::Clamp(static_cast<int32>(Number(BoundsData,TEXT("height"),BaseCells)),3,256);
    const FIntRect NewBounds(MinX,MinY,MinX+Width,MinY+Height);
    const float NewUnits=FMath::Clamp(static_cast<float>(Number(Rules,TEXT("cellSize"),CellUnits)),20.f,1000.f);
    if(NewBounds.Min!=YardBounds.Min||NewBounds.Max!=YardBounds.Max||BaseCells!=ProjectionBaseCells||!FMath::IsNearlyEqual(NewUnits,CellUnits))
    {YardBounds=NewBounds;GridCells=FMath::Max(Width,Height);ProjectionBaseCells=BaseCells;CellUnits=NewUnits;bGroundCreated=false;MakeGround();}
    if(TSharedPtr<FJsonObject> Catalog=Object(Root,TEXT("catalog")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Array=nullptr;
        if(Catalog->TryGetArrayField(TEXT("buildings"),Array))
            for(const auto& V:*Array) if(V.IsValid()&&V->Type==EJson::Object)
            {const auto O=V->AsObject(); const int32 Id=Number(O,TEXT("id"));if(!BuildingCatalog.Contains(Id))BuildingCatalog.Add(Id,O);}
        if(Catalog->TryGetArrayField(TEXT("monsters"),Array))
            for(const auto& V:*Array) if(V.IsValid()&&V->Type==EJson::Object)
            {const auto O=V->AsObject(); const FString Id=String(O,TEXT("id"));if(!MonsterCatalog.Contains(Id))MonsterCatalog.Add(Id,O);}
    }
    const FString YardId=String(Yard,TEXT("id"));
    if(CurrentYard!=YardId)
    {
        ClearHover();
        TArray<FString> Keys; Sprites.GetKeys(Keys);
        for(const auto& Key:Keys) if(Key.StartsWith(TEXT("b_"))||Key.StartsWith(TEXT("m_"))||Key.StartsWith(TEXT("d_"))||Key.StartsWith(TEXT("r_"))||Key.StartsWith(TEXT("w_"))||Key.StartsWith(TEXT("shroom_"))||Key.StartsWith(TEXT("scorch_"))) ReleaseSprite(Key);
        ScorchExpiry.Empty();ScorchOrder.Empty();
        Residents.Empty();Workers.Empty();Mushrooms.Empty();ResidentLayout.Empty();ClearCombatEffects();CurrentBattle.Empty();LastBattleServerTime=0.;
        CurrentYard=YardId;
    }
    LastYard=Yard;
    Buildings.Empty();
    TSet<FString> Seen;
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
    if(Yard->TryGetArrayField(TEXT("buildings"),Items))
    {
        for(const auto& V:*Items)
        {
            if(!V.IsValid()||V->Type!=EJson::Object) continue;
            const auto B=V->AsObject(); const FString Id=String(B,TEXT("id")); if(Id.IsEmpty()) continue;
            const FString Key=TEXT("b_")+Id; Seen.Add(Key); Buildings.Add(Id,B);
            // Original normal/damaged/destroyed composites already define their
            // colors. Avoid an invented blue construction or red damage tint.
            PutBuildingSprite(Key,B,FLinearColor::White);
        }
    }
    const auto ActiveBattle=Object(Root,TEXT("battle"));
    const auto Battle=ActiveBattle.IsValid()?ActiveBattle:Object(Root,TEXT("completedBattle"));
    bHasBattle=Battle.IsValid();
    bShowYardBoundary=String(Root,TEXT("selfId"))==YardId&&!bHasBattle;
    const FString BattleId=String(Battle,TEXT("id"));
    if(CurrentBattle!=BattleId)
    {
        ClearCombatEffects();CurrentBattle=BattleId;LastBattleServerTime=0.;
        TArray<FString> TroopKeys;Sprites.GetKeys(TroopKeys);
        for(const FString& Key:TroopKeys)if(Key.StartsWith(TEXT("m_"))||Key.StartsWith(TEXT("d_")))ReleaseSprite(Key);
    }
    const double ServerTime=Number(Battle,TEXT("serverTime"));
    SnapshotInterval=FMath::Clamp(static_cast<float>(Number(Rules,TEXT("battleSnapshotIntervalSeconds"),
        GetDefault<UBYMSettings>()->BattleSnapshotIntervalSeconds)),.1f,1.f);
    if(LastBattleServerTime>0.&&ServerTime>LastBattleServerTime)
        SnapshotInterval=FMath::Clamp(static_cast<float>(ServerTime-LastBattleServerTime),.1f,1.f);
    if(ServerTime>LastBattleServerTime)LastBattleServerTime=ServerTime;
    SetMusicMode(ActiveBattle.IsValid()?(String(ActiveBattle,TEXT("attacker"))==String(Root,TEXT("selfId"))?TEXT("attack"):TEXT("defense")):TEXT("building"));
    // Doors close from their current phase when battle presentation ends.
    // Repeated peaceful snapshots preserve that closing clock.
    if(!ActiveBattle.IsValid())
        for(auto& Pair:Sprites)
        {
            FBYMSpriteVisual& Visual=Pair.Value;
            if(!Pair.Key.StartsWith(TEXT("b_"))
                ||static_cast<int32>(Number(Buildings.FindRef(Pair.Key.Mid(2)),TEXT("type")))!=22)continue;
            if(Visual.bBunkerDoorOpening||!Visual.bBunkerDoorMoving)
            {
                Visual.BunkerDoorPhase=BYMBuildingAnimation::BunkerDoorPhase(Visual.BunkerDoorPhase,
                    FMath::Max(0.,BunkerClock-Visual.BunkerDoorClock),Visual.bBunkerDoorOpening,Visual.bBunkerDoorMoving);
                Visual.BunkerDoorClock=BunkerClock;Visual.bBunkerDoorOpening=false;
                Visual.bBunkerDoorMoving=Visual.BunkerDoorPhase>0.;
            }
            UpdateSpriteFrame(Visual,false);
        }
    if(ActiveBattle.IsValid()&&Battle->TryGetArrayField(TEXT("bunkers"),Items))
        for(const auto& Value:*Items)
        {
            if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
            const auto Bunker=Value->AsObject();
            FBYMSpriteVisual* Visual=Sprites.Find(TEXT("b_")+String(Bunker,TEXT("id")));
            // Keep the authoritative door clock even while damaged artwork or
            // a pending texture hides its animation; repair must not resurrect
            // an old door phase from before the damage.
            if(!Visual)continue;
            const FString DoorState=String(Bunker,TEXT("state"));
            Visual->bBunkerDoorOpening=DoorState==TEXT("opening");
            Visual->bBunkerDoorMoving=Visual->bBunkerDoorOpening||DoorState==TEXT("closing");
            const double Age=FMath::Max(0.,ServerTime-Number(Bunker,TEXT("doorAt"),ServerTime));
            Visual->BunkerDoorPhase=BYMBuildingAnimation::BunkerDoorPhase(Number(Bunker,TEXT("doorFrame")),Age,
                Visual->bBunkerDoorOpening,Visual->bBunkerDoorMoving);
            Visual->BunkerDoorClock=BunkerClock;
            UpdateSpriteFrame(*Visual,false);
        }
    if(Battle.IsValid()&&Battle->TryGetArrayField(TEXT("defenses"),Items))
        for(const auto& Value:*Items)
        {
            if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
            const auto D=Value->AsObject();FBYMSpriteVisual* Visual=Sprites.Find(TEXT("b_")+String(D,TEXT("id")));
            if(!Visual||!Visual->Atlas.IsValid())continue;
            FVector2D Facing(Number(D,TEXT("facingX")),Number(D,TEXT("facingY")));
            if(String(Visual->Atlas,TEXT("facingSpace"))==TEXT("isometric_screen"))
                Facing=FVector2D(Facing.X-Facing.Y,(Facing.X+Facing.Y)*.5);
            if(!Facing.IsNearlyZero())
            {
                const int32 Directions=FMath::Clamp(static_cast<int32>(Number(Visual->Atlas,TEXT("directions"),Visual->Frames)),1,Visual->Frames);
                const double Angle=FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Facing.Y,Facing.X))+360.,360.);
                const double Step=Number(Visual->Atlas,TEXT("sourceDegreesPerDirection"),360./Directions);
                Visual->Facing=FMath::Clamp(FMath::FloorToInt(Angle/FMath::Max(.001,Step)),0,Directions-1);
            }
            const int64 Sequence=static_cast<int64>(Number(D,TEXT("attackSequence")));
            if(Sequence!=Visual->AttackSequence)
            {
                Visual->AttackSequence=Sequence;
                Visual->AttackStartedAt=AnimationClock-FMath::Max(0.,ServerTime-Number(D,TEXT("lastAttackAt")));
                if(String(Visual->Atlas,TEXT("mode"))!=TEXT("firing"))Visual->DisplayedState.Empty();
            }
            const FString Phase=String(D,TEXT("state"));
            Visual->CoolingStartFrame=static_cast<int32>(Number(D,TEXT("coolingStartFrame"),41));
            Visual->RequestedState=ActiveBattle.IsValid()?(Phase==TEXT("charging")?TEXT("charge"):
                (Phase==TEXT("attacking")?TEXT("attack"):(Phase==TEXT("cooling")?TEXT("cool"):TEXT("idle")))):
                (Visual->DisplayedState==TEXT("cool")?TEXT("cool"):TEXT("idle"));
            const double StateAt=Number(D,TEXT("stateStartedAt"));
            const bool bStateChanged=Visual->DisplayedState!=Visual->RequestedState;
            SetAnimation(*Visual,Visual->RequestedState);
            if(ActiveBattle.IsValid()&&StateAt>0.&&(bStateChanged||Visual->SourceStateStartedAt!=StateAt))
            {
                Visual->SourceStateStartedAt=StateAt;
                Visual->AnimationStartedAt=AnimationClock-FMath::Max(0.,ServerTime-StateAt);
            }
        }
    ApplyBattleMonsters(Battle,TEXT("troops"),TEXT("m_"),ServerTime,Seen,false);
    ApplyBattleMonsters(Battle,TEXT("defenders"),TEXT("d_"),ServerTime,Seen,true);
    UpdateResidents(Yard);
    UpdateWorkers(Yard,Number(Root,TEXT("now")));
    UpdateMushrooms(Yard,Number(Root,TEXT("now")));
    DrawYardBoundary();
    ApplyCombatEvents(Battle,ServerTime);
    TArray<FString> Keys; Sprites.GetKeys(Keys);
    for(const auto& Key:Keys)if((Key.StartsWith(TEXT("b_"))||Key.StartsWith(TEXT("m_"))||Key.StartsWith(TEXT("d_")))&&!Seen.Contains(Key))
    {
        FBYMSpriteVisual* Visual=Sprites.Find(Key);
        if((Key.StartsWith(TEXT("m_"))||Key.StartsWith(TEXT("d_")))&&bHasBattle&&Visual)
        {
            if(Visual->DeathStartedAt<0)Visual->DeathStartedAt=AnimationClock;
            Visual->RequestedState=TEXT("death");Visual->bMoving=false;Visual->bPendingAttackFeedback=false;
        }
        else ReleaseSprite(Key);
    }
}

void ABYMBoard::ApplyBattleMonsters(const TSharedPtr<FJsonObject>& Battle,const TCHAR* Field,
    const FString& Prefix,double ServerTime,TSet<FString>& Seen,bool bDefenders)
{
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
    if(Battle.IsValid()&&Battle->TryGetArrayField(Field,Items))
    {
        for(const auto& V:*Items)
        {
            if(!V.IsValid()||V->Type!=EJson::Object) continue;
            const auto T=V->AsObject(); FString Id=String(T,TEXT("id"));
            if(Id.IsEmpty()) Id=FString::Printf(TEXT("%lld"),static_cast<long long>(Number(T,TEXT("uid"))));
            const FString Key=Prefix+Id;
            // Returning defenders are still alive; removing their visual must
            // never replay a death pose or a troop deployment bounce.
            if(bDefenders&&Boolean(T,TEXT("returned"))){ReleaseSprite(Key);continue;}
            if(Number(T,TEXT("hp"),1)<=0)
            {
                if(FBYMSpriteVisual* Visual=Sprites.Find(Key))
                {
                    if(Visual->DeathStartedAt<0)Visual->DeathStartedAt=AnimationClock;
                    Visual->RequestedState=TEXT("death");Visual->bMoving=false;Visual->bPendingAttackFeedback=false;Seen.Add(Key);
                }
                continue;
            }
            Seen.Add(Key);
            const FString Type=String(T,TEXT("type"));const auto Def=MonsterCatalog.FindRef(Type);
            const float X=Number(T,TEXT("x")),Y=Number(T,TEXT("y"));
            const FString State=String(T,TEXT("state"),TEXT("idle"));
            const bool bNew=!Sprites.Contains(Key);
            PutMonsterSprite(Key,Type,FVector2D(X,Y),FVector2D(Number(T,TEXT("facingX")),Number(T,TEXT("facingY"))),
                State==TEXT("moving")?TEXT("walk"):(State==TEXT("attacking")?TEXT("attack"):TEXT("idle")),true,false);
            if(FBYMSpriteVisual* Visual=Sprites.Find(Key))
            {
                Visual->bRanged=Number(Def,TEXT("range"))>1;
                Visual->AttackInterval=FMath::Max(.1f,static_cast<float>(Number(T,TEXT("attackInterval"),1)));
                const int64 Sequence=static_cast<int64>(Number(T,TEXT("attackSequence")));
                if(Sequence!=Visual->AttackSequence)
                {
                    Visual->AttackSequence=Sequence;
                    Visual->AttackStartedAt=AnimationClock-FMath::Max(0.,ServerTime-Number(T,TEXT("lastAttackAt")));
                    Visual->bPendingAttackFeedback=Sequence>0&&AnimationClock-Visual->AttackStartedAt<.5f;
                    Visual->FeedbackExpiresAt=AnimationClock+FMath::Max(.3f,SnapshotInterval*2.f);
                    Visual->DisplayedState.Empty();
                }
                if(bNew&&!bDefenders&&Number(T,TEXT("deployedAt"))>0.)
                    Visual->SpawnedAt=AnimationClock-FMath::Max(0.,ServerTime-Number(T,TEXT("deployedAt")));
            }
        }
    }
}

void ABYMBoard::ClearCombatEffects()
{
    for(const auto& Effect:EffectPool)ReleaseSprite(Effect.Key);
    EffectPool.Empty();SeenBattleEvents.Empty();BattleEventOrder.Empty();
}

void ABYMBoard::SpawnCombatEffect(const TSharedPtr<FJsonObject>& Definition,const FVector2D& Start,
    const FVector2D& End,float Age,bool bProjectile,const FVector2D& MuzzleOffset)
{
    if(!Definition.IsValid())return;
    const FString Texture=String(Definition,TEXT("texture"));if(Texture.IsEmpty())return;
    const float PixelUnits=CellUnits/20.f;
    const float Scale=Number(Definition,TEXT("sourceScale"),1);
    const FString Mode=String(Definition,TEXT("animationMode"));
    const bool bBeam=Mode==TEXT("source_vector_beam"),bBolt=Mode==TEXT("source_vector_bolt");
    const FVector From=GridToLocal(Start,20.f)+FVector(MuzzleOffset.X*PixelUnits,MuzzleOffset.Y*PixelUnits,0);
    const FVector To=GridToLocal(End,20.01f);
    const float FlightSeconds=FMath::Clamp(static_cast<float>((To-From).Size2D()/(PixelUnits*600.f)),.08f,.55f);
    const float Duration=FMath::Clamp(static_cast<float>(Number(Definition,TEXT("duration"),bProjectile?FlightSeconds:.6f)),.04f,3.f);
    // Present fresh events when they arrive, including source flashes shorter
    // than the network snapshot interval. Discard old catch-up effects.
    if(Age>FMath::Max(Duration,SnapshotInterval+.25f))return;
    const float PlaybackAge=Age<=SnapshotInterval+.15f?0.f:Age;
    const uint32 RGB=static_cast<uint32>(Number(Definition,TEXT("rgb"),0xffffff));
    const FLinearColor Tint=FLinearColor::FromSRGBColor(FColor((RGB>>16)&255,(RGB>>8)&255,RGB&255,255));
    auto Emit=[&](const FVector& A,const FVector& B,bool bLine,float Width)
    {
        constexpr int32 MaximumEffects=128;
        int32 Slot=INDEX_NONE;
        for(int32 I=0;I<EffectPool.Num();++I)if(!EffectPool[I].bActive){Slot=I;break;}
        if(Slot==INDEX_NONE&&EffectPool.Num()<MaximumEffects)Slot=EffectPool.AddDefaulted();
        if(Slot==INDEX_NONE)
        {
            Slot=0;
            for(int32 I=1;I<EffectPool.Num();++I)if(EffectPool[I].StartedAt<EffectPool[Slot].StartedAt)Slot=I;
        }
        FBYMCombatEffect& Effect=EffectPool[Slot];
        Effect.Key=FString::Printf(TEXT("fx_%d"),Slot);
        Effect.Start=A;Effect.End=B;Effect.StartedAt=AnimationClock-PlaybackAge;Effect.Duration=Duration;
        Effect.bProjectile=bProjectile&&!bLine;Effect.bActive=true;Effect.Tint=Tint;
        Effect.AlphaDivisorPerFrame=Number(Definition,TEXT("alphaDivisorPerFrame"),1);
        const float FrameWidth=Number(Definition,TEXT("frameWidth"),1),FrameHeight=Number(Definition,TEXT("frameHeight"),1);
        const FVector Offset((FrameWidth*.5-Number(Definition,TEXT("pivotX"),FrameWidth*.5))*PixelUnits*Scale,
            (FrameHeight*.5-Number(Definition,TEXT("pivotY"),FrameHeight*.5))*PixelUnits*Scale,0);
        Effect.Start+=bLine?FVector::ZeroVector:Offset;Effect.End+=bLine?FVector::ZeroVector:Offset;
        const FVector Location=bLine?(A+B)*.5:(Effect.bProjectile?Effect.Start:Effect.End);
        const FVector2D Size=bLine?FVector2D(FMath::Max(1.,(B-A).Size2D()),Width*PixelUnits)
            :FVector2D(FrameWidth,FrameHeight)*PixelUnits*Scale;
        PutSprite(Effect.Key,Texture,Location,Size,Tint);
        SetAtlas(Effect.Key,Definition);
        if(FBYMSpriteVisual* Visual=Sprites.Find(Effect.Key))
        {
            Visual->bMonster=false;Visual->bAmbient=false;Visual->RequestedState=TEXT("idle");
            Visual->EffectStartedAt=Effect.StartedAt;Visual->EffectDuration=Duration;
            Visual->DisplayedState.Empty();SetAnimation(*Visual,TEXT("idle"));
            Visual->AnimationStartedAt=Effect.StartedAt;
            if(Mode==TEXT("particle_life_selects_frame"))Visual->FPS=Visual->Frames/Duration;
            if(Visual->Mesh.IsValid())
            {
                Visual->bPresentationVisible=true;
                Visual->Mesh->SetVisibility(Visual->bAtlasReady&&TextureCache.FindRef(Texture)!=nullptr);
                Visual->Mesh->SetRelativeRotation(bLine?FRotator(0,FMath::RadiansToDegrees(FMath::Atan2((B-A).Y,(B-A).X)),0):FRotator::ZeroRotator);
            }
        }
    };
    if(bBolt)
    {
        // Original EFFECTS.Lightning builds a jittered, colored vector path.
        // Reproduce its segments with native planes; no replacement sprite art.
        const float SegmentPixels=FMath::Max(1.f,static_cast<float>(Number(Definition,TEXT("segmentPixels"),30)));
        const int32 Segments=FMath::Clamp(FMath::CeilToInt((To-From).Size2D()/(SegmentPixels*PixelUnits)),1,24);
        FRandomStream Random(GetTypeHash(CurrentBattle)^GetTypeHash(BattleEventOrder.Num()));
        FVector Previous=From;
        for(int32 I=1;I<=Segments;++I)
        {
            FVector Next=FMath::Lerp(From,To,static_cast<float>(I)/Segments);
            if(I<Segments)
            {
                const float Min=Number(Definition,TEXT("jitterMinimumPixels"),-7);
                const float Max=Min+Number(Definition,TEXT("jitterRangePixels"),15);
                Next.X+=Random.FRandRange(Min,Max)*PixelUnits;Next.Y+=Random.FRandRange(Min,Max)*PixelUnits;
            }
            Emit(Previous,Next,true,Number(Definition,TEXT("lineWidthPixels"),1));Previous=Next;
        }
    }
    else Emit(From,To,bBeam,bBeam?Number(Definition,TEXT("lineWidthMinPixels"),2):1);
}

void ABYMBoard::PlayCombatSound(const FString& SoundKey,const TSharedPtr<FJsonObject>& Battle,
    float Age,int32& SoundsInBatch)
{
    const auto* Settings=GetDefault<UBYMSettings>();
    const FString SoundPath=String(CombatSounds,*SoundKey);
    const float* LastSound=LastCombatSoundAt.Find(SoundKey);
    if(Settings->bCombatSoundsEnabled&&Settings->CombatSoundVolume>0&&GetNetMode()!=NM_DedicatedServer
        &&String(Battle,TEXT("phase"))!=TEXT("completed")&&Age<=SnapshotInterval+.15f
        &&!SoundPath.IsEmpty()&&SoundsInBatch<2&&CombatSoundsInWindow<12
        &&(!LastSound||AnimationClock-*LastSound>=.12f))
    {
        USoundBase* Sound=CombatSoundCache.FindRef(SoundKey).Get();
        if(!CombatSoundCache.Contains(SoundKey))
        {
            Sound=LoadObject<USoundBase>(nullptr,*AssetObjectPath(SoundPath));
            CombatSoundCache.Add(SoundKey,Sound);
        }
        if(Sound)
        {
            UGameplayStatics::PlaySound2D(this,Sound,FMath::Clamp(Settings->CombatSoundVolume,0.f,1.f));
            LastCombatSoundAt.Add(SoundKey,AnimationClock);++SoundsInBatch;++CombatSoundsInWindow;
        }
    }
}

void ABYMBoard::ApplyCombatEvents(const TSharedPtr<FJsonObject>& Battle,double ServerTime)
{
    const TArray<TSharedPtr<FJsonValue>>* Events=nullptr;
    if(!Battle.IsValid()||!Battle->TryGetArrayField(TEXT("events"),Events))return;
    if(AnimationClock-SoundWindowStartedAt>=1.f){SoundWindowStartedAt=AnimationClock;CombatSoundsInWindow=0;}
    int32 SoundsInBatch=0;
    for(const auto& Value:*Events)
    {
        if(!Value.IsValid()||Value->Type!=EJson::Object)continue;
        const auto Event=Value->AsObject();const FString Id=String(Event,TEXT("id"));
        if(Id.IsEmpty()||SeenBattleEvents.Contains(Id))continue;
        SeenBattleEvents.Add(Id);BattleEventOrder.Add(Id);
        while(BattleEventOrder.Num()>512){SeenBattleEvents.Remove(BattleEventOrder[0]);BattleEventOrder.RemoveAt(0);}
        const float Age=FMath::Max(0.,ServerTime-Number(Event,TEXT("at"),ServerTime));
        FVector2D From(Number(Event,TEXT("sourceX")),Number(Event,TEXT("sourceY")));
        const FVector2D To(Number(Event,TEXT("targetX")),Number(Event,TEXT("targetY")));
        const FString Kind=String(Event,TEXT("kind"));
        if(Kind==TEXT("defenseAttack"))
        {
            const int32 BuildingType=static_cast<int32>(Number(Event,TEXT("buildingType")));
            const auto Def=BuildingCatalog.FindRef(BuildingType);
            const auto Presentation=Object(Def,TEXT("combatPresentation"));
            PlayCombatSound(String(Presentation,TEXT("sound")),Battle,Age,SoundsInBatch);
            FVector2D Muzzle=FVector2D::ZeroVector;
            if(BuildingType!=138)
            {
                const TArray<TSharedPtr<FJsonValue>>* Offset=nullptr;
                if(Presentation.IsValid()&&Presentation->TryGetArrayField(TEXT("muzzleOffsetPixels"),Offset)&&Offset->Num()==2)
                    Muzzle=FVector2D((*Offset)[0]->AsNumber(),(*Offset)[1]->AsNumber());
                const auto Building=Buildings.FindRef(String(Event,TEXT("actor")));
                if(Building.IsValid())From=FVector2D(Number(Building,TEXT("x")),Number(Building,TEXT("y")));
            }
            // GuardTowerStronghold has four independently aimed source guns.
            // Their host-authored event positions already include each emitter;
            // replacing them with the building origin collapses all four beams.
            SpawnCombatEffect(Object(CombatEffects,*String(Presentation,TEXT("projectile"))),From,To,Age,true,Muzzle);
        }
        else if(Kind==TEXT("bunkerDoor"))
            PlayCombatSound(TEXT("bunkerdoor"),Battle,Age,SoundsInBatch);
        else if(Kind==TEXT("monsterAttack")||Kind==TEXT("defenderAttack"))
        {
            const auto Def=MonsterCatalog.FindRef(String(Event,TEXT("monsterType")));
            const FString Projectile=String(Def,TEXT("projectileKey"));
            if(!Projectile.IsEmpty())SpawnCombatEffect(Object(CombatEffects,*Projectile),From,To,Age,true);
            // Contact feedback uses the original particle, never client damage.
            SpawnCombatEffect(Object(CombatEffects,TEXT("smoke")),To,To,Age,false);
        }
        else if(Kind==TEXT("monsterExplode"))
        {
            // Active-source Eye-Ra uses EFFECTS.Scorch, not a looping fireball.
            // Keep the original ground mark after transient battle FX expire.
            const FString Key=TEXT("scorch_")+Id;
            if(Number(Event,TEXT("damage"))>0.&&!ScorchExpiry.Contains(Key)&&Age<172800.f)
            {
                while(ScorchOrder.Num()>=128)
                {
                    const FString Oldest=ScorchOrder[0];ScorchOrder.RemoveAt(0);
                    ScorchExpiry.Remove(Oldest);ReleaseSprite(Oldest);
                }
                const float PixelUnits=CellUnits/20.f;
                PutSprite(Key,TEXT("source_client/images/1820_ParticleScorch1_ParticleScorch1.png"),
                    GridToLocal(From,0.f),FVector2D(200,100)*PixelUnits,FLinearColor::White);
                SetAtlas(Key,nullptr);
                ScorchOrder.Add(Key);ScorchExpiry.Add(Key,AnimationClock+172800.f-Age);
            }
        }
        else if(Kind==TEXT("buildingDestroyed"))
            SpawnCombatEffect(Object(CombatEffects,TEXT("smoke")),To,To,Age,false);
        else if(Kind==TEXT("troopDied")||Kind==TEXT("defenderDied"))
            SpawnCombatEffect(Object(CombatEffects,TEXT("splat")),To,To,Age,false);
    }
}

void ABYMBoard::ReleaseSprite(const FString& Key)
{
    if(FBYMSpriteVisual* V=Sprites.Find(Key))
        if(UStaticMeshComponent* M=V->Mesh.Get()) {VisualComponents.Remove(M);M->DestroyComponent();}
    Sprites.Remove(Key);
}

void ABYMBoard::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);AnimationClock+=DeltaSeconds;BunkerClock+=static_cast<double>(DeltaSeconds);
    for(int32 I=ScorchOrder.Num()-1;I>=0;--I)
        if(ScorchExpiry.FindRef(ScorchOrder[I])<=AnimationClock)
        {
            const FString Key=ScorchOrder[I];ScorchOrder.RemoveAt(I);
            ScorchExpiry.Remove(Key);ReleaseSprite(Key);
        }
    if(!bHasBattle)
        for(auto& Pair:Residents)
        {
            auto& Resident=Pair.Value;
            if(!Resident.Walker.bInitialized)continue;
            const auto Def=MonsterCatalog.FindRef(Resident.Type);
            const double ScreenSpeed=Number(Def,TEXT("sourceMovePixelsPerSecond"),Number(Def,TEXT("speed"),1)*18.);
            const FVector2D Heading=Resident.Walker.Facing;
            const double ScreenLength=FVector2D(Heading.X-Heading.Y,(Heading.X+Heading.Y)*.5).Size();
            const float CellsPerSecond=FMath::Clamp(static_cast<float>(ScreenSpeed/20./FMath::Max(.5,ScreenLength)),.1f,8.f);
            BYMYardResidents::TickWalker(Resident.Walker,ResidentGrid,DeltaSeconds,CellsPerSecond);
            PutMonsterSprite(Pair.Key,Resident.Type,Resident.Walker.Position,Resident.Walker.Facing,
                Resident.Walker.bMoving?TEXT("walk"):TEXT("idle"),false,true);
        }
    PresentWorkers();
    PresentMushrooms();
    for(FBYMCombatEffect& Effect:EffectPool)
    {
        if(!Effect.bActive)continue;
        FBYMSpriteVisual* Visual=Sprites.Find(Effect.Key);if(!Visual||!Visual->Mesh.IsValid())continue;
        const float Progress=(AnimationClock-Effect.StartedAt)/Effect.Duration;
        if(Progress>=1.f){Effect.bActive=false;Visual->bPresentationVisible=false;Visual->Mesh->SetVisibility(false);continue;}
        if(Visual->Material.IsValid())
        {
            FLinearColor Tint=Effect.Tint;
            if(Effect.AlphaDivisorPerFrame>1.f)
                Tint.A/=FMath::Pow(Effect.AlphaDivisorPerFrame,FMath::Max(0.f,AnimationClock-Effect.StartedAt)*40.f);
            Visual->Material->SetVectorParameterValue(TEXT("SpriteTint"),Tint);
        }
        if(Effect.bProjectile)
        {
            Visual->BasePosition=FMath::Lerp(Effect.Start,Effect.End,FMath::Clamp(Progress,0.f,1.f));
            Visual->Mesh->SetRelativeLocation(Visual->BasePosition);
        }
    }
    TArray<FString> Expired;
    for(auto& Pair:Sprites)
    {
        FBYMSpriteVisual& V=Pair.Value; if(!V.Mesh.IsValid()) continue;
        bool bWalking=false;
        if(V.bMoving)
        {
            V.InterpolationElapsed=FMath::Min(V.InterpolationElapsed+DeltaSeconds,V.InterpolationDuration);
            const float Alpha=FMath::Clamp(V.InterpolationElapsed/FMath::Max(.01f,V.InterpolationDuration),0.f,1.f);
            bWalking=Alpha<1.f&&(V.Target-V.InterpolationStart).SizeSquared2D()>1.f;
            V.BasePosition=FMath::Lerp(V.InterpolationStart,V.Target,Alpha);
        }
        if(V.bMonster)
        {
            FVector Presented=V.BasePosition;
            const float PixelUnits=CellUnits/20.f;
            if(V.bPendingAttackFeedback&&!bWalking&&V.RequestedState==TEXT("attack"))
            {
                if(AnimationClock<=V.FeedbackExpiresAt)V.FeedbackStartedAt=AnimationClock;
                V.bPendingAttackFeedback=false;
            }
            const float AttackAge=AnimationClock-V.FeedbackStartedAt;
            const float StrikeDuration=FMath::Min(.28f,V.AttackInterval*.6f);
            const auto Attack=Object(Object(V.Atlas,TEXT("animations")),TEXT("attack"));
            const TArray<TSharedPtr<FJsonValue>>* AttackRows=nullptr;
            const bool bSourceAttack=Attack.IsValid()&&Attack->TryGetArrayField(TEXT("rows"),AttackRows)&&AttackRows->Num()>1;
            if(!V.bAmbient&&!bSourceAttack&&!bWalking&&V.RequestedState==TEXT("attack")&&AttackAge>=0&&AttackAge<StrikeDuration)
            {
                // NXT presentation feedback for original single-pose species.
                // This changes only the displayed offset, never server position,
                // damage, facing columns, range or the owned trained army.
                const FVector2D Direction=FVector2D(V.LogicalFacing.X-V.LogicalFacing.Y,
                    (V.LogicalFacing.X+V.LogicalFacing.Y)*.5).GetSafeNormal();
                const float Nudge=FMath::Sin(AttackAge/StrikeDuration*PI)*(V.bRanged?-2.f:4.f)*PixelUnits;
                Presented+=FVector(Direction.X*Nudge,Direction.Y*Nudge,0);
            }
            const float SpawnAge=AnimationClock-V.SpawnedAt;
            if(!V.bAmbient&&SpawnAge>=0&&SpawnAge<.6f)
                Presented.Y-=90.f*PixelUnits*(1.f-SourceBounceOut(SpawnAge/.6f));
            if(V.HoverAmplitude>0)Presented.Y+=FMath::Sin(AnimationClock*V.HoverAngularSpeed)*V.HoverAmplitude*PixelUnits;
            if(V.DeathStartedAt>=0)
            {
                const float Alpha=1.f-(AnimationClock-V.DeathStartedAt)/.35f;
                if(Alpha<=0){Expired.Add(Pair.Key);continue;}
                if(V.Material.IsValid())V.Material->SetVectorParameterValue(TEXT("SpriteTint"),FLinearColor(1,1,1,Alpha));
            }
            V.Mesh->SetRelativeLocation(Presented);
            V.Mesh->SetTranslucentSortPriority(SpriteSortPriority(V.BasePosition.Z));
        }
        UpdateSpriteFrame(V,bWalking);
    }
    for(const FString& Key:Expired)ReleaseSprite(Key);
}

FString ABYMBoard::GetBuildingAtCell(int32 X,int32 Y) const
{
    for(const auto& Pair:Mushrooms)
    {
        const double MX=Number(Pair.Value,TEXT("x")),MY=Number(Pair.Value,TEXT("y"));
        if(X>=MX&&Y>=MY&&X<MX+1.5&&Y<MY+1.5)return Pair.Key;
    }
    for(const auto& Pair:Buildings)
    {
        const auto& B=Pair.Value;
        const int32 BX=Number(B,TEXT("x")),BY=Number(B,TEXT("y"));
        const int32 Size=FMath::Max(1,static_cast<int32>(Number(B,TEXT("size"),5)));
        if(X>=BX&&Y>=BY&&X<BX+Size&&Y<BY+Size) return Pair.Key;
    }
    return FString();
}

FString ABYMBoard::GetBuildingAtWorld(const FVector& WorldPoint) const
{
    // Original buildings can rise well above their logical footprints. Hit the
    // visible sprite bounds first so clicking a roof selects that building.
    // This is a rectangular hit area, not an exported Flash hit silhouette.
    FString Result;
    double FrontDepth=-TNumericLimits<double>::Max();
    for(const auto& Pair:Mushrooms)
    {
        const auto* Visual=Sprites.Find(TEXT("shroom_top_")+Pair.Key);
        const auto* Mesh=Visual?Visual->Mesh.Get():nullptr;if(!Mesh||!Mesh->IsVisible())continue;
        const FVector Local=Mesh->GetComponentTransform().InverseTransformPosition(WorldPoint);
        if(FMath::Abs(Local.X)<=50.&&FMath::Abs(Local.Y)<=50.)
        {
            const double Depth=Mesh->GetComponentLocation().Z;
            if(Depth>FrontDepth){Result=Pair.Key;FrontDepth=Depth;}
        }
    }
    for(const auto& Pair:Buildings)
    {
        const FBYMSpriteVisual* Visual=Sprites.Find(TEXT("b_")+Pair.Key);
        UStaticMeshComponent* Mesh=Visual?Visual->Mesh.Get():nullptr;
        if(!Mesh||!Mesh->IsVisible()) continue;
        const FVector Local=Mesh->GetComponentTransform().InverseTransformPosition(WorldPoint);
        if(FMath::Abs(Local.X)<=50.&&FMath::Abs(Local.Y)<=50.)
        {
            const double Depth=Mesh->GetComponentLocation().Z;
            if(Depth>FrontDepth)
            {
                FrontDepth=Depth;
                Result=Pair.Key;
            }
        }
    }
    if(!Result.IsEmpty()) return Result;
    const FVector2D Cell=WorldToGrid(WorldPoint);
    return GetBuildingAtCell(FMath::FloorToInt(Cell.X+1.e-6),FMath::FloorToInt(Cell.Y+1.e-6));
}

void ABYMBoard::SetHoveredCell(int32 X,int32 Y,int32 Footprint,bool bValid,
    int32 BuildingType,const FString& MovingBuilding,bool bDeployment)
{
    Footprint=FMath::Max(1,Footprint);
    const FVector2D Centre(X+Footprint*.5,Y+Footprint*.5);
    const FVector2D Size(Footprint*CellUnits*2.f,Footprint*CellUnits);
    const FLinearColor MarkerTint=bValid?FLinearColor(.1f,1.f,.35f,1):FLinearColor(1.f,.08f,.04f,1);
    // Explicit unlit layers keep the clicked logical cell visible over grass,
    // troops and buildings. The 2:1 diamond is the actual GRID projection.
    PutSprite(TEXT("hover_fill"),TEXT("derived/ui/placement_diamond_fill.png"),
        GridToLocal(Centre,30.f),Size,MarkerTint);
    PutSprite(TEXT("hover_outline_dark"),TEXT("derived/ui/placement_diamond_outline.png"),
        GridToLocal(Centre,31.f),Size*1.16,FLinearColor(0,0,0,1));
    PutSprite(TEXT("hover_outline_white"),TEXT("derived/ui/placement_diamond_outline.png"),
        GridToLocal(Centre,31.2f),Size*1.08,FLinearColor::White);
    PutSprite(TEXT("hover"),TEXT("derived/ui/placement_diamond_outline.png"),
        GridToLocal(Centre,31.4f),Size,MarkerTint);
    // Show both edges of the original outer-three-cell deployment band. This
    // is an interaction overlay, not a modification of the endless terrain.
    for(int32 Band=0;Band<2;++Band)
    {
        const double Inset=Band==0?0.:3.;
        const double LX=YardBounds.Min.X+Inset,LY=YardBounds.Min.Y+Inset,RX=YardBounds.Max.X-Inset,RY=YardBounds.Max.Y-Inset;
        const FVector2D Corners[]={FVector2D(LX,LY),FVector2D(RX,LY),FVector2D(RX,RY),FVector2D(LX,RY)};
        for(int32 Edge=0;Edge<4;++Edge)
        {
            const FString Key=FString::Printf(TEXT("hover_boundary_%d"),Band*4+Edge);
            if(!bDeployment){ReleaseSprite(Key);continue;}
            const FVector A=GridToLocal(Corners[Edge],29.8f),B=GridToLocal(Corners[(Edge+1)%4],29.8f);
            PutSprite(Key,TEXT("derived/effects/source_line.png"),(A+B)*.5,
                FVector2D((B-A).Size2D(),CellUnits*.10f),Band==0?FLinearColor(.8f,1.f,1.f,.8f):FLinearColor(1.f,.75f,.1f,.9f));
            if(FBYMSpriteVisual* Visual=Sprites.Find(Key))if(Visual->Mesh.IsValid())
                Visual->Mesh->SetRelativeRotation(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2((B-A).Y,(B-A).X)),0));
        }
    }
    const auto Existing=Buildings.FindRef(MovingBuilding);
    if(BuildingType>0||Existing.IsValid())
    {
        TSharedRef<FJsonObject> Preview=MakeShared<FJsonObject>();
        if(Existing.IsValid()) Preview->Values=Existing->Values;
        else
        {
            Preview->SetNumberField(TEXT("type"),BuildingType);
            Preview->SetNumberField(TEXT("level"),1);
            Preview->SetNumberField(TEXT("hp"),1);
            Preview->SetNumberField(TEXT("maxHp"),1);
        }
        Preview->SetNumberField(TEXT("x"),X);
        Preview->SetNumberField(TEXT("y"),Y);
        Preview->SetNumberField(TEXT("size"),Footprint);
        PutBuildingSprite(TEXT("placement_preview"),Preview,
            bValid?FLinearColor::White:FLinearColor(1.f,.45f,.45f),true);
    }
    else ReleaseSprite(TEXT("placement_preview"));
}
void ABYMBoard::ClearHover()
{
    TArray<FString> Keys;Sprites.GetKeys(Keys);
    for(const FString& Key:Keys)if(Key.StartsWith(TEXT("hover")))ReleaseSprite(Key);
    ReleaseSprite(TEXT("placement_preview"));
}

int32 ABYMBoard::GetFootprint(int32 BuildingType) const
{
    return FMath::Max(1,FMath::CeilToInt(Number(BuildingCatalog.FindRef(BuildingType),TEXT("size"),20)/20.));
}
bool ABYMBoard::CanPlacePreview(int32 X,int32 Y,int32 Size,const FString& IgnoreBuilding) const
{
    // Match the host's construction/move boundary, including its one-cell inset.
    if(Size<1||X<YardBounds.Min.X+1||Y<YardBounds.Min.Y+1||X+Size>=YardBounds.Max.X||Y+Size>=YardBounds.Max.Y) return false;
    for(const auto& Pair:Mushrooms)
    {
        const double MX=Number(Pair.Value,TEXT("x")),MY=Number(Pair.Value,TEXT("y"));
        if(X<MX+1.5&&X+Size>MX&&Y<MY+1.5&&Y+Size>MY)return false;
    }
    for(const auto& Pair:Buildings)
    {
        if(Pair.Key==IgnoreBuilding) continue;
        const int32 BX=Number(Pair.Value,TEXT("x")),BY=Number(Pair.Value,TEXT("y")),BS=Number(Pair.Value,TEXT("size"),5);
        if(X<BX+BS&&X+Size>BX&&Y<BY+BS&&Y+Size>BY) return false;
    }
    return true;
}

bool ABYMBoard::CanDeployPreview(int32 X,int32 Y,int32 Count) const
{
    // Deployment has different rules from construction: the outside row is legal,
    // only the outer three cells are available, and destroyed buildings do not block.
    if(X<YardBounds.Min.X||Y<YardBounds.Min.Y||X>=YardBounds.Max.X||Y>=YardBounds.Max.Y) return false;
    if(X>=YardBounds.Min.X+3&&Y>=YardBounds.Min.Y+3&&X<YardBounds.Max.X-3&&Y<YardBounds.Max.Y-3) return false;
    for(const auto& Pair:Buildings)
    {
        if(Number(Pair.Value,TEXT("hp"))<=0) continue;
        const double BX=Number(Pair.Value,TEXT("x")),BY=Number(Pair.Value,TEXT("y"));
        const auto Def=BuildingCatalog.FindRef(static_cast<int32>(Number(Pair.Value,TEXT("type"))));
        if(String(Def,TEXT("type"))==TEXT("decoration"))continue;
        const auto Footprint=Object(Def,TEXT("combatFootprint"));
        const TArray<TSharedPtr<FJsonValue>>* Rectangles=nullptr;
        if(Footprint.IsValid()&&Footprint->TryGetArrayField(TEXT("rectangles"),Rectangles))
            for(const auto& Value:*Rectangles)
            {
                if(!Value.IsValid()||Value->Type!=EJson::Array)continue;
                const auto& R=Value->AsArray();if(R.Num()!=4)continue;
                const double Left=BX+R[0]->AsNumber()/20.,Top=BY+R[1]->AsNumber()/20.;
                const double Right=Left+R[2]->AsNumber()/20.,Bottom=Top+R[3]->AsNumber()/20.;
                for(int32 I=0;I<FMath::Clamp(Count,1,9);++I)
                {
                    const double PX=X+.25+(I%3)*.2,PY=Y+.25+((I/3)%3)*.2;
                    if(PX>Left+.0001&&PX<Right-.0001&&PY>Top+.0001&&PY<Bottom-.0001)return false;
                }
            }
        else
        {
            const double Size=FMath::Max(1.,Number(Pair.Value,TEXT("size"),5));
            for(int32 I=0;I<FMath::Clamp(Count,1,9);++I)
            {
                const double PX=X+.25+(I%3)*.2,PY=Y+.25+((I/3)%3)*.2;
                if(PX>BX+.0001&&PX<BX+Size-.0001&&PY>BY+.0001&&PY<BY+Size-.0001)return false;
            }
        }
    }
    return true;
}

void ABYMBoard::SetMusicMode(const FString& Mode)
{
    if(!Music||MusicMode==Mode||GetNetMode()==NM_DedicatedServer)return;
    MusicMode=Mode;
    Music->Stop();
    const auto* S=GetDefault<UBYMSettings>();
    if(!S->bMusicEnabled)return;
    USoundBase* Sound=Mode==TEXT("attack")?BattleMusic.Get():(Mode==TEXT("defense")?DefenseMusic.Get():BuildingMusic.Get());
    if(Sound){Music->SetSound(Sound);Music->SetVolumeMultiplier(FMath::Clamp(S->MusicVolume,0.f,1.f));Music->Play();}
}

int32 ABYMBoard::GetExistingFootprint(const FString& BuildingId) const
{
    return FMath::Max(1,static_cast<int32>(Number(Buildings.FindRef(BuildingId),TEXT("size"),1)));
}
