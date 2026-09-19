# 06 - Arrays In, Arrays Out

**Goal:** send a list of numbers to the GPU, do maths on all of them at once, and
get the answers back on the CPU.

**Time:** about 25 minutes.

**Reference:** [`Lesson04_BufferMath.usf`](../Shaders/Private/Lesson04_BufferMath.usf) ·
[`Lesson04_BufferMath.cpp`](../Source/GPULab/Private/Lesson04_BufferMath.cpp)

No picture in this chapter. Not all GPU work is graphics.

---

## Two new ideas

### Buffers

| | Texture | Buffer |
|---|---|---|
| Shape | 2D (or 3D, or cube) | a flat array |
| Reading | `.Load()` or `.Sample()` | `Buf[index]` |
| Filtering | yes — bilinear, mips, wrapping | no, it's just memory |
| Good for | images, grids, anything spatial | lists, particles, physics, counters |

### SRV and UAV

```
        THE RESOURCE                    THE VIEW
   (the actual GPU memory)       (what you're allowed to do with it)

   ┌─────────────────────┐       ┌──────────────────────────────┐
   │                     │◄──────│ SRV - Shader Resource View   │
   │   a block of bytes  │       │      read only               │
   │                     │       └──────────────────────────────┘
   │                     │       ┌──────────────────────────────┐
   │                     │◄─────►│ UAV - Unordered Access View  │
   │                     │       │      read AND write          │
   └─────────────────────┘       └──────────────────────────────┘
```

A **view** is a permission slip. The memory is the resource; the view says how a
shader may touch it. You have been using a UAV since chapter 2 without meeting
the word properly.

| | HLSL | C++ macro | C++ creation |
|---|---|---|---|
| Read only | `StructuredBuffer<T>` | `SHADER_PARAMETER_RDG_BUFFER_SRV` | `GraphBuilder.CreateSRV(Buf)` |
| Read/write | `RWStructuredBuffer<T>` | `SHADER_PARAMETER_RDG_BUFFER_UAV` | `GraphBuilder.CreateUAV(Buf)` |

---

## Step 1 — The shader

Create **`Plugins/MyShaders/Shaders/Private/BufferMath.usf`**:

```hlsl
#include "/Engine/Public/Platform.ush"

// "Structured" means the GPU knows how big one element is, so InputValues[7]
// just works instead of you doing byte maths by hand.
StructuredBuffer<float>   InputValues;    // read only  (SRV)
RWStructuredBuffer<float> OutputValues;   // read/write (UAV)

float Multiplier;
uint  NumElements;

// A 1D problem, so a 1D group: 64 threads in a row.
[numthreads(64, 1, 1)]
void MainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	uint Index = DispatchThreadId.x;

	// Same guard as always. 100 numbers still means 2 groups = 128 threads.
	if (Index >= NumElements)
	{
		return;
	}

	// Every thread handles exactly one element, all at the same time.
	// There is no loop here. The parallelism IS the loop.
	OutputValues[Index] = InputValues[Index] * Multiplier;
}
```

---

## Step 2 — Declare it

In **`MyShadersLibrary.h`**, inside the class:

```cpp
	/**
	 * Multiplies every number in Input by Multiplier, on the GPU, and brings
	 * the answers back.
	 *
	 * WARNING: this stalls the game thread on purpose. Fine for learning,
	 * terrible in a real game - see the doc.
	 */
	UFUNCTION(BlueprintCallable, Category = "MyShaders")
	static void MultiplyOnGPU(
		const TArray<float>& Input,
		float Multiplier,
		TArray<float>& OutResult);
```

A `TArray<T>&` output parameter becomes a return pin in Blueprint.

---

## Step 3 — The C++

Create **`Plugins/MyShaders/Source/MyShaders/Private/BufferMath.cpp`**:

```cpp
#include "MyShadersLibrary.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"

static constexpr int32 kThreadGroupSize1D = 64;

class FMyBufferMathCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMyBufferMathCS);
	SHADER_USE_PARAMETER_STRUCT(FMyBufferMathCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>,   InputValues)
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
	FMyBufferMathCS,
	"/Plugin/MyShaders/Private/BufferMath.usf",
	"MainCS",
	SF_Compute);

void UMyShadersLibrary::MultiplyOnGPU(
	const TArray<float>& Input,
	float Multiplier,
	TArray<float>& OutResult)
{
	OutResult.Reset();

	const int32 NumElements = Input.Num();
	if (NumElements <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("MultiplyOnGPU: the Input array is empty."));
		return;
	}

	// Make room for the answers, and hand the render thread a raw pointer into
	// it. Safe ONLY because FlushRenderingCommands() at the bottom guarantees
	// the render thread has finished before we return.
	OutResult.SetNumZeroed(NumElements);
	float* ResultPtr = OutResult.GetData();

	// Copy the input - the render thread runs later, and the caller's array may
	// be gone by then.
	TArray<float> InputCopy = Input;

	ENQUEUE_RENDER_COMMAND(MyShaders_MultiplyOnGPU)(
		[InputCopy = MoveTemp(InputCopy), Multiplier, NumElements, ResultPtr]
		(FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			// Allocates GPU memory AND schedules the upload of our CPU data.
			// Arguments: name, size of one element, how many, pointer, total bytes.
			FRDGBufferRef InputBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("MyShaders.InputBuffer"),
				sizeof(float),
				NumElements,
				InputCopy.GetData(),
				sizeof(float) * NumElements);

			// Starts empty. RDG owns it and recycles it when the graph ends -
			// which is why we must copy the answers out below.
			FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), NumElements),
				TEXT("MyShaders.OutputBuffer"));

			FMyBufferMathCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMyBufferMathCS::FParameters>();

			Parameters->InputValues  = GraphBuilder.CreateSRV(InputBuffer);
			Parameters->OutputValues = GraphBuilder.CreateUAV(OutputBuffer);
			Parameters->Multiplier   = Multiplier;
			Parameters->NumElements  = static_cast<uint32>(NumElements);

			TShaderMapRef<FMyBufferMathCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			// 1D dispatch: how many 64-wide groups cover NumElements?
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(
				FIntVector(NumElements, 1, 1), FIntVector(kThreadGroupSize1D, 1, 1));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MyShaders.BufferMath"),
				ComputeShader,
				Parameters,
				GroupCount);

			// A readback is a staging buffer the CPU can actually see.
			// AddEnqueueCopyPass adds ANOTHER pass that copies our output into
			// it - and RDG works out by itself that it must run after the
			// compute pass, because both passes touch OutputBuffer.
			FRHIGPUBufferReadback Readback(TEXT("MyShaders.Readback"));
			AddEnqueueCopyPass(GraphBuilder, &Readback, OutputBuffer, sizeof(float) * NumElements);

			GraphBuilder.Execute();

			// ============================================================
			// DO NOT SHIP THIS LINE.
			// It stops this thread dead until the GPU has finished everything.
			// In a real game that costs milliseconds every call and wrecks your
			// frame rate. We do it only so Print String can show the numbers on
			// the same frame. The grown-up version is at the bottom of this page.
			// ============================================================
			RHICmdList.SubmitAndBlockUntilGPUIdle();

			if (const float* GPUData = static_cast<const float*>(
					Readback.Lock(sizeof(float) * NumElements)))
			{
				FMemory::Memcpy(ResultPtr, GPUData, sizeof(float) * NumElements);
			}
			Readback.Unlock();
		});

	// Wait for the render thread. Same warning as above.
	FlushRenderingCommands();
}
```

---

## Step 4 — Build and test

Build, restart. In the Level Blueprint on **BeginPlay**:

1. Add a **Make Array** node of floats — put in `1, 2, 3, 4, 5`.
2. Add **Multiply on GPU**. Wire the array into **Input**, set **Multiplier** to
   `10`.
3. Drag from **Out Result** → **For Each Loop** → **Print String** on the element.

> ### ✅ Checkpoint
>
> Press Play. The screen prints:
>
> ```
> 10.0
> 20.0
> 30.0
> 40.0
> 50.0
> ```
>
> Those numbers were computed on your graphics card and carried back to the CPU.

---

## Why getting answers back is awkward

```
  Game thread      frame 12   ->   frame 13   ->   frame 14
  Render thread               frame 11   ->   frame 12   ->   frame 13
  GPU                                    frame 10   ->   frame 11
```

The GPU runs **one to three frames behind**. That lag is deliberate — it keeps
everything busy. But it means when you ask "what's the answer?", the honest reply
is "I haven't started yet."

**Option A — stop and wait.** What this chapter does. Correct, simple, and it
throws away all the pipelining.

**Option B — ask now, collect later.** What real code does:

1. Kick the work off. Keep the `FRHIGPUBufferReadback` object alive somewhere.
2. On later frames, poll `Readback.IsReady()`.
3. When true, `Lock()`, read, `Unlock()`.

You get the answer two or three frames late and you never stall. Everything in
Unreal that reads GPU results back works this way — occlusion queries, GPU
particle counts, Nanite stats.

> **Rule of thumb: design so you never need the answer this frame.**

---

## Try it yourself

1. **Feed it 1,000,000 numbers.** It still returns quickly — that's a million
   multiplications at once. Then notice it's probably *slower* than the CPU would
   be, because the upload, the stall and the readback dominate. **The GPU only
   wins when the maths is heavy enough to pay for the trip.**

2. **Make the maths expensive.** Replace the multiply with `sin(cos(x)*100)` in a
   50-iteration loop. Now compare against the CPU again. *This* is the shape of
   problem the GPU is for.

3. **Add a second output.** Give the shader `RWStructuredBuffer<float> OutputSquares`
   and write `InputValues[Index] * InputValues[Index]` into it. You need a second
   buffer, a second UAV, and a second readback.

---

→ **Now read [Docs/04 - Buffers and Readback](../Docs/04-Buffers-And-Readback.md)**.

→ Next: **[07 - Two Passes In One Graph](07-Two-Passes.md)** — where RDG starts
earning its keep.
