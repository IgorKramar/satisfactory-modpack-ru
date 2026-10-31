using UnrealBuildTool;

public class SFPackRURuntime : ModuleRules
{
    public SFPackRURuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bUseUnity = false;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "Projects", "SML" });
    }
}
