using UnrealBuildTool;
public class NeonCoast : ModuleRules
{
    public NeonCoast(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "ChaosVehicles", "PhysicsCore", "UMG", "AIModule", "NavigationSystem"});
    }
}
