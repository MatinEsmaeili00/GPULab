# 04 - Send Values To The GPU

**Goal:** stop hard-coding things. Send a time, a zoom and two colours from
Blueprint into your shader, and watch the pattern animate.

**Time:** about 20 minutes.

**Reference:** GPULab's finished version is
[`Lesson02_Parameters.usf`](../Shaders/Private/Lesson02_Parameters.usf) and
[`Lesson02_Parameters.cpp`](../Source/GPULab/Private/Lesson02_Parameters.cpp) —
diff yours against it when you're done.

---

## The pattern you will repeat from now on

Every remaining chapter is the same four moves. Learn the shape once:

```
   1. write a new .usf                     Shaders/Private/<Name>.usf
   2. write a new .cpp                     Source/MyShaders/Private/<Name>.cpp
   3. declare the function in the header   Source/MyShaders/Public/MyShadersLibrary.h
   4. build, restart editor, wire it up
```

Nothing from chapter 3 changes — same render target, same material, same plane.

---

## Step 1 — The shader

Create **`Plugins/MyShaders/Shaders/Private/Swirl.usf`**:

```hlsl
#include "/Engine/Public/Platform.ush"

RWTexture2D<float4> OutTexture;
uint2  TextureSize;

// The new part. These four arrive from C++ on every dispatch.
float  Time;      // seconds - we feed it in and the pattern moves
float  Scale;     // how many rings fit across the image
float4 ColorA;    // an FLinearColor in C++ becomes a float4 here
float4 ColorB;

[numthreads(8, 8, 1)]
void MainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	uint2 PixelCoord = DispatchThreadId.xy;
	if (PixelCoord.x >= TextureSize.x || PixelCoord.y >= TextureSize.y)
	{
		return;
	}

	float2 UV = (float2(PixelCoord) + 0.5f) / float2(TextureSize);

	// Move the origin to the middle, so (0,0) is the centre of the image.
	float2 Centered = UV - 0.5f;

	float Radius = length(Centered);                  // distance -> rings
	float Angle  = atan2(Centered.y, Centered.x);     // angle    -> spokes

	// Combine them into one wobbling value and slide it with Time.
	// 6.2831853 is 2*pi, so Scale = 8 means 8 rings.
	float Wave = sin(Radius * Scale * 6.2831853f - Time * 2.0f + Angle * 3.0f);

	// sin() gives -1..1. Remap to 0..1 so we can blend colours with it.
	float T = Wave * 0.5f + 0.5f;

	// lerp(a, b, t) = a when t is 0, b when t is 1, a mix in between.
	OutTexture[PixelCoord] = float4(lerp(ColorA.rgb, ColorB.rgb, T), 1.0f);
}
```

---

## Step 2 — Declare the node

Open **`Source/MyShaders/Public/MyShadersLibrary.h`** and add a second function
inside the class, after `DrawGradient`:

```cpp
	/** An animated swirl. Feed Time from Get Game Time in Seconds on Tick. */
	UFUNCTION(BlueprintCallable, Category = "MyShaders")
	static void DrawSwirl(
		UTextureRenderTarget2D* OutputRT,
		float Time = 0.0f,
		float Scale = 8.0f,
		FLinearColor ColorA = FLinearColor(0.05f, 0.1f, 0.4f, 1.0f),
		FLinearColor ColorB = FLinearColor(1.0f, 0.6f, 0.1f, 1.0f));
```

Default values in the signature become the node's default pin values in
Blueprint — a small kindness to your future self.

---

## Step 3 — The C++

Create **`Plugins/MyShaders/Source/MyShaders/Private/Swirl.cpp`**:

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

class FMySwirlCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMySwirlCS);
	SHADER_USE_PARAMETER_STRUCT(FMySwirlCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
		SHADER_PARAMETER(FUintVector2, TextureSize)

		// The C++ type on the left decides the HLSL type on the right.
		SHADER_PARAMETER(float,        Time)
		SHADER_PARAMETER(float,        Scale)
		SHADER_PARAMETER(FLinearColor, ColorA)
		SHADER_PARAMETER(FLinearColor, ColorB)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMySwirlCS,
	"/Plugin/MyShaders/Private/Swirl.usf",
	"MainCS",
	SF_Compute);

void UMyShadersLibrary::DrawSwirl(
	UTextureRenderTarget2D* OutputRT,
	float Time,
	float Scale,
	FLinearColor ColorA,
	FLinearColor ColorB)
{
	if (!OutputRT || !OutputRT->bCanCreateUAV)
	{
		UE_LOG(LogTemp, Error, TEXT("DrawSwirl: missing Render Target, or it does not support UAV."));
		return;
	}

	FTextureRenderTargetResource* Resource = OutputRT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	// Every value the shader needs is captured BY VALUE. This is how you move
	// data from the game thread to the render thread: copy it, never share it.
	ENQUEUE_RENDER_COMMAND(MyShaders_DrawSwirl)(
		[Resource, Time, Scale, ColorA, ColorB](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("MyShaders.Swirl.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			FMySwirlCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMySwirlCS::FParameters>();

			Parameters->OutTexture  = GraphBuilder.CreateUAV(OutputTexture);
			Parameters->TextureSize = FUintVector2(Size.X, Size.Y);
			Parameters->Time        = Time;
			Parameters->Scale       = Scale;
			Parameters->ColorA      = ColorA;
			Parameters->ColorB      = ColorB;

			TShaderMapRef<FMySwirlCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MyShaders.Swirl"),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize));

			GraphBuilder.Execute();
		});
}
```

Compare it with `Gradient.cpp`. Almost identical — four extra lines in the
parameter struct, four extra captures, four extra assignments. **That is the
whole job of adding a parameter**, and it is three edits in three places:

```
   .usf                 float Scale;
   FParameters          SHADER_PARAMETER(float, Scale)
   where you fill it    Parameters->Scale = Scale;
```

Miss the `.usf` line → shader won't compile. Miss the assignment → garbage value.
Miss the struct line → shader won't compile.

---

## Step 4 — Build and wire it up

Close the editor, build, reopen.

In the **Level Blueprint**, replace your BeginPlay wiring with:

```
   ┌──────────────┐          ┌─────────────────────────────┐
   │  Event Tick  │─────────►│  Draw Swirl                 │
   │            ▷ │          │    Output RT: RT_MyShaders  │
   └──────────────┘          │    Time:      ◄─────┐       │
                             │    Scale:     8.0   │       │
   ┌──────────────────────┐  │    Color A:   ■     │       │
   │ Get Game Time        │  │    Color B:   ■     │       │
   │   in Seconds  ───────┼──┴─────────────────────┘       │
   └──────────────────────┘  └─────────────────────────────┘
```

Right-click → **Get Game Time in Seconds** → plug its output into **Time**.

> ### ✅ Checkpoint
>
> Press Play. The pattern **moves** — a three-armed spiral sliding outward from
> the centre.
>
> If it renders but does not animate, `Time` is not connected, or you are still
> on `Event BeginPlay`.

---

## Why the shader doesn't just read the clock

`DrawSwirl` takes `Time` rather than calling a timer itself. That is deliberate:
a shader that is *handed* its time can be paused, scrubbed, stepped and tested.
One that reads the clock can only ever do one thing.

The same instinct applies everywhere in rendering — pass state in, don't reach
out for it.

---

## The types you can send

| C++ | HLSL |
|---|---|
| `float` | `float` |
| `int32` / `uint32` | `int` / `uint` |
| `FVector2f` | `float2` |
| `FVector3f` | `float3` |
| `FVector4f` / `FLinearColor` | `float4` |
| `FIntPoint` | `int2` |
| `FUintVector2` | `uint2` |
| `FMatrix44f` | `float4x4` |

> **Always the `f` types.** Shaders are single precision. Unreal's gameplay
> `FVector` and `FMatrix` are **double** precision. Use `FVector3f`, `FVector4f`,
> `FMatrix44f` in parameter structs, and convert explicitly at the boundary:
> ```cpp
> const FVector2f P(static_cast<float>(In.X), static_cast<float>(In.Y));
> ```

---

## Try it yourself

1. **Add a `Twist` parameter** and use it instead of the hard-coded `3.0` on
   `Angle`. Remember: all three places.

2. **Rings only.** Delete `+ Angle * 3.0f` from the `Wave` line.

3. **Spokes only.** Delete `Radius * Scale * 6.2831853f` instead.

4. **Change colours from Blueprint** while the game is running — they're
   parameters, so no rebuild, no shader recompile.

---

→ **Now read [Docs/02 - Parameters](../Docs/02-Parameters.md)** — especially the
part about 16-byte packing, which will matter the first time you send a
`float3`.

→ Next: **[05 - See The Threads](05-See-The-Threads.md)**
