// GPULab - the menu of lessons.
//
// This is a Blueprint Function Library. Every lesson gets one static function
// here, and each function is implemented in its own .cpp file so you can read one
// lesson at a time without anything else getting in the way.
//
//   Lesson01_HelloCompute.cpp   - the smallest possible compute shader
//   Lesson02_Parameters.cpp     - sending numbers and colors from C++ to the GPU
//   Lesson03_ThreadIDs.cpp      - what a thread, a group, and a dispatch actually are
//   Lesson04_BufferMath.cpp     - buffers in, buffers out, and reading results back
//   Lesson05_MultiPass.cpp      - two passes in one RDG graph, second reads the first
//   Lesson06_FullscreenPS.cpp   - the other kind of shader: vertex + pixel (raster)
//   Lesson07_Ripple.cpp         - keeping state alive across frames (a simulation)

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GPULabLibrary.generated.h"

class UTextureRenderTarget2D;

/** Which value Lesson 3 should paint into the texture, so you can see it. */
UENUM(BlueprintType)
enum class EGPULabThreadView : uint8
{
	/** Colour = the thread's global X,Y position across the whole image. */
	DispatchThreadID	UMETA(DisplayName = "Dispatch Thread ID (global position)"),
	/** Colour = which group this thread is in. You see big blocks. */
	GroupID				UMETA(DisplayName = "Group ID (which block)"),
	/** Colour = the thread's position inside its own group. You see a repeating tile. */
	GroupThreadID		UMETA(DisplayName = "Group Thread ID (position inside block)"),
	/** Colour = the flat 0..63 index of the thread inside its group. */
	GroupIndex			UMETA(DisplayName = "Group Index (flat number inside block)"),
};

UCLASS()
class GPULAB_API UGPULabLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------
	// Lesson 1 - Hello Compute
	// ---------------------------------------------------------------------
	/**
	 * Runs the smallest useful compute shader: it paints a red/green gradient
	 * into a Render Target, one pixel per GPU thread.
	 *
	 * The Render Target MUST have "Support UAV" ticked in its asset settings,
	 * because the shader writes to it directly.
	 */
	UFUNCTION(BlueprintCallable, Category = "GPULab|01 Hello Compute")
	static void Lesson01_HelloCompute(UTextureRenderTarget2D* OutputRT);

	// ---------------------------------------------------------------------
	// Lesson 2 - Parameters
	// ---------------------------------------------------------------------
	/**
	 * Same idea as Lesson 1, but now C++ sends values to the shader: a time, a
	 * zoom level, and two colours. Call this every frame with an increasing Time
	 * and the pattern animates.
	 */
	UFUNCTION(BlueprintCallable, Category = "GPULab|02 Parameters")
	static void Lesson02_Parameters(
		UTextureRenderTarget2D* OutputRT,
		float Time = 0.0f,
		float Scale = 8.0f,
		FLinearColor ColorA = FLinearColor(0.05f, 0.1f, 0.4f, 1.0f),
		FLinearColor ColorB = FLinearColor(1.0f, 0.6f, 0.1f, 1.0f));

	// ---------------------------------------------------------------------
	// Lesson 3 - Threads, groups, dispatches
	// ---------------------------------------------------------------------
	/**
	 * Paints the GPU's own bookkeeping numbers into the texture so you can
	 * literally see how the work was split up. Switch View to compare them.
	 */
	UFUNCTION(BlueprintCallable, Category = "GPULab|03 Threads And Groups")
	static void Lesson03_ThreadIDs(
		UTextureRenderTarget2D* OutputRT,
		EGPULabThreadView View = EGPULabThreadView::DispatchThreadID);

	// ---------------------------------------------------------------------
	// Lesson 4 - Buffers in, buffers out, results back on the CPU
	// ---------------------------------------------------------------------
	/**
	 * Sends an array of floats to the GPU, multiplies every one of them in
	 * parallel, and brings the answers back to the CPU so Blueprint can print
	 * them.
	 *
	 * This one STALLS the game thread waiting for the GPU. That is fine for
	 * learning and terrible in a real game - Lesson 4's doc explains why.
	 */
	UFUNCTION(BlueprintCallable, Category = "GPULab|04 Buffers")
	static void Lesson04_BufferMath(
		const TArray<float>& Input,
		float Multiplier,
		TArray<float>& OutResult);

	// ---------------------------------------------------------------------
	// Lesson 5 - Two passes in one graph
	// ---------------------------------------------------------------------
	/**
	 * Pass A draws some dots into a scratch texture that only exists inside the
	 * graph. Pass B blurs that scratch texture into your Render Target.
	 * RDG works out on its own that B must wait for A.
	 */
	UFUNCTION(BlueprintCallable, Category = "GPULab|05 Multi Pass")
	static void Lesson05_MultiPassBlur(
		UTextureRenderTarget2D* OutputRT,
		float Time = 0.0f,
		int32 BlurRadius = 4);

	// ---------------------------------------------------------------------
	// Lesson 6 - The other kind of shader: raster (vertex + pixel)
	// ---------------------------------------------------------------------
	/**
	 * Draws one big triangle over the whole Render Target and colours it with a
	 * pixel shader. No UAV needed here - this is the classic "draw" path rather
	 * than the compute path.
	 */
	UFUNCTION(BlueprintCallable, Category = "GPULab|06 Pixel Shader")
	static void Lesson06_FullscreenPixelShader(
		UTextureRenderTarget2D* OutputRT,
		float Time = 0.0f,
		FLinearColor Tint = FLinearColor::White);

	// ---------------------------------------------------------------------
	// Lesson 7 - State that survives between frames
	// ---------------------------------------------------------------------
	/**
	 * A little water-ripple simulation. Call it every frame. It keeps two hidden
	 * textures alive between calls and swaps them - "ping-pong" - because a
	 * shader cannot read and write the same texture at the same time.
	 *
	 * DropStrength > 0 pokes the water at DropPosition (0..1 across the texture).
	 */
	UFUNCTION(BlueprintCallable, Category = "GPULab|07 Simulation")
	static void Lesson07_RippleStep(
		UTextureRenderTarget2D* OutputRT,
		float Damping = 0.995f,
		FVector2D DropPosition = FVector2D(0.5, 0.5),
		float DropStrength = 0.0f,
		bool bReset = false);
};
