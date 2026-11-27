using UnrealBuildTool;
using System.Collections.Generic;
public class BackyardMonstersNXTTarget : TargetRules
{
    public BackyardMonstersNXTTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("BackyardMonstersNXT");
    }
}
