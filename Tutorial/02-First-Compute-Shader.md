# 02 - Your First Compute Shader

**Goal:** write a program that runs on your graphics card, and get Unreal to
actually run it.

**Time:** about 30 minutes.

> ### Read this before you start
>
> **You will not see anything on screen in this chapter.** That is expected, not
> a failure.
>
> A shader paints into a texture. Getting that texture onto something you can
> look at needs a material and a plane, and that is chapter 3.
>
> This chapter's success is: the code builds, a new node appears in Blueprint,
> and calling it produces no errors. Chapter 3 is the payoff, and it is short.

---

## The three pieces

Every GPU program in Unreal is these three things. You are about to write one of
each.

```
   1. THE SHADER  (.usf)        the HLSL that runs on the GPU
          ▲
          │ linked by IMPLEMENT_GLOBAL_SHADER
          ▼
   2. THE C++ CLASS             a description of the shader's inputs
          ▲
          │ used by
          ▼
   3. THE RDG PASS              code that fills in those inputs and runs it
```

---

## Step 1 — Delete the placeholder

Delete `Plugins/MyShaders/Shaders/Private/Placeholder.usf`. You are about to
write a real one.

---

## Step 2 — The shader

Create **`Plugins/MyShaders/Shaders/Private/Gradient.usf`**:

```hlsl
// Always start a .usf with this. It smooths over the differences between
// DirectX, Vulkan, Metal and consoles so you write one shader, not five.
// Note the leading slash: shader includes use VIRTUAL paths, not file paths.
#include "/Engine/Public/Platform.ush"

// --- inputs ---
// These names must match the C++ parameter struct EXACTLY. Unreal links the two
// sides by string name. A typo here gives you a shader that compiles fine and
// silently does nothing.

// "RW" means read-write. This is the output image.
RWTexture2D<float4> OutTexture;

// A plain value is just a global variable in HLSL.
uint2 TextureSize;

// [numthreads(8, 8, 1)] means: one GROUP of work is 8 threads wide, 8 tall,
// 1 deep. So 64 threads per group. The C++ side decides how many groups to
// launch. 64 is a good default - chapter 5 explains why.
[numthreads(8, 8, 1)]
void MainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	// SV_DispatchThreadID is this thread's unique position across the WHOLE
	// launch. For a 256x256 image it runs 0..255 in x and y.
	// So we can use it directly as a pixel coordinate: one thread, one pixel.
	uint2 PixelCoord = DispatchThreadId.xy;

	// We can only launch whole groups. If the image is 250 pixels wide we still
	// launch 32 groups = 256 threads per row, and the last 6 have no pixel to
	// write. Without this guard they would write outside the texture.
	if (PixelCoord.x >= TextureSize.x || PixelCoord.y >= TextureSize.y)
	{
		return;
	}

	// Turn the pixel position into a 0..1 coordinate.
	// The + 0.5 puts us in the middle of the pixel rather than its corner.
	float2 UV = (float2(PixelCoord) + 0.5f) / float2(TextureSize);

	// Red rises to the right, green rises downward, blue stays 0.
	OutTexture[PixelCoord] = float4(UV.x, UV.y, 0.0f, 1.0f);
}
```

> **The bounds check is not optional.** Every compute shader that writes into
> something fixed-size needs one. Leaving it out writes past the end of the
> texture: corruption, or a GPU crash, and often not on the machine you are
> testing on.

---

## Step 3 — The Blueprint node's header

Create **`Plugins/MyShaders/Source/MyShaders/Public/MyShadersLibrary.h`**:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyShadersLibrary.generated.h"

class UTextureRenderTarget2D;

/**
 * A Blueprint Function Library: a bag of static functions that show up as
 * Blueprint nodes. This is the menu of everything MyShaders can do.
 */
UCLASS()
class MYSHADERS_API UMyShadersLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Paints a red/green gradient into a Render Target using a compute shader.
	 *
	 * The Render Target MUST have "Support UAV" ticked in its asset settings,
	 * because the shader writes to it directly.
	 */
	UFUNCTION(BlueprintCallable, Category = "MyShaders")
	static void DrawGradient(UTextureRenderTarget2D* OutputRT);
};
```

Three things that must be exactly right:

- **`MYSHADERS_API`** — your module name in CAPITALS, then `_API`. Unreal
  generates this macro for you. Spell it wrong and you get a flood of linker
  errors.
- **`#include "MyShadersLibrary.generated.h"`** — must be the **last** include.
  Unreal's header tool generates this file; putting anything after it is a
  compile error.
- The file must be in **`Public/`**, not `Private/`, for other code to see it.

---

## Step 4 — The real work

Create **`Plugins/MyShaders/Source/MyShaders/Private/Gradient.cpp`**.

This is the big one, so it is broken into pieces below. Put them all in the one
file, in this order.

### 4a — Includes

```cpp
#include "MyShadersLibrary.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"

// The group size from the .usf file. It MUST match [numthreads(8, 8, 1)].
// Getting these two out of sync is the most common compute shader bug there is.
static constexpr int32 kThreadGroupSize = 8;
```

### 4b — Piece 2: describe the shader to C++

```cpp
// A "global shader" is a shader not attached to any material or mesh. It is just
// a program you can run whenever you like. That is exactly what we want.
class FMyGradientCS : public FGlobalShader
{
public:
	// Registers the type with Unreal's shader system.
	DECLARE_GLOBAL_SHADER(FMyGradientCS);

	// "My inputs are the FParameters struct below - generate the binding code."
	SHADER_USE_PARAMETER_STRUCT(FMyGradientCS, FGlobalShader);

	// THE CONTRACT between C++ and HLSL.
	// Every line here needs a matching global in the .usf with the SAME NAME.
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// First argument is the HLSL type, written exactly as in the shader.
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)

		// FUintVector2 on this side becomes uint2 in HLSL.
		SHADER_PARAMETER(FUintVector2, TextureSize)
	END_SHADER_PARAMETER_STRUCT()

	// Which platforms should this be built for? SM5 is a safe floor meaning
	// "has proper compute support".
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
```

Put the two sides side by side and check them:

```
  .usf:   RWTexture2D<float4> OutTexture;
  .cpp:   SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
                                           ^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^
                                           same HLSL type       same name
```

### 4c — Piece 3: connect the class to the file

```cpp
// class, virtual path to the .usf, entry function name, shader stage.
// "/Plugin/MyShaders" is the virtual folder we registered in chapter 1.
IMPLEMENT_GLOBAL_SHADER(
	FMyGradientCS,
	"/Plugin/MyShaders/Private/Gradient.usf",
	"MainCS",
	SF_Compute);
```

### 4d — Piece 4: run it

```cpp
void UMyShadersLibrary::DrawGradient(UTextureRenderTarget2D* OutputRT)
{
	// --- GAME THREAD ---

	if (!OutputRT)
	{
		UE_LOG(LogTemp, Error, TEXT("DrawGradient: no Render Target given."));
		return;
	}

	// A compute shader writes through a UAV (Unordered Access View - "a view
	// that lets any thread write anywhere"). Unreal only creates that view if
	// the asset was told to allow it.
	if (!OutputRT->bCanCreateUAV)
	{
		UE_LOG(LogTemp, Error,
			TEXT("DrawGradient: Render Target '%s' does not support UAV. ")
			TEXT("Open the asset, tick 'Support UAV', and save it."),
			*OutputRT->GetName());
		return;
	}

	FTextureRenderTargetResource* Resource = OutputRT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		UE_LOG(LogTemp, Error, TEXT("DrawGradient: Render Target has no GPU resource yet."));
		return;
	}

	// Rendering cannot happen on the game thread. Hand a job to the render
	// thread instead. Everything it needs is captured BY VALUE - never capture a
	// UObject pointer and touch it later, it may be garbage collected first.
	ENQUEUE_RENDER_COMMAND(MyShaders_DrawGradient)(
		[Resource](FRHICommandListImmediate& RHICmdList)
		{
			// --- RENDER THREAD, some time later ---

			// FRDGBuilder is the Render Dependency Graph. You do not issue GPU
			// commands directly. You describe passes, and at Execute() time RDG
			// works out the order, the memory, and the barriers.
			FRDGBuilder GraphBuilder(RHICmdList);

			// Our Render Target already exists outside the graph, so we register
			// it: "here is a texture you did not create, please track it."
			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("MyShaders.Gradient.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			// AllocParameters, NOT a local variable. The pass runs later, after
			// this function has returned, so RDG must own the memory.
			FMyGradientCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMyGradientCS::FParameters>();

			Parameters->OutTexture  = GraphBuilder.CreateUAV(OutputTexture);
			Parameters->TextureSize = FUintVector2(Size.X, Size.Y);

			// Look up the compiled shader.
			TShaderMapRef<FMyGradientCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			// How many GROUPS cover the image? 8x8 pixels each, so a 256x256
			// texture needs 32x32 groups. GetGroupCount divides and ROUNDS UP,
			// which matters: 250/8 = 31.25, and 31 groups would leave two pixel
			// columns unpainted.
			const FIntVector GroupCount =
				FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize);

			// Add the pass. Nothing has run on the GPU yet.
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MyShaders.Gradient"),
				ComputeShader,
				Parameters,
				GroupCount);

			// NOW the graph is compiled and real GPU commands are recorded.
			GraphBuilder.Execute();
		});
}
```

---

## Where you should be now

```
MyShaders/
  MyShaders.uplugin
  Shaders/
    Private/
      Gradient.usf                                       <- step 2
  Source/
    MyShaders/
      MyShaders.Build.cs
      Private/
        MyShadersModule.cpp
        Gradient.cpp                                     <- step 4
      Public/
        MyShadersLibrary.h                               <- step 3
```

`Placeholder.usf` is gone.

---

## Step 5 — Build

Close the editor, then run your build command from chapter 0.

> ### ✅ Checkpoint 1 — it compiles
>
> ```
> Result: Succeeded
> ```
>
> A compile error here is almost always one of: a missing include, the
> `.generated.h` not being last, or `MYSHADERS_API` misspelled.

---

## Step 6 — Check the shader compiled

**The build tool never looks at your `.usf` file.** Shaders are compiled by the
*editor*, at startup. A broken shader builds perfectly and then fails when you
open the project.

Open the editor. Search the log for `FMyGradientCS`:

```
LogShaderCompilers: Display:  FMyGradientCS (compiled 1 times, average 0.01 sec, ...)
```

Also search for `error` near any `.usf` mention — a broken shader says so loudly
here and nowhere else.

> ### ✅ Checkpoint 2 — the shader compiled
>
> Your shader class name appears in the log and there are no shader errors.
>
> This means Unreal found `Gradient.usf` through the virtual path, compiled the
> HLSL, and matched it to your C++ class. The hard part is done.

---

## Step 7 — Check the node exists

In the editor, open any Blueprint (the Level Blueprint will do), right-click on
the graph, and search for **`Draw Gradient`**.

> ### ✅ Checkpoint 3 — you can call it
>
> The node appears, with one input pin called **Output RT**.
>
> You can even wire it to **Event BeginPlay** and press Play. Nothing visible
> happens — you have no Render Target yet — but the log should show your
> "no Render Target given" error, which proves your C++ ran.
>
> **That error message is a success.** It means the whole chain works: Blueprint
> found your node, called your function, and your code responded.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Cannot open include file: 'MyShadersLibrary.generated.h'` | header tool hasn't run | make sure `.generated.h` is the LAST include, then rebuild |
| Flood of unresolved externals on the library class | wrong API macro | `MYSHADERS_API` — module name in caps + `_API` |
| Node doesn't appear in Blueprint | module didn't reload | restart the editor after building; check for a `UFUNCTION(BlueprintCallable)` |
| Editor **crashes on startup** with `Couldn't find source file of virtual shader path '/Plugin/MyShaders/...'` | the path in `IMPLEMENT_GLOBAL_SHADER` doesn't resolve | see the box below |
| Shader compile errors mentioning a name | `.usf` / `.cpp` mismatch | the parameter names must match exactly, including case |
| Compiles and runs, texture stays black | usually a name typo | Unreal silently ignores a parameter it cannot match. Check spelling on both sides. |

> ### The virtual-path crash, in full
>
> This is worth recognising on sight, because it kills the editor before the
> window appears. The exact wording is:
>
> ```
> Fatal error: [File:...\RenderCore\Private\ShaderCore.cpp] [Line: 2894]
> Couldn't find source file of virtual shader path
> '/Plugin/MyShaders/Private/Gradient.usf'
> ```
>
> It means the string in `IMPLEMENT_GLOBAL_SHADER` did not resolve to a real
> file. Check these four, in order:
>
> 1. The `.usf` really is at `Plugins/MyShaders/Shaders/Private/Gradient.usf`.
>    Case matters.
> 2. The virtual prefix matches what `MyShadersModule.cpp` registered —
>    `/Plugin/MyShaders`, not `/Plugins/MyShaders` or `/Plugin/MyShader`.
> 3. `"LoadingPhase": "PostConfigInit"` is in the `.uplugin`. Any later and the
>    mapping is registered *after* shaders start compiling, which produces this
>    same message.
> 4. The `Private/` in the virtual path is the folder under `Shaders/`, not the
>    one under `Source/MyShaders/`. Two different `Private` folders — easy to mix up.
>
> This error is actually good news: it proves your mapping is live and being
> consulted. Silence is the confusing failure, not this.

---

## What you actually did

You wrote a program that runs **65,536 times simultaneously** (for a 256x256
texture), once per pixel, on hardware designed to do exactly that.

The loop you would have written on the CPU:

```cpp
for (int y = 0; y < Height; ++y)
    for (int x = 0; x < Width; ++x)
        Pixel[x][y] = ...;
```

does not exist in your shader. You wrote the *body* once, and launched a copy
per pixel. **The parallelism is the loop.** Getting comfortable with that flip is
most of learning GPU programming.

---

→ **Now read [Docs/01 - Hello Compute](../Docs/01-Hello-Compute.md)** for the
deeper explanation of every piece you just typed.

→ Next: **[03 - See It On Screen](03-See-It-On-Screen.md)** — the payoff.
