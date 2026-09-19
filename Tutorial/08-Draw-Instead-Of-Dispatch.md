# 08 - Draw Instead Of Dispatch

**Goal:** do the same kind of job the *other* way the GPU offers — by drawing a
triangle and colouring it with a pixel shader.

**Time:** about 20 minutes.

**Reference:** [`Lesson06_FullscreenPS.usf`](../Shaders/Private/Lesson06_FullscreenPS.usf) ·
[`Lesson06_FullscreenPS.cpp`](../Source/GPULab/Private/Lesson06_FullscreenPS.cpp)

Everything so far was **compute**. This is a **draw**.

---

## The two halves of the GPU

|  | Compute (ch 2–7) | Raster (this chapter) |
|---|---|---|
| What you say | "run this function N times" | "draw these triangles" |
| Who picks the pixels | you do, from the thread ID | the triangle's shape does |
| Output goes to | a UAV you bound | the bound render target |
| Needs "Support UAV" | **yes** | **no** |
| C++ helper | `FComputeShaderUtils` | `FPixelShaderUtils` |
| Shader stage | `SF_Compute` | `SF_Vertex` + `SF_Pixel` |
| Depth test, blending, stencil | not available | available |

Both run on the same hardware. Raster adds fixed-function machinery around your
code: it turns triangles into pixel coverage, can test and write depth, and can
blend your output with what's already there.

---

## The one big triangle

To cover a whole render target you *could* draw two triangles forming a quad.
Almost nobody does. Instead you draw **one triangle bigger than the screen**:

```
        (-1, 3)
           |\
           | \
           |  \
   ┌───────┼───\──────┐
   │       │    \     │   <- the render target
   │       │     \    │
   │       │      \   │
   └───────┼───────\──┘
           |        \
        (-1,-1)──────(3,-1)
```

GPUs shade in 2×2 pixel quads, so the diagonal seam of a two-triangle quad gets
shaded twice. One big triangle has no internal seam. It's free, and it's what
everyone does.

**You don't write this.** Unreal has `FScreenVertexShaderVS` for exactly this
job, and `FPixelShaderUtils::AddFullscreenPass` picks it up for you.

---

## Step 1 — The pixel shader

Create **`Plugins/MyShaders/Shaders/Private/Checkerboard.usf`**:

```hlsl
#include "/Engine/Public/Platform.ush"

float4 Tint;
float  Time;
uint2  OutputSize;

// SV_POSITION as a pixel shader INPUT is the pixel's position on screen, and it
// is ALREADY at the pixel centre - a 512-wide target gives 0.5, 1.5 ... 511.5.
//
// That is why there is no "+ 0.5" here, unlike every compute chapter.
// SV_DispatchThreadID gives whole numbers; SV_POSITION gives centres. Mixing
// them up shifts your image half a pixel - just visible enough to annoy you,
// just subtle enough to be hard to find.
//
// SV_Target0 means "write this to the first bound render target".
void MainPS(
	float4 SvPosition : SV_POSITION,
	out float4 OutColor : SV_Target0)
{
	float2 UV = SvPosition.xy / float2(OutputSize);

	// A checkerboard: floor onto an 8x8 grid, add, look at odd/even.
	float2 Grid    = floor(UV * 8.0f);
	float  Checker = fmod(Grid.x + Grid.y, 2.0f);

	// A diagonal band sliding across.
	float Band = saturate(sin((UV.x + UV.y) * 10.0f - Time * 3.0f) * 0.5f + 0.5f);

	float3 Dark  = float3(0.05f, 0.06f, 0.10f);
	float3 Light = float3(0.85f, 0.88f, 0.95f);

	float3 Color = lerp(Dark, Light, Checker);
	Color += Band * 0.35f * float3(0.9f, 0.4f, 0.1f);

	OutColor = float4(Color * Tint.rgb, 1.0f);
}
```

---

## Step 2 — Declare it

In **`MyShadersLibrary.h`**:

```cpp
	/**
	 * Draws a checkerboard using the RASTER path (vertex + pixel shader).
	 * This one does NOT need "Support UAV" on the render target.
	 */
	UFUNCTION(BlueprintCallable, Category = "MyShaders")
	static void DrawCheckerboard(
		UTextureRenderTarget2D* OutputRT,
		float Time = 0.0f,
		FLinearColor Tint = FLinearColor::White);
```

---

## Step 3 — The C++

Create **`Plugins/MyShaders/Source/MyShaders/Private/Checkerboard.cpp`**:

```cpp
#include "MyShadersLibrary.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PixelShaderUtils.h"      // <- new
#include "RenderingThread.h"

class FMyCheckerboardPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMyCheckerboardPS);
	SHADER_USE_PARAMETER_STRUCT(FMyCheckerboardPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FLinearColor, Tint)
		SHADER_PARAMETER(float,        Time)
		SHADER_PARAMETER(FUintVector2, OutputSize)

		// THIS LINE is what makes it a raster pass.
		//
		// It adds hidden members saying WHICH textures the pixel shader draws
		// into and what happens to what was already there. RDG refuses to build
		// a raster pass without it. There is no compute equivalent, because
		// compute has no "bound render target" concept at all.
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// SF_Pixel, not SF_Compute. That last argument tells Unreal which stage of the
// pipeline to compile this for.
IMPLEMENT_GLOBAL_SHADER(
	FMyCheckerboardPS,
	"/Plugin/MyShaders/Private/Checkerboard.usf",
	"MainPS",
	SF_Pixel);

void UMyShadersLibrary::DrawCheckerboard(
	UTextureRenderTarget2D* OutputRT, float Time, FLinearColor Tint)
{
	// Note: NO bCanCreateUAV check. We are drawing, not writing a UAV.
	if (!OutputRT)
	{
		UE_LOG(LogTemp, Error, TEXT("DrawCheckerboard: no Render Target given."));
		return;
	}

	FTextureRenderTargetResource* Resource = OutputRT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(MyShaders_DrawCheckerboard)(
		[Resource, Time, Tint](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder, Resource->GetRenderTargetTexture(), TEXT("MyShaders.Checker.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			FMyCheckerboardPS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMyCheckerboardPS::FParameters>();

			Parameters->Tint       = Tint;
			Parameters->Time       = Time;
			Parameters->OutputSize = FUintVector2(Size.X, Size.Y);

			// Bind the render target in slot 0. The load action decides what
			// happens to the existing contents first:
			//   ELoad     - keep them, draw on top. Always safe.
			//   EClear    - wipe to the texture's clear colour.
			//   ENoAction - "I promise to overwrite every pixel". Fastest; if
			//               you lie, you get whatever garbage was in memory.
			Parameters->RenderTargets[0] =
				FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

			TShaderMapRef<FMyCheckerboardPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			// The raster equivalent of FComputeShaderUtils::AddPass.
			// Instead of a group count it takes a VIEWPORT - the rectangle the
			// triangle is allowed to touch.
			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(GMaxRHIFeatureLevel),   // to find the vertex shader
				RDG_EVENT_NAME("MyShaders.Checkerboard"),
				PixelShader,
				Parameters,
				FIntRect(0, 0, Size.X, Size.Y));

			GraphBuilder.Execute();
		});
}
```

Compare the two AddPass calls. Same shape; the difference is the last argument:

```cpp
// compute
FComputeShaderUtils::AddPass(..., ComputeShader, Parameters, GroupCount);

// raster
FPixelShaderUtils::AddFullscreenPass(..., PixelShader, Parameters, Viewport);
```

---

## Step 4 — Build and run

> ### ✅ Checkpoint
>
> A checkerboard with a bright diagonal band sliding across it (wire **Time** to
> **Get Game Time in Seconds** on **Event Tick**).

---

## Prove the UAV difference

This is the most instructive two minutes in the chapter.

1. Make a **second** Render Target, `RT_NoUAV`, and leave **Support UAV**
   **unticked**.
2. Run **Draw Checkerboard** on it → **it works**.
3. Run **Draw Gradient** (chapter 2) on it → the log says:
   ```
   DrawGradient: Render Target 'RT_NoUAV' does not support UAV.
   ```

Same texture, two shaders, two outcomes. Compute writes through a UAV; raster
draws into a bound render target. Different machinery.

---

## So which should I use?

**Compute when:**
- the work isn't really pixels (arrays, particles, physics, sorting)
- you need to write to scattered locations
- you want `groupshared` memory so threads can cooperate
- you want to run at a resolution unrelated to anything being drawn

**Raster when:**
- you're drawing actual geometry
- you want depth testing, stencil, or hardware blending
- you're on mobile, where compute is often slower

For "fill a texture with a pattern", **both work**, and compute is usually
marginally faster. This chapter exists so you've written one of each.

---

## Try it yourself

1. **Change the checker size.** `floor(UV * 8.0f)` → 4, 16, 32.

2. **Add blending.** Pass
   `TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI()` as the blend
   state argument. Now each call *adds* to the target and repeated calls blow out
   to white.

3. **Write to two targets.** Add `out float4 OutColor2 : SV_Target1` and bind a
   second texture to `RenderTargets[1]`. That's a mini G-buffer.

4. **Port it to compute.** Rewrite this chapter as a compute shader. Fifteen
   minutes, and it fixes the compute/raster distinction in your head permanently.

---

→ **Now read [Docs/06 - Pixel Shader Pass](../Docs/06-Pixel-Shader-Pass.md)**.

→ Next: **[09 - Make It Remember](09-Make-It-Remember.md)** — the last one, and
the best one.
