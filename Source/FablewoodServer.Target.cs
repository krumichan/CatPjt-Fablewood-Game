using UnrealBuildTool;

/// <summary>소스 엔진에서 사용할 전용 서버 빌드 대상 규칙.</summary>
public class FablewoodServerTarget : TargetRules
{
    /// <summary>생성 시 필요한 기본 설정과 초기 상태를 구성한다.</summary>
    public FablewoodServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Fablewood");
    }
}
