// =============================================================================
// LESSON 2 - Parameters: getting CPU values onto the GPU
//
// Lesson 1's shader was hard-coded. This one takes a time, a zoom and two
// colours from C++. That is the whole lesson: what the parameter struct really
// is and how a value gets from a Blueprint pin into an HLSL variable.
//
// The journey of a single float:
//   Blueprint pin  ->  C++ function argument
//                  ->  copied into FParameters on the render thread
//                  ->  RDG packs the whole struct into a uniform buffer
//                  ->  uniform buffer is uploaded to GPU memory
//                  ->  HLSL reads it as a global variable
//
// A uniform buffer is just a small block of constants that every thread in the
// dispatch can read but none can write. It is the cheap way to pass values.
// =============================================================================

#include "GPULabLibrary.h"
#include "GPULabCommon.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"

static constexpr int32 kThreadGroupSize = 8;

class FGPULabParametersCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabParametersCS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabParametersCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
		SHADER_PARAMETER(FUintVector2, TextureSize)

		// The C++ type on the left decides the HLSL type on the right:
		//   float        -> float
		//   FVector2f    -> float2
		//   FVector3f    -> float3    (careful, this one has padding rules)
		//   FVector4f    -> float4
		//   FLinearColor -> float4
		//   int32        -> int
		//   FIntPoint    -> int2
		//   FUintVector2 -> uint2
		//   FMatrix44f   -> float4x4
		//
		// Note the "f" suffix types. Shaders are single precision, so always use
		// FVector3f / FVector4f / FMatrix44f here, never the double-precision
		// FVector / FMatrix that gameplay code uses.
		SHADER_PARAMETER(float,        Time)
		SHADER_PARAMETER(float,        Scale)
		SHADER_PARAMETER(FLinearColor, ColorA)
		SHADER_PARAMETER(FLinearColor, ColorB)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// This one is new. It runs at SHADER COMPILE time and lets you inject #define
	// lines into the HLSL before it is built. It is how you write one .usf file
	// that builds several different ways.
	//
	// Here we push the C++ group size into the shader as a #define, so the two
	// sides can never drift apart. (Lesson 2's .usf does not use it yet - see the
	// doc for how to switch [numthreads(8,8,1)] over to it.)
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("GPULAB_THREADS_XY"), kThreadGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FGPULabParametersCS,
	"/Plugin/GPULab/Private/Lesson02_Parameters.usf",
	"MainCS",
	SF_Compute);

void UGPULabLibrary::Lesson02_Parameters(
	UTextureRenderTarget2D* OutputRT,
	float Time,
	float Scale,
	FLinearColor ColorA,
	FLinearColor ColorB)
{
	FTextureRenderTargetResource* Resource = GPULab::GetWritableRTResource(OutputRT, TEXT("Lesson02"));
	if (!Resource)
	{
		return;
	}

	// Every value the shader needs is captured BY VALUE into the lambda. This is
	// the standard way to move data from the game thread to the render thread:
	// copy it, do not share it.
	ENQUEUE_RENDER_COMMAND(GPULab_Lesson02)(
		[Resource, Time, Scale, ColorA, ColorB](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutputTexture = RegisterExternalTexture(
				GraphBuilder,
				Resource->GetRenderTargetTexture(),
				TEXT("GPULab.Lesson02.Output"));

			const FIntPoint Size = OutputTexture->Desc.Extent;

			FGPULabParametersCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FGPULabParametersCS::FParameters>();

			Parameters->OutTexture  = GraphBuilder.CreateUAV(OutputTexture);
			Parameters->TextureSize = FUintVector2(Size.X, Size.Y);
			Parameters->Time        = Time;
			Parameters->Scale       = Scale;
			Parameters->ColorA      = ColorA;
			Parameters->ColorB      = ColorB;

			TShaderMapRef<FGPULabParametersCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GPULab.Lesson02.Parameters"),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(Size, kThreadGroupSize));

			GraphBuilder.Execute();
		});
}
