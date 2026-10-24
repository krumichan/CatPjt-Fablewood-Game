using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>Unreal 에디터 모듈의 빌드 대상 규칙.</summary>
public class FablewoodEditorTarget : TargetRules
{
    /// <summary>생성 시 필요한 기본 설정과 초기 상태를 구성한다.</summary>
    public FablewoodEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Fablewood");
    }
}
