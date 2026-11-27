#include "BYMServerSubsystem.h"

#include "BYMPassword.h"
#include "BYMAccountSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "BYMGameMode.h"
#include "BYMSettings.h"
#include "BYMSimulation.h"
#include "Async/Async.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_UNIX
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogBYMServer, Log, All);

struct FBYMPreparedAuthentication
{
    FString ConnectionKey;
    FString Ticket;
    FString CanonicalUsername;
    FString Password;
    FBYMStoredAccount Account;
    FString WorkError;
    bool bCreate = false;
    bool bWorkSucceeded = false;
    ~FBYMPreparedAuthentication() { BYMPassword::ClearString(Password); }
};

namespace
{
    constexpr int32 SaveSchema = 2;
    constexpr int64 MaximumAccountSaveBytes = 64LL * 1024LL * 1024LL;
    constexpr int64 MaximumSaveBytes = 128LL * 1024LL * 1024LL;
    constexpr int32 MaximumAccounts = 100000;

    FString JSON(const TSharedRef<FJsonObject>& Object)
    {
        FString Result;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
        if (!FJsonSerializer::Serialize(Object, Writer)) return FString();
        return Result;
    }

    FString Failure(const FString& Message)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetBoolField(TEXT("ok"), false); Object->SetStringField(TEXT("error"), Message);
        return JSON(Object);
    }

    bool ParseJSON(const FString& Text, TSharedPtr<FJsonObject>& Out)
    {
        return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
    }

    bool ValidUsername(const FString& Username)
    {
        if (Username.Len() < 3 || Username.Len() > 24) return false;
        for (TCHAR Character : Username)
        {
            if (!((Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('A') && Character <= TEXT('Z'))
                || (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_') || Character == TEXT('-'))) return false;
        }
        return true;
    }

    bool ValidPassword(const FString& Password)
    {
        if (Password.Len() < 10 || Password.Len() > 72) return false;
        for (TCHAR Character : Password) if (Character < 32 || Character == 127) return false;
        const FTCHARToUTF8 Encoded(*Password);
        return Encoded.Length() <= 128;
    }

    // Replace in the same directory after fully flushing the candidate. The old destination
    // survives a failed replacement. Windows is the supported packaged target.
    bool AtomicReplace(const FString& Candidate, const FString& Destination)
    {
#if PLATFORM_WINDOWS
        return ::MoveFileExW(*Candidate, *Destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#elif PLATFORM_UNIX
        const FTCHARToUTF8 SourceUTF8(*Candidate), DestinationUTF8(*Destination);
        if (::rename(SourceUTF8.Get(), DestinationUTF8.Get()) != 0) return false;
        const FTCHARToUTF8 DirectoryUTF8(*FPaths::GetPath(Destination));
        const int DirectoryFD = ::open(DirectoryUTF8.Get(), O_RDONLY | O_DIRECTORY);
        if (DirectoryFD >= 0) { ::fsync(DirectoryFD); ::close(DirectoryFD); }
        return true;
#else
        return false;
#endif
    }

    bool DurableWriteBytes(const FString& Destination, const uint8* Data, int64 Size, FString& Error)
    {
        const FString Candidate = Destination + TEXT(".pending");
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*Candidate, false, false));
        if (!Handle)
        {
            Error = TEXT("The host cannot open its save directory for writing."); return false;
        }
        const bool bWritten = Handle->Write(Data, Size) && Handle->Flush(true);
        Handle.Reset();
        if (!bWritten)
        {
            Error = TEXT("The host could not flush the new save. The previous save is unchanged."); return false;
        }
#if PLATFORM_UNIX
        const FTCHARToUTF8 CandidateUTF8(*Candidate);
        ::chmod(CandidateUTF8.Get(), S_IRUSR | S_IWUSR);
#endif
        if (!AtomicReplace(Candidate, Destination))
        {
            Error = TEXT("The host could not commit its new save. The previous save is unchanged."); return false;
        }
        return true;
    }

    bool DurableWrite(const FString& Destination, const FString& Text, FString& Error)
    {
        const FTCHARToUTF8 Bytes(*Text);
        return DurableWriteBytes(Destination, reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Error);
    }

}

UBYMServerSubsystem::UBYMServerSubsystem() = default;
UBYMServerSubsystem::~UBYMServerSubsystem() = default;

void UBYMServerSubsystem::Deinitialize()
{
    StopAuthority();
    Super::Deinitialize();
}

bool UBYMServerSubsystem::IsHostWorld() const
{
    const UWorld* World = GetWorld();
    return World && World->IsGameWorld() && World->GetNetMode() != NM_Client
        && World->GetAuthGameMode<ABYMGameMode>() != nullptr;
}

bool UBYMServerSubsystem::IsAuthorityRunning() const
{
    return bRunning && !bSaveBlocked && IsHostWorld() && Simulation.IsValid();
}

bool UBYMServerSubsystem::AcquireWorldLock(FString& Error)
{
    const FString LockPath = SaveDirectory / TEXT("host.lock");
#if PLATFORM_WINDOWS
    HANDLE Handle = ::CreateFileW(*LockPath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle != INVALID_HANDLE_VALUE) { WorldLockHandle = Handle; return true; }
#elif PLATFORM_UNIX
    const FTCHARToUTF8 UTF8Path(*LockPath);
    WorldLockDescriptor = ::open(UTF8Path.Get(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (WorldLockDescriptor >= 0 && ::flock(WorldLockDescriptor, LOCK_EX | LOCK_NB) == 0) return true;
    if (WorldLockDescriptor >= 0) { ::close(WorldLockDescriptor); WorldLockDescriptor = -1; }
#endif
    Error = TEXT("This world is already hosted by another process, or its save directory is not writable.");
    return false;
}

void UBYMServerSubsystem::ReleaseWorldLock()
{
#if PLATFORM_WINDOWS
    if (WorldLockHandle) { ::CloseHandle(static_cast<HANDLE>(WorldLockHandle)); WorldLockHandle = nullptr; }
#elif PLATFORM_UNIX
    if (WorldLockDescriptor >= 0)
    {
        ::flock(WorldLockDescriptor, LOCK_UN); ::close(WorldLockDescriptor); WorldLockDescriptor = -1;
    }
#endif
}

bool UBYMServerSubsystem::StartAuthority(FString& Error)
{
    Error.Reset();
    if (!IsHostWorld()) { Error = TEXT("Only the hosted OpenWorld game mode can open the server world."); return false; }
    if (bRunning) return IsAuthorityRunning();
    SaveDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BackyardMMO"));
    WorldPath = SaveDirectory / TEXT("world.json");
    AccountPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames/BackyardMonstersNXT_Accounts.sav"));
    if (!IFileManager::Get().DirectoryExists(*SaveDirectory) && !IFileManager::Get().MakeDirectory(*SaveDirectory, true))
    {
        Error = TEXT("The host cannot create its local save directory."); return false;
    }
    if (!AcquireWorldLock(Error)) return false;
    if (!LoadWorld(Error))
    {
        UE_LOG(LogBYMServer, Error, TEXT("Host startup refused: %s"), *Error);
        ResetState(); ReleaseWorldLock(); return false;
    }
    bRunning = true;
    const double Now = FPlatformTime::Seconds();
    LastAutosaveMonotonic = Now; LastCleanupMonotonic = Now; LastSimulationMonotonic = Now;
    if (!bWorldWasLoaded && !SaveInternal(Error))
    {
        bRunning = false; ResetState(); ReleaseWorldLock(); return false;
    }
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBYMServerSubsystem::TickAuthority));
    UE_LOG(LogBYMServer, Display, TEXT("Host world loaded. Accounts: %d."), Accounts.Num());
    return true;
}

bool UBYMServerSubsystem::ValidateLoadedAccounts(const TArray<TSharedPtr<FJsonValue>>& Values, FString& Error)
{
    if (Values.Num() > MaximumAccounts) { Error = TEXT("The saved account count exceeds the supported limit."); return false; }
    for (const TSharedPtr<FJsonValue>& Value : Values)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid())
        { Error = TEXT("The account store contains an invalid record."); return false; }
        FBYMStoredAccount Record;
        double Iterations = 0.0;
        if (!(*Object)->TryGetStringField(TEXT("id"), Record.Id)
            || !(*Object)->TryGetStringField(TEXT("username"), Record.Username)
            || !(*Object)->TryGetStringField(TEXT("salt"), Record.Salt)
            || !(*Object)->TryGetStringField(TEXT("passwordHash"), Record.PasswordHash)
            || !(*Object)->TryGetNumberField(TEXT("iterations"), Iterations)
            || !FMath::IsFinite(Iterations) || Iterations != FMath::FloorToDouble(Iterations)
            || Iterations < 100000 || Iterations > 1000000)
        { Error = TEXT("The account store contains incomplete password metadata."); return false; }
        Record.Iterations = static_cast<int32>(Iterations);
        if (!AddLoadedAccount(Record, Error)) return false;
    }
    return true;
}

bool UBYMServerSubsystem::AddLoadedAccount(const FBYMStoredAccount& Record, FString& Error)
{
    FGuid ParsedId;
    const FString Canonical = Record.Username.ToLower();
    if (!FGuid::Parse(Record.Id, ParsedId) || !ParsedId.IsValid() || !ValidUsername(Record.Username)
        || Accounts.Contains(Record.Id) || UsernameToId.Contains(Canonical)
        || !BYMPassword::ValidatePasswordRecord(Record.Salt, Record.PasswordHash, Record.Iterations))
    { Error = TEXT("The account store contains invalid or duplicate account identities."); return false; }
    Accounts.Add(Record.Id, Record); UsernameToId.Add(Canonical, Record.Id);
    return true;
}

bool UBYMServerSubsystem::LoadAccounts(FString& Error)
{
    const int64 Size = IFileManager::Get().FileSize(*AccountPath);
    if (Size <= 0 || Size > MaximumAccountSaveBytes || !FFileHelper::LoadFileToArray(LastCommittedAccounts, *AccountPath))
    { Error = TEXT("The native account .sav file is missing, empty, unreadable or too large. It was not reset."); return false; }
    UBYMAccountSaveGame* Saved = Cast<UBYMAccountSaveGame>(UGameplayStatics::LoadGameFromMemory(LastCommittedAccounts));
    if (!Saved || Saved->SchemaVersion != 1 || Saved->Accounts.Num() > MaximumAccounts)
    { Error = TEXT("The native account .sav is corrupt or unsupported. Restore its matching backup; it was not reset."); return false; }
    Accounts.Reset(); UsernameToId.Reset();
    for (const FBYMAccountSaveRecord& Value : Saved->Accounts)
    {
        FBYMStoredAccount Record;
        Record.Id = Value.Id; Record.Username = Value.Username; Record.Salt = Value.Salt;
        Record.PasswordHash = Value.PasswordHash; Record.Iterations = Value.Iterations;
        if (!AddLoadedAccount(Record, Error)) return false;
    }
    return true;
}

bool UBYMServerSubsystem::SaveAccounts(FString& Error)
{
    if (bSaveBlocked || (!WorldLockHandle && WorldLockDescriptor < 0) || AccountPath.IsEmpty())
    { Error = TEXT("Only the locked hosted world can save accounts."); return false; }
    const FString Directory = FPaths::GetPath(AccountPath);
    if (!IFileManager::Get().DirectoryExists(*Directory) && !IFileManager::Get().MakeDirectory(*Directory, true))
    { Error = TEXT("The host cannot create its native SaveGames directory."); return false; }
    UBYMAccountSaveGame* Saved = NewObject<UBYMAccountSaveGame>();
    TArray<FString> Ids; Accounts.GetKeys(Ids); Ids.Sort();
    for (const FString& Id : Ids)
    {
        const FBYMStoredAccount& Record = Accounts.FindChecked(Id);
        FBYMAccountSaveRecord Value;
        Value.Id = Record.Id; Value.Username = Record.Username; Value.Salt = Record.Salt;
        Value.PasswordHash = Record.PasswordHash; Value.Iterations = Record.Iterations;
        Saved->Accounts.Add(MoveTemp(Value));
    }
    TArray<uint8> Bytes;
    if (!UGameplayStatics::SaveGameToMemory(Saved, Bytes) || Bytes.IsEmpty() || Bytes.Num() > MaximumAccountSaveBytes)
    { Error = TEXT("Unreal could not serialize the native account save."); return false; }
    UBYMAccountSaveGame* Verified = Cast<UBYMAccountSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!Verified || Verified->SchemaVersion != 1 || Verified->Accounts.Num() != Accounts.Num())
    { Error = TEXT("Unreal could not verify the serialized account save; the previous save is unchanged."); return false; }
    const FString Backup = FPaths::GetPath(AccountPath) / TEXT("BackyardMonstersNXT_Accounts_Previous.sav");
    if (!LastCommittedAccounts.IsEmpty() && !DurableWriteBytes(Backup, LastCommittedAccounts.GetData(), LastCommittedAccounts.Num(), Error)) return false;
    if (!DurableWriteBytes(AccountPath, Bytes.GetData(), Bytes.Num(), Error)) return false;
    LastCommittedAccounts = MoveTemp(Bytes);
    return true;
}

bool UBYMServerSubsystem::LoadWorld(FString& Error)
{
    Accounts.Reset(); UsernameToId.Reset(); LastCommittedWorld.Reset(); LastCommittedAccounts.Reset();
    PendingRegistrationYards.Reset(); bSaveBlocked = false; bWorldWasLoaded = false;
    const int64 FileSize = IFileManager::Get().FileSize(*WorldPath);
    TSharedPtr<FJsonObject> Root, SavedSimulation;
    bool bLegacyAccounts = false;
    if (FileSize < 0)
    {
        if (IFileManager::Get().FileExists(*(WorldPath + TEXT(".pending")))
            || IFileManager::Get().FileExists(*(SaveDirectory / TEXT("world.previous.json"))))
        {
            bSaveBlocked = true;
            Error = TEXT("world.json is missing but recovery files exist. Nothing was reset. Restore a verified world.previous.json or pending save as world.json."); return false;
        }
    }
    else
    {
        double Schema = 0;
        const TSharedPtr<FJsonObject>* SimulationObject = nullptr;
        if (FileSize == 0 || FileSize > MaximumSaveBytes || !FFileHelper::LoadFileToString(LastCommittedWorld, *WorldPath)
            || !ParseJSON(LastCommittedWorld, Root) || !Root->TryGetNumberField(TEXT("schema"), Schema) || (Schema != 1 && Schema != SaveSchema)
            || !Root->TryGetObjectField(TEXT("simulation"), SimulationObject) || !SimulationObject || !SimulationObject->IsValid())
        {
            bSaveBlocked = true;
            Error = TEXT("world.json is corrupt, unsupported, or too large. It was not overwritten. Restore a verified world.previous.json backup."); return false;
        }
        bLegacyAccounts = Schema == 1;
        SavedSimulation = *SimulationObject;
        Root->TryGetNumberField(TEXT("savedAt"), LastSaveUnix);
        if (!bLegacyAccounts)
        {
            const TArray<TSharedPtr<FJsonValue>>* Pending = nullptr;
            if (!Root->TryGetArrayField(TEXT("pendingRegistrations"), Pending) || !Pending || Pending->Num() > 16)
            { Error = TEXT("The world registration journal is invalid. The save was preserved."); return false; }
            for (const TSharedPtr<FJsonValue>& Value : *Pending)
            {
                FString Id; FGuid Guid;
                if (!Value.IsValid() || !Value->TryGetString(Id) || !FGuid::Parse(Id, Guid) || !Guid.IsValid() || PendingRegistrationYards.Contains(Id))
                { Error = TEXT("The world registration journal contains an invalid identity."); return false; }
                PendingRegistrationYards.Add(Id);
            }
        }
        bWorldWasLoaded = true;
    }
    const bool bNativeExists = IFileManager::Get().FileExists(*AccountPath);
    if (bNativeExists)
    {
        if (!LoadAccounts(Error)) { bSaveBlocked = true; return false; }
        if (bLegacyAccounts)
        {
            // A migration interrupted after writing the native store must agree with
            // the complete legacy store before its credentials can be removed.
            const TMap<FString, FBYMStoredAccount> NativeAccounts = Accounts;
            Accounts.Reset(); UsernameToId.Reset();
            const TArray<TSharedPtr<FJsonValue>>* Legacy = nullptr;
            if (!Root->TryGetArrayField(TEXT("accounts"), Legacy) || !Legacy || !ValidateLoadedAccounts(*Legacy, Error))
            { bSaveBlocked = true; if (Error.IsEmpty()) Error = TEXT("Legacy account records are invalid; neither account store was changed."); return false; }
            bool bAccountsMatch = Accounts.Num() == NativeAccounts.Num();
            for (const TPair<FString, FBYMStoredAccount>& Pair : Accounts)
            {
                const FBYMStoredAccount* Native = NativeAccounts.Find(Pair.Key);
                const FBYMStoredAccount& LegacyRecord = Pair.Value;
                if (!Native || Native->Username != LegacyRecord.Username || Native->Salt != LegacyRecord.Salt
                    || Native->PasswordHash != LegacyRecord.PasswordHash || Native->Iterations != LegacyRecord.Iterations)
                { bAccountsMatch = false; break; }
            }
            if (!bAccountsMatch)
            { bSaveBlocked = true; Error = TEXT("Legacy and native account records disagree. Restore matching backups; neither store was overwritten."); return false; }
        }
    }
    else
    {
        const FString Backup = FPaths::GetPath(AccountPath) / TEXT("BackyardMonstersNXT_Accounts_Previous.sav");
        if (IFileManager::Get().FileExists(*Backup) || (!bLegacyAccounts && bWorldWasLoaded))
        { bSaveBlocked = true; Error = TEXT("The native account .sav is missing for this existing world. Restore the matching account backup; accounts were not reset."); return false; }
        if (bLegacyAccounts)
        {
            const TArray<TSharedPtr<FJsonValue>>* Legacy = nullptr;
            if (!Root->TryGetArrayField(TEXT("accounts"), Legacy) || !Legacy || !ValidateLoadedAccounts(*Legacy, Error))
            { bSaveBlocked = true; if (Error.IsEmpty()) Error = TEXT("Legacy account records are invalid; migration was not attempted."); return false; }
        }
    }
    Simulation = MakeUnique<FBYMSimulation>();
    if (!Simulation->Initialize(SavedSimulation, Error)) { bSaveBlocked = true; return false; }
    // Only an explicitly journaled, uncommitted registration can be removed on recovery.
    // Existing accounts, ordinary saved yards and every bot are preserved unchanged.
    for (const FString& Id : PendingRegistrationYards)
    {
        if (!Accounts.Contains(Id))
        {
            Simulation->RemovePlayer(Id);
            if (Simulation->HasYard(Id))
            { bSaveBlocked = true; Error = TEXT("An incomplete registration yard cannot be recovered safely. The world was preserved."); return false; }
        }
    }
    PendingRegistrationYards.Reset();
    for (const TPair<FString, FBYMStoredAccount>& Pair : Accounts)
    {
        if (!Simulation->HasYard(Pair.Key))
        { bSaveBlocked = true; Error = TEXT("An account has no matching saved yard. Restore a matching account/world backup; neither store was reset."); return false; }
    }
    if (!bNativeExists && !SaveAccounts(Error)) { bSaveBlocked = true; return false; }
    // A validated legacy world is rewritten on startup without its former account array.
    // Keep the original world.previous.json recovery copy; gameplay serialization is unchanged.
    if (bLegacyAccounts) bWorldWasLoaded = false;
    return true;
}

bool UBYMServerSubsystem::SaveInternal(FString& Error)
{
    Error.Reset();
    if (!bRunning || bSaveBlocked || !Simulation.IsValid() || (!WorldLockHandle && WorldLockDescriptor < 0))
    { Error = TEXT("The server world is not open for saving."); return false; }
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema"), SaveSchema);
    const double SavedAt = static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp());
    Root->SetNumberField(TEXT("savedAt"), SavedAt);
    TArray<TSharedPtr<FJsonValue>> Pending;
    for (const FString& Id : PendingRegistrationYards) Pending.Add(MakeShared<FJsonValueString>(Id));
    Root->SetArrayField(TEXT("pendingRegistrations"), Pending);
    const TSharedPtr<FJsonObject> State = Simulation->Serialize();
    if (!State.IsValid()) { Error = TEXT("The server simulation could not produce a save."); LastSaveError = Error; return false; }
    Root->SetObjectField(TEXT("simulation"), State);
    const FString Candidate = JSON(Root);
    TSharedPtr<FJsonObject> VerifiedCandidate;
    if (Candidate.IsEmpty() || !ParseJSON(Candidate, VerifiedCandidate))
    { Error = TEXT("The host refused an invalid serialized save; the committed save is unchanged."); LastSaveError = Error; return false; }
    VerifiedCandidate.Reset();
    const FTCHARToUTF8 CandidateUTF8(*Candidate);
    if (CandidateUTF8.Length() > MaximumSaveBytes)
    { Error = TEXT("The server save exceeds its size limit; current state remains in memory."); LastSaveError = Error; return false; }
    if (!LastCommittedWorld.IsEmpty()
        && !DurableWrite(SaveDirectory / TEXT("world.previous.json"), LastCommittedWorld, Error))
    { LastSaveError = Error; return false; }
    if (!DurableWrite(WorldPath, Candidate, Error)) { LastSaveError = Error; return false; }
    LastCommittedWorld = Candidate; LastSaveUnix = SavedAt; LastSaveError.Reset();
    UE_LOG(LogBYMServer, Display, TEXT("Server world saved: %d accounts; %d UTF-8 bytes."), Accounts.Num(), CandidateUTF8.Length());
    return true;
}

bool UBYMServerSubsystem::SaveNow(FString& Error)
{
    if (!IsAuthorityRunning()) { Error = TEXT("Only the hosted server can save this world."); return false; }
    const bool bSaved = SaveInternal(Error);
    if (bSaved) LastAutosaveMonotonic = FPlatformTime::Seconds();
    return bSaved;
}

void UBYMServerSubsystem::StopAuthority()
{
    if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
    // A previously opened authority retains its lock through shutdown. This internal flush is
    // permitted during world teardown, when GetWorld() may no longer expose its game mode.
    if (bRunning && GetDefault<UBYMSettings>()->bSaveOnShutdown)
    {
        FString Error;
        if (!SaveInternal(Error)) UE_LOG(LogBYMServer, Error, TEXT("Shutdown save failed; previous committed save remains: %s"), *Error);
    }
    bRunning = false; ResetState(); ReleaseWorldLock();
}

void UBYMServerSubsystem::ResetState()
{
    bRunning = false; Simulation.Reset();
    Accounts.Reset(); UsernameToId.Reset(); ConnectionToAccount.Reset(); AccountToConnection.Reset();
    PendingAuthenticationTickets.Reset(); AuthenticationAttempts.Reset(); UsernameAttempts.Reset(); GlobalAttempts.Reset();
    BYMPassword::ClearString(LastCommittedWorld);
    LastCommittedAccounts.Reset(); PendingRegistrationYards.Reset();
    LastSaveError.Reset(); bWorldWasLoaded = false; bSaveBlocked = false;
}

bool UBYMServerSubsystem::TickAuthority(float DeltaSeconds)
{
    if (!bRunning) return false;
    if (!IsHostWorld()) return true;
    const double Now = FPlatformTime::Seconds();
    const UBYMSettings* Settings = GetDefault<UBYMSettings>();
    const double PeacefulStep = FMath::Clamp(static_cast<double>(Settings->SimulationStepSeconds), 0.1, 10.0);
    const double Step = Simulation->HasActiveBattles()
        ? FMath::Min(PeacefulStep, FMath::Clamp(static_cast<double>(Settings->CombatSimulationStepSeconds), 0.05, 0.1))
        : PeacefulStep;
    if (Now - LastSimulationMonotonic >= Step)
    {
        const float Elapsed = static_cast<float>(FMath::Clamp(Now - LastSimulationMonotonic, 0.0, 10.0));
        LastSimulationMonotonic = Now;
        // Keep subsecond attack/event times: integer Unix seconds made movement and
        // animation bursts persist even when the client requested faster snapshots.
        const FDateTime UtcNow = FDateTime::UtcNow();
        Simulation->Tick(static_cast<double>(UtcNow.ToUnixTimestamp()) + UtcNow.GetMillisecond() / 1000.0, Elapsed);
    }
    const double Interval = FMath::Max(10.0, static_cast<double>(Settings->AutosaveSeconds));
    if (Now - LastAutosaveMonotonic >= Interval)
    {
        FString Error;
        if (SaveInternal(Error)) LastAutosaveMonotonic = Now;
        else if (Now - LastFailedSaveLogMonotonic >= 60.0)
        {
            UE_LOG(LogBYMServer, Error, TEXT("Autosave failed; current progress remains in host memory: %s"), *Error);
            LastFailedSaveLogMonotonic = Now;
        }
        // Retry at most once a minute on an unwritable disk; do not hammer it each frame.
        if (!Error.IsEmpty()) LastAutosaveMonotonic = Now - Interval + FMath::Min(60.0, Interval);
    }
    if (Now - LastCleanupMonotonic >= 60.0) { CleanupAuthState(); LastCleanupMonotonic = Now; }
    return true;
}

bool UBYMServerSubsystem::ConsumeAttempt(TMap<FString, FBYMAuthAttemptWindow>& Windows, const FString& Key, int32 Limit, double Period)
{
    const double Now = FPlatformTime::Seconds();
    if (!Windows.Contains(Key) && Windows.Num() >= 4096) return false;
    FBYMAuthAttemptWindow& Window = Windows.FindOrAdd(Key);
    if (Window.StartedAt <= 0.0 || Now - Window.StartedAt >= Period) { Window.StartedAt = Now; Window.Count = 0; }
    return ++Window.Count <= Limit;
}

void UBYMServerSubsystem::CleanupAuthState()
{
    const double Now = FPlatformTime::Seconds();
    auto Cleanup = [Now](TMap<FString, FBYMAuthAttemptWindow>& Map)
    {
        for (auto It = Map.CreateIterator(); It; ++It) if (Now - It.Value().StartedAt > 600.0) It.RemoveCurrent();
    };
    Cleanup(AuthenticationAttempts); Cleanup(UsernameAttempts); Cleanup(GlobalAttempts);
}

TSharedPtr<FBYMPreparedAuthentication, ESPMode::ThreadSafe> UBYMServerSubsystem::PrepareAuthentication(
    const FString& ConnectionKey, const FString& Username, const FString& Password, bool bCreate, FString& Error)
{
    Error.Reset();
    if (!IsAuthorityRunning()) { Error = TEXT("The server world is unavailable."); return nullptr; }
    if (ConnectionKey.IsEmpty() || ConnectionKey.Len() > 128)
    { Error = TEXT("Invalid account request."); return nullptr; }
    if (ConnectionToAccount.Contains(ConnectionKey)) { Error = TEXT("This connection is already logged in."); return nullptr; }
    if (PendingAuthenticationTickets.Contains(ConnectionKey)) { Error = TEXT("A sign-in request is already in progress."); return nullptr; }
    if (PendingAuthenticationTickets.Num() >= 16) { Error = TEXT("The host is processing other sign-ins. Please retry shortly."); return nullptr; }
    if (!ConsumeAttempt(AuthenticationAttempts, ConnectionKey, 5, 60.0)
        || !ConsumeAttempt(GlobalAttempts, TEXT("global"), 120, 60.0))
    { Error = TEXT("Too many authentication attempts. Wait a minute."); return nullptr; }
    if (!ValidUsername(Username) || !ValidPassword(Password))
    {
        Error = TEXT("Use a 3–24 character username (letters, digits, _ or -) and a 10–72 character password (up to 128 UTF-8 bytes).");
        return nullptr;
    }
    const TSharedRef<FBYMPreparedAuthentication, ESPMode::ThreadSafe> Request = MakeShared<FBYMPreparedAuthentication, ESPMode::ThreadSafe>();
    Request->Password = Password;
    Request->bCreate = bCreate;
    Request->CanonicalUsername = Username.ToLower();
    if (!ConsumeAttempt(UsernameAttempts, Request->CanonicalUsername, 10, 300.0))
    { Error = TEXT("Too many attempts for this account. Wait five minutes."); return nullptr; }
    const FString* ExistingId = UsernameToId.Find(Request->CanonicalUsername);
    if (Request->bCreate)
    {
        if (ExistingId || Accounts.Num() >= MaximumAccounts) { Error = TEXT("That username is unavailable."); return nullptr; }
        Request->Account.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
        Request->Account.Username = Username;
        Request->Account.Iterations = FMath::Clamp(GetDefault<UBYMSettings>()->PBKDF2Iterations, 210000, 1000000);
    }
    else
    {
        const FBYMStoredAccount* Existing = ExistingId ? Accounts.Find(*ExistingId) : nullptr;
        if (Existing) Request->Account = *Existing;
        else
        {
            // Unknown users perform the same slow KDF before receiving the generic login error.
            Request->Account.Salt = TEXT("AAAAAAAAAAAAAAAAAAAAAA==");
            Request->Account.PasswordHash = TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAA=");
            Request->Account.Iterations = 210000;
        }
    }
    Request->ConnectionKey = ConnectionKey;
    Request->Ticket = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    PendingAuthenticationTickets.Add(ConnectionKey, Request->Ticket);
    return Request;
}

namespace
{
    void PerformPasswordWork(const TSharedRef<FBYMPreparedAuthentication, ESPMode::ThreadSafe>& Request)
    {
        if (Request->bCreate)
        {
            Request->bWorkSucceeded = BYMPassword::CreatePasswordHash(Request->Password, Request->Account.Iterations,
                Request->Account.Salt, Request->Account.PasswordHash, Request->WorkError);
        }
        else
        {
            Request->bWorkSucceeded = BYMPassword::VerifyPassword(Request->Password, Request->Account.Salt,
                Request->Account.PasswordHash, Request->Account.Iterations) && !Request->Account.Id.IsEmpty();
            if (!Request->bWorkSucceeded) Request->WorkError = TEXT("Incorrect username or password.");
        }
        BYMPassword::ClearString(Request->Password);
    }
}

bool UBYMServerSubsystem::FinishAuthentication(const TSharedRef<FBYMPreparedAuthentication, ESPMode::ThreadSafe>& Request,
    FString& OutAccountId, FString& Error)
{
    OutAccountId.Reset(); Error.Reset();
    const FString* CurrentTicket = PendingAuthenticationTickets.Find(Request->ConnectionKey);
    if (!IsAuthorityRunning() || !CurrentTicket || *CurrentTicket != Request->Ticket)
    { Error = TEXT("The connection closed before sign-in completed."); return false; }
    PendingAuthenticationTickets.Remove(Request->ConnectionKey);
    if (!Request->bWorkSucceeded) { Error = Request->WorkError; return false; }
    if (ConnectionToAccount.Contains(Request->ConnectionKey)) { Error = TEXT("This connection is already signed in."); return false; }
    if (Request->bCreate)
    {
        // Another connection might have created the same username while this KDF was in flight.
        if (UsernameToId.Contains(Request->CanonicalUsername) || Accounts.Num() >= MaximumAccounts)
        { Error = TEXT("That username is unavailable."); return false; }
        if (!Simulation->AddPlayer(Request->Account.Id, Request->Account.Username, Error)) return false;
        Accounts.Add(Request->Account.Id, Request->Account);
        UsernameToId.Add(Request->CanonicalUsername, Request->Account.Id);
        // Commit the world first with a recovery journal, then the native account .sav.
        // A failed account commit leaves only a tagged initial yard that startup can remove safely.
        PendingRegistrationYards.Add(Request->Account.Id);
        const bool bWorldSaved = SaveInternal(Error);
        const bool bAccountSaved = bWorldSaved && SaveAccounts(Error);
        PendingRegistrationYards.Remove(Request->Account.Id);
        if (!bAccountSaved)
        {
            Simulation->RemovePlayer(Request->Account.Id);
            Accounts.Remove(Request->Account.Id); UsernameToId.Remove(Request->CanonicalUsername);
            Error = TEXT("Account creation was rolled back because the host could not commit its world and native account .sav. Ask the host to check available disk space.");
            return false;
        }
    }
    else
    {
        const FBYMStoredAccount* Account = Accounts.Find(Request->Account.Id);
        if (!Account || Account->PasswordHash != Request->Account.PasswordHash)
        { Error = TEXT("The account changed while signing in. Please retry."); return false; }
        if (AccountToConnection.Contains(Account->Id)) { Error = TEXT("This account is already online on this host."); return false; }
    }
    OutAccountId = Request->Account.Id;
    ConnectionToAccount.Add(Request->ConnectionKey, OutAccountId); AccountToConnection.Add(OutAccountId, Request->ConnectionKey);
    Simulation->SetPlayerOnline(OutAccountId, true);
    return true;
}

void UBYMServerSubsystem::AuthenticateAsync(const FString& ConnectionKey, const FString& Username, const FString& Password, bool bCreate,
    TFunction<void(bool, const FString&, const FString&)> Completion)
{
    FString Error;
    const TSharedPtr<FBYMPreparedAuthentication, ESPMode::ThreadSafe> Request = PrepareAuthentication(ConnectionKey, Username, Password, bCreate, Error);
    if (!Request.IsValid()) { Completion(false, FString(), Error); return; }
    const TWeakObjectPtr<UBYMServerSubsystem> WeakThis(this);
    Async(EAsyncExecution::ThreadPool, [WeakThis, Request, Completion = MoveTemp(Completion)]() mutable
    {
        // Worker code sees only the immutable account copy and its private password buffer.
        // It never reads UObjects, connection maps, simulation data, or the filesystem.
        PerformPasswordWork(Request.ToSharedRef());
        AsyncTask(ENamedThreads::GameThread, [WeakThis, Request, Completion = MoveTemp(Completion)]() mutable
        {
            FString AccountId, ResultError;
            bool bSuccess = false;
            if (UBYMServerSubsystem* Server = WeakThis.Get())
                bSuccess = Server->FinishAuthentication(Request.ToSharedRef(), AccountId, ResultError);
            else ResultError = TEXT("The host shut down before sign-in completed.");
            Completion(bSuccess, AccountId, ResultError);
        });
    });
}

void UBYMServerSubsystem::Disconnect(const FString& ConnectionKey, const FString& AccountId)
{
    if (!IsAuthorityRunning()) return;
    PendingAuthenticationTickets.Remove(ConnectionKey);
    const FString* BoundId = ConnectionToAccount.Find(ConnectionKey);
    if (BoundId && *BoundId == AccountId)
    {
        Simulation->SetPlayerOnline(*BoundId, false);
        AccountToConnection.Remove(*BoundId); ConnectionToAccount.Remove(ConnectionKey);
    }
    // Deliberately retain the complete yard in Simulation. Logout is not an autosave trigger.
}

bool UBYMServerSubsystem::IsAccountOnline(const FString& AccountId) const
{
    return IsAuthorityRunning() && Accounts.Contains(AccountId) && AccountToConnection.Contains(AccountId);
}

FString UBYMServerSubsystem::Execute(const FString& AccountId, const FString& Operation, const FString& Payload)
{
    if (!IsAccountOnline(AccountId)) return Failure(TEXT("Log in before issuing game commands."));
    const int32 MaxBytes = FMath::Clamp(GetDefault<UBYMSettings>()->MaxCommandBytes, 1024, 65536);
    if (Operation.Len() > 32 || Payload.Len() > MaxBytes)
        return Failure(TEXT("The game command exceeds the server limit."));
    const FTCHARToUTF8 PayloadUTF8(*Payload);
    if (PayloadUTF8.Length() > MaxBytes) return Failure(TEXT("The game command exceeds the server byte limit."));
    return Simulation->Execute(AccountId, Operation, Payload);
}

FString UBYMServerSubsystem::GetSnapshot(const FString& AccountId, const FString& ViewedYardId)
{
    if (!IsAccountOnline(AccountId)) return Failure(TEXT("Log in before viewing a yard."));
    if (ViewedYardId.Len() > 64) return Failure(TEXT("Invalid yard identifier."));
    const FString Snapshot = Simulation->Snapshot(AccountId, ViewedYardId);
    TSharedPtr<FJsonObject> Object;
    if (!ParseJSON(Snapshot, Object)) return Failure(TEXT("The server could not prepare the yard view."));
    const TSharedRef<FJsonObject> Server = MakeShared<FJsonObject>();
    Server->SetNumberField(TEXT("now"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()));
    Server->SetNumberField(TEXT("lastSave"), LastSaveUnix);
    const double UntilSave = FMath::Max(0.0, FMath::Max(10.0, static_cast<double>(GetDefault<UBYMSettings>()->AutosaveSeconds))
        - (FPlatformTime::Seconds() - LastAutosaveMonotonic));
    Server->SetNumberField(TEXT("nextSaveIn"), UntilSave);
    Server->SetStringField(TEXT("saveError"), LastSaveError);
    Server->SetNumberField(TEXT("online"), AccountToConnection.Num());
    Server->SetNumberField(TEXT("capacity"), FMath::Clamp(GetDefault<UBYMSettings>()->MaxPlayers, 1, 10000));
    Object->SetObjectField(TEXT("server"), Server);
    return JSON(Object.ToSharedRef());
}

FString UBYMServerSubsystem::GetLadder(const FString& AccountId, int32 Page)
{
    if (!IsAccountOnline(AccountId)) return Failure(TEXT("Log in before viewing the ladder."));
    return Simulation->Ladder(FMath::Clamp(Page, 0, 100000));
}
