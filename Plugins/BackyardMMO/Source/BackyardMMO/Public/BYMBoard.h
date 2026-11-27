#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "BYMYardResidents.h"
#include "BYMSpriteAtlas.h"
#include "BYMBuildingAnimation.h"
#include "BYMWorkerMovement.h"
#include "BYMBoard.generated.h"
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class UAudioComponent;
class USoundBase;

struct FBYMSpriteVisual
{
    TWeakObjectPtr<UStaticMeshComponent> Mesh;
    TWeakObjectPtr<UMaterialInstanceDynamic> Material;
    FVector Target = FVector::ZeroVector;
    FVector InterpolationStart = FVector::ZeroVector;
    FVector BasePosition = FVector::ZeroVector;
    FVector2D LogicalFacing = FVector2D(0,1);
    FString TexturePath;
    FString RequestedState = TEXT("idle");
    FString DisplayedState;
    TSharedPtr<FJsonObject> Atlas;
    TSharedPtr<FJsonObject> PendingAtlas;
    FString AtlasTexturePath;
    BYMSpriteAtlas::FLayout AtlasLayout;
    bool bAtlasReady = false;
    bool bPresentationVisible = true;
    BYMBuildingAnimation::FClock ActivityClock;
    TArray<int32> ActivityFrames;
    uint32 ActivitySeed = 0;
    bool bActivityWorking = false;
    int32 FillFrame = 0;
    int32 DisplayedFillFrame = 0;
    int64 FillDisplayTick = -1;
    double BunkerDoorPhase = 0.;
    double BunkerDoorClock = 0.;
    bool bBunkerDoorOpening = false;
    bool bBunkerDoorMoving = false;
    int32 Frames = 1;
    int32 Columns = 1;
    int32 Rows = 1;
    int32 Directions = 1;
    float FPS = 12.f;
    bool bMoving = false;
    bool bDirectional = false;
    bool bInitialized = false;
    bool bMonster = false;
    bool bAmbient = false;
    bool bRanged = false;
    float HoverAmplitude = 0.f;
    float HoverAngularSpeed = 0.f;
    bool bLoop = true;
    float InterpolationElapsed = 0.f;
    float InterpolationDuration = .2f;
    float AnimationStartedAt = 0.f;
    float EffectStartedAt = 0.f;
    float EffectDuration = 0.f;
    float AttackStartedAt = -100.f;
    float FeedbackStartedAt = -100.f;
    float FeedbackExpiresAt = -100.f;
    bool bPendingAttackFeedback = false;
    float AttackInterval = 2.f;
    int64 AttackSequence = -1;
    float DeathStartedAt = -1.f;
    float SpawnedAt = -100.f;
    int32 Facing = 0;
    float FacingOffset = 0.f;
    TArray<int32> AnimationFrames;
    int32 LoopStartFrame = 0;
    int32 CoolingStartFrame = 41;
    double SourceStateStartedAt = -1.;
};

struct FBYMResidentVisual
{
    FString Type;
    BYMYardResidents::FWalker Walker;
};

struct FBYMWorkerVisual
{
    TArray<BYMWorkerMovement::FRoutePoint> Route;
    FVector2D Position = FVector2D::ZeroVector;
    FVector2D Facing = FVector2D(0,1);
    double StartedAt = 0.;
    FString State;
};

struct FBYMCombatEffect
{
    FString Key;
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ZeroVector;
    float StartedAt = 0.f;
    float Duration = .3f;
    bool bProjectile = false;
    bool bActive = false;
    FLinearColor Tint = FLinearColor::White;
    float AlphaDivisorPerFrame = 1.f;
};

/** Local presentation of the host's current yard snapshot. Never owns game economy or combat. */
UCLASS()
class BACKYARDMMO_API ABYMBoard : public AActor
{
    GENERATED_BODY()
public:
    ABYMBoard();
    virtual void PostRegisterAllComponents() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void ApplySnapshot(const FString& Json);
    static FString AssetObjectPath(const FString& RelativePath);
    FString GetBuildingAtCell(int32 X, int32 Y) const;
    FString GetBuildingAtWorld(const FVector& WorldPoint) const;
    int32 GetFootprint(int32 BuildingType) const;
    int32 GetExistingFootprint(const FString& BuildingId) const;
    int32 GetGridCells() const { return GridCells; }
    FIntRect GetYardBounds() const { return YardBounds; }
    void GetTerrainWorldBounds(FVector2D& OutMin,FVector2D& OutMax) const;
    bool GetWorkerFocusPosition(const FString& WorkerId,FVector& OutWorldPosition) const;
    float GetCellSize() const { return CellUnits; }
    /** Original GRID.ToISO / FromISO projection; gameplay coordinates remain logical cells. */
    FVector GridToWorld(const FVector2D& GridPoint, float Height = 0.f) const;
    FVector2D WorldToGrid(const FVector& WorldPoint) const;
    static float CalculateTerrainMargin(float CellSize);
    float GetTerrainMargin() const { return CalculateTerrainMargin(CellUnits); }
    const FString& GetPresentationError() const { return PresentationError; }
    bool CanPlacePreview(int32 X,int32 Y,int32 Size,const FString& IgnoreBuilding=TEXT("")) const;
    bool CanDeployPreview(int32 X, int32 Y, int32 Count = 1) const;
    void SetHoveredCell(int32 X, int32 Y, int32 Footprint, bool bValid,
        int32 BuildingType = 0, const FString& MovingBuilding = TEXT(""), bool bDeployment = false);
    void ClearHover();
private:
    UPROPERTY() TObjectPtr<USceneComponent> BoardRoot;
    UPROPERTY() TObjectPtr<UStaticMesh> PlaneMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> SpriteMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> AlphaSpriteMaterial;
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UStaticMeshComponent>> VisualComponents;
    UPROPERTY() TObjectPtr<UAudioComponent> Music;
    UPROPERTY(Transient) TObjectPtr<USoundBase> BuildingMusic;
    UPROPERTY(Transient) TObjectPtr<USoundBase> BattleMusic;
    UPROPERTY(Transient) TObjectPtr<USoundBase> DefenseMusic;
    FString MusicMode;
    void SetMusicMode(const FString& Mode);
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UTexture2D>> TextureCache;
    TMap<FString, FBYMSpriteVisual> Sprites;
    TMap<FString,TSharedPtr<FJsonObject>> Buildings;
    TMap<int32,TSharedPtr<FJsonObject>> BuildingCatalog;
    TMap<FString,TSharedPtr<FJsonObject>> MonsterCatalog;
    TSharedPtr<FJsonObject> WorkerSprite;
    TMap<FString,FBYMWorkerVisual> Workers;
    TMap<FString,TSharedPtr<FJsonObject>> Mushrooms;
    double WorkerServerTime = 0.;
    double WorkerSnapshotClock = 0.;
    TSharedPtr<FJsonObject> CombatEffects;
    TSharedPtr<FJsonObject> CombatSounds;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<USoundBase>> CombatSoundCache;
    TMap<FString,float> LastCombatSoundAt;
    float SoundWindowStartedAt = -100.f;
    int32 CombatSoundsInWindow = 0;
    TMap<FString,FBYMResidentVisual> Residents;
    BYMYardResidents::FGrid ResidentGrid;
    FString ResidentLayout;
    FString CurrentBattle;
    TSet<FString> SeenBattleEvents;
    TArray<FString> BattleEventOrder;
    TArray<FBYMCombatEffect> EffectPool;
    TMap<FString,float> ScorchExpiry;
    TArray<FString> ScorchOrder;
    bool bHasBattle = false;
    bool bShowYardBoundary = false;
    double LastBattleServerTime = 0.;
    float SnapshotInterval = .2f;
    TSharedPtr<FJsonObject> LastYard;
    FString CurrentYard;
    float AnimationClock = 0.f;
    double BunkerClock = 0.;
    int32 GridCells = 40;
    FIntRect YardBounds = FIntRect(0,0,50,40);
    int32 ProjectionBaseCells = 40;
    float CellUnits = 100.f;
    FString PresentationError;
    bool bBuildingEditorPreview = false;
    void InitializePresentation();
    void ReportPresentationError(const FString& Error);
    bool bGroundCreated = false;
    bool bLoggedMissingMaterial = false;
    UStaticMeshComponent* MakePlane(const FString& Key);
    void PutSprite(const FString& Key,const FString& Texture,const FVector& Location,const FVector2D& Size,const FLinearColor& Tint,bool bSmooth=false);
    FVector GridToLocal(const FVector2D& GridPoint, float Height = 0.f) const;
    void PutBuildingSprite(const FString& Key, const TSharedPtr<FJsonObject>& Building,
        const FLinearColor& Tint, bool bPreview = false);
    void MakeGround();
    void DrawYardBoundary();
    void UpdateWorkers(const TSharedPtr<FJsonObject>& Yard,double ServerTime);
    void PresentWorkers();
    void UpdateMushrooms(const TSharedPtr<FJsonObject>& Yard,double ServerTime);
    void PresentMushrooms();
    void ReleaseSprite(const FString& Key);
    void SetAtlas(const FString& Key, const TSharedPtr<FJsonObject>& Data);
    bool TryConfigureAtlas(FBYMSpriteVisual& Visual, TSharedPtr<FJsonObject> Data);
    void SetAnimation(FBYMSpriteVisual& Visual, const FString& State);
    void UpdateSpriteFrame(FBYMSpriteVisual& Visual, bool bTravelling);
    void PutMonsterSprite(const FString& Key, const FString& Type, const FVector2D& Position,
        const FVector2D& Facing, const FString& State, bool bSmooth, bool bAmbient);
    void UpdateResidents(const TSharedPtr<FJsonObject>& Yard);
    void ApplyCombatEvents(const TSharedPtr<FJsonObject>& Battle, double ServerTime);
    void ApplyBattleMonsters(const TSharedPtr<FJsonObject>& Battle, const TCHAR* Field,
        const FString& Prefix, double ServerTime, TSet<FString>& Seen, bool bDefenders);
    void PlayCombatSound(const FString& SoundKey, const TSharedPtr<FJsonObject>& Battle,
        float Age, int32& SoundsInBatch);
    void SpawnCombatEffect(const TSharedPtr<FJsonObject>& Definition, const FVector2D& Start,
        const FVector2D& End, float Age, bool bProjectile, const FVector2D& MuzzleOffset = FVector2D::ZeroVector);
    void ClearCombatEffects();
};
