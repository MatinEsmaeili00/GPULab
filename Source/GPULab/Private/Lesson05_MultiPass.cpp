// =============================================================================
// LESSON 5 - Two passes in one graph, and why RDG exists
//
// Up to now every lesson was a single pass, so RDG looked like pointless
// paperwork. This is the lesson where it starts paying for itself.
//
// We do two things in a row:
//   Pass A  draws moving dots into a SCRATCH texture
//   Pass B  reads that scratch texture, blurs it, writes the Render Target
//
// Pass B must not start until pass A has completely finished, and the GPU must
// be told that the scratch texture has changed from "being written" to "being
// read". On old-style graphics APIs you wrote that barrier by hand, and getting
// it wrong gave you flickering, or garbage, or a hang.
//
// You will notice there is no barrier code below. That is the point. RDG looks
// at which passes touch which resources, and inserts the barriers for you. You
// declared ScratchTexture as a UAV in pass A and as a plain texture in pass B;
// that is all the information it needed.
//
// The scratch texture is TRANSIENT: RDG creates it when the graph starts and
// throws it away when the graph ends. It never becomes an asset, it costs no
// permanent memory, and RDG will happily hand the same block of memory to some
// other pass later in the frame once we are done with it.
// =============================================================================

#include "GPULabLibrary.h"
#include "GPULabCommon.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"

static constexpr int32 kThreadGroupSize = 8;

// -----------------------------------------------------------------------------
// Pass A's shader
// -----------------------------------------------------------------------------
class FGPULabDotsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabDotsCS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabDotsCS, FGlobalShader);

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

// -----------------------------------------------------------------------------
// Pass B's shader
// -----------------------------------------------------------------------------
class FGPULabBlurCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabBlurCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Note the different macro. SHADER_PARAMETER_RDG_TEXTURE is a read-only
		// view; SHADER_PARAMETER_RDG_TEXTURE_UAV is read/write. Which macro you
		// pick is literally how RDG learns the direction of the dependency.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>,      BlurInput)
		SHADER_PARAMETER_SAMPLER(SamplerState,               BlurSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, BlurOut)
		SHADER_PARAMETER(FUintVector2, OutSize)
		SHADER_PARAMETER(int32,        BlurRadius)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// Two entry points, same file. The third argument is the function name, so this
// is how one .usf can hold a whole little library of shaders.
IMPLEMENT_GLOBAL_SHADER(FGPULabDotsCS, "/Plugin/GPULab/Private/Lesson05_MultiPass.usf", "DotsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGPULabBlurCS, "/Plugin/GPULab/Private/Lesson05_MultiPass.usf", "BlurCS", SF_Compute);

void UGPULabLibrary::Lesson05_MultiPassBlur(UTextureRenderTarget2D* OutputRT, float Time, int32 BlurRadius)
{
	FTextureRenderTargetResource* Resource = GPULab::GetWritableRTResource(OutputRT, TEXT("Lesson05"));
	if (!Resource)
	{
		return;
	}

	// A box blur costs (2r+1)^2 samples per pixel. At radius 16 that is 1089
	// texture reads for every single pixel, so clamp it before someone types 500.
	const int32 ClampedRadius = FMath::Clamp(BlurRadius, 0, 16);

	ENQUEUE_RENDER_COMMAND(GPULab_Lesson05)(
		[Resource, Time, ClampedRadius](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("GPULab.Lesson05.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			// ---------------------------------------------------------------
			// The scratch texture. It exists only inside this graph.
			//
			// The flags matter and are a common source of errors:
			//   TexCreate_UAV            - pass A needs to write it
			//   TexCreate_ShaderResource - pass B needs to read it
			// Leave one out and RDG will assert when you try to use it that way.
			// ---------------------------------------------------------------
			const FRDGTextureDesc ScratchDesc = FRDGTextureDesc::Create2D(
				Size,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef ScratchTexture = GraphBuilder.CreateTexture(ScratchDesc, TEXT("GPULab.Lesson05.Scratch"));

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize);

			// --- PASS A ------------------------------------------------------
			{
				FGPULabDotsCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FGPULabDotsCS::FParameters>();

				Parameters->ScratchOut  = GraphBuilder.CreateUAV(ScratchTexture);
				Parameters->ScratchSize = FUintVector2(Size.X, Size.Y);
				Parameters->Time        = Time;

				TShaderMapRef<FGPULabDotsCS> DotsShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPULab.Lesson05.PassA_Dots"),
					DotsShader,
					Parameters,
					GroupCount);
			}

			// --- PASS B ------------------------------------------------------
			{
				FGPULabBlurCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FGPULabBlurCS::FParameters>();

				// Same ScratchTexture pointer, but handed over read-only this
				// time. That single difference is what tells RDG "B depends on A".
				Parameters->BlurInput = ScratchTexture;

				// A sampler describes HOW to read a texture: how to blend between
				// texels (bilinear here) and what to do past the edges (clamp to
				// the last row/column, so the blur does not wrap around).
				// TStaticSamplerState objects are shared and free to ask for.
				Parameters->BlurSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

				Parameters->BlurOut    = GraphBuilder.CreateUAV(OutputTexture);
				Parameters->OutSize    = FUintVector2(Size.X, Size.Y);
				Parameters->BlurRadius = ClampedRadius;

				TShaderMapRef<FGPULabBlurCS> BlurShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPULab.Lesson05.PassB_Blur"),
					BlurShader,
					Parameters,
					GroupCount);
			}

			// Only now does RDG look at both passes, work out that B needs A,
			// insert the barrier between them, and record the real commands.
			GraphBuilder.Execute();
		});
}
