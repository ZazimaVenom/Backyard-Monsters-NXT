#pragma once

#include "CoreMinimal.h"

/** Server-side password verifiers using Unreal Core only. No transport or key service. */
namespace BYMPassword
{
    BACKYARDMMO_API bool CreatePasswordHash(const FString& Password, int32 Iterations, FString& OutSalt, FString& OutHash, FString& Error);
    BACKYARDMMO_API bool VerifyPassword(const FString& Password, const FString& Salt, const FString& ExpectedHash, int32 Iterations);
    BACKYARDMMO_API bool ValidatePasswordRecord(const FString& Salt, const FString& Hash, int32 Iterations);
    BACKYARDMMO_API void ClearString(FString& Value);

#if WITH_DEV_AUTOMATION_TESTS
    // Test-only entry point for published PBKDF2 vectors. Account APIs enforce a
    // much higher work-factor minimum and always generate their own salt.
    BACKYARDMMO_API bool DeriveForTests(const TArray<uint8>& Password, const TArray<uint8>& Salt, int32 Iterations, TArray<uint8>& Out);
#endif
}
