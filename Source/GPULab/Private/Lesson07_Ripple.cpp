// =============================================================================
// LESSON 7 - State that survives between frames
//
// Every lesson so far started from nothing and finished inside one graph. A
// simulation cannot do that: this frame's water needs last frame's water.
//
// Two problems to solve.
//
// PROBLEM 1: a shader cannot safely read and write the same texture.
//   Threads run in an unpredictable order. If thread A reads its neighbour's
//   height while thread B is writing that same height, A gets either the old or
//   the new value depending on pure luck, and your simulation turns to noise.
//
//   The fix is PING-PONG: keep two textures. Read from one, write to the other,
//   then swap them next frame. Read-only and write-only, never both.
//
// PROBLEM 2: RDG throws its textures away when the graph finishes.
//   That is normally the point. Here we need one to survive.
//
//   The fix is QueueTextureExtraction. It tells RDG "when you are done, hand
//   this texture to me instead of recycling it". You get back a
//   TRefCountPtr<IPooledRenderTarget> that you can keep, and feed into next
//   frame's graph with RegisterExternalTexture.
//
// This file does the ping-pong with a single stored pointer: the texture we
// extracted last frame becomes this frame's read-only input, and we write into
// a brand new one. Same effect, less bookkeeping.
// =============================================================================

#include "GPULabLibrary.h"
#include "GPULabCommon.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RenderingThread.h"

static constexpr int32 kThreadGroupSize = 8;

// -----------------------------------------------------------------------------
// The surviving state.
//
// This is a global, which normally deserves a raised eyebrow. It is acceptable
// here for one reason: it is only ever touched inside ENQUEUE_RENDER_COMMAND
// lambdas, so only the render thread ever sees it, so there is no race.
//
// In a real project you would hang this off your own scene-view extension or a
// world subsystem rather than a file-static.
// -----------------------------------------------------------------------------
static TRefCountPtr<IPooledRenderTarget> GRippleState;

// -----------------------------------------------------------------------------
// Pass 1 - move the simulation forward one step.
// -----------------------------------------------------------------------------
class FGPULabRippleSimCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabRippleSimCS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabRippleSimCS, FGlobalShader);

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

// -----------------------------------------------------------------------------
// Pass 2 - turn the state into a picture.
// -----------------------------------------------------------------------------
class FGPULabRippleVisCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabRippleVisCS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabRippleVisCS, FGlobalShader);

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

IMPLEMENT_GLOBAL_SHADER(FGPULabRippleSimCS, "/Plugin/GPULab/Private/Lesson07_Ripple.usf", "SimulateCS",  SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGPULabRippleVisCS, "/Plugin/GPULab/Private/Lesson07_Ripple.usf", "VisualiseCS", SF_Compute);

void UGPULabLibrary::Lesson07_RippleStep(
	UTextureRenderTarget2D* OutputRT,
	float Damping,
	FVector2D DropPosition,
	float DropStrength,
	bool bReset)
{
	FTextureRenderTargetResource* Resource = GPULab::GetWritableRTResource(OutputRT, TEXT("Lesson07"));
	if (!Resource)
	{
		return;
	}

	// Above 1.0 the wave gains energy every step and blows up to infinity within
	// a second or two, which shows up as a flashing mess.
	const float ClampedDamping = FMath::Clamp(Damping, 0.90f, 1.0f);
	const FVector2f Drop(static_cast<float>(DropPosition.X), static_cast<float>(DropPosition.Y));

	ENQUEUE_RENDER_COMMAND(GPULab_Lesson07)(
		[Resource, ClampedDamping, Drop, DropStrength, bReset](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("GPULab.Lesson07.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			// PF_G32R32F = two 32-bit floats per pixel. We are storing physics
			// numbers, not colours, so precision matters more than memory here;
			// a 16-bit format would slowly drift and ruin the simulation.
			const FRDGTextureDesc StateDesc = FRDGTextureDesc::Create2D(
				Size,
				PF_G32R32F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			// Do we have usable state from last frame? Resizing the Render Target
			// invalidates it, so check the size too.
			const bool bHaveState =
				GRippleState.IsValid() &&
				GRippleState->GetDesc().Extent == Size;

			FRDGTextureRef StateIn;

			if (bReset || !bHaveState)
			{
				// First run (or a reset): make a fresh texture and zero it, so the
				// water starts perfectly flat.
				StateIn = GraphBuilder.CreateTexture(StateDesc, TEXT("GPULab.Lesson07.StateInit"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StateIn), FLinearColor::Black);
			}
			else
			{
				// Hand last frame's survivor back to RDG as an ordinary texture.
				StateIn = GraphBuilder.RegisterExternalTexture(GRippleState, TEXT("GPULab.Lesson07.StatePrev"));
			}

			// Always write into a NEW texture. This is the ping-pong.
			FRDGTextureRef StateOut = GraphBuilder.CreateTexture(StateDesc, TEXT("GPULab.Lesson07.StateNext"));

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize);

			// --- PASS 1: simulate ---------------------------------------------
			{
				FGPULabRippleSimCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FGPULabRippleSimCS::FParameters>();

				Parameters->StateIn      = StateIn;
				Parameters->StateOut     = GraphBuilder.CreateUAV(StateOut);
				Parameters->SimSize      = FUintVector2(Size.X, Size.Y);
				Parameters->Damping      = ClampedDamping;
				Parameters->DropPosition = Drop;
				Parameters->DropStrength = DropStrength;

				TShaderMapRef<FGPULabRippleSimCS> SimShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPULab.Lesson07.Simulate"),
					SimShader,
					Parameters,
					GroupCount);
			}

			// --- PASS 2: draw it ----------------------------------------------
			{
				FGPULabRippleVisCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FGPULabRippleVisCS::FParameters>();

				Parameters->VisIn   = StateOut;   // read the state we just wrote
				Parameters->VisOut  = GraphBuilder.CreateUAV(OutputTexture);
				Parameters->VisSize = FUintVector2(Size.X, Size.Y);

				TShaderMapRef<FGPULabRippleVisCS> VisShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPULab.Lesson07.Visualise"),
					VisShader,
					Parameters,
					GroupCount);
			}

			// --- keep the state alive for next frame ---------------------------
			// Without this line StateOut is recycled the instant Execute()
			// finishes, GRippleState stays empty, and every frame starts from
			// flat water - you would see a single flash and nothing else.
			GraphBuilder.QueueTextureExtraction(StateOut, &GRippleState);

			GraphBuilder.Execute();
		});
}
