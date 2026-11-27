#include "BYMAccountSaveGame.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BYMPassword.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMNativeAccountSaveTest, "BackyardMMO.Persistence.NativeAccountSaveGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMNativeAccountSaveTest::RunTest(const FString& Parameters)
{
    UBYMAccountSaveGame* AccountSave = NewObject<UBYMAccountSaveGame>();
    FBYMAccountSaveRecord Record;
    Record.Id = TEXT("d119e877-512b-4796-8c7d-93c7fc17fa02");
    Record.Username = TEXT("NativeSaveTest");
    FString Error;
    if (!TestTrue(TEXT("Hash fixture password"), BYMPassword::CreatePasswordHash(TEXT("native-save-fixture-password"), Record.Iterations, Record.Salt, Record.PasswordHash, Error))) return false;
    AccountSave->Accounts.Add(Record);

    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Serialize using Unreal's native SaveGame archive"), UGameplayStatics::SaveGameToMemory(AccountSave, Bytes))) return false;
    TestTrue(TEXT("Unreal SaveGame header exists"), Bytes.Num() > 4);
    if (Bytes.Num() >= 4)
    {
        TestTrue(TEXT("Native GVAS file magic, not JSON with a changed extension"),
            Bytes[0] == 'G' && Bytes[1] == 'V' && Bytes[2] == 'A' && Bytes[3] == 'S');
    }
    UBYMAccountSaveGame* Loaded = Cast<UBYMAccountSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Native loader restores the custom account SaveGame class"), Loaded)) return false;
    TestEqual(TEXT("Save schema survives native serialization"), Loaded->SchemaVersion, 1);
    if (!TestEqual(TEXT("Exactly one typed account record is restored"), Loaded->Accounts.Num(), 1)) return false;
    const FBYMAccountSaveRecord& Restored = Loaded->Accounts[0];
    TestEqual(TEXT("Account ID remains stable"), Restored.Id, Record.Id);
    TestEqual(TEXT("Username remains stable"), Restored.Username, Record.Username);
    TestEqual(TEXT("Password work factor remains stable"), Restored.Iterations, Record.Iterations);
    TestTrue(TEXT("Correct password still verifies after native account save/load"),
        BYMPassword::VerifyPassword(TEXT("native-save-fixture-password"), Restored.Salt, Restored.PasswordHash, Restored.Iterations));
    TestFalse(TEXT("Different password cannot authenticate the restored account"),
        BYMPassword::VerifyPassword(TEXT("different-fixture-password"), Restored.Salt, Restored.PasswordHash, Restored.Iterations));
    return true;
}
#endif
