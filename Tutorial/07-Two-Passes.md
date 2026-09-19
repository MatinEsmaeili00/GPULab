# 07 - Two Passes In One Graph

**Goal:** run two shaders in a row, where the second reads what the first wrote —
and never write a barrier yourself.

**Time:** about 25 minutes.

**Reference:** [`Lesson05_MultiPass.usf`](../Shaders/Private/Lesson05_MultiPass.usf) ·
[`Lesson05_MultiPass.cpp`](../Source/GPULab/Private/Lesson05_MultiPass.cpp)

RDG has felt like pointless paperwork for five chapters. This is where it pays
you back.

---

## What you are building

```
   ┌──────────────┐                      ┌──────────────┐
   │   PASS A     │  writes   ┌───────┐  │   PASS B     │  writes
   │   DotsCS     │──────────►│Scratch│─►│   BlurCS     │────────► Render Target
   └──────────────┘           │texture│  └──────────────┘
                              └───────┘     reads
```

Pass B must not start until pass A has finished. The GPU must also be told the
scratch texture has changed role, from "being written" to "being read".

**You will not write that code.** That's the chapter.

---

## Step 1 — The shader (two entry points, one file)

Create **`Plugins/MyShaders/Shaders/Private/DotsAndBlur.usf`**:

```hlsl
#include "/Engine/Public/Platform.ush"

// ---------------------------------------------------------------------------
// PASS A - draw moving dots into a scratch texture.
// ---------------------------------------------------------------------------
RWTexture2D<float4> ScratchOut;
uint2 ScratchSize;
float Time;

[numthreads(8, 8, 1)]
void DotsCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	uint2 PixelCoord = DispatchThreadId.xy;
	if (PixelCoord.x >= ScratchSize.x || PixelCoord.y >= ScratchSize.y)
	{
		return;
	}

	float2 UV = (float2(PixelCoord) + 0.5f) / float2(ScratchSize);
	float3 Color = float3(0.02f, 0.02f, 0.04f);

	// Five dots orbiting the middle.
	for (int i = 0; i < 5; ++i)
	{
		float Phase   = Time * 0.7f + float(i) * 1.2566f;   // 1.2566 = 2*pi/5
		float2 Center = 0.5f + 0.32f * float2(cos(Phase), sin(Phase));

		// smoothstep(a, b, x) fades 1 -> 0 as x goes a -> b.
		// So this is a soft round dot of radius ~0.03.
		float Dot = 1.0f - smoothstep(0.0f, 0.03f, length(UV - Center));

		float3 DotColor = 0.5f + 0.5f * cos(float3(0, 2, 4) + float(i));
		Color += Dot * DotColor;
	}

	ScratchOut[PixelCoord] = float4(Color, 1.0f);
}

// ---------------------------------------------------------------------------
// PASS B - read the scratch texture and blur it into the final output.
// This pass READS what pass A WROTE. That is the whole point.
// ---------------------------------------------------------------------------
Texture2D<float4>   BlurInput;     // read-only view of pass A's result
SamplerState        BlurSampler;   // how to read it: bilinear, clamped
RWTexture2D<float4> BlurOut;
uint2 OutSize;
int   BlurRadius;

[numthreads(8, 8, 1)]
void BlurCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	uint2 PixelCoord = DispatchThreadId.xy;
	if (PixelCoord.x >= OutSize.x || PixelCoord.y >= OutSize.y)
	{
		return;
	}

	float2 TexelSize = 1.0f / float2(OutSize);
	float2 UV = (float2(PixelCoord) + 0.5f) * TexelSize;

	float4 Sum   = 0.0f;
	float  Count = 0.0f;

	// A plain box blur: average every texel in a square around us.
	for (int y = -BlurRadius; y <= BlurRadius; ++y)
	{
		for (int x = -BlurRadius; x <= BlurRadius; ++x)
		{
			// SampleLevel, NOT Sample. A compute shader has no 2x2 pixel quads,
			// so it cannot pick a mip level for you. You must say which. 0 is
			// the biggest. Calling Sample() here is a compile error.
			Sum   += BlurInput.SampleLevel(BlurSampler, UV + float2(x, y) * TexelSize, 0);
			Count += 1.0f;
		}
	}

	BlurOut[PixelCoord] = Sum / Count;
}
```

---

## Step 2 — Declare it

In **`MyShadersLibrary.h`**:

```cpp
	/** Draws orbiting dots, then blurs them - two GPU passes in one graph. */
	UFUNCTION(BlueprintCallable, Category = "MyShaders")
	static void DrawBlurredDots(
		UTextureRenderTarget2D* OutputRT,
		float Time = 0.0f,
		int32 BlurRadius = 4);
```

---

## Step 3 — The C++

Create **`Plugins/MyShaders/Source/MyShaders/Private/DotsAndBlur.cpp`**:

```cpp
#include "MyShadersLibrary.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"

static constexpr int32 kThreadGroupSize = 8;

// --- Pass A's shader ---
class FMyDotsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMyDotsCS);
	SHADER_USE_PARAMETER_STRUCT(FMyDotsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ScratchOut)
		SHADER_PARAMETER(FUintVector2, ScratchSize)
		SHADER_PARAMETER(float,        Time)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// --- Pass B's shader ---
class FMyBlurCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMyBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FMyBlurCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Note the DIFFERENT macro: no _UAV. This is a read-only view, and
		// that single difference is how RDG learns B depends on A.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>,        BlurInput)
		SHADER_PARAMETER_SAMPLER(SamplerState,                 BlurSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>,  BlurOut)
		SHADER_PARAMETER(FUintVector2, OutSize)
		SHADER_PARAMETER(int32,        BlurRadius)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// Two entry points, same file. The third argument is the function name.
IMPLEMENT_GLOBAL_SHADER(FMyDotsCS, "/Plugin/MyShaders/Private/DotsAndBlur.usf", "DotsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FMyBlurCS, "/Plugin/MyShaders/Private/DotsAndBlur.usf", "BlurCS", SF_Compute);

void UMyShadersLibrary::DrawBlurredDots(UTextureRenderTarget2D* OutputRT, float Time, int32 BlurRadius)
{
	if (!OutputRT || !OutputRT->bCanCreateUAV)
	{
		UE_LOG(LogTemp, Error, TEXT("DrawBlurredDots: missing Render Target, or no UAV support."));
		return;
	}

	FTextureRenderTargetResource* Resource = OutputRT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	// A box blur costs (2r+1)^2 samples per pixel. At radius 16 that's 1089
	// texture reads for EVERY pixel, so clamp before someone types 500.
	const int32 ClampedRadius = FMath::Clamp(BlurRadius, 0, 16);

	ENQUEUE_RENDER_COMMAND(MyShaders_DrawBlurredDots)(
		[Resource, Time, ClampedRadius](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder, Resource->GetRenderTargetTexture(), TEXT("MyShaders.Blur.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			// The scratch texture. It exists ONLY inside this graph.
			// The flags matter:
			//   TexCreate_UAV            - pass A writes it
			//   TexCreate_ShaderResource - pass B reads it
			// Leave one out and RDG asserts when you use it that way.
			const FRDGTextureDesc ScratchDesc = FRDGTextureDesc::Create2D(
				Size,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef ScratchTexture =
				GraphBuilder.CreateTexture(ScratchDesc, TEXT("MyShaders.Blur.Scratch"));

			const FIntVector GroupCount =
				FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize);

			// --- PASS A ---
			{
				FMyDotsCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FMyDotsCS::FParameters>();

				Parameters->ScratchOut  = GraphBuilder.CreateUAV(ScratchTexture);
				Parameters->ScratchSize = FUintVector2(Size.X, Size.Y);
				Parameters->Time        = Time;

				TShaderMapRef<FMyDotsCS> DotsShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder, RDG_EVENT_NAME("MyShaders.PassA_Dots"),
					DotsShader, Parameters, GroupCount);
			}

			// --- PASS B ---
			{
				FMyBlurCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FMyBlurCS::FParameters>();

				// Same texture, handed over READ-ONLY this time. No CreateUAV.
				Parameters->BlurInput = ScratchTexture;

				// A sampler says HOW to read: bilinear blending, and clamp at
				// the edges so the blur doesn't wrap around the image.
				Parameters->BlurSampler =
					TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

				Parameters->BlurOut    = GraphBuilder.CreateUAV(OutputTexture);
				Parameters->OutSize    = FUintVector2(Size.X, Size.Y);
				Parameters->BlurRadius = ClampedRadius;

				TShaderMapRef<FMyBlurCS> BlurShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder, RDG_EVENT_NAME("MyShaders.PassB_Blur"),
					BlurShader, Parameters, GroupCount);
			}

			// Only NOW does RDG look at both passes, see that B needs A, insert
			// the barrier between them, and record the real commands.
			GraphBuilder.Execute();
		});
}
```

---

## Step 4 — Build and run

Wire `Draw Blurred Dots` to **Event Tick** with **Get Game Time in Seconds** into
**Time**, and Blur Radius `6`.

> ### ✅ Checkpoint
>
> Five soft coloured dots orbiting the centre, visibly blurred.
>
> Set Blur Radius to `0` and you see pass A's raw output — hard-edged dots. Walk
> it up to 8 and watch them soften. That slider is proof both passes are running
> and that B is reading A.

---

## The barrier you didn't write

Search your `.cpp` for the word "barrier". It isn't there.

Modern GPUs overlap and reorder passes and keep writes sitting in caches other
parts of the chip can't see. A **barrier** is you telling the driver: *stop,
finish every write to this texture, flush the caches, and change its state from
writable to readable.*

Written by hand, the classic bugs are:

| Mistake | What you see |
|---|---|
| Forgot a barrier | flickering, garbage, or last frame's data — often on only one vendor's card |
| Barrier too early | same as forgetting it |
| Too many barriers | correct, but slow — you serialised work that could have overlapped |
| Wrong state | validation spam, or a device-removal crash |

Miserable bugs: timing-dependent, hardware-dependent, frequently invisible on
your own machine.

RDG deleted the whole category, from just this:

```cpp
Parameters->ScratchOut = GraphBuilder.CreateUAV(ScratchTexture);  // pass A: WRITE
Parameters->BlurInput  = ScratchTexture;                          // pass B: READ
```

> **Choosing the macro is how you declare the dependency.** Use a UAV where a
> read-only view would do, and RDG must assume you might write — so it inserts
> barriers you didn't need and serialises passes that could have overlapped.

---

## Transient resources

Your scratch texture is created when the graph runs and gone when it finishes.
It never becomes an asset, and **RDG hands its memory to other passes** once
you're done with it. A frame of a modern renderer has dozens of temporary
full-screen textures; if each had its own memory you'd run out.

---

## Try it yourself

1. **Blur Radius 0 vs 8.** Do this first — it proves the two passes to your own
   eyes.

2. **Delete a flag.** Remove `TexCreate_ShaderResource` from `ScratchDesc`. RDG
   asserts when pass B tries to read, and names `MyShaders.Blur.Scratch` in the
   message. That's why good names matter.

3. **Watch the graph.** Console: `r.RDG.Debug 1`. Your passes appear by the names
   you gave `RDG_EVENT_NAME`.

4. **Make the blur separable.** The big one. A box blur is `(2r+1)²` reads per
   pixel — 81 at radius 4. Blur horizontally into a second scratch texture, then
   vertically, and it's `2(2r+1)` = 18. Same result, four times cheaper.

   You now have a three-pass graph, and you still haven't written a barrier.

---

→ **Now read [Docs/05 - Multi-Pass RDG](../Docs/05-Multi-Pass-RDG.md)**.

→ Next: **[08 - Draw Instead Of Dispatch](08-Draw-Instead-Of-Dispatch.md)**
