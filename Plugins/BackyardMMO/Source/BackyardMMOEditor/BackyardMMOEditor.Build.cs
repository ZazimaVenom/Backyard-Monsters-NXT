using UnrealBuildTool;

public class BackyardMMOEditor : ModuleRules
{
    public BackyardMMOEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "BackyardMMO", "UnrealEd", "AudioEditor", "AssetTools", "AssetRegistry", "Projects",
            "Slate", "SlateCore", "ToolMenus", "LevelEditor", "Json", "JsonUtilities",
            "DeveloperSettings", "DeveloperToolSettings", "EngineSettings", "MessageLog", "RenderCore", "RHI"
        });
    }
}
