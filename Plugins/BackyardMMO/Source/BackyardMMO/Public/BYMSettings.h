#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BYMSettings.generated.h"

/** Host defaults and game rules. Remote clients never override an authority's rules. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Backyard Monsters NXT"))
class BACKYARDMMO_API UBYMSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UBYMSettings();
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    UPROPERTY(Config, EditAnywhere, Category="Graphics", meta=(ToolTip="Automatically apply the FPS limit and inexpensive sprite-game graphics at startup and map entry. Disable before adding a custom video-settings system. Takes effect on the next game launch."))
    bool bApplyOptimizedGraphicsDefaults = true;
    UPROPERTY(Config, EditAnywhere, Category="Graphics", meta=(ClampMin="15.0", ClampMax="240.0", EditCondition="bApplyOptimizedGraphicsDefaults", ToolTip="Local rendering FPS cap for both the menu and gameplay. Defaults to 60; does not change simulation time or use a fixed timestep."))
    float FrameRateLimit = 60.0f;

    UPROPERTY(Config, EditAnywhere, Category="Connection", meta=(ToolTip="Default host IP or DNS name, used by every packaged client. No protocol or password."))
    FString HostAddress = TEXT("127.0.0.1");
    UPROPERTY(Config, EditAnywhere, Category="Connection", meta=(ClampMin="1024", ClampMax="65535"))
    int32 ServerPort = 7777;
    UPROPERTY(Config, EditAnywhere, Category="Connection", meta=(ClampMin="1", ClampMax="1000", ToolTip="Includes the local host. Capacity is a limit, not a tested performance guarantee."))
    int32 MaxPlayers = 100;
    UPROPERTY(Config, EditAnywhere, Category="Rivals", meta=(ClampMin="0", ClampMax="10000", ToolTip="Active rival population. Lowering preserves dormant rivals and their saved yards."))
    int32 RivalBotCount = 1000;
    UPROPERTY(Config, EditAnywhere, Category="Rivals", meta=(ClampMin="1", ClampMax="100"))
    int32 BotActionsPerTick = 4;
    UPROPERTY(Config, EditAnywhere, Category="Rivals", meta=(ClampMin="5.0", ClampMax="86400.0"))
    float BotDecisionSeconds = 60.0f;
    UPROPERTY(Config, EditAnywhere, Category="Persistence", meta=(ClampMin="30.0", ClampMax="86400.0", Units="s"))
    float AutosaveSeconds = 3600.0f;
    UPROPERTY(Config, EditAnywhere, Category="Persistence")
    bool bSaveOnShutdown = true;
    UPROPERTY(Config, EditAnywhere, Category="Invasions", meta=(ClampMin="0.0", ClampMax="604800.0", Units="s"))
    float ShieldSeconds = 3600.0f;
    UPROPERTY(Config, EditAnywhere, Category="Invasions", meta=(ClampMin="0", ClampMax="100000"))
    int32 RPWin = 25;
    UPROPERTY(Config, EditAnywhere, Category="Invasions", meta=(ClampMin="0", ClampMax="100000"))
    int32 RPLoss = 15;
    UPROPERTY(Config, EditAnywhere, Category="Invasions")
    bool bClampRPAtZero = false;
    UPROPERTY(Config, EditAnywhere, Category="Economy", meta=(ClampMin="0", ClampMax="1000000000"))
    int32 StartingResources = 5000;
    UPROPERTY(Config, EditAnywhere, Category="Economy", meta=(ClampMin="0", ClampMax="1000000000", ToolTip="Initial Shiny for a new yard. 1500 matches GameConfig in the supplied source (its unconfigured fallback is 1000). Also initializes older worlds which predate Shiny once; existing saved balances are retained."))
    int32 StartingShiny = 1500;
    UPROPERTY(Config, EditAnywhere, Category="Economy", meta=(ClampMin="10", ClampMax="500"))
    int32 MaxBuildingsPerYard = 150;
    UPROPERTY(Config, EditAnywhere, Category="World", meta=(ClampMin="40", ClampMax="100", ToolTip="Base yard height in 20-source-pixel cells. The default 40 gives the original 50 by 40 yard; Shiny expansions grow both dimensions using the original rules."))
    int32 YardCells = 40;
    UPROPERTY(Config, EditAnywhere, Category="World", meta=(ClampMin="20.0", ClampMax="1000.0"))
    float CellSize = 100.0f;
    UPROPERTY(Config, EditAnywhere, Category="World", meta=(ClampMin="1000.0", ClampMax="20000.0"))
    float CameraWidth = 6200.0f;
    UPROPERTY(Config, EditAnywhere, Category="World", meta=(ClampMin="0", ClampMax="50", ToolTip="Maximum visible trained monsters roaming each viewed yard. Displays a balanced selection of owned, ready monsters; never changes the army. Hard maximum 50."))
    int32 MaxVisibleYardMonsters = 50;
    UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.1", ClampMax="5.0", Units="s"))
    float SimulationStepSeconds = 1.0f;
    UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.05", ClampMax="0.1", Units="s", ToolTip="Host update interval while battles are active. Bot decisions retain their separate scheduling."))
    float CombatSimulationStepSeconds = 0.1f;
    UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.1", ClampMax="1.0", Units="s", ToolTip="Update interval for players viewing active battles. Smaller intervals use more bandwidth. Peaceful yards use Yard Snapshot Interval Seconds."))
    float BattleSnapshotIntervalSeconds = 0.2f;
    UPROPERTY(Config, EditAnywhere, Category="Network bandwidth", meta=(ToolTip="Use Unreal's built-in lossless compression for yard, map and ranking payloads. No external service or additional installer is required."))
    bool bCompressNetworkState = true;
    UPROPERTY(Config, EditAnywhere, Category="Network bandwidth", meta=(ToolTip="Send only changes from each client's acknowledged state. A complete state is sent on first connection and after a resynchronization."))
    bool bUseNetworkDeltas = true;
    UPROPERTY(Config, EditAnywhere, Category="Network bandwidth", meta=(ClampMin="0.5", ClampMax="10.0", Units="s", ToolTip="Peaceful-yard update interval. Commands still request immediate updates; combat uses Battle Snapshot Interval Seconds."))
    float YardSnapshotIntervalSeconds = 2.0f;
    UPROPERTY(Config, EditAnywhere, Category="Network bandwidth", meta=(ClampMin="1", ClampMax="8", ToolTip="Maximum 8 KiB state chunks per controller tick across yard, map and rankings. Bounds bursts without changing simulation speed."))
    int32 NetworkChunksPerTick = 4;
    UPROPERTY(Config, EditAnywhere, Category="Network bandwidth", meta=(ClampMin="10.0", ClampMax="120.0", Units="s", ToolTip="Maximum time without progress or acknowledgement during a state transfer. Stalled connections are closed instead of accumulating an unbounded reliable queue."))
    float NetworkAckTimeoutSeconds = 30.0f;
    UPROPERTY(Config, EditAnywhere, Category="Security", meta=(ClampMin="1024", ClampMax="65536"))
    int32 MaxCommandBytes = 16384;
    UPROPERTY(Config, EditAnywhere, Category="Security", meta=(ClampMin="2", ClampMax="30"))
    int32 MaxCommandsPerSecond = 12;
    UPROPERTY(Config, EditAnywhere, Category="Security", meta=(ClampMin="210000", ClampMax="1000000", ToolTip="Password hash work factor. Stored per account, no plaintext passwords are saved."))
    int32 PBKDF2Iterations = 210000;
    UPROPERTY(Config, EditAnywhere, Category="Balance", meta=(ClampMin="0.01", ClampMax="100.0"))
    float ConstructionTimeMultiplier = 1.0f;
    UPROPERTY(Config, EditAnywhere, Category="Balance", meta=(ClampMin="0.01", ClampMax="100.0"))
    float TrainingTimeMultiplier = 1.0f;
    UPROPERTY(Config, EditAnywhere, Category="Balance", meta=(ClampMin="0.01", ClampMax="100.0", ToolTip="Multiplier for Monster Locker unlock and Monster Academy research timers. 1 uses original source durations."))
    float MonsterProgressionTimeMultiplier = 1.0f;
    UPROPERTY(Config, EditAnywhere, Category="Balance", meta=(ClampMin="0.0", ClampMax="100.0"))
    float ResourceProductionMultiplier = 1.0f;
    UPROPERTY(Config, EditAnywhere, Category="Balance", meta=(ClampMin="0.01", ClampMax="100.0"))
    float CombatDamageMultiplier = 1.0f;
    UPROPERTY(Config, EditAnywhere, Category="Balance", meta=(ClampMin="0.01", ClampMax="10.0"))
    float MonsterMovementMultiplier = 1.0f;
    UPROPERTY(Config, EditAnywhere, Category="Invasions", meta=(ClampMin="30.0", ClampMax="3600.0", Units="s"))
    float MaximumBattleSeconds = 300.0f;
    UPROPERTY(Config, EditAnywhere, Category="Invasions", meta=(ClampMin="1", ClampMax="100"))
    int32 MaximumConcurrentBattles = 32;
    UPROPERTY(Config, EditAnywhere, Category="Invasions", meta=(ClampMin="1", ClampMax="500"))
    int32 MaximumTroopsPerBattle = 200;
    UPROPERTY(Config, EditAnywhere, Category="Invasions", meta=(ClampMin="0.0", ClampMax="1.0"))
    float InvasionLootFraction = 0.1f;
    UPROPERTY(Config, EditAnywhere, Category="Economy", meta=(ClampMin="0", ClampMax="1000000000"))
    int32 ResourceStorageFloor = 1000;
    UPROPERTY(Config, EditAnywhere, Category="Audio")
    bool bMusicEnabled = true;
    UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MusicVolume = 0.3f;
    UPROPERTY(Config, EditAnywhere, Category="Audio")
    bool bCombatSoundsEnabled = true;
    UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0.0", ClampMax="1.0"))
    float CombatSoundVolume = 0.6f;
    UPROPERTY(Config, VisibleAnywhere, Category="Generated maps")
    FString WorldMap = TEXT("/BackyardMMO/Maps/OpenWorld");
    UPROPERTY(Config, VisibleAnywhere, Category="Generated maps")
    FString MainMenuMap = TEXT("/BackyardMMO/Maps/MainMenu");
};
