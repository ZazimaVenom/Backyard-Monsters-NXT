#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BYMSetupCommandlet.generated.h"

/** Deterministic asset, material, map and project setup. Run before cooking a clean checkout. */
UCLASS()
class BACKYARDMMOEDITOR_API UBYMSetupCommandlet final : public UCommandlet
{
    GENERATED_BODY()
public:
    UBYMSetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
