using System.IO;
using UnrealBuildTool;

/// <summary>게임 모듈의 의존성과 패키지에 필요한 배포 파일을 정의하는 빌드 규칙.</summary>
public class Fablewood : ModuleRules
{
    /// <summary>생성 시 필요한 기본 설정과 초기 상태를 구성한다.</summary>
    public Fablewood(ReadOnlyTargetRules Target) : base(Target)
    {
        // JSON 계약과 검증 기록 처리
        PCHUsage = PCHUsageMode.NoPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Json",
                                                            "Slate", "SlateCore", "ApplicationCore", "HTTP",
                                                            "JsonUtilities" });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

        // Keep the font's required redistribution notice beside the packaged project.

        string FontLicense =
            Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ArtSource", "Fonts", "LICENSE.txt"));
        RuntimeDependencies.Add("$(ProjectDir)/ThirdPartyNotices/NanumGothic-LICENSE.txt", FontLicense,
                                StagedFileType.NonUFS);
    }
}
