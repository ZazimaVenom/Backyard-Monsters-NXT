#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "BYMGameInstance.generated.h"

/** Owns connection UX only. Passwords are transient and never serialized to a save or URL. */
UCLASS()
class BACKYARDMMO_API UBYMGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    virtual void Shutdown() override;
    bool HostAndPlay(const FString& Username, const FString& Password, bool bCreate, FString& Error);
    bool JoinHost(const FString& Address, const FString& Username, const FString& Password, bool bCreate, FString& Error);
    void ReturnToMenu(const FString& Reason = TEXT(""));
    bool ConsumeCredentials(FString& Username, FString& Password, bool& bCreate);
    void ClearCredentials();
    FString LastConnectionError;
    FString ConnectedAddress;
    bool bInitiatedHost = false;
private:
    FString PendingUsername;
    FString PendingPassword;
    bool bPendingCreate = false;
    bool bHasPendingCredentials = false;
    bool bReturningToMenu = false;
    FDelegateHandle NetworkFailureHandle;
    FDelegateHandle TravelFailureHandle;
    void NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& Error);
    void TravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);
    bool PrepareCredentials(const FString& Username, const FString& Password, bool bCreate, FString& Error);
};
