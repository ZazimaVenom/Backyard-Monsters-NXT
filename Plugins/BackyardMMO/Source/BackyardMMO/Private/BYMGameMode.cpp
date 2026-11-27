#include "BYMGameMode.h"
#include "BYMCameraPawn.h"
#include "BYMHUD.h"
#include "BYMPlayerController.h"
#include "BYMServerSubsystem.h"
#include "BYMSettings.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameSession.h"

ABYMGameMode::ABYMGameMode()
{
    PlayerControllerClass = ABYMPlayerController::StaticClass();
    DefaultPawnClass = ABYMCameraPawn::StaticClass();
    HUDClass = ABYMHUD::StaticClass();
    bUseSeamlessTravel = false;
}

void ABYMGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    if (GameSession) { GameSession->MaxPlayers = FMath::Clamp(GetDefault<UBYMSettings>()->MaxPlayers, 1, 1000); }
}

void ABYMGameMode::StartPlay()
{
    UBYMServerSubsystem* Server = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>();
    if (!Server || !Server->StartAuthority(StartupError))
    {
        if (StartupError.IsEmpty()) { StartupError = TEXT("The host could not start its world save."); }
        UE_LOG(LogTemp, Error, TEXT("Backyard Monsters NXT startup failed: %s"), *StartupError);
    }
    Super::StartPlay();
}

void ABYMGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty()) { return; }
    if (!StartupError.IsEmpty()) { ErrorMessage = TEXT("The host world could not be loaded. Contact the host."); return; }
    const int32 Cap = FMath::Clamp(GetDefault<UBYMSettings>()->MaxPlayers, 1, 1000);
    if (GetNumPlayers() + GetNumSpectators() >= Cap) { ErrorMessage = TEXT("This server is full. Please try again later."); }
}

void ABYMGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    // Recheck after registration so simultaneous admissions cannot exceed the cap.
    const int32 Cap = FMath::Clamp(GetDefault<UBYMSettings>()->MaxPlayers, 1, 1000);
    if (NewPlayer && !NewPlayer->IsLocalController() && GameSession && GetNumPlayers() + GetNumSpectators() > Cap)
    {
        GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("This server is full. Please try again later.")));
    }
}

void ABYMGameMode::Logout(AController* Exiting)
{
    if (ABYMPlayerController* PC = Cast<ABYMPlayerController>(Exiting)) { PC->ReleaseServerSession(); }
    Super::Logout(Exiting);
}

void ABYMGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetGameInstance())
    {
        if (UBYMServerSubsystem* Server = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>()) { Server->StopAuthority(); }
    }
    Super::EndPlay(EndPlayReason);
}
