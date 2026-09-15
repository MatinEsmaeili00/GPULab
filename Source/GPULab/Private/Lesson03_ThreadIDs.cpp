// =============================================================================
// LESSON 3 - Threads, groups and dispatches
//
// This is the mental model that everything else rests on. Three layers:
//
//   THREAD    one instance of your shader function. Runs once, for one pixel /
//             one array element / one particle. Millions of them exist.
//
//   GROUP     a fixed-size box of threads declared by [numthreads(X, Y, Z)].
//             Threads in the same group can talk to each other through fast
//             "groupshared" memory and can wait for each other. Threads in
//             DIFFERENT groups cannot - assume they run in any order, or at the
//             same time, or on different parts of the chip.
//
//   DISPATCH  how many groups you launch, decided by C++ at AddPass time.
//             Total threads = GroupCount * ThreadsPerGroup.
//
// Worked example, the one used in this lesson:
//   texture   256 x 256 pixels
//   numthreads(8, 8, 1)  -> 64 threads per group
//   group count          -> 256/8 = 32 across, 32 down = 1024 groups
//   total threads        -> 1024 * 64 = 65,536 = exactly one per pixel
//
// The four system values you can ask for in HLSL:
//   SV_DispatchThreadID  global position   = GroupID * numthreads + GroupThreadID
//   SV_GroupID           which group
//   SV_GroupThreadID     position inside the group
//   SV_GroupIndex        that same position flattened to a single number
//
// Run this lesson with each View setting and the pictures explain it better
// than any paragraph can.
// =============================================================================

#include "GPULabLibrary.h"
#include "GPULabCommon.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"

static constexpr int32 kThreadGroupSize = 8;

class FGPULabThreadIDsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabThreadIDsCS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabThreadIDsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
		SHADER_PARAMETER(FUintVector2, TextureSize)
		SHADER_PARAMETER(uint32,       ViewMode)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// The .usf writes [numthreads(THREADS, THREADS, 1)] using this define, so
		// C++ and HLSL can never disagree about the group size.
		OutEnvironment.SetDefine(TEXT("GPULAB_THREADS_XY"), kThreadGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FGPULabThreadIDsCS,
	"/Plugin/GPULab/Private/Lesson03_ThreadIDs.usf",
	"MainCS",
	SF_Compute);

void UGPULabLibrary::Lesson03_ThreadIDs(UTextureRenderTarget2D* OutputRT, EGPULabThreadView View)
{
	FTextureRenderTargetResource* Resource = GPULab::GetWritableRTResource(OutputRT, TEXT("Lesson03"));
	if (!Resource)
	{
		return;
	}

	const uint32 ViewMode = static_cast<uint32>(View);

	ENQUEUE_RENDER_COMMAND(GPULab_Lesson03)(
		[Resource, ViewMode](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("GPULab.Lesson03.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			FGPULabThreadIDsCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FGPULabThreadIDsCS::FParameters>();

			Parameters->OutTexture  = GraphBuilder.CreateUAV(OutputTexture);
			Parameters->TextureSize = FUintVector2(Size.X, Size.Y);
			Parameters->ViewMode    = ViewMode;

			TShaderMapRef<FGPULabThreadIDsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			// This is the dispatch size. Read it as: "how many 8x8 boxes do I need
			// to cover a Size.X by Size.Y image?"
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize);

			UE_LOG(LogGPULab, Log,
				TEXT("Lesson03: texture %dx%d, groups %dx%d, threads/group %d, total threads %d"),
				Size.X, Size.Y,
				GroupCount.X, GroupCount.Y,
				kThreadGroupSize * kThreadGroupSize,
				GroupCount.X * GroupCount.Y * kThreadGroupSize * kThreadGroupSize);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GPULab.Lesson03.ThreadIDs"),
				ComputeShader,
				Parameters,
				GroupCount);

			GraphBuilder.Execute();
		});
}
