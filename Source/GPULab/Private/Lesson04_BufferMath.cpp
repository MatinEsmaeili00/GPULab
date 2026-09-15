// =============================================================================
// LESSON 4 - Buffers in, buffers out, and getting answers back
//
// Two new ideas.
//
// 1. BUFFERS. A texture is for pictures. A buffer is just an array in GPU
//    memory. You meet them in two flavours:
//      SRV (Shader Resource View)  - read only.  HLSL: StructuredBuffer<T>
//      UAV (Unordered Access View) - read/write. HLSL: RWStructuredBuffer<T>
//    "View" is the key word. The buffer is the memory; the view is the
//    permission slip that says how a shader is allowed to touch it.
//
// 2. READBACK. Getting data back from the GPU is slow and awkward, because the
//    GPU normally runs one to three frames behind the game thread. To read a
//    result immediately you have to stop and wait for it, which throws that head
//    start away. This lesson does exactly that, on purpose, so you can see the
//    numbers in Blueprint - but read the "do not ship this" note below.
// =============================================================================

#include "GPULabLibrary.h"
#include "GPULabCommon.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"

static constexpr int32 kThreadGroupSize1D = 64;

class FGPULabBufferMathCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPULabBufferMathCS);
	SHADER_USE_PARAMETER_STRUCT(FGPULabBufferMathCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Read-only array. The type in brackets must match the .usf exactly.
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>,   InputValues)

		// Read/write array - where the answers go.
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputValues)

		SHADER_PARAMETER(float,  Multiplier)
		SHADER_PARAMETER(uint32, NumElements)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FGPULabBufferMathCS,
	"/Plugin/GPULab/Private/Lesson04_BufferMath.usf",
	"MainCS",
	SF_Compute);

void UGPULabLibrary::Lesson04_BufferMath(const TArray<float>& Input, float Multiplier, TArray<float>& OutResult)
{
	OutResult.Reset();

	const int32 NumElements = Input.Num();
	if (NumElements <= 0)
	{
		UE_LOG(LogGPULab, Warning, TEXT("Lesson04: the Input array is empty, nothing to do."));
		return;
	}

	// Make room for the answers up front. We hand the render thread a raw pointer
	// into this array. That is only safe because FlushRenderingCommands() at the
	// bottom guarantees the render thread has finished before we return.
	OutResult.SetNumZeroed(NumElements);
	float* ResultPtr = OutResult.GetData();

	// Copy the input. The render thread runs later, and the array we were handed
	// may well be gone by then.
	TArray<float> InputCopy = Input;

	ENQUEUE_RENDER_COMMAND(GPULab_Lesson04)(
		[InputCopy = MoveTemp(InputCopy), Multiplier, NumElements, ResultPtr](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			// --- the input buffer -------------------------------------------
			// CreateStructuredBuffer both allocates GPU memory AND schedules the
			// upload of our CPU data into it. Arguments are:
			//   name, size of one element, how many, pointer to data, total bytes
			FRDGBufferRef InputBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("GPULab.Lesson04.InputBuffer"),
				sizeof(float),
				NumElements,
				InputCopy.GetData(),
				sizeof(float) * NumElements);

			// --- the output buffer ------------------------------------------
			// This one starts empty. RDG owns it, and it stops existing once the
			// graph finishes - which is why we must copy the results out below.
			FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), NumElements),
				TEXT("GPULab.Lesson04.OutputBuffer"));

			FGPULabBufferMathCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FGPULabBufferMathCS::FParameters>();

			// CreateSRV = "let the shader read this"
			// CreateUAV = "let the shader read and write this"
			Parameters->InputValues  = GraphBuilder.CreateSRV(InputBuffer);
			Parameters->OutputValues = GraphBuilder.CreateUAV(OutputBuffer);
			Parameters->Multiplier   = Multiplier;
			Parameters->NumElements  = static_cast<uint32>(NumElements);

			TShaderMapRef<FGPULabBufferMathCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			// 1D dispatch: how many 64-wide groups cover NumElements?
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(
				FIntVector(NumElements, 1, 1), FIntVector(kThreadGroupSize1D, 1, 1));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GPULab.Lesson04.BufferMath"),
				ComputeShader,
				Parameters,
				GroupCount);

			// --- ask for the results back ------------------------------------
			// A readback is a staging buffer in memory that the CPU can actually
			// see. AddEnqueueCopyPass adds one more pass to the graph that copies
			// the output buffer into it, AFTER the compute pass. RDG works that
			// ordering out by itself, because it can see we used OutputBuffer in
			// both passes.
			FRHIGPUBufferReadback Readback(TEXT("GPULab.Lesson04.Readback"));
			AddEnqueueCopyPass(GraphBuilder, &Readback, OutputBuffer, sizeof(float) * NumElements);

			GraphBuilder.Execute();

			// ================================================================
			// DO NOT SHIP THIS LINE.
			//
			// SubmitAndBlockUntilGPUIdle stops this thread dead until the GPU has
			// finished absolutely everything. In a real game that costs several
			// milliseconds every single time and wrecks your frame rate.
			//
			// The grown-up version is: kick the work off, keep the readback object
			// alive, and poll Readback.IsReady() on a later frame. You get the
			// answer two or three frames late and you never stall. Most GPU
			// systems in Unreal work that way.
			//
			// We stall here only so a Blueprint "Print String" can show you the
			// numbers on the very same frame.
			// ================================================================
			RHICmdList.SubmitAndBlockUntilGPUIdle();

			// Lock gives us a CPU pointer to the copied data. Always Unlock after.
			if (const float* GPUData = static_cast<const float*>(Readback.Lock(sizeof(float) * NumElements)))
			{
				FMemory::Memcpy(ResultPtr, GPUData, sizeof(float) * NumElements);
			}
			Readback.Unlock();
		});

	// Wait for the render thread to reach the end of the lambda above. Same
	// warning as the GPU stall above: fine for a lesson, awful in a shipping game.
	FlushRenderingCommands();
}
