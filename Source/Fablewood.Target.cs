using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>Windows 게임 실행 파일의 빌드 대상 규칙.</summary>
public class FablewoodTarget : TargetRules
{
    /// <summary>생성 시 필요한 기본 설정과 초기 상태를 구성한다.</summary>
    public FablewoodTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Fablewood");
    }
}
