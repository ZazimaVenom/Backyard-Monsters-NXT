#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
class SWidget;

// Native editor layout tests use the production widget builders. The lifetime
// owner keeps their brush storage and captured callbacks alive without a world,
// player controller, server connection, imported asset requirement or account.
namespace BYMUIAutomation
{
    struct FFixture
    {
        TSharedPtr<SWidget> OwnerLifetime;
        TSharedPtr<SWidget> Root;
        TMap<FName, TSharedPtr<SWidget>> Probes;
    };

    BACKYARDMMO_API FFixture BuildLayoutFixture(FName Variant);
}
#endif
