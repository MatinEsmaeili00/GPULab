// GPULab module startup.
//
// This file does exactly ONE important thing: it tells Unreal's shader compiler
// where our .usf shader files live on disk.
//
// Unreal does not read shaders by real folder path. It reads them by a "virtual"
// path that always starts with a slash, like "/Engine/Private/Something.usf".
// Our plugin gets its own virtual folder, "/Plugin/GPULab", and we point that at
// the real "Shaders" folder next to this plugin's .uplugin file.
//
// After the mapping below exists, a line like:
//     IMPLEMENT_GLOBAL_SHADER(FMyCS, "/Plugin/GPULab/Private/Thing.usf", "MainCS", SF_Compute);
// resolves to:
//     <project>/Plugins/GPULab/Shaders/Private/Thing.usf
//
// IMPORTANT: this has to happen BEFORE any shader compiling starts. That is why
// GPULab.uplugin sets "LoadingPhase": "PostConfigInit" - one of the earliest
// phases the engine has. If you use the default "Default" phase instead, you get
// a crash on startup that says the virtual shader path was not found.

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FGPULabModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Find our own plugin folder on disk, e.g. D:/.../GPULearning/Plugins/GPULab
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GPULab"));
		check(Plugin.IsValid());

		const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));

		// Virtual path  ->  real path
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/GPULab"), ShaderDir);
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FGPULabModule, GPULab)
