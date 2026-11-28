#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BYMNetworkStream.h"
#include "BYMPlayerController.generated.h"

class ABYMBoard;
class FJsonObject;

UCLASS()
class BACKYARDMMO_API ABYMPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ABYMPlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
    void SubmitCredentials(const FString& Username, const FString& Password, bool bCreate);
    bool SendCommand(const FString& Op, const FString& Payload);
    void RequestLadder(int32 Page = 0);
    void RequestWorldMap(int32 CenterX, int32 CenterY, int32 Radius = 15, const FString& SelectedCell = TEXT(""), int32 OwnedPage = 0);
    void ViewYard(const FString& Id);
    void ReturnHome();
    void LeaveServer();
    void HandleBoardClick(int32 X, int32 Y, const FString& BuildingId = TEXT(""));
    bool IsMenuWorld() const;
    bool IsHost() const;
    bool IsCommandPending() const { return !PendingOperation.IsEmpty(); }
    void ReleaseServerSession();
    UFUNCTION(Exec) void BYMNetStats();
    FString GetAuthenticatedId() const { return AuthenticatedId; }

    FString LastSnapshot;
    FString LastLadder;
    FString LastWorldMap;
    FString StatusMessage;
    FString SelectedBuilding;
    FString SelectedMonsterType = TEXT("C1");
    FString PlacementMode;
    FString PendingOperation;
    FString LastCommandOperation;
    bool bLastCommandSucceeded = false;
    int32 CommandRevision = 0;
    int32 SelectedBuildingType = 1;
    int32 SelectedMonsterCount = 1;
    int32 SnapshotRevision = 0;
    int32 LadderRevision = 0;
    int32 WorldMapRevision = 0;
    bool bAuthenticated = false;
    bool bConnecting = false;
    bool bPointerOverGameUI = false; // Local Slate panel hit regions; never replicated.
    bool bWorldMapOpen = false; // Local map input owns pan/zoom while its window is open.

    UFUNCTION(Server, Reliable) void ServerAuthenticate(const FString& Username, const FString& Password, bool bCreate);
    UFUNCTION(Server, Reliable) void ServerCommand(const FString& Operation, const FString& Payload);
    UFUNCTION(Server, Reliable) void ServerViewYard(const FString& YardId);
    UFUNCTION(Server, Reliable) void ServerLadder(int32 Page);
    UFUNCTION(Server, Reliable) void ServerWorldMap(int32 CenterX, int32 CenterY, int32 Radius, const FString& SelectedCell, int32 OwnedPage);
    UFUNCTION(Client, Reliable) void ClientAuthentication(bool bSuccess, const FString& Message);
    UFUNCTION(Client, Reliable) void ClientStateChunk(uint8 StreamId, int32 Sequence, int32 BaseSequence,
        int32 Index, int32 Count, int32 RawBytes, bool bCompressed, bool bDelta, const TArray<uint8>& Bytes);
    UFUNCTION(Server, Reliable) void ServerStateAck(uint8 StreamId, int32 Sequence, bool bAccepted);
    UFUNCTION(Server, Reliable) void ServerStateChunkAck(uint8 StreamId, int32 Sequence, int32 Index);
    UFUNCTION(Client, Reliable) void ClientStatus(const FString& Message);
    UFUNCTION(Client, Reliable) void ClientCommandResult(const FString& Operation, bool bSuccess, const FString& Message);
private:
    FString ConnectionKey;
    FString AuthenticatedId; // Server-only; never replicated and never supplied by a remote client.
    FString ViewedYardId;
    bool bAuthenticationInFlight = false;
    double ConnectedAt = 0;
    double NextSnapshotAt = 0;
    double RateWindowAt = 0;
    int32 WindowCalls = 0;
    int32 RateViolations = 0;
    double LastLocalCommandAt = 0;
    double LastLocalMapRequestAt = -1;
    double LastServerMapRequestAt = -1;
    bool bMapRequestQueued = false;
    int32 QueuedMapX = -1, QueuedMapY = -1, QueuedMapRadius = 15, QueuedOwnedPage = 0;
    FString QueuedMapSelected;
    FString LastSentMapKey;
    bool bServerMapRequestQueued = false;
    int32 ServerMapX = -1, ServerMapY = -1, ServerMapRadius = 15, ServerOwnedPage = 0;
    FString ServerMapSelected;
    TWeakObjectPtr<ABYMBoard> Board;
    TSharedPtr<FJsonObject> LocalCatalog;
    bool bCatalogLoadAttempted = false;
    BYMNetworkStream::FSender NetworkSenders[3];
    BYMNetworkStream::FReceiver NetworkReceivers[3];
    double NetworkFrameStartedAt[3] = {0, 0, 0};
    int32 NetworkTrackedSequence[3] = {0, 0, 0};
    int32 NextNetworkStream = 0;
    FString LastQueuedSnapshotIdentity;
    bool bNetworkFailed = false;
    double NetworkStatsStartedAt = 0;
    uint64 NetworkOfferedJsonBytes = 0;
    uint64 NetworkReceivedBytes = 0;
    uint64 NetworkReceivedFrames = 0;
    void ResetNetworkState();
    void QueueNetworkPayload(uint8 StreamId, const FString& Json, bool bMustDeliver);
    void FlushNetworkPayloads();
    void ApplyNetworkPayload(uint8 StreamId, const FString& Json);
    void ApplySnapshotJson(const FString& Json);
    void ApplyLadderJson(const FString& Json);
    void ApplyWorldMapJson(const FString& Json);
    void FailNetworkStream(const FString& Reason);
    FString EnrichSnapshotLocally(const FString& Json);
    void FlushPendingMapRequest();
    void FlushServerMapRequest();
    bool AllowServerCall(int32 PayloadLength, bool bRequiresAccount);
    void RefreshSnapshot(bool bMustDeliver = false);
    void DisconnectAbusiveClient(const FString& Reason);
};
