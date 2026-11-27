using UnrealBuildTool;
using System.IO;

public class BackyardMMO : ModuleRules
{
    public BackyardMMO(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings", "Json" });
        PrivateDependencyModuleNames.AddRange(new[] { "JsonUtilities", "Projects", "Slate", "SlateCore", "UMG", "ApplicationCore", "NetCore", "Sockets", "Networking" });
        string DataDirectory = Path.Combine(PluginDirectory, "Content", "Data");
        string FontDirectory = Path.Combine(PluginDirectory, "Content", "UI", "Fonts");
        if (Directory.Exists(FontDirectory))
        {
            foreach (string Font in Directory.GetFiles(FontDirectory, "*.ttf"))
                RuntimeDependencies.Add(Font, StagedFileType.UFS);
        }
        if (Directory.Exists(DataDirectory))
        {
            foreach (string File in Directory.GetFiles(DataDirectory, "*.json", SearchOption.AllDirectories))
                RuntimeDependencies.Add(File, StagedFileType.UFS);
            foreach (string Notice in new[] { "SOURCE_LICENSE", "SOURCE_README.md" })
            {
                string NoticePath = Path.Combine(DataDirectory, "Original", Notice);
                if (System.IO.File.Exists(NoticePath)) RuntimeDependencies.Add(NoticePath, StagedFileType.NonUFS);
            }
        }
    }
}
