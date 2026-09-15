// GPULab - a teaching plugin for low-level GPU programming in Unreal.

using UnrealBuildTool;

public class GPULab : ModuleRules
{
	public GPULab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// "Projects" gives us IPluginManager, which we use to find this plugin's
			// folder on disk so we can tell the shader compiler where the .usf files are.
			"Projects",

			// The three rendering modules. This is the important part:
			//   RHI        - the hardware abstraction (buffers, textures, command lists)
			//   RenderCore - FGlobalShader, shader parameters, and RDG live here
			//   Renderer   - the actual renderer; we need it for the global shader map
			"RHI",
			"RenderCore",
			"Renderer",
		});
	}
}
