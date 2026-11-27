#include "BYMSetupCommandlet.h"
#include "BYMEditorSetup.h"
#include "Misc/Parse.h"

UBYMSetupCommandlet::UBYMSetupCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    ShowErrorCount = true;
}

int32 UBYMSetupCommandlet::Main(const FString& Params)
{
    const FBYMSetupReport Report = FParse::Param(*Params, TEXT("ValidateOnly"))
        ? FBYMEditorSetup::Validate() : FBYMEditorSetup::SetupAll();
    FBYMEditorSetup::Present(Report);
    return Report.Succeeded() ? 0 : 1;
}
