using UnrealBuildTool;

/// <summary>클라이언트 전용 빌드 대상 규칙.</summary>
public class FablewoodClientTarget : TargetRules
{
    /// <summary>생성 시 필요한 기본 설정과 초기 상태를 구성한다.</summary>
    public FablewoodClientTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Client;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Fablewood");
    }
}
