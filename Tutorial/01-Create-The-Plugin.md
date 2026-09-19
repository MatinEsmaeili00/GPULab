# 01 - Create The Plugin

**Goal:** an empty plugin that compiles, loads, and knows where your shaders will
live. No shaders yet — just the scaffolding.

**Time:** about 15 minutes, most of it waiting for the build.

Boring chapter, important chapter. Four small files, and if any one of them is
wrong nothing later works.

---

## Step 1 — Make the folders

In your project folder (the one with the `.uproject` file), create this tree:

```
YourProject/
  YourProject.uproject
  Plugins/                      <- create if it doesn't exist
    MyShaders/                  <- create
      Shaders/                  <- create
        Private/                <- create
      Source/                   <- create
        MyShaders/              <- create
          Private/              <- create
          Public/               <- create
```

From a terminal in your project folder, that is one command:

**PowerShell**
```powershell
mkdir Plugins\MyShaders\Shaders\Private, Plugins\MyShaders\Source\MyShaders\Private, Plugins\MyShaders\Source\MyShaders\Public -Force
```

**Git Bash**
```bash
mkdir -p Plugins/MyShaders/Shaders/Private Plugins/MyShaders/Source/MyShaders/{Private,Public}
```

Note the folder name appears **twice**: `Source/MyShaders/`. That is not a typo.
`Source/` holds modules, and this plugin's one module is also called `MyShaders`.
A plugin can have several modules, which is why the extra level exists.

---

## Step 2 — The plugin manifest

Create **`Plugins/MyShaders/MyShaders.uplugin`**:

```json
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "1.0",
	"FriendlyName": "MyShaders",
	"Description": "My own GPU shader lessons, built from the GPULab tutorial.",
	"Category": "Rendering",
	"CanContainContent": false,
	"Modules": [
		{
			"Name": "MyShaders",
			"Type": "Runtime",
			"LoadingPhase": "PostConfigInit"
		}
	]
}
```

This tells Unreal the plugin exists and what is inside it.

> ### ⚠️ `"LoadingPhase": "PostConfigInit"` is not optional
>
> This is the single most important line in the file, and the one people copy
> wrong.
>
> Your module's job is to tell the shader compiler where your `.usf` files are.
> That has to happen **before any shader compiling starts**. `PostConfigInit` is
> one of the earliest phases the engine has.
>
> Use the default (`"Default"`) instead and the engine starts compiling shaders
> before your module has spoken up. You get a **crash on startup** saying a
> virtual shader path was not found — and because it happens before the editor
> window appears, it is genuinely confusing the first time.

You do **not** need to add this plugin to your `.uproject`. Plugins in your
project's `Plugins/` folder are found and enabled automatically.

---

## Step 3 — The build rules

Create **`Plugins/MyShaders/Source/MyShaders/MyShaders.Build.cs`**:

```csharp
using UnrealBuildTool;

public class MyShaders : ModuleRules
{
	public MyShaders(ReadOnlyTargetRules Target) : base(Target)
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
			// Gives us IPluginManager, so we can find our own folder on disk.
			"Projects",

			// The three rendering modules. This is the part that matters:
			//   RHI        - the hardware layer: buffers, textures, command lists
			//   RenderCore - FGlobalShader, shader parameters, and RDG live here
			//   Renderer   - the renderer itself; needed for the global shader map
			"RHI",
			"RenderCore",
			"Renderer",
		});
	}
}
```

This is a C# file, not C++. Unreal's build tool runs it to work out what to
compile and link.

**The class name must exactly match the file name and the module name.** All
three are `MyShaders`. Get one wrong and the build fails with a confusing
message about a missing module rules object.

Those last three dependencies are the ones that make GPU work possible. Without
`RenderCore` you cannot use `FGlobalShader`. Without `Renderer` you cannot look
up a compiled shader. Almost every "unresolved external symbol" error in this
kind of plugin traces back to a missing line here.

---

## Step 4 — The module

Create **`Plugins/MyShaders/Source/MyShaders/Private/MyShadersModule.cpp`**:

```cpp
// This file does exactly one important thing: it tells Unreal's shader compiler
// where our .usf files live on disk.
//
// Unreal does not read shaders by real folder path. It reads them by a "virtual"
// path that always starts with a slash, like "/Engine/Private/Something.usf".
// We claim our own virtual folder, "/Plugin/MyShaders", and point it at the real
// "Shaders" folder next to the .uplugin file.
//
// After the mapping below exists, a line like this (which we write in chapter 2):
//     IMPLEMENT_GLOBAL_SHADER(FMyCS, "/Plugin/MyShaders/Private/Thing.usf", ...)
// resolves to this real file:
//     <YourProject>/Plugins/MyShaders/Shaders/Private/Thing.usf

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FMyShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Find our own plugin folder on disk.
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MyShaders"));
		check(Plugin.IsValid());

		const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));

		// Virtual path  ->  real path
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/MyShaders"), ShaderDir);
	}

	virtual void ShutdownModule() override
	{
	}
};

// First argument: the class above. Second: the module name. They are different
// things and both must be right.
IMPLEMENT_MODULE(FMyShadersModule, MyShaders)
```

The string `TEXT("MyShaders")` passed to `FindPlugin` must match your
`.uplugin` **file name**. If they disagree, `Plugin.IsValid()` is false and the
`check` crashes the editor on startup.

---

## Step 5 — A placeholder shader file

The `Shaders/Private/` folder is empty, and Unreal is happier if the folder it
is told about actually has something in it. Create
**`Plugins/MyShaders/Shaders/Private/Placeholder.usf`**:

```hlsl
// Placeholder so the Shaders folder is not empty.
// Chapter 2 replaces this with a real shader. Nothing compiles this file.
```

A `.usf` that nothing references is never compiled, so this costs nothing.

---

## Where you should be now

```
YourProject/
  YourProject.uproject
  Plugins/
    MyShaders/
      MyShaders.uplugin                                  <- step 2
      Shaders/
        Private/
          Placeholder.usf                                <- step 5
      Source/
        MyShaders/
          MyShaders.Build.cs                             <- step 3
          Private/
            MyShadersModule.cpp                          <- step 4
          Public/                                        (empty for now)
```

Five items. Compare carefully — especially the doubled `Source/MyShaders/`.

---

## Step 6 — Build it

**Close the Unreal editor first.** (See
[chapter 0](00-Before-You-Start.md#the-build-command) if you need the reminder
about Live Coding.)

Then, substituting your own paths:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" YourProjectEditor Win64 Development -Project="D:\Path\To\YourProject.uproject" -WaitMutex
```

The first build after adding a plugin is slower than usual — Unreal notices the
new `.uplugin` and rebuilds its internal makefile. Expect 30 seconds to a few
minutes.

> ### ✅ Checkpoint 1 — it compiles
>
> The last lines should be:
>
> ```
> [n/n] Link [x64] UnrealEditor-MyShaders.dll
> Result: Succeeded
> ```
>
> Seeing your own module name in a Link line is the thing to look for.

---

## Step 7 — Check it actually loads

**Compiling is not the same as working.** The build tool never looks at your
`.uplugin`, never checks `LoadingPhase`, and never validates the shader path
mapping. All three of those fail at *runtime*.

So: open the editor.

Then check the log — **Window → Output Log**, and type `MyShaders` in its filter
box. Or open the file directly at
`YourProject/Saved/Logs/YourProject.log` and search it.

You are looking for two lines:

```
LogPluginManager: Mounting Project plugin MyShaders
LogModuleManager: InternalLoadLibrary: 'MyShaders' ('.../Binaries/Win64/UnrealEditor-MyShaders.dll')
```

> ### ✅ Checkpoint 2 — it loads
>
> Both lines present, editor open, no crash. That means:
>
> - the `.uplugin` is valid and was found
> - your module compiled and loaded
> - `StartupModule` ran, so `/Plugin/MyShaders` is now a real virtual path
>
> **You now have a working shader plugin.** It does nothing yet, but every piece
> of plumbing is in place, and chapter 2 is mostly just filling it in.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Unable to build while Live Coding is active` | editor still open, or `LiveCodingConsole.exe` lingering | close the editor; `taskkill /F /IM LiveCodingConsole.exe` |
| Build succeeds but no `MyShaders` line in the log | plugin not found | check the `.uplugin` is directly inside `Plugins/MyShaders/` and is named `MyShaders.uplugin` |
| `Could not find definition for module 'MyShaders'` | folder layout | you probably missed the doubled `Source/MyShaders/` level |
| Crash on startup: `Couldn't find source file of virtual shader path` | loading too late | `"LoadingPhase": "PostConfigInit"` in the `.uplugin`. You'll only hit this in chapter 2, once a shader exists. |
| Crash on startup in `check(Plugin.IsValid())` | name mismatch | the string in `FindPlugin` must match the `.uplugin` file name |
| `Unresolved external symbol` mentioning shader or RDG types | missing dependency | check all of `RHI`, `RenderCore`, `Renderer`, `Projects` are in `Build.cs` |
| JSON parse error at startup | trailing comma | JSON does not allow a comma after the last item |

---

## What you just built, in one picture

```
   MyShaders.uplugin          "this plugin exists, load its module early"
          │
          ▼
   MyShaders.Build.cs         "link me against RenderCore, RHI, Renderer"
          │
          ▼
   MyShadersModule.cpp        "/Plugin/MyShaders  =  <plugin>/Shaders"
          │
          ▼
   ...chapter 2 can now write a .usf and point C++ at it by virtual path
```

---

→ **Now read [Docs/00 - The Big Picture](../Docs/00-Big-Picture.md)** if you
haven't. The words in it will mean something now.

→ Next: **[02 - Your First Compute Shader](02-First-Compute-Shader.md)**
