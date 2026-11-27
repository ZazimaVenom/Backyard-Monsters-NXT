using UnrealBuildTool;
public class BackyardMonstersNXT : ModuleRules
{
    public BackyardMonstersNXT(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "BackyardMMO" });
    }
}
