#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "BYMAccountSaveGame.generated.h"

/** Typed account data owned exclusively by the hosted server. Never contains plaintext passwords. */
USTRUCT()
struct BACKYARDMMO_API FBYMAccountSaveRecord
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FString Id;
    UPROPERTY(SaveGame) FString Username;
    UPROPERTY(SaveGame) FString Salt;
    UPROPERTY(SaveGame) FString PasswordHash;
    UPROPERTY(SaveGame) int32 Iterations = 210000;
};

/** Native Unreal SaveGame account store. Yard/bot simulation remains in its existing world save. */
UCLASS()
class BACKYARDMMO_API UBYMAccountSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 SchemaVersion = 1;
    UPROPERTY(SaveGame) TArray<FBYMAccountSaveRecord> Accounts;
};
