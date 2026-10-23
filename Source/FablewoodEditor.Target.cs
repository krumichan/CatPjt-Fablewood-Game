using UnrealBuildTool;
using System.Collections.Generic;
public class FablewoodEditorTarget : TargetRules
{
    public FablewoodEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Fablewood");
    }
}
