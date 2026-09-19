# 09 - Make It Remember

**Goal:** a water simulation. This frame's water depends on last frame's water,
and it all lives on the GPU.

**Time:** about 30 minutes.

**Reference:** [`Lesson07_Ripple.usf`](../Shaders/Private/Lesson07_Ripple.usf) ·
[`Lesson07_Ripple.cpp`](../Source/GPULab/Private/Lesson07_Ripple.cpp)

Everything so far started from nothing each frame. A simulation can't.

---

## Two problems

### Problem 1 — you can't read and write the same texture

Threads run in an unpredictable order:

```
  thread A reads its neighbour B's height   ─┐
                                             ├── who goes first? nobody knows
  thread B writes its own new height        ─┘
```

A gets the old value or the new one depending on luck, and the luck differs every
frame, on every GPU. Your simulation turns to noise.

**The fix: ping-pong.** Two textures. Read one, write the other, swap. Every
texture is read-only or write-only within a pass, never both.

```
   frame 1:   [ A ] ──read──►  shader  ──write──►  [ B ]
   frame 2:   [ B ] ──read──►  shader  ──write──►  [ A ]
```

### Problem 2 — RDG throws its textures away

Normally that's the point (chapter 7). Here we need one to survive.

**The fix: `QueueTextureExtraction`.** "When the graph finishes, hand me this one
instead of recycling it."

---

## Step 1 — The shader

Create **`Plugins/MyShaders/Shaders/Private/Ripple.usf`**:

```hlsl
#include "/Engine/Public/Platform.ush"

// The state texture holds two numbers per pixel, not a colour:
//   .x = height   (how high the water is here)
//   .y = velocity (how fast it is moving up or down)
Texture2D<float2>   StateIn;    // last frame - READ ONLY
RWTexture2D<float2> StateOut;   // this frame - WRITE ONLY

uint2  SimSize;
float  Damping;
float2 DropPosition;   // 0..1 across the texture
float  DropStrength;   // 0 means "no poke this frame"

float2 LoadState(int2 Coord)
{
	Coord = clamp(Coord, int2(0, 0), int2(SimSize) - 1);
	// .Load() reads one exact texel, no filtering, no sampler.
	// The third component of the int3 is the mip level.
	return StateIn.Load(int3(Coord, 0));
}

[numthreads(8, 8, 1)]
void SimulateCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	uint2 PixelCoord = DispatchThreadId.xy;
	if (PixelCoord.x >= SimSize.x || PixelCoord.y >= SimSize.y)
	{
		return;
	}

	int2 Coord = int2(PixelCoord);

	float2 Self     = LoadState(Coord);
	float  Height   = Self.x;
	float  Velocity = Self.y;

	float Left  = LoadState(Coord + int2(-1,  0)).x;
	float Right = LoadState(Coord + int2( 1,  0)).x;
	float Up    = LoadState(Coord + int2( 0, -1)).x;
	float Down  = LoadState(Coord + int2( 0,  1)).x;

	// The wave equation, and it is simpler than it sounds.
	// "Laplacian" just means: how far am I from the average of my neighbours?
	// Lower than everyone around me -> positive -> I get pushed up.
	float Laplacian = (Left + Right + Up + Down) - 4.0f * Height;

	// Acceleration pushes velocity, velocity pushes height. Newton, but
	// sideways and everywhere at once.
	Velocity += Laplacian * 0.25f;

	// Without damping the ripples never stop and the numbers eventually explode.
	Velocity *= Damping;
	Height   += Velocity;

	// Poke the water.
	if (DropStrength > 0.0f)
	{
		float2 UV   = (float2(PixelCoord) + 0.5f) / float2(SimSize);
		float  Blob = 1.0f - smoothstep(0.0f, 0.02f, length(UV - DropPosition));
		Height += Blob * DropStrength;
	}

	StateOut[PixelCoord] = float2(Height, Velocity);
}

// ---------------------------------------------------------------------------
// Turn height into a picture.
// ---------------------------------------------------------------------------
Texture2D<float2>   VisIn;
RWTexture2D<float4> VisOut;
uint2 VisSize;

[numthreads(8, 8, 1)]
void VisualiseCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	uint2 PixelCoord = DispatchThreadId.xy;
	if (PixelCoord.x >= VisSize.x || PixelCoord.y >= VisSize.y)
	{
		return;
	}

	int2 Coord    = int2(PixelCoord);
	int2 MaxCoord = int2(VisSize) - 1;

	float Height = VisIn.Load(int3(Coord, 0)).x;

	// Fake lighting: compare heights left-vs-right and up-vs-down to get the
	// slope, then pretend a light shines from the top-left. Same trick a normal
	// map uses.
	float Lx = VisIn.Load(int3(clamp(Coord + int2(-1, 0), int2(0,0), MaxCoord), 0)).x;
	float Rx = VisIn.Load(int3(clamp(Coord + int2( 1, 0), int2(0,0), MaxCoord), 0)).x;
	float Uy = VisIn.Load(int3(clamp(Coord + int2( 0,-1), int2(0,0), MaxCoord), 0)).x;
	float Dy = VisIn.Load(int3(clamp(Coord + int2( 0, 1), int2(0,0), MaxCoord), 0)).x;

	float2 Slope = float2(Rx - Lx, Dy - Uy);
	float  Light = saturate(0.5f + dot(Slope, normalize(float2(-1.0f, -1.0f))) * 4.0f);

	float3 Deep    = float3(0.02f, 0.06f, 0.20f);
	float3 Shallow = float3(0.30f, 0.85f, 1.00f);

	float3 Color = lerp(Deep, Shallow, saturate(Height * 4.0f + 0.5f)) * Light;

	VisOut[PixelCoord] = float4(Color, 1.0f);
}
```

---

## Step 2 — Declare it

In **`MyShadersLibrary.h`**:

```cpp
	/**
	 * One step of a water ripple simulation. Call it every frame.
	 * Drop Strength > 0 pokes the water at Drop Position (0..1).
	 */
	UFUNCTION(BlueprintCallable, Category = "MyShaders")
	static void RippleStep(
		UTextureRenderTarget2D* OutputRT,
		float Damping = 0.997f,
		FVector2D DropPosition = FVector2D(0.5, 0.5),
		float DropStrength = 0.0f,
		bool bReset = false);
```

---

## Step 3 — The C++

Create **`Plugins/MyShaders/Source/MyShaders/Private/Ripple.cpp`**:

```cpp
#include "MyShadersLibrary.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RenderingThread.h"

static constexpr int32 kThreadGroupSize = 8;

// ---------------------------------------------------------------------------
// The surviving state.
//
// A file-static global normally deserves a raised eyebrow. It's acceptable here
// for one reason: it is only ever touched inside ENQUEUE_RENDER_COMMAND lambdas,
// so only the render thread ever sees it, so there is no race.
//
// In a real project hang this off a UWorldSubsystem or the component that owns
// the effect. A global means ONE simulation for the whole process, which breaks
// the moment you want two.
// ---------------------------------------------------------------------------
static TRefCountPtr<IPooledRenderTarget> GRippleState;

class FMyRippleSimCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMyRippleSimCS);
	SHADER_USE_PARAMETER_STRUCT(FMyRippleSimCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>,       StateIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, StateOut)
		SHADER_PARAMETER(FUintVector2, SimSize)
		SHADER_PARAMETER(float,        Damping)
		SHADER_PARAMETER(FVector2f,    DropPosition)
		SHADER_PARAMETER(float,        DropStrength)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FMyRippleVisCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMyRippleVisCS);
	SHADER_USE_PARAMETER_STRUCT(FMyRippleVisCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>,       VisIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VisOut)
		SHADER_PARAMETER(FUintVector2, VisSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMyRippleSimCS, "/Plugin/MyShaders/Private/Ripple.usf", "SimulateCS",  SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FMyRippleVisCS, "/Plugin/MyShaders/Private/Ripple.usf", "VisualiseCS", SF_Compute);

void UMyShadersLibrary::RippleStep(
	UTextureRenderTarget2D* OutputRT,
	float Damping,
	FVector2D DropPosition,
	float DropStrength,
	bool bReset)
{
	if (!OutputRT || !OutputRT->bCanCreateUAV)
	{
		UE_LOG(LogTemp, Error, TEXT("RippleStep: missing Render Target, or no UAV support."));
		return;
	}

	FTextureRenderTargetResource* Resource = OutputRT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	// Above 1.0 the wave gains energy every step and explodes within seconds.
	const float ClampedDamping = FMath::Clamp(Damping, 0.90f, 1.0f);

	// FVector2D is double precision; shaders are single. Convert explicitly.
	const FVector2f Drop(static_cast<float>(DropPosition.X), static_cast<float>(DropPosition.Y));

	ENQUEUE_RENDER_COMMAND(MyShaders_RippleStep)(
		[Resource, ClampedDamping, Drop, DropStrength, bReset]
		(FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder, Resource->GetRenderTargetTexture(), TEXT("MyShaders.Ripple.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			// PF_G32R32F = two 32-bit floats per pixel. We're storing physics,
			// not colour, so precision matters more than memory. A 16-bit format
			// would slowly drift and ruin the simulation after a few hundred
			// frames - and it looks like "the physics is wrong", not "the format
			// is wrong", which makes it a horrible bug to find.
			const FRDGTextureDesc StateDesc = FRDGTextureDesc::Create2D(
				Size,
				PF_G32R32F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			// Three cases, and all three bite in real code:
			//   1. first ever run - nothing stored
			//   2. reset requested
			//   3. the render target was RESIZED - stored texture is wrong size
			const bool bHaveState =
				GRippleState.IsValid() && GRippleState->GetDesc().Extent == Size;

			FRDGTextureRef StateIn;

			if (bReset || !bHaveState)
			{
				StateIn = GraphBuilder.CreateTexture(StateDesc, TEXT("MyShaders.Ripple.Init"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StateIn), FLinearColor::Black);
			}
			else
			{
				// Hand last frame's survivor back to RDG as an ordinary texture.
				StateIn = GraphBuilder.RegisterExternalTexture(GRippleState, TEXT("MyShaders.Ripple.Prev"));
			}

			// Always write into a NEW texture. This is the ping-pong.
			FRDGTextureRef StateOut =
				GraphBuilder.CreateTexture(StateDesc, TEXT("MyShaders.Ripple.Next"));

			const FIntVector GroupCount =
				FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize);

			// --- PASS 1: simulate ---
			{
				FMyRippleSimCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FMyRippleSimCS::FParameters>();

				Parameters->StateIn      = StateIn;
				Parameters->StateOut     = GraphBuilder.CreateUAV(StateOut);
				Parameters->SimSize      = FUintVector2(Size.X, Size.Y);
				Parameters->Damping      = ClampedDamping;
				Parameters->DropPosition = Drop;
				Parameters->DropStrength = DropStrength;

				TShaderMapRef<FMyRippleSimCS> SimShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder, RDG_EVENT_NAME("MyShaders.Ripple.Simulate"),
					SimShader, Parameters, GroupCount);
			}

			// --- PASS 2: draw it ---
			{
				FMyRippleVisCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FMyRippleVisCS::FParameters>();

				Parameters->VisIn   = StateOut;   // read what we just wrote
				Parameters->VisOut  = GraphBuilder.CreateUAV(OutputTexture);
				Parameters->VisSize = FUintVector2(Size.X, Size.Y);

				TShaderMapRef<FMyRippleVisCS> VisShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder, RDG_EVENT_NAME("MyShaders.Ripple.Visualise"),
					VisShader, Parameters, GroupCount);
			}

			// --- keep the state alive for next frame ---
			// Without this line StateOut is recycled the instant Execute()
			// finishes, GRippleState stays empty, and every frame starts from
			// flat water. You'd see one flash and nothing else.
			GraphBuilder.QueueTextureExtraction(StateOut, &GRippleState);

			GraphBuilder.Execute();
		});
}
```

---

## Step 4 — Build and drive it

In the Level Blueprint, on **Event Tick**, call **Ripple Step** with your render
target. To drop stones, add a **Timer** or just use a random check:

```
   Event Tick ──► Ripple Step
                    Output RT:     RT_MyShaders
                    Damping:       0.997
                    Drop Position: (Random 0.2-0.8, Random 0.2-0.8)
                    Drop Strength: 0.6 every ~40 frames, otherwise 0.0
                    Reset:         false
```

The simplest version: a **Random Float in Range** 0–1 into a **< 0.02** branch;
when true, pass Drop Strength `0.6`, otherwise `0.0`.

> ### ✅ Checkpoint
>
> Rings spread outward from each drop, bounce off the edges, cross through each
> other, and slowly fade.
>
> **Watch several drops at different ages at once.** That's the proof: older
> rings are wider and fainter than newer ones, which can only happen if the
> state survived from frame to frame.

---

## Try it yourself

1. **Delete the `QueueTextureExtraction` line.** You get one flash and then
   nothing, forever. Put it back. **Now you'll recognise that symptom for life.**

2. **Set Damping to 1.0.** Watch it slowly go unstable. This is what
   "numerically unstable" looks like, and it's worth seeing once.

3. **Change the wave speed.** `Velocity += Laplacian * 0.25f` → try `0.1` (slow,
   syrupy) and `0.5` (fast, sharp). At about `0.6` it explodes — you've broken
   the stability limit of this integration scheme.

4. **Resize the render target while it's running.** The `GetDesc().Extent == Size`
   check catches it and restarts cleanly. Then comment that check out and try
   again.

5. **Simulate twice per frame.** Add both passes to the graph a second time,
   feeding step one's output into step two's input. Ripples move twice as fast
   and stay stable. That's *substepping*, and it's how real simulations get
   speed without losing stability.

6. **Make it Conway's Game of Life.** Same structure, different rules — swap
   `PF_G32R32F` for `PF_R32_FLOAT`, count live neighbours, apply the rules.
   Everything else stays identical.

   **If you can do this one, you have learned the pattern rather than the
   example.** That's the whole goal of the tutorial.

---

→ **Now read [Docs/07 - Persistent Simulation](../Docs/07-Persistent-Simulation.md)**.

→ Last: **[10 - Where To Go Next](10-Where-To-Go-Next.md)**
