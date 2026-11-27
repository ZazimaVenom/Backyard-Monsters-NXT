#include "BYMPassword.h"

#include "Containers/StringConv.h"
#include "Misc/Base64.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"

namespace
{
    constexpr int32 DigestBytes = 20;
    constexpr int32 SaltBytes = 16;
    constexpr int32 MinimumIterations = 100000;
    constexpr int32 MaximumIterations = 1000000;
    constexpr int32 MaximumPasswordBytes = 128;

    void EraseBytes(void* Data, SIZE_T Size)
    {
        // Volatile writes prevent the compiler from dropping this cleanup when
        // the temporary buffer has no later ordinary reads.
        volatile uint8* Bytes = static_cast<volatile uint8*>(Data);
        for (SIZE_T Index = 0; Index < Size; ++Index) Bytes[Index] = 0;
    }

    bool Derive(const uint8* Password, int32 PasswordLength, const uint8* Salt, int32 SaltLength, int32 Iterations, uint8* Out)
    {
        if (!Password || !Salt || !Out || PasswordLength < 1 || PasswordLength > MaximumPasswordBytes
            || SaltLength < 1 || SaltLength > 64 || Iterations < 1 || Iterations > MaximumIterations) return false;

        // PBKDF2-HMAC-SHA1, one 20-byte output block (RFC 8018 section 5.2).
        // Pre-hash long HMAC keys explicitly, so HMACBuffer always receives a
        // key no longer than SHA-1's 64-byte compression block.
        uint8 ShortKey[DigestBytes] = {};
        const uint8* HMACKey = Password;
        int32 HMACKeyLength = PasswordLength;
        if (PasswordLength > 64)
        {
            FSHA1::HashBuffer(Password, static_cast<uint64>(PasswordLength), ShortKey);
            HMACKey = ShortKey;
            HMACKeyLength = DigestBytes;
        }
        uint8 SaltBlock[68] = {};
        FMemory::Memcpy(SaltBlock, Salt, SaltLength);
        SaltBlock[SaltLength + 3] = 1; // INT_32_BE(1)
        uint8 Current[DigestBytes] = {};
        uint8 Next[DigestBytes] = {};
        FSHA1::HMACBuffer(HMACKey, static_cast<uint32>(HMACKeyLength), SaltBlock,
            static_cast<uint64>(SaltLength + 4), Current);
        FMemory::Memcpy(Out, Current, DigestBytes);
        for (int32 Iteration = 1; Iteration < Iterations; ++Iteration)
        {
            FSHA1::HMACBuffer(HMACKey, static_cast<uint32>(HMACKeyLength), Current,
                static_cast<uint64>(DigestBytes), Next);
            for (int32 Byte = 0; Byte < DigestBytes; ++Byte) Out[Byte] ^= Next[Byte];
            FMemory::Memcpy(Current, Next, DigestBytes);
        }
        EraseBytes(ShortKey, sizeof(ShortKey));
        EraseBytes(SaltBlock, sizeof(SaltBlock));
        EraseBytes(Current, sizeof(Current));
        EraseBytes(Next, sizeof(Next));
        return true;
    }

    bool DeriveAccountPassword(const FString& Password, const TArray<uint8>& Salt, int32 Iterations, uint8* Out)
    {
        if (Password.Len() < 10 || Password.Len() > 72 || Salt.Num() != SaltBytes
            || Iterations < MinimumIterations || Iterations > MaximumIterations) return false;
        for (const TCHAR Character : Password) if (Character < 32 || Character == 127) return false;
        FTCHARToUTF8 Encoded(*Password);
        const bool bWithinLimit = Encoded.Length() > 0 && Encoded.Length() <= MaximumPasswordBytes;
        const bool bDerived = bWithinLimit && Derive(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length(),
            Salt.GetData(), Salt.Num(), Iterations, Out);
        if (Encoded.Length() > 0) EraseBytes(const_cast<void*>(static_cast<const void*>(Encoded.Get())), static_cast<SIZE_T>(Encoded.Length()));
        return bDerived;
    }

    bool EqualDigest(const uint8* A, const uint8* B)
    {
        volatile uint8 Difference = 0;
        for (int32 Index = 0; Index < DigestBytes; ++Index)
            Difference = static_cast<uint8>(Difference | static_cast<uint8>(A[Index] ^ B[Index]));
        return Difference == 0;
    }
}

bool BYMPassword::CreatePasswordHash(const FString& Password, int32 Iterations, FString& OutSalt, FString& OutHash, FString& Error)
{
    OutSalt.Reset(); OutHash.Reset(); Error.Reset();
    if (Iterations < MinimumIterations || Iterations > MaximumIterations)
    {
        Error = TEXT("The password storage work factor is outside its supported range.");
        return false;
    }
    const FGuid SaltId = FGuid::NewGuid();
    if (!SaltId.IsValid()) { Error = TEXT("Could not create a unique account salt."); return false; }
    TArray<uint8> Salt;
    Salt.Reserve(SaltBytes);
    // A salt is unique public metadata, not an encryption key. Serialize the
    // GUID's four words explicitly instead of relying on a platform byte order.
    for (const uint32 Word : {SaltId.A, SaltId.B, SaltId.C, SaltId.D})
        for (int32 Shift = 24; Shift >= 0; Shift -= 8) Salt.Add(static_cast<uint8>((Word >> Shift) & 0xffU));
    uint8 Hash[DigestBytes] = {};
    const bool bDerived = DeriveAccountPassword(Password, Salt, Iterations, Hash);
    if (bDerived)
    {
        OutSalt = FBase64::Encode(Salt);
        OutHash = FBase64::Encode(Hash, static_cast<uint32>(DigestBytes));
    }
    EraseBytes(Hash, sizeof(Hash));
    if (!bDerived) Error = TEXT("Use a password of 10–72 characters and no more than 128 UTF-8 bytes.");
    return bDerived;
}

bool BYMPassword::VerifyPassword(const FString& Password, const FString& SaltText, const FString& ExpectedHash, int32 Iterations)
{
    if (!ValidatePasswordRecord(SaltText, ExpectedHash, Iterations)) return false;
    TArray<uint8> Salt, Expected;
    if (!FBase64::Decode(SaltText, Salt) || !FBase64::Decode(ExpectedHash, Expected)) return false;
    uint8 Actual[DigestBytes] = {};
    const bool bDerived = DeriveAccountPassword(Password, Salt, Iterations, Actual);
    const bool bEqual = bDerived && EqualDigest(Actual, Expected.GetData());
    EraseBytes(Actual, sizeof(Actual));
    if (Expected.Num() > 0) EraseBytes(Expected.GetData(), static_cast<SIZE_T>(Expected.Num()));
    return bEqual;
}

bool BYMPassword::ValidatePasswordRecord(const FString& Salt, const FString& Hash, int32 Iterations)
{
    if (Salt.Len() > 64 || Hash.Len() > 64 || Iterations < MinimumIterations || Iterations > MaximumIterations) return false;
    TArray<uint8> SaltData, HashData;
    return FBase64::Decode(Salt, SaltData) && SaltData.Num() == SaltBytes
        && FBase64::Decode(Hash, HashData) && HashData.Num() == DigestBytes;
}

void BYMPassword::ClearString(FString& Value)
{
    if (!Value.IsEmpty()) EraseBytes(Value.GetCharArray().GetData(), static_cast<SIZE_T>(Value.GetCharArray().Num()) * sizeof(TCHAR));
    Value.Empty();
}

#if WITH_DEV_AUTOMATION_TESTS
bool BYMPassword::DeriveForTests(const TArray<uint8>& Password, const TArray<uint8>& Salt, int32 Iterations, TArray<uint8>& Out)
{
    Out.Reset();
    uint8 Derived[DigestBytes] = {};
    const bool bDerived = Derive(Password.GetData(), Password.Num(), Salt.GetData(), Salt.Num(), Iterations, Derived);
    if (bDerived) Out.Append(Derived, DigestBytes);
    EraseBytes(Derived, sizeof(Derived));
    return bDerived;
}
#endif
