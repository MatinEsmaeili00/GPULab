// =============================================================================
// A one-click way to SEE the lessons.
//
// Drop this actor in a level, pick a lesson in the Details panel, and it shows
// up on a plane. It runs in the editor viewport - you do not have to press Play.
//
// It also answers a question the lessons themselves skip over: where does the
// Render Target come from? Here it is built in C++ at run time, which is worth
// reading because the order of the lines matters (see the .cpp).
// =============================================================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GPULabLibrary.h"
#include "GPULabDemoActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EGPULabLesson : uint8
{
	Lesson01_HelloCompute	UMETA(DisplayName = "01 - Hello Compute"),
	Lesson02_Parameters		UMETA(DisplayName = "02 - Parameters"),
	Lesson03_ThreadIDs		UMETA(DisplayName = "03 - Threads And Groups"),
	Lesson04_BufferMath		UMETA(DisplayName = "04 - Buffer Math (no picture - read the Output Log)"),
	Lesson05_MultiPassBlur	UMETA(DisplayName = "05 - Multi Pass Blur"),
	Lesson06_FullscreenPS	UMETA(DisplayName = "06 - Fullscreen Pixel Shader"),
	Lesson07_Ripple			UMETA(DisplayName = "07 - Ripple Simulation"),
};

UCLASS(Blueprintable, meta = (DisplayName = "GPULab Demo"))
class GPULAB_API AGPULabDemoActor : public AActor
{
	GENERATED_BODY()

public:
	AGPULabDemoActor();

	/**
	 * The actor's root.
	 *
	 * The plane is deliberately NOT the root. A root component's "relative"
	 * transform IS the actor's world transform, so the rotation and scale we set
	 * on it in the constructor would be wiped out the moment anyone moved the
	 * actor. Giving the plane a plain scene component to hang off keeps the two
	 * independent.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GPULab")
	TObjectPtr<USceneComponent> Root;

	/** The plane the Render Target is shown on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GPULab")
	TObjectPtr<UStaticMeshComponent> DisplayPlane;

	/** Which lesson to run every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab")
	EGPULabLesson Lesson = EGPULabLesson::Lesson01_HelloCompute;

	/** Size of the Render Target, in pixels, both directions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab", meta = (ClampMin = "16", ClampMax = "2048"))
	int32 Resolution = 512;

	/**
	 * A material with a Texture Parameter to show the result through.
	 * Doc 08 walks through making one. Leave it empty and this actor will tell
	 * you what to do in the log rather than silently showing nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab")
	TObjectPtr<UMaterialInterface> DisplayMaterial;

	/** The name of the Texture Parameter inside that material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab")
	FName TextureParameterName = TEXT("GPULabTexture");

	// --- per-lesson knobs ---------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 02")
	float Scale = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 02")
	FLinearColor ColorA = FLinearColor(0.05f, 0.10f, 0.40f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 02")
	FLinearColor ColorB = FLinearColor(1.00f, 0.60f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 03")
	EGPULabThreadView ThreadView = EGPULabThreadView::DispatchThreadID;

	/** The numbers Lesson 04 sends to the GPU. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 04")
	TArray<float> BufferMathInput = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 04")
	float BufferMathMultiplier = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 05", meta = (ClampMin = "0", ClampMax = "16"))
	int32 BlurRadius = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 06")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 07", meta = (ClampMin = "0.9", ClampMax = "1.0"))
	float Damping = 0.997f;

	/** How often Lesson 07 drops a new stone in the water, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPULab|Lesson 07", meta = (ClampMin = "0.1"))
	float DropInterval = 1.2f;

	/** The Render Target this actor made. Read-only, but handy to inspect. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GPULab")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	virtual void Tick(float DeltaSeconds) override;
	virtual void PostInitializeComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Ticking in the editor viewport is what lets you see this without pressing Play. */
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	/** Builds the Render Target and the dynamic material. Safe to call repeatedly. */
	void EnsureSetup();

	/** Dispatches whichever lesson is selected. */
	void RunCurrentLesson(float DeltaSeconds);

	/** Throws the current Render Target away so the next tick rebuilds it. */
	void InvalidateSetup();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMID;

	float RunningTime = 0.0f;
	float TimeSinceLastDrop = 0.0f;
	bool  bRippleNeedsReset = true;

	/**
	 * Lesson 04 stalls the game thread on purpose, so we run it once when asked
	 * rather than every frame. Running a blocking readback every tick would drag
	 * the whole editor to a crawl - which is itself a useful thing to know.
	 */
	bool  bBufferMathPending = true;

	int32 BuiltResolution = 0;
};
