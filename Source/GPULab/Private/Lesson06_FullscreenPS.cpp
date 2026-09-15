// =============================================================================
// LESSON 6 - The other half of the GPU: the raster path
//
// Lessons 1-5 were all compute. This one is a DRAW.
//
// The difference in one table:
//
//                      COMPUTE                     RASTER (vertex + pixel)
//   what you say       "run this N times"          "draw these triangles"
//   who picks pixels   you do, via thread ID       the triangle's shape does
//   output goes to     a UAV you bound             the bound render target
//   C++ helper         FComputeShaderUtils         FPixelShaderUtils
//   needs UAV flag     yes                         no
//   shader type        SF_Compute                  SF_Vertex and SF_Pixel
//
// Notice the Render Target here does NOT need "Support UAV" ticked, because we
// are not writing through a UAV - we are drawing into it the normal way, the
// same way the whole engine draws everything else.
//
// We only write the pixel shader. Unreal supplies the vertex shader
// (FScreenVertexShaderVS) that puts one oversized triangle over the target.
// =============================================================================

#include "GPULabLibrary.h"
#include "GPULabCommon.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PixelShaderUtils.h"
#include "RenderingThread.h"

class FGPULabFullscreenPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabFullscreenPS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabFullscreenPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FLinearColor, Tint)
		SHADER_PARAMETER(float,        Time)
		SHADER_PARAMETER(FUintVector2, OutputSize)

		// This line is the one that makes it a raster pass.
		//
		// It adds hidden members that say WHICH textures the pixel shader draws
		// into, and what to do with whatever was already there. RDG refuses to
		// build a raster pass without it. There is no equivalent in a compute
		// pass, because compute has no "bound render target" concept at all.
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// SF_Pixel, not SF_Compute. That last argument is how Unreal knows which stage
// of the pipeline to compile this for.
IMPLEMENT_GLOBAL_SHADER(
	FGPULabFullscreenPS,
	"/Plugin/GPULab/Private/Lesson06_FullscreenPS.usf",
	"MainPS",
	SF_Pixel);

void UGPULabLibrary::Lesson06_FullscreenPixelShader(UTextureRenderTarget2D* OutputRT, float Time, FLinearColor Tint)
{
	// Note: the drawable check, not the writable one. No UAV needed.
	FTextureRenderTargetResource* Resource = GPULab::GetDrawableRTResource(OutputRT, TEXT("Lesson06"));
	if (!Resource)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(GPULab_Lesson06)(
		[Resource, Time, Tint](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("GPULab.Lesson06.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			FGPULabFullscreenPS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FGPULabFullscreenPS::FParameters>();

			Parameters->Tint       = Tint;
			Parameters->Time       = Time;
			Parameters->OutputSize = FUintVector2(Size.X, Size.Y);

			// Bind the render target in slot 0. The load action decides what
			// happens to the existing contents before we draw:
			//   ELoad     - keep them (we draw on top). Always safe.
			//   EClear    - wipe to the texture's clear colour first.
			//   ENoAction - "I promise to overwrite every pixel". Fastest, but if
			//               you lie you get whatever garbage was in memory.
			Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

			TShaderMapRef<FGPULabFullscreenPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			// The raster equivalent of FComputeShaderUtils::AddPass.
			// Instead of a group count it takes a VIEWPORT - the rectangle of the
			// render target we are allowed to draw into.
			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(GMaxRHIFeatureLevel),
				RDG_EVENT_NAME("GPULab.Lesson06.FullscreenPS"),
				PixelShader,
				Parameters,
				FIntRect(0, 0, Size.X, Size.Y));

			GraphBuilder.Execute();
		});
}
