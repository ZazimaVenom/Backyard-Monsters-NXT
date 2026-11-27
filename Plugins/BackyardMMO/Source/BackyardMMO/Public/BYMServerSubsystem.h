#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BYMSimulation.h"
#include "BYMServerSubsystem.generated.h"

class FJsonObject;
class FJsonValue;
struct FBYMPreparedAuthentication;

struct FBYMStoredAccount
{
    FString Id;
    FString Username;
    FString Salt;
    FString PasswordHash;
    int32 Iterations = 210000;
};

struct FBYMAuthAttemptWindow
{
    double StartedAt = 0.0;
    int32 Count = 0;
};

/**
 * Listen-server-only account store and world simulation owner.
 * Nothing in this object is replicated. Clients receive sanitized simulation snapshots only.
 */
UCLASS()
class BACKYARDMMO_API UBYMServerSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UBYMServerSubsystem();
    virtual ~UBYMServerSubsystem() override;
    virtual void Deinitialize() override;

    bool StartAuthority(FString& Error);
    bool IsAuthorityRunning() const;
    void StopAuthority();
    bool SaveNow(FString& Error);

    void AuthenticateAsync(const FString& ConnectionKey, const FString& Username, const FString& Password, bool bCreate,
        TFunction<void(bool, const FString&, const FString&)> Completion);
    void Disconnect(const FString& ConnectionKey, const FString& AccountId);
    FString Execute(const FString& AccountId, const FString& Operation, const FString& Payload);
    FString GetSnapshot(const FString& AccountId, const FString& ViewedYardId = TEXT(""));
    FString GetLadder(const FString& AccountId, int32 Page = 0);

private:
    TSharedPtr<FBYMPreparedAuthentication, ESPMode::ThreadSafe> PrepareAuthentication(
        const FString& ConnectionKey, const FString& Username, const FString& Password, bool bCreate, FString& Error);
    bool FinishAuthentication(const TSharedRef<FBYMPreparedAuthentication, ESPMode::ThreadSafe>& Request, FString& OutAccountId, FString& Error);
    bool IsHostWorld() const;
    bool IsAccountOnline(const FString& AccountId) const;
    bool TickAuthority(float DeltaSeconds);
    bool LoadWorld(FString& Error);
    bool AcquireWorldLock(FString& Error);
    void ReleaseWorldLock();
    bool SaveInternal(FString& Error);
    bool SaveAccounts(FString& Error);
    bool LoadAccounts(FString& Error);
    bool AddLoadedAccount(const FBYMStoredAccount& Record, FString& Error);
    bool ValidateLoadedAccounts(const TArray<TSharedPtr<FJsonValue>>& Values, FString& Error);
    bool ConsumeAttempt(TMap<FString, FBYMAuthAttemptWindow>& Windows, const FString& Key, int32 Limit, double Period);
    void CleanupAuthState();
    void ResetState();

    TUniquePtr<FBYMSimulation> Simulation;
    TMap<FString, FBYMStoredAccount> Accounts;
    TMap<FString, FString> UsernameToId;
    TMap<FString, FString> ConnectionToAccount;
    TMap<FString, FString> AccountToConnection;
    TMap<FString, FString> PendingAuthenticationTickets;
    TMap<FString, FBYMAuthAttemptWindow> AuthenticationAttempts;
    TMap<FString, FBYMAuthAttemptWindow> UsernameAttempts;
    TMap<FString, FBYMAuthAttemptWindow> GlobalAttempts;
    FTSTicker::FDelegateHandle TickHandle;
    FString SaveDirectory;
    FString WorldPath;
    FString AccountPath;
    TArray<uint8> LastCommittedAccounts;
    TSet<FString> PendingRegistrationYards;
    FString LastSaveError;
    FString LastCommittedWorld;
    void* WorldLockHandle = nullptr;
    int32 WorldLockDescriptor = -1;
    bool bRunning = false;
    bool bSaveBlocked = false;
    bool bWorldWasLoaded = false;
    double LastAutosaveMonotonic = 0.0;
    double LastCleanupMonotonic = 0.0;
    double LastSaveUnix = 0.0;
    double LastSimulationMonotonic = 0.0;
    double LastFailedSaveLogMonotonic = 0.0;
};
