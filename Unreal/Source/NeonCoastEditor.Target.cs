using UnrealBuildTool;
using System.Collections.Generic;
public class NeonCoastEditorTarget : TargetRules
{
    public NeonCoastEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("NeonCoast");
    }
}
