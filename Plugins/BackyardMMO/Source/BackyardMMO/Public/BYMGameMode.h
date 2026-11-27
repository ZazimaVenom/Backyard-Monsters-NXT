#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BYMGameMode.generated.h"

UCLASS()
class BACKYARDMMO_API ABYMGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ABYMGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void StartPlay() override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    FString StartupError;
};
