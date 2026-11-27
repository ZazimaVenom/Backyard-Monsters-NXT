#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BYMHUD.generated.h"

class SBYMRoot;

UCLASS()
class BACKYARDMMO_API ABYMHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    TSharedPtr<SBYMRoot> RootWidget;
};
