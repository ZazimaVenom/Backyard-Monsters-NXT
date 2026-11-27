#include "BYMPassword.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMPasswordVerifierTest, "BackyardMMO.Accounts.PasswordVerifier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMPasswordVerifierTest::RunTest(const FString& Parameters)
{
    // Published PBKDF2-HMAC-SHA1 vectors: https://www.rfc-editor.org/rfc/rfc6070
    // These use a test-only low-iteration entry point; account APIs reject those
    // work factors and always generate a unique 16-byte salt.
    TArray<uint8> VectorPassword, VectorSalt;
    const uint8 PasswordBytes[] = {'p','a','s','s','w','o','r','d'};
    const uint8 SaltBytes[] = {'s','a','l','t'};
    VectorPassword.Append(PasswordBytes, UE_ARRAY_COUNT(PasswordBytes));
    VectorSalt.Append(SaltBytes, UE_ARRAY_COUNT(SaltBytes));
    const int32 Rounds[] = {1, 2, 4096};
    const TCHAR* Expected[] = {
        TEXT("0c60c80f961f0e71f3a9b524af6012062fe037a6"),
        TEXT("ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"),
        TEXT("4b007901b765489abead49d926f721d065a429c1")
    };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rounds); ++Index)
    {
        TArray<uint8> Derived;
        if (!TestTrue(TEXT("Derive the published verifier vector"), BYMPassword::DeriveForTests(VectorPassword, VectorSalt, Rounds[Index], Derived))) continue;
        FString Hex;
        for (const uint8 Byte : Derived) Hex += FString::Printf(TEXT("%02x"), Byte);
        TestEqual(FString::Printf(TEXT("RFC6070 vector with %d iterations"), Rounds[Index]), Hex, FString(Expected[Index]));
    }

    const FString Password = TEXT("example password for test only");
    FString Salt, Hash, OtherSalt, OtherHash, Error;
    if (!TestTrue(TEXT("Create a salted server password verifier"), BYMPassword::CreatePasswordHash(Password, 210000, Salt, Hash, Error))) return false;
    TestTrue(TEXT("Record satisfies stored verifier constraints"), BYMPassword::ValidatePasswordRecord(Salt, Hash, 210000));
    TestTrue(TEXT("Correct password accepted"), BYMPassword::VerifyPassword(Password, Salt, Hash, 210000));
    TestFalse(TEXT("Wrong password rejected"), BYMPassword::VerifyPassword(TEXT("an incorrect password"), Salt, Hash, 210000));
    TestTrue(TEXT("Second independent password verifier"), BYMPassword::CreatePasswordHash(Password, 210000, OtherSalt, OtherHash, Error));
    TestNotEqual(TEXT("Per-account salts differ"), Salt, OtherSalt);
    TestNotEqual(TEXT("Same password has different stored verifier"), Hash, OtherHash);
    TestFalse(TEXT("Reject excessive work factor from save"), BYMPassword::ValidatePasswordRecord(Salt, Hash, 1000001));
    TestFalse(TEXT("Reject weak work factor from save"), BYMPassword::ValidatePasswordRecord(Salt, Hash, 1));
    TestFalse(TEXT("Reject malformed stored salt"), BYMPassword::VerifyPassword(Password, TEXT("bad"), Hash, 210000));
    TArray<uint8> LegacyDigest; LegacyDigest.SetNumZeroed(32);
    TestFalse(TEXT("Reject unsupported old record format"), BYMPassword::ValidatePasswordRecord(Salt, FBase64::Encode(LegacyDigest), 210000));
    TestFalse(TEXT("Reject too-short account password"), BYMPassword::CreatePasswordHash(TEXT("short"), 210000, OtherSalt, OtherHash, Error));
    TestTrue(TEXT("Failed creation leaves no stored verifier"), OtherHash.IsEmpty() && OtherSalt.IsEmpty());
    FString TemporaryPassword = Password;
    BYMPassword::ClearString(TemporaryPassword);
    TestTrue(TEXT("Password clearing empties the temporary string"), TemporaryPassword.IsEmpty());
    return true;
}
#endif
