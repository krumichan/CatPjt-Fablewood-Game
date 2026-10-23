using System.IO;
using UnrealBuildTool;

public class Fablewood : ModuleRules
{
    public Fablewood(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "InputCore", "Json", "Slate", "SlateCore", "ApplicationCore", "HTTP", "JsonUtilities"
        });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

        // Keep the font's required redistribution notice beside the packaged project.
        string FontLicense = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ArtSource", "Fonts", "LICENSE.txt"));
        RuntimeDependencies.Add("$(ProjectDir)/ThirdPartyNotices/NanumGothic-LICENSE.txt", FontLicense, StagedFileType.NonUFS);
    }
}
