#pragma once

#include "CoreMinimal.h"

struct FBYMSetupReport
{
    int32 Imported = 0;
    int32 Unchanged = 0;
    int32 TextureDefaultsVerified = 0;
    int32 TextureDefaultsCorrected = 0;
    int32 CreatedMaps = 0;
    TArray<FString> Errors;
    TArray<FString> Notes;
    bool Succeeded() const { return Errors.IsEmpty(); }
    FString Summary() const;
};

/** Only this class writes generated plugin assets. Existing maps/materials are preserved. */
class FBYMEditorSetup final
{
public:
    static FBYMSetupReport SetupAll();
    static FBYMSetupReport ImportAssets();
    static FBYMSetupReport GenerateMaps();
    static FBYMSetupReport Validate();
    static bool NeedsFirstSetup();
    static bool IsBusy();
    static void Present(const FBYMSetupReport& Report);
private:
    static void Import(FBYMSetupReport& Report);
    static void CreateMaterial(FBYMSetupReport& Report);
    static void CreateMaps(FBYMSetupReport& Report);
    static void ConfigureProject(FBYMSetupReport& Report);
    static void ValidateInternal(FBYMSetupReport& Report);
    static void WriteReport(const FBYMSetupReport& Report);
};
