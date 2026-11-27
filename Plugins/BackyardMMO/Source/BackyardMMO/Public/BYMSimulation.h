#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Server-owned game model. Never construct or expose this model on a remote client. */
class BACKYARDMMO_API FBYMSimulation final
{
public:
    FBYMSimulation();
    ~FBYMSimulation();
    FBYMSimulation(const FBYMSimulation&) = delete;
    FBYMSimulation& operator=(const FBYMSimulation&) = delete;

    /** Reads world schemas 1-5. Missing optional worker/economy extensions are migrated without deleting paid jobs. */
    bool Initialize(const TSharedPtr<FJsonObject>& Saved, FString& Error);
    /** Writes schema 5 with host-owned worker/economy state; native account SaveGame storage is unchanged. */
    TSharedPtr<FJsonObject> Serialize() const;
    bool AddPlayer(const FString& Id, const FString& DisplayName, FString& Error);
    void RemovePlayer(const FString& Id);
    void SetPlayerOnline(const FString& Id, bool bOnline);
    bool HasYard(const FString& Id) const;
    bool HasActiveBattles() const;
    void Tick(double NowSeconds, float DeltaSeconds);
    FString Execute(const FString& Id, const FString& Operation, const FString& Payload);
    FString Snapshot(const FString& Id, const FString& ViewedYardId) const;
    FString Ladder(int32 Page) const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
