#include "BYMPlayerController.h"
#include "BYMBoard.h"
#include "BYMGameInstance.h"
#include "BYMGameMode.h"
#include "BYMServerSubsystem.h"
#include "BYMSettings.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameSession.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogBYMNetwork, Log, All);

namespace
{
    TSharedPtr<FJsonObject> ReadJson(const FString& Json)
    {
        TSharedPtr<FJsonObject> Result;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Result);
        return Result;
    }
    FString WriteJson(const TSharedRef<FJsonObject>& Value)
    {
        FString Json;
        FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json));
        return Json;
    }
    FString StringField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
    {
        FString Value;
        if (Obj) { Obj->TryGetStringField(Field, Value); }
        return Value;
    }
    TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        return Obj && Obj->TryGetObjectField(Field, Value) && Value ? *Value : nullptr;
    }
    void Wipe(FString& Value)
    {
        if (!Value.IsEmpty()) { FMemory::Memzero(Value.GetCharArray().GetData(), Value.GetCharArray().Num() * sizeof(TCHAR)); }
        Value.Empty();
    }
}

ABYMPlayerController::ABYMPlayerController()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.05f;
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

bool ABYMPlayerController::IsMenuWorld() const
{
    return UGameplayStatics::GetCurrentLevelName(this, true).Equals(
        FPackageName::GetShortName(GetDefault<UBYMSettings>()->MainMenuMap), ESearchCase::IgnoreCase);
}

bool ABYMPlayerController::IsHost() const
{
    return !IsMenuWorld() && GetNetMode() != NM_Client;
}

void ABYMPlayerController::BeginPlay()
{
    Super::BeginPlay();
    ResetNetworkState();
    ConnectedAt = FPlatformTime::Seconds();
    RateWindowAt = ConnectedAt;
    if (HasAuthority()) { ConnectionKey = FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    if (!IsLocalController()) { return; }
    SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
    bShowMouseCursor = true;
    UBYMGameInstance* GI = Cast<UBYMGameInstance>(GetGameInstance());
    if (GI) { GI->ApplyGraphicsDefaults(); }
    if (IsMenuWorld())
    {
        StatusMessage = GI ? GI->LastConnectionError : TEXT("");
        return;
    }
    StatusMessage = TEXT("Connected. Sign in to enter your yard.");
    if (GI)
    {
        FString Username, Password;
        bool bCreate = false;
        if (GI->ConsumeCredentials(Username, Password, bCreate))
        {
            SubmitCredentials(Username, Password, bCreate);
            Wipe(Password);
        }
    }
}

void ABYMPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ReleaseServerSession();
    ResetNetworkState();
    Super::EndPlay(EndPlayReason);
}

void ABYMPlayerController::ReleaseServerSession()
{
    if (!HasAuthority() || ConnectionKey.IsEmpty()) { return; }
    if (UBYMServerSubsystem* Server = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>())
    {
        Server->Disconnect(ConnectionKey, AuthenticatedId);
    }
    AuthenticatedId.Empty();
    ConnectionKey.Empty();
}

void ABYMPlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsLocalController()) { FlushPendingMapRequest(); }
    if (IsMenuWorld() || !HasAuthority()) { return; }
    const double Now = FPlatformTime::Seconds();
    if (AuthenticatedId.IsEmpty())
    {
        if (!IsLocalController() && Now - ConnectedAt > 180.0)
        {
            DisconnectAbusiveClient(TEXT("Sign-in timed out. Rejoin when you are ready."));
        }
        return;
    }
    if (Now >= NextSnapshotAt)
    {
        RefreshSnapshot();
    }
    FlushServerMapRequest();
    FlushNetworkPayloads();
}

void ABYMPlayerController::DisconnectAbusiveClient(const FString& Reason)
{
    if (!HasAuthority()) { return; }
    if (AGameModeBase* Mode = GetWorld()->GetAuthGameMode())
    {
        if (Mode->GameSession && !IsLocalController())
        {
            Mode->GameSession->KickPlayer(this, FText::FromString(Reason));
        }
    }
}

bool ABYMPlayerController::AllowServerCall(int32 PayloadLength, bool bRequiresAccount)
{
    if (!HasAuthority() || IsMenuWorld() || !GetGameInstance()) { return false; }
    UBYMServerSubsystem* Server = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>();
    if (!Server || !Server->IsAuthorityRunning())
    {
        const ABYMGameMode* Mode = Cast<ABYMGameMode>(GetWorld()->GetAuthGameMode());
        ClientStatus(Mode && !Mode->StartupError.IsEmpty() ? Mode->StartupError : TEXT("The host is not ready."));
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - RateWindowAt >= 1.0) { RateWindowAt = Now; WindowCalls = 0; }
    if (++WindowCalls > FMath::Clamp(GetDefault<UBYMSettings>()->MaxCommandsPerSecond, 2, 60))
    {
        ClientStatus(TEXT("Too many actions at once. Wait a moment before trying again."));
        if (++RateViolations >= 30) { DisconnectAbusiveClient(TEXT("Too many requests.")); }
        return false;
    }
    if (PayloadLength < 0 || PayloadLength > FMath::Clamp(GetDefault<UBYMSettings>()->MaxCommandBytes, 512, 65536))
    {
        DisconnectAbusiveClient(TEXT("Request exceeds the server limit."));
        return false;
    }
    if (bRequiresAccount && AuthenticatedId.IsEmpty())
    {
        ClientStatus(TEXT("Sign in before performing that action."));
        return false;
    }
    return true;
}

void ABYMPlayerController::SubmitCredentials(const FString& Username, const FString& Password, bool bCreate)
{
    if (IsMenuWorld() || !IsLocalController() || bAuthenticated || bConnecting) { return; }
    const FString CleanName = Username.TrimStartAndEnd();
    if (CleanName.Len() < 3 || CleanName.Len() > 24 || Password.Len() < 10 || Password.Len() > 72)
    {
        StatusMessage = TEXT("Use a username of 3–24 characters and a password of 10–72 characters.");
        return;
    }
    bConnecting = true;
    StatusMessage = bCreate ? TEXT("Creating your account on the host…") : TEXT("Signing in to the host…");
    FString RequestPassword = Password;
    ServerAuthenticate(CleanName, RequestPassword, bCreate);
    Wipe(RequestPassword);
}

void ABYMPlayerController::ServerAuthenticate_Implementation(const FString& Username, const FString& Password, bool bCreate)
{
    const int64 RequestLength = static_cast<int64>(Username.Len()) + Password.Len();
    if (!AllowServerCall(static_cast<int32>(FMath::Min<int64>(RequestLength, MAX_int32)), false)
        || !AuthenticatedId.IsEmpty() || bAuthenticationInFlight) { return; }
    if (Username.Len() < 3 || Username.Len() > 24 || Password.Len() < 10 || Password.Len() > 72)
    {
        ClientAuthentication(false, TEXT("Use a username of 3–24 characters and a password of 10–72 characters."));
        return;
    }
    UBYMServerSubsystem* Server = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>();
    bAuthenticationInFlight = true;
    const TWeakObjectPtr<ABYMPlayerController> WeakThis(this);
    const FString ExpectedConnection = ConnectionKey;
    Server->AuthenticateAsync(ConnectionKey, Username, Password, bCreate,
        [WeakThis, ExpectedConnection](bool bSuccess, const FString& AccountId, const FString& Error)
        {
            if (!WeakThis.IsValid() || WeakThis->ConnectionKey != ExpectedConnection) { return; }
            ABYMPlayerController* Self = WeakThis.Get();
            Self->bAuthenticationInFlight = false;
            if (!bSuccess) { Self->ClientAuthentication(false, Error); return; }
            Self->ResetNetworkState();
            Self->AuthenticatedId = AccountId;
            Self->ViewedYardId.Empty();
            Self->ClientAuthentication(true, TEXT("Welcome to your yard."));
            Self->RefreshSnapshot(true);
        });
}

void ABYMPlayerController::ClientAuthentication_Implementation(bool bSuccess, const FString& Message)
{
    bAuthenticated = bSuccess;
    bConnecting = false;
    StatusMessage = Message.Left(500);
    if (bSuccess)
    {
        for (auto& Receiver : NetworkReceivers) { Receiver.Reset(); }
        RequestLadder(0);
        if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
    }
}

bool ABYMPlayerController::SendCommand(const FString& Op, const FString& Payload)
{
    if (!bAuthenticated || Op.Len() > 32 || Payload.Len() > 16384) { return false; }
    if (IsCommandPending())
    {
        StatusMessage = TEXT("Waiting for the host to confirm the previous action…");
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - LastLocalCommandAt < 0.10)
    {
        StatusMessage = TEXT("Wait a moment before repeating that action.");
        return false;
    }
    LastLocalCommandAt = Now;
    // Set before invoking the RPC: on a listen host the result may arrive inline.
    PendingOperation = Op;
    ServerCommand(Op, Payload);
    return true;
}

void ABYMPlayerController::ServerCommand_Implementation(const FString& Operation, const FString& Payload)
{
    if (!AllowServerCall(Operation.Len() + Payload.Len(), true))
    {
        ClientCommandResult(Operation, false, TEXT(""));
        return;
    }
    static const TSet<FString> Allowed{TEXT("build"),TEXT("upgrade"),TEXT("move"),TEXT("collect"),TEXT("train"),
        TEXT("invade"),TEXT("deploy"),TEXT("retreat"),TEXT("repair"),TEXT("inspect"),
        TEXT("unlock"),TEXT("research"),TEXT("cancelUnlock"),TEXT("cancelResearch"),TEXT("cancelTraining"),
        TEXT("cancelConstruction"),TEXT("cancelUpgrade"),TEXT("bunkerTransfer"),TEXT("bunkerDismiss"),
        TEXT("shopBuy"),TEXT("shopSpeedup"),TEXT("decorate"),TEXT("clearmushroom"),
        TEXT("mapInspect"),TEXT("mapInvade"),TEXT("mapBookmark"),TEXT("mapRepair"),TEXT("mapMove"),TEXT("mapAbandon"),TEXT("mapRelocate")};
    if (!Allowed.Contains(Operation)) { ClientCommandResult(Operation, false, TEXT("Unknown action.")); return; }
    const FString Result = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>()->Execute(AuthenticatedId, Operation, Payload);
    const TSharedPtr<FJsonObject> Obj = ReadJson(Result);
    bool bOk = false;
    if (Obj) { Obj->TryGetBoolField(TEXT("ok"), bOk); }
    FString Message = StringField(Obj, bOk ? TEXT("message") : TEXT("error"));
    if (Message.IsEmpty()) { Message = bOk ? TEXT("Action accepted.") : TEXT("The server rejected that action."); }
    if (bOk && Operation == TEXT("invade"))
    {
        const TSharedPtr<FJsonObject> Request = ReadJson(Payload);
        ViewedYardId = StringField(Request, TEXT("target"));
    }
    if (bOk && Operation == TEXT("retreat")) { ViewedYardId.Empty(); }
    if (bOk && (Operation == TEXT("mapInspect") || Operation == TEXT("mapInvade")))
    {
        // The host resolves the selected map cell. Never trust a requested yard
        // identifier to choose a territory's actual defender or viewed base.
        const FString AuthorizedYard = StringField(Obj, TEXT("viewYardId"));
        if (!AuthorizedYard.IsEmpty()) { ViewedYardId = AuthorizedYard; }
    }
    if (bOk && Operation == TEXT("mapRelocate")) { ViewedYardId.Empty(); }
    ClientCommandResult(Operation, bOk, Message);
    RefreshSnapshot(true);
}

void ABYMPlayerController::ClientCommandResult_Implementation(const FString& Operation, bool bSuccess, const FString& Message)
{
    PendingOperation.Empty();
    LastCommandOperation = Operation;
    bLastCommandSucceeded = bSuccess;
    ++CommandRevision;
    if (!Message.IsEmpty()) { StatusMessage = Message.Left(500); }
    // Keep failed placement tools active so the player can choose another cell.
    // A move must never appear complete before the host has accepted it.
    if (bSuccess && (Operation == TEXT("build") || Operation == TEXT("decorate") || Operation == TEXT("move") || Operation == TEXT("mapMove") || Operation == TEXT("retreat")))
    {
        PlacementMode.Empty();
    }
    if (bSuccess && (Operation == TEXT("invade") || Operation == TEXT("retreat") || Operation == TEXT("mapInspect") || Operation == TEXT("mapInvade")))
    {
        SelectedBuilding.Empty();
        PlacementMode.Empty();
    }
    if (bSuccess && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}

void ABYMPlayerController::RefreshSnapshot(bool bMustDeliver)
{
    if (!HasAuthority() || AuthenticatedId.IsEmpty() || bNetworkFailed) { return; }
    if (UBYMServerSubsystem* Server = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>())
    {
        const FString Payload = Server->GetSnapshot(AuthenticatedId, ViewedYardId);
        const TSharedPtr<FJsonObject> Snapshot = ReadJson(Payload);
        const TSharedPtr<FJsonObject> Battle = ObjectField(Snapshot, TEXT("battle"));
        const TSharedPtr<FJsonObject> Yard = ObjectField(Snapshot, TEXT("yard"));
        const bool bViewingBattle = Battle && Yard
            && StringField(Battle, TEXT("defender")) == StringField(Yard, TEXT("id"));
        const double Interval = bViewingBattle
            ? FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->BattleSnapshotIntervalSeconds), 0.1, 1.0)
            : FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->YardSnapshotIntervalSeconds), 0.5, 10.0);
        NextSnapshotAt = FPlatformTime::Seconds() + Interval;
        const FString Identity = StringField(Yard, TEXT("id")) + TEXT("|")
            + StringField(Battle, TEXT("id")) + TEXT("|")
            + StringField(ObjectField(Snapshot, TEXT("completedBattle")), TEXT("id"));
        // Preserve start/end and viewed-yard transitions under backpressure.
        // Ordinary timer updates may coalesce; command results remain reliable.
        const bool bTransition = Identity != LastQueuedSnapshotIdentity;
        LastQueuedSnapshotIdentity = Identity;
        QueueNetworkPayload(0, Payload, bMustDeliver || bTransition);
    }
}

void ABYMPlayerController::ApplySnapshotJson(const FString& Json)
{
    if (Json.Len() > 1000000) { return; }
    const FString PreviousYard = StringField(ObjectField(ReadJson(LastSnapshot), TEXT("yard")), TEXT("id"));
    LastSnapshot = EnrichSnapshotLocally(Json);
    const TSharedPtr<FJsonObject> Snapshot = ReadJson(LastSnapshot);
    const FString CurrentYard = StringField(ObjectField(Snapshot, TEXT("yard")), TEXT("id"));
    if (!PreviousYard.IsEmpty() && PreviousYard != CurrentYard)
    {
        PlacementMode.Empty();
        SelectedBuilding.Empty();
    }
    const TSharedPtr<FJsonObject> Battle = ObjectField(Snapshot, TEXT("battle"));
    if (PlacementMode == TEXT("deploy") && (!Battle || StringField(Battle, TEXT("attacker")) != StringField(Snapshot, TEXT("selfId"))))
    {
        // A finished or expired invasion cannot leave a stale deployment cursor.
        PlacementMode.Empty();
    }
    ++SnapshotRevision;
    if (!Board.IsValid())
    {
        for (TActorIterator<ABYMBoard> It(GetWorld()); It; ++It) { Board = *It; break; }
        if (!Board.IsValid()) { Board = GetWorld()->SpawnActor<ABYMBoard>(); }
    }
    if (Board.IsValid())
    {
        Board->ApplySnapshot(LastSnapshot);
        if (!Board->GetPresentationError().IsEmpty())
            StatusMessage = Board->GetPresentationError().Left(500);
    }
}

void ABYMPlayerController::RequestLadder(int32 Page)
{
    if (bAuthenticated) { ServerLadder(FMath::Max(0, Page)); }
}

void ABYMPlayerController::ServerLadder_Implementation(int32 Page)
{
    if (!AllowServerCall(0, true)) { return; }
    QueueNetworkPayload(2, GetGameInstance()->GetSubsystem<UBYMServerSubsystem>()->GetLadder(AuthenticatedId, FMath::Clamp(Page, 0, 100000)), false);
}

void ABYMPlayerController::ApplyLadderJson(const FString& Json)
{
    if (Json.Len() > 100000) { return; }
    LastLadder = Json;
    ++LadderRevision;
}

void ABYMPlayerController::RequestWorldMap(int32 CenterX, int32 CenterY, int32 Radius, const FString& SelectedCell, int32 OwnedPage)
{
    if (!bAuthenticated || SelectedCell.Len() > 128) { return; }
    // Retain the newest drag/selection request across the throttle interval.
    // In particular, the last mouse-up must not leave the prior viewport stuck.
    QueuedMapX=FMath::Clamp(CenterX,-1,499);QueuedMapY=FMath::Clamp(CenterY,-1,499);
    QueuedMapRadius=FMath::Clamp(Radius,4,15);QueuedMapSelected=SelectedCell;QueuedOwnedPage=FMath::Clamp(OwnedPage,0,5000);bMapRequestQueued=true;
    FlushPendingMapRequest();
}

void ABYMPlayerController::FlushPendingMapRequest()
{
    if (!bAuthenticated || !bMapRequestQueued) { return; }
    const double Now = FPlatformTime::Seconds();
    if (Now - LastLocalMapRequestAt < .30) { return; }
    const FString Key=FString::Printf(TEXT("%d,%d,%d,%d:"),QueuedMapX,QueuedMapY,QueuedMapRadius,QueuedOwnedPage)+QueuedMapSelected;
    // Mouse-up and selection can queue the same view in one gesture. Coalesce
    // those duplicates while still permitting the periodic authoritative refresh.
    if(Key==LastSentMapKey&&Now-LastLocalMapRequestAt<1.){bMapRequestQueued=false;return;}
    LastSentMapKey=Key;LastLocalMapRequestAt = Now;
    bMapRequestQueued=false;
    // -1,-1 requests the authenticated player's own main-yard coordinates.
    ServerWorldMap(QueuedMapX,QueuedMapY,QueuedMapRadius,QueuedMapSelected,QueuedOwnedPage);
}

void ABYMPlayerController::ServerWorldMap_Implementation(int32 CenterX, int32 CenterY, int32 Radius, const FString& SelectedCell, int32 OwnedPage)
{
    if (!AllowServerCall(SelectedCell.Len(),true) || SelectedCell.Len() > 128) { return; }
    ServerMapX=FMath::Clamp(CenterX,-1,499);ServerMapY=FMath::Clamp(CenterY,-1,499);
    ServerMapRadius=FMath::Clamp(Radius,4,15);ServerMapSelected=SelectedCell;ServerOwnedPage=FMath::Clamp(OwnedPage,0,5000);bServerMapRequestQueued=true;
    FlushServerMapRequest();
}

void ABYMPlayerController::FlushServerMapRequest()
{
    if (!HasAuthority() || AuthenticatedId.IsEmpty() || !bServerMapRequestQueued || !GetGameInstance()) { return; }
    const double Now = FPlatformTime::Seconds();
    if (Now - LastServerMapRequestAt < .25) { return; }
    LastServerMapRequestAt = Now;
    bServerMapRequestQueued=false;
    const TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
    Request->SetNumberField(TEXT("x"),ServerMapX);
    Request->SetNumberField(TEXT("y"),ServerMapY);
    Request->SetNumberField(TEXT("radius"),ServerMapRadius);
    Request->SetStringField(TEXT("selected"),ServerMapSelected);
    Request->SetNumberField(TEXT("ownedPage"),ServerOwnedPage);
    const FString Json = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>()->Execute(AuthenticatedId,TEXT("mapView"),WriteJson(Request));
    QueueNetworkPayload(1, Json, false);
}

void ABYMPlayerController::ApplyWorldMapJson(const FString& Json)
{
    if (Json.Len() > 1000000) { return; }
    const TSharedPtr<FJsonObject> Response = ReadJson(Json);
    if (!Response) { return; }
    bool bOk = false;
    if (Response->TryGetBoolField(TEXT("ok"),bOk) && !bOk)
    {
        const FString Error = StringField(Response,TEXT("error"));
        if (!Error.IsEmpty()) { StatusMessage = Error.Left(500); }
    }
    LastWorldMap = Json;
    ++WorldMapRevision;
}

void ABYMPlayerController::ViewYard(const FString& Id)
{
    if (bAuthenticated && Id.Len() <= 128)
    {
        PlacementMode.Empty();
        SelectedBuilding.Empty();
        ServerViewYard(Id);
    }
}

void ABYMPlayerController::ServerViewYard_Implementation(const FString& YardId)
{
    if (!AllowServerCall(YardId.Len(), true) || YardId.Len() > 128) { return; }
    UBYMServerSubsystem* Server = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>();
    const TSharedPtr<FJsonObject> Snapshot = ReadJson(Server->GetSnapshot(AuthenticatedId, ViewedYardId));
    const TSharedPtr<FJsonObject> Battle = ObjectField(Snapshot, TEXT("battle"));
    if (StringField(Battle, TEXT("attacker")) == AuthenticatedId
        && YardId != StringField(Battle, TEXT("defender")))
    {
        ClientStatus(TEXT("Finish or retreat from this invasion before visiting another yard."));
        RefreshSnapshot();
        return;
    }
    if (!YardId.IsEmpty())
    {
        TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("target"), YardId);
        const FString Result = GetGameInstance()->GetSubsystem<UBYMServerSubsystem>()->Execute(AuthenticatedId, TEXT("inspect"), WriteJson(Request));
        const TSharedPtr<FJsonObject> Response = ReadJson(Result);
        bool bOk = false;
        if (!Response || !Response->TryGetBoolField(TEXT("ok"), bOk) || !bOk)
        {
            ClientStatus(StringField(Response, TEXT("error")));
            return;
        }
    }
    ViewedYardId = YardId;
    RefreshSnapshot(true);
}

void ABYMPlayerController::ReturnHome()
{
    PlacementMode.Empty();
    SelectedBuilding.Empty();
    const TSharedPtr<FJsonObject> Snapshot = ReadJson(LastSnapshot);
    if (const TSharedPtr<FJsonObject> Battle = ObjectField(Snapshot, TEXT("battle")))
    {
        if (StringField(Battle, TEXT("attacker")) == StringField(Snapshot, TEXT("selfId")))
        {
            SendCommand(TEXT("retreat"), TEXT("{}"));
            return;
        }
    }
    ViewYard(TEXT(""));
}

void ABYMPlayerController::LeaveServer()
{
    if (UBYMGameInstance* GI = Cast<UBYMGameInstance>(GetGameInstance())) { GI->ReturnToMenu(); }
}

void ABYMPlayerController::ClientStatus_Implementation(const FString& Message)
{
    StatusMessage = Message.Left(500);
    if (!bAuthenticated)
    {
        bConnecting = false;
    }
}

void ABYMPlayerController::HandleBoardClick(int32 X, int32 Y, const FString& BuildingId)
{
    if (!bAuthenticated) { return; }
    TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
    Request->SetNumberField(TEXT("x"), X);
    Request->SetNumberField(TEXT("y"), Y);
    if (PlacementMode == TEXT("build") || PlacementMode == TEXT("decorate"))
    {
        Request->SetNumberField(TEXT("type"), SelectedBuildingType);
        SendCommand(PlacementMode == TEXT("decorate") ? TEXT("decorate") : TEXT("build"), WriteJson(Request));
    }
    else if (PlacementMode == TEXT("deploy"))
    {
        Request->SetStringField(TEXT("type"), SelectedMonsterType);
        Request->SetNumberField(TEXT("count"), SelectedMonsterCount);
        SendCommand(TEXT("deploy"), WriteJson(Request));
    }
    else if (PlacementMode == TEXT("move") && !SelectedBuilding.IsEmpty())
    {
        Request->SetStringField(TEXT("building"), SelectedBuilding);
        const auto Cell = ObjectField(ObjectField(ReadJson(LastSnapshot),TEXT("yard")),TEXT("mapCell"));
        double Kind=101.;bool bOwned=false;
        if(Cell){Cell->TryGetNumberField(TEXT("kind"),Kind);Cell->TryGetBoolField(TEXT("owned"),bOwned);}
        if(Cell&&bOwned&&Kind!=101.)
        {
            Request->SetStringField(TEXT("cell"),StringField(Cell,TEXT("id")));
            SendCommand(TEXT("mapMove"),WriteJson(Request));
        }
        else SendCommand(TEXT("move"), WriteJson(Request));
    }
    else
    {
        SelectedBuilding = BuildingId;
        StatusMessage = BuildingId.IsEmpty() ? TEXT("Select a building, or choose a construction or deployment tool.") : TEXT("Building selected. Upgrade, repair, or move it from the Yard panel.");
    }
}

void ABYMPlayerController::ResetNetworkState()
{
    for (int32 Channel = 0; Channel < 3; ++Channel)
    {
        NetworkSenders[Channel].Reset();
        NetworkReceivers[Channel].Reset();
        NetworkFrameStartedAt[Channel] = 0;
        NetworkTrackedSequence[Channel] = 0;
    }
    NextNetworkStream = 0;
    LastQueuedSnapshotIdentity.Empty();
    bNetworkFailed = false;
    NetworkStatsStartedAt = FPlatformTime::Seconds();
    NetworkOfferedJsonBytes = NetworkReceivedBytes = NetworkReceivedFrames = 0;
}

void ABYMPlayerController::FailNetworkStream(const FString& Reason)
{
    if (bNetworkFailed) { return; }
    bNetworkFailed = true;
    UE_LOG(LogBYMNetwork, Warning, TEXT("State transfer stopped for %s: %s"), *AuthenticatedId.Left(8), *Reason);
    ClientStatus(TEXT("The connection could not keep the yard synchronized. Rejoin the host to refresh it."));
    DisconnectAbusiveClient(TEXT("State synchronization could not complete. Please rejoin."));
}

void ABYMPlayerController::QueueNetworkPayload(uint8 StreamId, const FString& Json, bool bMustDeliver)
{
    if (!HasAuthority() || AuthenticatedId.IsEmpty() || StreamId >= 3 || bNetworkFailed) { return; }
    if (Json.Len() > BYMNetworkSnapshot::MaxDecodedCharacters)
    {
        FailNetworkStream(TEXT("Response exceeds the bounded state size."));
        return;
    }
    // A listen host already owns the data. Do not compress or run a network
    // acknowledgement loop for its local player.
    if (IsLocalController()) { ApplyNetworkPayload(StreamId, Json); return; }
    FString Error;
    if (!NetworkSenders[StreamId].Queue(Json, false, bMustDeliver, Error))
    {
        FailNetworkStream(Error);
        return;
    }
    const FTCHARToUTF8 Utf8(*Json);
    NetworkOfferedJsonBytes += static_cast<uint64>(Utf8.Length());
    // Dispatch only from Tick, so several commands in one frame cannot evade
    // the shared chunk budget or recursively flush from an ACK callback.
}

void ABYMPlayerController::FlushNetworkPayloads()
{
    if (!HasAuthority() || IsLocalController() || AuthenticatedId.IsEmpty() || bNetworkFailed) { return; }
    const UBYMSettings* Settings = GetDefault<UBYMSettings>();
    const double Now = FPlatformTime::Seconds();
    for (int32 Channel = 0; Channel < 3; ++Channel)
    {
        FString Error;
        if (!NetworkSenders[Channel].Prepare(Settings->bUseNetworkDeltas, Settings->bCompressNetworkState, Error))
        {
            FailNetworkStream(Error);
            return;
        }
        const int32 Sequence = NetworkSenders[Channel].GetInFlightSequence();
        if (Sequence != NetworkTrackedSequence[Channel])
        {
            NetworkTrackedSequence[Channel] = Sequence;
            NetworkFrameStartedAt[Channel] = Sequence ? Now : 0;
        }
        if (Sequence && Now - NetworkFrameStartedAt[Channel] > FMath::Clamp(static_cast<double>(Settings->NetworkAckTimeoutSeconds), 10.0, 120.0))
        {
            FailNetworkStream(TEXT("Timed out waiting for a state acknowledgement."));
            return;
        }
    }
    const int32 Budget = FMath::Clamp(Settings->NetworkChunksPerTick, 1, 8);
    for (int32 Sent = 0; Sent < Budget; ++Sent)
    {
        bool bSent = false;
        for (int32 Offset = 0; Offset < 3; ++Offset)
        {
            const int32 Channel = (NextNetworkStream + Offset) % 3;
            BYMNetworkStream::FChunk Chunk;
            if (!NetworkSenders[Channel].TakeNextChunk(Chunk)) { continue; }
            NextNetworkStream = (Channel + 1) % 3;
            ClientStateChunk(static_cast<uint8>(Channel), Chunk.Sequence, Chunk.BaseSequence,
                Chunk.Index, Chunk.Count, Chunk.RawBytes, Chunk.bCompressed, Chunk.bDelta, Chunk.Bytes);
            bSent = true;
            break;
        }
        if (!bSent) { break; }
    }
}

void ABYMPlayerController::ServerStateAck_Implementation(uint8 StreamId, int32 Sequence, bool bAccepted)
{
    if (!HasAuthority() || IsMenuWorld() || AuthenticatedId.IsEmpty() || StreamId >= 3 || bNetworkFailed) { return; }
    // ACKs acknowledge only an exact sent frame and do not mutate gameplay.
    // They have their own bounded state gate instead of consuming the player's
    // command allowance during five-per-second combat updates.
    if (!NetworkSenders[StreamId].Acknowledge(Sequence, bAccepted)) { return; }
    NetworkTrackedSequence[StreamId] = 0;
    NetworkFrameStartedAt[StreamId] = 0;
    if (NetworkSenders[StreamId].GetConsecutiveRejects() >= 3)
        FailNetworkStream(TEXT("Repeated invalid state reconstruction."));
}

void ABYMPlayerController::ServerStateChunkAck_Implementation(uint8 StreamId, int32 Sequence, int32 Index)
{
    if (!HasAuthority() || IsMenuWorld() || AuthenticatedId.IsEmpty() || StreamId >= 3 || bNetworkFailed) { return; }
    // One unacknowledged chunk per channel bounds Unreal's reliable queue even
    // when a large full state is split over hundreds of chunks. This is progress
    // only: the raw baseline is committed by the final whole-frame ACK.
    if (NetworkSenders[StreamId].AcknowledgeChunk(Sequence, Index))
        NetworkFrameStartedAt[StreamId] = FPlatformTime::Seconds();
}

void ABYMPlayerController::ClientStateChunk_Implementation(uint8 StreamId, int32 Sequence, int32 BaseSequence,
    int32 Index, int32 Count, int32 RawBytes, bool bCompressed, bool bDelta, const TArray<uint8>& Bytes)
{
    if (!IsLocalController() || !bAuthenticated || StreamId >= 3) { return; }
    BYMNetworkStream::FChunk Chunk;
    Chunk.Sequence = Sequence; Chunk.BaseSequence = BaseSequence;
    Chunk.Index = Index; Chunk.Count = Count; Chunk.RawBytes = RawBytes;
    Chunk.bCompressed = bCompressed; Chunk.bDelta = bDelta; Chunk.Bytes = Bytes;
    NetworkReceivedBytes += static_cast<uint64>(Bytes.Num());
    FString Json, Error;
    const BYMNetworkStream::EReceiveResult Result = NetworkReceivers[StreamId].Accept(Chunk, Json, Error);
    if (Result == BYMNetworkStream::EReceiveResult::Complete)
    {
        ++NetworkReceivedFrames;
        ApplyNetworkPayload(StreamId, Json);
        ServerStateAck(StreamId, Sequence, true);
    }
    else if (Result == BYMNetworkStream::EReceiveResult::Rejected)
    {
        UE_LOG(LogBYMNetwork, Warning, TEXT("State frame rejected; requesting a full refresh: %s"), *Error);
        ServerStateAck(StreamId, Sequence, false);
    }
    else if (Result == BYMNetworkStream::EReceiveResult::Pending)
    {
        ServerStateChunkAck(StreamId, Sequence, Index);
    }
}

void ABYMPlayerController::ApplyNetworkPayload(uint8 StreamId, const FString& Json)
{
    if (StreamId == 0) { ApplySnapshotJson(Json); }
    else if (StreamId == 1) { ApplyWorldMapJson(Json); }
    else if (StreamId == 2) { ApplyLadderJson(Json); }
}

void ABYMPlayerController::BYMNetStats()
{
    if (!IsLocalController()) { return; }
    if (HasAuthority())
    {
        for (TActorIterator<ABYMPlayerController> It(GetWorld()); It; ++It)
        {
            ABYMPlayerController* Remote = *It;
            if (Remote->IsLocalController() || Remote->AuthenticatedId.IsEmpty()) { continue; }
            uint64 PayloadBytes = 0, Frames = 0, Coalesced = 0, Suppressed = 0;
            for (const auto& Sender : Remote->NetworkSenders)
            {
                PayloadBytes += Sender.GetPayloadBytesSent(); Frames += Sender.GetFramesSent();
                Coalesced += Sender.GetCoalescedUpdates(); Suppressed += Sender.GetSuppressedUpdates();
            }
            const double Seconds = FMath::Max(1.0, FPlatformTime::Seconds() - Remote->NetworkStatsStartedAt);
            UE_LOG(LogBYMNetwork, Display, TEXT("Client %s: offered JSON %.1f KiB, state payload %.1f KiB, average %.2f Kbit/s, frames %llu, coalesced %llu, unchanged %llu (excludes Unreal/UDP overhead)."),
                *Remote->AuthenticatedId.Left(8), static_cast<double>(Remote->NetworkOfferedJsonBytes) / 1024.0, static_cast<double>(PayloadBytes) / 1024.0,
                static_cast<double>(PayloadBytes) * 8.0 / Seconds / 1000.0, static_cast<unsigned long long>(Frames),
                static_cast<unsigned long long>(Coalesced), static_cast<unsigned long long>(Suppressed));
        }
        StatusMessage = TEXT("Network payload totals for remote players were written to the log. The local host uses no state RPC payload.");
    }
    else
    {
        const double Seconds = FMath::Max(1.0, FPlatformTime::Seconds() - NetworkStatsStartedAt);
        StatusMessage = FString::Printf(TEXT("Received state: %.1f KiB, average %.2f Kbit/s, %llu frames. Excludes Unreal/UDP overhead."),
            static_cast<double>(NetworkReceivedBytes) / 1024.0, static_cast<double>(NetworkReceivedBytes) * 8.0 / Seconds / 1000.0,
            static_cast<unsigned long long>(NetworkReceivedFrames));
        UE_LOG(LogBYMNetwork, Display, TEXT("%s"), *StatusMessage);
    }
}

FString ABYMPlayerController::EnrichSnapshotLocally(const FString& Json)
{
    if (!bCatalogLoadAttempted)
    {
        bCatalogLoadAttempted = true;
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
        FString CatalogText;
        if (Plugin.IsValid() && FFileHelper::LoadFileToString(CatalogText, *FPaths::Combine(Plugin->GetContentDir(), TEXT("Data/catalog.json"))))
        {
            LocalCatalog = ReadJson(CatalogText);
        }
    }
    TSharedPtr<FJsonObject> SnapshotObject = ReadJson(Json);
    if (!SnapshotObject || !LocalCatalog)
    {
        if (!LocalCatalog) { StatusMessage = TEXT("The packaged game catalog is missing. Run Setup All and package again."); }
        return Json;
    }
    auto Array = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Key) -> TArray<TSharedPtr<FJsonValue>>
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        return Object && Object->TryGetArrayField(Key, Values) && Values ? *Values : TArray<TSharedPtr<FJsonValue>>();
    };
    auto Number = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Default = 0.0) -> double
    {
        double Value = 0;
        return Object && Object->TryGetNumberField(Key, Value) ? Value : Default;
    };
    const TSharedPtr<FJsonObject>* RulesPointer = nullptr;
    SnapshotObject->TryGetObjectField(TEXT("rules"), RulesPointer);
    const TSharedPtr<FJsonObject> Rules = RulesPointer ? *RulesPointer : nullptr;
    const double ConstructionTimeMultiplier = Number(Rules, TEXT("constructionTimeMultiplier"), 1.0);
    const double TrainingTimeMultiplier = Number(Rules, TEXT("trainingTimeMultiplier"), 1.0);
    TMap<int32,TSharedPtr<FJsonObject>> Definitions;
    for (const TSharedPtr<FJsonValue>& Value : Array(LocalCatalog,TEXT("buildings")))
    {
        const TSharedPtr<FJsonObject> Definition = Value->AsObject();
        if (Definition) { Definitions.Add(static_cast<int32>(Number(Definition,TEXT("id"))),Definition); }
    }
    auto MakeCost = [&Number](const TSharedPtr<FJsonObject>& Level) -> TSharedRef<FJsonObject>
    {
        TSharedRef<FJsonObject> Cost = MakeShared<FJsonObject>();
        for (const TCHAR* Key : {TEXT("twigs"),TEXT("pebbles"),TEXT("putty"),TEXT("goo")}) { Cost->SetNumberField(Key,Number(Level,Key)); }
        return Cost;
    };
    TSharedRef<FJsonObject> PublicCatalog = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> PublicBuildings;
    // Decorations have a separate authoritative purchase/placement command.
    // Enrich only the decoration types actually offered by this host, alongside
    // ordinary buildable types, so their native shop cards retain original art.
    TArray<TSharedPtr<FJsonValue>> VisibleTypes = Array(SnapshotObject,TEXT("buildableTypes"));
    TSet<int32> VisibleTypeIds;
    for (const TSharedPtr<FJsonValue>& Value : VisibleTypes) if (Value.IsValid()) VisibleTypeIds.Add(static_cast<int32>(Value->AsNumber()));
    const TSharedPtr<FJsonObject> Shop = ObjectField(ObjectField(SnapshotObject,TEXT("home")),TEXT("shop"));
    for (const TSharedPtr<FJsonValue>& Value : Array(Shop,TEXT("decorations")))
    {
        if (!Value.IsValid() || Value->Type != EJson::Object) continue;
        const int32 Id = static_cast<int32>(Number(Value->AsObject(),TEXT("type")));
        if (!VisibleTypeIds.Contains(Id) && Definitions.Contains(Id))
        { VisibleTypes.Add(MakeShared<FJsonValueNumber>(Id)); VisibleTypeIds.Add(Id); }
    }
    for (const TSharedPtr<FJsonValue>& Value : VisibleTypes)
    {
        const int32 Id = static_cast<int32>(Value->AsNumber());
        const TSharedPtr<FJsonObject>* DefinitionPtr = Definitions.Find(Id);
        if (!DefinitionPtr) { continue; }
        const TSharedPtr<FJsonObject> Definition = *DefinitionPtr;
        const TArray<TSharedPtr<FJsonValue>> Levels = Array(Definition,TEXT("levels"));
        if (Levels.IsEmpty()) { continue; }
        const TSharedPtr<FJsonObject> Level = Levels[0]->AsObject();
        TSharedRef<FJsonObject> Public = MakeShared<FJsonObject>();
        Public->SetNumberField(TEXT("id"),Id);
        Public->SetStringField(TEXT("name"),StringField(Definition,TEXT("name")));
        Public->SetStringField(TEXT("type"),StringField(Definition,TEXT("type")));
        Public->SetNumberField(TEXT("group"),Number(Definition,TEXT("group")));
        Public->SetStringField(TEXT("description"),StringField(Definition,TEXT("description")));
        FString Texture = StringField(Level,TEXT("compositeTexture"));
        if (Texture.IsEmpty()) { Texture = StringField(Definition,TEXT("texture")); }
        Public->SetStringField(TEXT("texture"),Texture);
        FString Thumbnail = StringField(Definition,TEXT("thumbnailTexture"));
        if (Thumbnail.IsEmpty()) { Thumbnail = Texture; }
        Public->SetStringField(TEXT("thumbnailTexture"),Thumbnail);
        FString MenuTexture = StringField(Definition,TEXT("menuTexture"));
        if (MenuTexture.IsEmpty()) { MenuTexture = Thumbnail; }
        Public->SetStringField(TEXT("menuTexture"),MenuTexture);
        Public->SetNumberField(TEXT("size"),FMath::Max(1,FMath::CeilToInt(Number(Definition,TEXT("size"),20.0)/20.0)));
        Public->SetNumberField(TEXT("time"),FMath::Clamp(Number(Level,TEXT("time")) * ConstructionTimeMultiplier, 1.0, 31536000.0));
        Public->SetNumberField(TEXT("maxLevel"),Levels.Num());
        Public->SetNumberField(TEXT("hp"),Number(Level,TEXT("hp")));
        Public->SetObjectField(TEXT("cost"),MakeCost(Level));
        TArray<FString> Required;
        for (const TSharedPtr<FJsonValue>& RequirementValue : Array(Level,TEXT("requirements")))
        {
            const TArray<TSharedPtr<FJsonValue>>& Requirement = RequirementValue->AsArray();
            if (Requirement.Num() < 3) { continue; }
            const int32 RequiredId = static_cast<int32>(Requirement[0]->AsNumber());
            const TSharedPtr<FJsonObject>* RequiredDef = Definitions.Find(RequiredId);
            Required.Add(FString::Printf(TEXT("%d × %s L%d"),static_cast<int32>(Requirement[1]->AsNumber()),
                RequiredDef ? *StringField(*RequiredDef,TEXT("name")) : *FString::FromInt(RequiredId),static_cast<int32>(Requirement[2]->AsNumber())));
        }
        Public->SetStringField(TEXT("requirementsText"),FString::Join(Required,TEXT(", ")));
        PublicBuildings.Add(MakeShared<FJsonValueObject>(Public));
    }
    PublicCatalog->SetArrayField(TEXT("buildings"),PublicBuildings);
    TSet<FString> Trainable;
    for (const TSharedPtr<FJsonValue>& Value : Array(SnapshotObject,TEXT("trainableTypes"))) { Trainable.Add(Value->AsString()); }
    const TSharedPtr<FJsonObject>* MonsterOptionsPointer = nullptr;
    SnapshotObject->TryGetObjectField(TEXT("monsterOptions"), MonsterOptionsPointer);
    const TSharedPtr<FJsonObject> MonsterOptions = MonsterOptionsPointer ? *MonsterOptionsPointer : nullptr;
    TArray<TSharedPtr<FJsonValue>> PublicMonsters;
    for (const TSharedPtr<FJsonValue>& Value : Array(LocalCatalog,TEXT("monsters")))
    {
        const TSharedPtr<FJsonObject> Definition = Value->AsObject();
        const FString Id = StringField(Definition,TEXT("id"));
        // Locked monsters must remain visible in the Locker. The host defines
        // the supported roster and all action availability; local data adds art.
        const TSharedPtr<FJsonObject>* OptionPointer = nullptr;
        if (MonsterOptions) { MonsterOptions->TryGetObjectField(Id, OptionPointer); }
        const TSharedPtr<FJsonObject> Option = OptionPointer ? *OptionPointer : nullptr;
        if (!Option && !Trainable.Contains(Id)) { continue; }
        TSharedRef<FJsonObject> Public = MakeShared<FJsonObject>();
        Public->SetStringField(TEXT("id"),Id);
        Public->SetStringField(TEXT("name"),StringField(Definition,TEXT("name")));
        const TSharedPtr<FJsonObject>* Sprite = nullptr;
        FString Texture;
        if (Definition->TryGetObjectField(TEXT("sprite"),Sprite) && Sprite) { Texture = StringField(*Sprite,TEXT("idleTexture")); }
        if (Texture.IsEmpty()) { Texture = StringField(Definition,TEXT("texture")); }
        Public->SetStringField(TEXT("texture"),Texture);
        FString MenuTexture = StringField(Definition,TEXT("menuTexture"));
        if (MenuTexture.IsEmpty()) { MenuTexture = Texture; }
        Public->SetStringField(TEXT("menuTexture"),MenuTexture);
        Public->SetStringField(TEXT("portraitTexture"),StringField(Definition,TEXT("portraitTexture")));
        Public->SetStringField(TEXT("mediumTexture"),StringField(Definition,TEXT("mediumTexture")));
        Public->SetStringField(TEXT("description"),StringField(Definition,TEXT("descriptionPlain")));
        Public->SetNumberField(TEXT("sourcePage"),Number(Definition,TEXT("sourcePage"),1));
        Public->SetNumberField(TEXT("sourceOrder"),Number(Definition,TEXT("sourceOrder")));
        for (const TCHAR* Key : {TEXT("sourceBlocked"),TEXT("sourceFake")})
        {
            bool bValue = false;
            Definition->TryGetBoolField(Key, bValue);
            Public->SetBoolField(Key, bValue);
        }
        for (const TCHAR* Key : {TEXT("hp"),TEXT("damage"),TEXT("speed"),TEXT("range"),TEXT("housing"),TEXT("goo"),TEXT("time"),TEXT("requiredLevel")})
        {
            Public->SetNumberField(Key,Number(Definition,Key));
        }
        Public->SetNumberField(TEXT("time"), FMath::Clamp(Number(Definition,TEXT("time")) * TrainingTimeMultiplier, 1.0, 86400.0));
        const TSharedPtr<FJsonObject>* StatsPointer = nullptr;
        if (Option) { Option->TryGetObjectField(TEXT("currentStats"), StatsPointer); }
        if (StatsPointer && StatsPointer->IsValid())
        {
            // These are the owning host's researched level and effective hatch
            // duration. Do not fall back to level one or multiply the time twice.
            for (const TCHAR* Key : {TEXT("level"),TEXT("hp"),TEXT("damage"),TEXT("speed"),TEXT("range"),TEXT("housing"),TEXT("goo"),TEXT("time"),TEXT("targetGroup")})
                Public->SetNumberField(Key, Number(*StatsPointer, Key));
        }
        PublicMonsters.Add(MakeShared<FJsonValueObject>(Public));
    }
    PublicCatalog->SetArrayField(TEXT("monsters"),PublicMonsters);
    SnapshotObject->SetObjectField(TEXT("catalog"),PublicCatalog);
    for (const TCHAR* YardKey : {TEXT("yard"),TEXT("home")})
    {
        const TSharedPtr<FJsonObject>* Yard = nullptr;
        if (!SnapshotObject->TryGetObjectField(YardKey,Yard) || !Yard) { continue; }
        for (const TSharedPtr<FJsonValue>& BuildingValue : Array(*Yard,TEXT("buildings")))
        {
            const TSharedPtr<FJsonObject> Building = BuildingValue->AsObject();
            if (!Building) { continue; }
            const TSharedPtr<FJsonObject>* Definition = Definitions.Find(static_cast<int32>(Number(Building,TEXT("type"))));
            if (!Definition) { continue; }
            const TArray<TSharedPtr<FJsonValue>> Levels = Array(*Definition,TEXT("levels"));
            const int32 CurrentLevel = FMath::Max(1,static_cast<int32>(Number(Building,TEXT("level"),1)));
            Building->SetStringField(TEXT("name"),StringField(*Definition,TEXT("name")));
            Building->SetStringField(TEXT("description"),StringField(*Definition,TEXT("description")));
            Building->SetNumberField(TEXT("maxLevel"),Levels.Num());
            const TSharedPtr<FJsonObject> Level = Levels.IsValidIndex(CurrentLevel-1) ? Levels[CurrentLevel-1]->AsObject() : nullptr;
            FString Texture = StringField(Level,TEXT("compositeTexture"));
            if (Texture.IsEmpty()) { Texture = StringField(*Definition,TEXT("texture")); }
            Building->SetStringField(TEXT("texture"),Texture);
            if (Levels.IsValidIndex(CurrentLevel))
            {
                const TSharedPtr<FJsonObject> NextLevel = Levels[CurrentLevel]->AsObject();
                Building->SetObjectField(TEXT("upgradeCost"),MakeCost(NextLevel));
                Building->SetNumberField(TEXT("upgradeTime"),FMath::Clamp(Number(NextLevel,TEXT("time")) * ConstructionTimeMultiplier, 1.0, 31536000.0));
            }
        }
    }
    return WriteJson(SnapshotObject.ToSharedRef());
}
