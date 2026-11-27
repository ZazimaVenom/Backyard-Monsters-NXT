#include "BYMGameInstance.h"
#include "BYMSettings.h"
#include "BYMServerSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

namespace
{
    void EraseString(FString& Value)
    {
        if (!Value.IsEmpty()) { FMemory::Memzero(Value.GetCharArray().GetData(), Value.GetCharArray().Num() * sizeof(TCHAR)); }
        Value.Empty();
    }
}

void UBYMGameInstance::Init()
{
    Super::Init();
    if (GEngine)
    {
        NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UBYMGameInstance::NetworkFailure);
        TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UBYMGameInstance::TravelFailure);
    }
}

void UBYMGameInstance::Shutdown()
{
    ClearCredentials();
    if (UBYMServerSubsystem* Server = GetSubsystem<UBYMServerSubsystem>()) { Server->StopAuthority(); }
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
        GEngine->OnTravelFailure().Remove(TravelFailureHandle);
    }
    Super::Shutdown();
}

bool UBYMGameInstance::PrepareCredentials(const FString& Username, const FString& Password, bool bCreate, FString& Error)
{
    ClearCredentials();
    const FString CleanName = Username.TrimStartAndEnd();
    if (CleanName.Len() < 3 || CleanName.Len() > 24 || Password.Len() < 10 || Password.Len() > 72)
    {
        Error = TEXT("Use a username of 3–24 characters and a password of 10–72 characters.");
        return false;
    }
    PendingUsername = CleanName;
    PendingPassword = Password;
    bPendingCreate = bCreate;
    bHasPendingCredentials = true;
    bReturningToMenu = false;
    LastConnectionError.Empty();
    return true;
}

bool UBYMGameInstance::HostAndPlay(const FString& Username, const FString& Password, bool bCreate, FString& Error)
{
    if (!PrepareCredentials(Username, Password, bCreate, Error)) { return false; }
    const UBYMSettings* Settings = GetDefault<UBYMSettings>();
    if (!FPackageName::DoesPackageExist(Settings->WorldMap))
    {
        ClearCredentials();
        Error = TEXT("OpenWorld is missing. Run Backyard Monsters NXT > Setup All in the editor, then package again.");
        return false;
    }
    bInitiatedHost = true;
    // Set the FURL port used to create the listen socket, not merely a map option.
    FURL::UrlConfig.DefaultPort = FMath::Clamp(Settings->ServerPort, 1024, 65535);
    ConnectedAddress = FString::Printf(TEXT("127.0.0.1:%d"), FMath::Clamp(Settings->ServerPort, 1024, 65535));
    UGameplayStatics::OpenLevel(this, FName(*Settings->WorldMap), true, TEXT("listen"));
    return true;
}

bool UBYMGameInstance::JoinHost(const FString& Address, const FString& Username, const FString& Password, bool bCreate, FString& Error)
{
    const FString CleanAddress = Address.TrimStartAndEnd();
    if (CleanAddress.IsEmpty() || CleanAddress.Len() > 253)
    {
        Error = TEXT("Enter the host IP address or DNS name.");
        return false;
    }
    // The address is a host, never an arbitrary travel URL containing map options or credentials.
    for (const TCHAR Ch : CleanAddress)
    {
        if (!FChar::IsAlnum(Ch) && Ch != TEXT('.') && Ch != TEXT('-') && Ch != TEXT(':') && Ch != TEXT('[') && Ch != TEXT(']'))
        {
            Error = TEXT("The host address may contain only a hostname or IP address.");
            return false;
        }
    }
    if (!PrepareCredentials(Username, Password, bCreate, Error)) { return false; }
    APlayerController* PC = GetFirstLocalPlayerController();
    if (!PC)
    {
        ClearCredentials();
        Error = TEXT("The local player is not ready. Please try again.");
        return false;
    }
    const int32 Port = FMath::Clamp(GetDefault<UBYMSettings>()->ServerPort, 1024, 65535);
    FString Host = CleanAddress;
    if (Host.Contains(TEXT(":")) && !Host.StartsWith(TEXT("["))) { Host = TEXT("[") + Host + TEXT("]"); }
    ConnectedAddress = FString::Printf(TEXT("%s:%d"), *Host, Port);
    bInitiatedHost = false;
    PC->ClientTravel(ConnectedAddress, TRAVEL_Absolute);
    return true;
}

bool UBYMGameInstance::ConsumeCredentials(FString& Username, FString& Password, bool& bCreate)
{
    if (!bHasPendingCredentials) { return false; }
    Username = PendingUsername;
    Password = PendingPassword;
    bCreate = bPendingCreate;
    ClearCredentials();
    return true;
}

void UBYMGameInstance::ClearCredentials()
{
    EraseString(PendingPassword);
    PendingUsername.Empty();
    bHasPendingCredentials = false;
    bPendingCreate = false;
}

void UBYMGameInstance::ReturnToMenu(const FString& Reason)
{
    ClearCredentials();
    LastConnectionError = Reason;
    if (bReturningToMenu) { return; }
    bReturningToMenu = true;
    if (UBYMServerSubsystem* Server = GetSubsystem<UBYMServerSubsystem>()) { Server->StopAuthority(); }
    bInitiatedHost = false;
    ConnectedAddress.Empty();
    UGameplayStatics::OpenLevel(this, FName(*GetDefault<UBYMSettings>()->MainMenuMap), true);
}

void UBYMGameInstance::NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& Error)
{
    if (World && World->GetGameInstance() != this) { return; }
    if (bInitiatedHost && World && World->GetNetMode() != NM_Client
        && FailureType != ENetworkFailure::NetDriverAlreadyExists
        && FailureType != ENetworkFailure::NetDriverCreateFailure
        && FailureType != ENetworkFailure::NetDriverListenFailure)
    {
        // A remote player's dropped connection must not close the entire host.
        return;
    }
    ReturnToMenu(FString::Printf(TEXT("Connection ended: %s"), *Error.Left(400)));
}

void UBYMGameInstance::TravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
    if (World && World->GetGameInstance() != this) { return; }
    ReturnToMenu(FString::Printf(TEXT("Unable to open the game: %s"), *Error.Left(400)));
}
