using UnrealBuildTool;

public class FablewoodServerTarget : TargetRules
{
    public FablewoodServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Fablewood");
    }
}
