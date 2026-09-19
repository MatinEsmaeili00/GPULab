# 05 - See The Threads

**Goal:** paint the GPU's own bookkeeping numbers into the texture, so you can
*see* how your work got split up.

**Time:** about 20 minutes.

**Reference:** [`Lesson03_ThreadIDs.usf`](../Shaders/Private/Lesson03_ThreadIDs.usf) ·
[`Lesson03_ThreadIDs.cpp`](../Source/GPULab/Private/Lesson03_ThreadIDs.cpp)

This is the most important chapter in the tutorial. Everything else is detail.

---

## Three layers

```
  DISPATCH  ── you launch this many groups ──┐
                                             │
      ┌──────────┬──────────┬──────────┬─────┘
      │          │          │          │
   ┌──▼──┐    ┌──▼──┐    ┌──▼──┐    ┌──▼──┐
   │GROUP│    │GROUP│    │GROUP│    │GROUP│   ... 4096 of them
   └──┬──┘    └─────┘    └─────┘    └─────┘
      │
      │  each group is [numthreads(8,8,1)] = 64 threads
      ▼
   ┌────────────────┐
   │ T T T T T T T T│  8 across
   │ T T T T T T T T│  8 down
   │ T T T T T T T T│  = 64 threads, each running your function once
   │ T T T T T T T T│
   │ T T T T T T T T│
   │ T T T T T T T T│
   │ T T T T T T T T│
   │ T T T T T T T T│
   └────────────────┘
```

**Thread** — one run of your function. One pixel.

**Group** — a fixed box of threads set by `[numthreads(X, Y, Z)]`. Threads in the
same group can share fast memory and wait for each other. Threads in *different*
groups can do **neither** — assume they run in any order, or at once, or far
apart.

**Dispatch** — how many groups you launch. Decided in C++.

> **Total threads = group count × threads per group**

For your 512×512 render target with 8×8 groups:

```
  512/8 = 64 groups across, 64 down  =  4,096 groups
  4,096 × 64 threads                 =  262,144 threads  =  one per pixel ✓
```

---

## Step 1 — The shader

Create **`Plugins/MyShaders/Shaders/Private/ThreadView.usf`**:

```hlsl
#include "/Engine/Public/Platform.ush"

RWTexture2D<float4> OutTexture;
uint2 TextureSize;
uint  ViewMode;

// This define is pushed in from C++ so there is exactly ONE place that decides
// the group size. Change it in the .cpp and this file follows automatically.
#define THREADS MYSHADERS_THREADS_XY

[numthreads(THREADS, THREADS, 1)]
void MainCS(
	uint3 DispatchThreadId : SV_DispatchThreadID,  // position across the whole launch
	uint3 GroupId          : SV_GroupID,           // which group am I in
	uint3 GroupThreadId    : SV_GroupThreadID,     // position inside my own group
	uint  GroupIndex       : SV_GroupIndex)        // that position, flattened
{
	uint2 PixelCoord = DispatchThreadId.xy;
	if (PixelCoord.x >= TextureSize.x || PixelCoord.y >= TextureSize.y)
	{
		return;
	}

	float3 Color = float3(0, 0, 0);

	if (ViewMode == 0)
	{
		// Global position. A smooth gradient over the whole image.
		// This is the number you want 95% of the time.
		Color = float3(float2(DispatchThreadId.xy) / float2(TextureSize), 0.0f);
	}
	else if (ViewMode == 1)
	{
		// Which group. Chunky blocks - every thread in a group returns the same
		// GroupId, so each block is one flat colour.
		uint2 GroupCount = (TextureSize + THREADS - 1) / THREADS;
		Color = float3(float2(GroupId.xy) / float2(GroupCount), 0.0f);
	}
	else if (ViewMode == 2)
	{
		// Position inside the group. The same small gradient tile, repeated
		// once per group.
		Color = float3(float2(GroupThreadId.xy) / float(THREADS), 0.0f);
	}
	else
	{
		// The flat 0..63 index inside the group, as grey. This is the order
		// threads sit in when you use groupshared memory.
		float T = float(GroupIndex) / float(THREADS * THREADS - 1);
		Color = float3(T, T, T);
	}

	OutTexture[PixelCoord] = float4(Color, 1.0f);
}
```

---

## Step 2 — Declare it

In **`MyShadersLibrary.h`**, add above the `UCLASS`:

```cpp
/** Which piece of the GPU's bookkeeping to paint. */
UENUM(BlueprintType)
enum class EMyThreadView : uint8
{
	DispatchThreadID	UMETA(DisplayName = "Dispatch Thread ID (global position)"),
	GroupID				UMETA(DisplayName = "Group ID (which block)"),
	GroupThreadID		UMETA(DisplayName = "Group Thread ID (position in block)"),
	GroupIndex			UMETA(DisplayName = "Group Index (flat number in block)"),
};
```

and inside the class:

```cpp
	/** Paints the GPU's thread numbering so you can see how work was divided. */
	UFUNCTION(BlueprintCallable, Category = "MyShaders")
	static void DrawThreadView(
		UTextureRenderTarget2D* OutputRT,
		EMyThreadView View = EMyThreadView::DispatchThreadID);
```

---

## Step 3 — The C++

Create **`Plugins/MyShaders/Source/MyShaders/Private/ThreadView.cpp`**.

Same as before, with **one new piece** — look for
`ModifyCompilationEnvironment`:

```cpp
#include "MyShadersLibrary.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"

static constexpr int32 kThreadGroupSize = 8;

class FMyThreadViewCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMyThreadViewCS);
	SHADER_USE_PARAMETER_STRUCT(FMyThreadViewCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
		SHADER_PARAMETER(FUintVector2, TextureSize)
		SHADER_PARAMETER(uint32,       ViewMode)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// NEW. This runs at SHADER COMPILE time and injects #define lines into the
	// HLSL before the compiler sees it. Here we push the C++ group size into the
	// shader, so the two sides can never drift apart.
	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MYSHADERS_THREADS_XY"), kThreadGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMyThreadViewCS,
	"/Plugin/MyShaders/Private/ThreadView.usf",
	"MainCS",
	SF_Compute);

void UMyShadersLibrary::DrawThreadView(UTextureRenderTarget2D* OutputRT, EMyThreadView View)
{
	if (!OutputRT || !OutputRT->bCanCreateUAV)
	{
		UE_LOG(LogTemp, Error, TEXT("DrawThreadView: missing Render Target, or no UAV support."));
		return;
	}

	FTextureRenderTargetResource* Resource = OutputRT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	const uint32 ViewMode = static_cast<uint32>(View);

	ENQUEUE_RENDER_COMMAND(MyShaders_DrawThreadView)(
		[Resource, ViewMode](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("MyShaders.ThreadView.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			FMyThreadViewCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMyThreadViewCS::FParameters>();

			Parameters->OutTexture  = GraphBuilder.CreateUAV(OutputTexture);
			Parameters->TextureSize = FUintVector2(Size.X, Size.Y);
			Parameters->ViewMode    = ViewMode;

			TShaderMapRef<FMyThreadViewCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			const FIntVector GroupCount =
				FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize);

			// Print the arithmetic so you can check it against your own.
			UE_LOG(LogTemp, Log,
				TEXT("ThreadView: texture %dx%d, groups %dx%d, threads/group %d, total %d"),
				Size.X, Size.Y,
				GroupCount.X, GroupCount.Y,
				kThreadGroupSize * kThreadGroupSize,
				GroupCount.X * GroupCount.Y * kThreadGroupSize * kThreadGroupSize);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MyShaders.ThreadView"),
				ComputeShader,
				Parameters,
				GroupCount);

			GraphBuilder.Execute();
		});
}
```

---

## Step 4 — Build, then look at all four

Build, restart, and wire `Draw Thread View` to **Event BeginPlay** with your
render target. The **View** pin is a dropdown.

**Run it four times, once per setting.** This is the whole point of the chapter.

> ### ✅ Checkpoint — four different pictures
>
> | View | What you should see |
> |---|---|
> | **Dispatch Thread ID** | a smooth gradient over the whole image |
> | **Group ID** | chunky blocks, each a flat colour — 64×64 of them |
> | **Group Thread ID** | the same tiny gradient tile, repeated over and over |
> | **Group Index** | a grey ramp repeating every 64 threads |
>
> If Group ID looks smooth rather than blocky, `MYSHADERS_THREADS_XY` is not
> reaching the shader — check `ModifyCompilationEnvironment`.
>
> Also check the **Output Log** for the `ThreadView:` line and compare its
> numbers to the arithmetic at the top of this chapter.

Put *Dispatch Thread ID* and *Group ID* side by side in your head. Same work,
same threads — one shows you each thread's global position, the other shows you
which box it lives in. That picture is what "the GPU divides work into groups"
actually means.

---

## Choosing a group size

**Make it a multiple of 64.** GPUs do not run threads one at a time — they run
them in lockstep bundles. NVIDIA calls a bundle a *warp* (32 threads), AMD a
*wavefront* (32 or 64). A group of 10 threads still occupies a full bundle and
throws away the rest.

So: **64, 128, 256.** Never 10, 50, or 100.

**Match the shape to the problem:**

| Problem | Group shape | Why |
|---|---|---|
| an image | `[numthreads(8, 8, 1)]` | 64 threads, and neighbours in x and y stay together — good for cache |
| a flat array | `[numthreads(64, 1, 1)]` | no 2D structure to preserve |
| a 3D volume | `[numthreads(4, 4, 4)]` | 64 threads, cubic |

**Hard limit:** `X * Y * Z` must be ≤ 1024.

**Don't obsess.** 8×8 for images and 64×1×1 for arrays are right nearly always.
Tuning group size is a last-resort optimisation you do with a profiler open.

---

## Try it yourself

1. **Change `kThreadGroupSize` to 16** — in the `.cpp` only. The `.usf` follows
   automatically because of the `#define`. Rebuild and look at *Group ID*: fewer,
   bigger blocks.

2. **Break the link on purpose.** Hard-code `[numthreads(4, 4, 1)]` in the `.usf`
   while C++ still thinks it is 8. You now launch a quarter of the threads you
   need, and only the top-left quarter of the image gets painted.

   **This is what a group-size mismatch looks like.** Learn to recognise it —
   it is one of the most common compute shader bugs, and the picture is always
   the same: part of your output is stale or black.

3. **Remove the bounds check** and set your render target to 500×500 instead of
   512. Watch what happens. Then put it back.

---

→ **Now read [Docs/03 - Threads and Groups](../Docs/03-Threads-And-Groups.md)**.

→ Next: **[06 - Arrays In, Arrays Out](06-Arrays-In-Arrays-Out.md)** — GPU work
that isn't a picture.
