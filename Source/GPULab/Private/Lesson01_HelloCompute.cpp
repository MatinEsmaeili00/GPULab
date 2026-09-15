// =============================================================================
// LESSON 1 - Hello Compute
//
// Goal: get one compute shader running and see its output on screen.
//
// Read this file top to bottom. There are only three parts:
//   PART 1  describe the shader to C++   (the FGlobalShader class)
//   PART 2  connect the class to the .usf file  (IMPLEMENT_GLOBAL_SHADER)
//   PART 3  actually run it              (the RDG pass)
// =============================================================================

#include "GPULabLibrary.h"
#include "GPULabCommon.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"

// The group size from the .usf file. We need the same number on the C++ side to
// work out how many groups to launch, and it MUST match [numthreads(8, 8, 1)].
// Getting these out of sync is the single most common compute-shader bug.
static constexpr int32 kThreadGroupSize = 8;

// =============================================================================
// PART 1 - Describe the shader to C++
//
// A "global shader" is a shader that is not attached to any material or mesh.
// It is just a program you can run whenever you like. That is what we want.
// =============================================================================
class FGPULabHelloComputeCS : public FGlobalShader
{
public:
	// Registers the type with Unreal's shader system. Always the class name.
	DECLARE_GLOBAL_SHADER(FGPULabHelloComputeCS);

	// Says "my inputs are described by the FParameters struct below, generate the
	// plumbing for me". Without this you would bind every parameter by hand.
	SHADER_USE_PARAMETER_STRUCT(FGPULabHelloComputeCS, FGlobalShader);

	// -------------------------------------------------------------------------
	// The parameter struct. This is the contract between C++ and HLSL.
	//
	// Each line here must have a matching global variable in the .usf file with
	// the SAME NAME. Unreal matches them by name, not by order.
	// -------------------------------------------------------------------------
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// A texture we write to. The first argument is the HLSL type, written
		// exactly as it appears in the shader.
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)

		// A plain value. FUintVector2 on the C++ side becomes uint2 in HLSL.
		SHADER_PARAMETER(FUintVector2, TextureSize)
	END_SHADER_PARAMETER_STRUCT()

	// Should this shader be built for a given platform? Returning false here
	// saves build time on platforms that could never run it. SM5 is a safe floor
	// for "has real compute support".
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// =============================================================================
// PART 2 - Connect the C++ class to the actual shader file
//
// Arguments are:
//   the class, the virtual path to the .usf, the entry function name, the type.
//
// "/Plugin/GPULab" is the virtual folder we registered in GPULabModule.cpp.
// SF_Compute means "this is a compute shader".
// =============================================================================
IMPLEMENT_GLOBAL_SHADER(
	FGPULabHelloComputeCS,
	"/Plugin/GPULab/Private/Lesson01_HelloCompute.usf",
	"MainCS",
	SF_Compute);

// =============================================================================
// PART 3 - Run it
// =============================================================================
void UGPULabLibrary::Lesson01_HelloCompute(UTextureRenderTarget2D* OutputRT)
{
	// --- still on the GAME thread here ---
	FTextureRenderTargetResource* Resource = GPULab::GetWritableRTResource(OutputRT, TEXT("Lesson01"));
	if (!Resource)
	{
		return;
	}

	// Rendering work cannot happen on the game thread. We hand a job over to the
	// render thread instead. Everything the lambda uses must be copied in by
	// value - never capture a UObject pointer and touch it later, it may be
	// garbage collected before the render thread gets there.
	ENQUEUE_RENDER_COMMAND(GPULab_Lesson01)(
		[Resource](FRHICommandListImmediate& RHICmdList)
		{
			// --- now on the RENDER thread ---

			// FRDGBuilder is the Render Dependency Graph. You do not issue GPU
			// commands directly any more. You describe passes, and at Execute()
			// time RDG works out the order, the memory, and the barriers.
			FRDGBuilder GraphBuilder(RHICmdList);

			// Our Render Target already exists outside the graph, so we "register"
			// it - we tell RDG about a texture it did not create. The name is what
			// shows up in RenderDoc and in `r.RDG.Debug` output.
			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("GPULab.Lesson01.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			// Allocate the parameter struct. Note: GraphBuilder.AllocParameters(),
			// NOT a plain local variable. RDG needs to own this memory because the
			// pass runs later, after this function has already returned.
			FGPULabHelloComputeCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FGPULabHelloComputeCS::FParameters>();

			// Fill in the contract from PART 1.
			Parameters->OutTexture  = GraphBuilder.CreateUAV(OutputTexture);
			Parameters->TextureSize = FUintVector2(Size.X, Size.Y);

			// Look up the compiled shader. GetGlobalShaderMap gives you every
			// global shader built for the current feature level.
			TShaderMapRef<FGPULabHelloComputeCS> ComputeShader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));

			// How many GROUPS do we need? The shader does 8x8 pixels per group,
			// so for a 256x256 texture that is 32x32 groups.
			// DivideAndRoundUp matters: 250 pixels / 8 = 31.25, and 31 groups
			// would leave the last 2 pixel columns unpainted.
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(
				Size, kThreadGroupSize);

			// Add the pass to the graph. Nothing has run on the GPU yet.
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GPULab.Lesson01.HelloCompute"),
				ComputeShader,
				Parameters,
				GroupCount);

			// NOW the graph is compiled and the real GPU commands are recorded.
			GraphBuilder.Execute();
		});
}
