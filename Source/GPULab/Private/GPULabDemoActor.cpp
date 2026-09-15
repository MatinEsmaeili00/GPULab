#include "GPULabDemoActor.h"
#include "GPULabCommon.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AGPULabDemoActor::AGPULabDemoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// An empty scene component as the root. See the header for why the plane must
	// not be the root itself.
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	DisplayPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayPlane"));
	DisplayPlane->SetupAttachment(Root);

	// The engine ships a 1x1 metre plane. Using it means this actor needs no
	// content of its own, so the plugin stays portable.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		DisplayPlane->SetStaticMesh(PlaneMesh.Object);
	}

	// The engine Plane lies flat facing up. Roll it 90 degrees to stand it up
	// like a picture on a wall, and scale it to 3m x 3m so it is easy to see.
	DisplayPlane->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
	DisplayPlane->SetRelativeScale3D(FVector(3.0f, 3.0f, 3.0f));
	DisplayPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AGPULabDemoActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureSetup();
}

void AGPULabDemoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Build everything and paint one frame straight away.
	//
	// This matters because the editor viewport only ticks actors when Realtime
	// is switched on (Ctrl+R). Without this, dropping the actor into a level
	// would show nothing at all until you turned Realtime on, which looks
	// exactly like a broken plugin.
	EnsureSetup();
	RunCurrentLesson(0.0f);
}

#if WITH_EDITOR
void AGPULabDemoActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Changed = PropertyChangedEvent.GetPropertyName();

	if (Changed == GET_MEMBER_NAME_CHECKED(AGPULabDemoActor, Resolution) ||
		Changed == GET_MEMBER_NAME_CHECKED(AGPULabDemoActor, DisplayMaterial) ||
		Changed == GET_MEMBER_NAME_CHECKED(AGPULabDemoActor, TextureParameterName))
	{
		InvalidateSetup();
	}

	if (Changed == GET_MEMBER_NAME_CHECKED(AGPULabDemoActor, Lesson))
	{
		// Starting a different lesson on top of another lesson's leftovers is
		// confusing, so wipe the simulation state.
		bRippleNeedsReset = true;
		bBufferMathPending = true;
	}

	if (Changed == GET_MEMBER_NAME_CHECKED(AGPULabDemoActor, BufferMathInput) ||
		Changed == GET_MEMBER_NAME_CHECKED(AGPULabDemoActor, BufferMathMultiplier))
	{
		bBufferMathPending = true;
	}

	// Repaint immediately so the viewport updates even with Realtime switched off.
	EnsureSetup();
	RunCurrentLesson(0.0f);
}
#endif

void AGPULabDemoActor::InvalidateSetup()
{
	RenderTarget = nullptr;
	DisplayMID = nullptr;
	BuiltResolution = 0;
	bRippleNeedsReset = true;
}

void AGPULabDemoActor::EnsureSetup()
{
	if (RenderTarget && BuiltResolution == Resolution && DisplayMID)
	{
		return;
	}

	// ---------------------------------------------------------------------
	// Building a Render Target in code.
	//
	// THE ORDER OF THESE LINES MATTERS. bCanCreateUAV is read when the GPU
	// resource is first created, which happens inside InitAutoFormat. Set it
	// afterwards and it has no effect - the texture exists by then, without a
	// UAV, and every compute lesson will refuse to run.
	// ---------------------------------------------------------------------
	RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	RenderTarget->RenderTargetFormat = RTF_RGBA16f;
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->bAutoGenerateMips = false;

	// <-- this line must come before InitAutoFormat
	RenderTarget->bCanCreateUAV = true;

	RenderTarget->InitAutoFormat(Resolution, Resolution);
	RenderTarget->UpdateResourceImmediate(true);

	BuiltResolution = Resolution;
	bRippleNeedsReset = true;

	// ---------------------------------------------------------------------
	// Point a material at it.
	// ---------------------------------------------------------------------
	if (!DisplayMaterial)
	{
		UE_LOG(LogGPULab, Warning,
			TEXT("%s: no Display Material set, so there is nothing to show the result on. ")
			TEXT("See Docs/08-Setup-In-Editor.md - you need a material with a Texture Parameter ")
			TEXT("called '%s' plugged into Emissive Color."),
			*GetName(), *TextureParameterName.ToString());
		return;
	}

	// A Dynamic Material Instance is a copy of a material you are allowed to
	// change at run time. You cannot set parameters on the original asset,
	// because every object using it would change too.
	DisplayMID = UMaterialInstanceDynamic::Create(DisplayMaterial, this);
	DisplayMID->SetTextureParameterValue(TextureParameterName, RenderTarget);
	DisplayPlane->SetMaterial(0, DisplayMID);
}

void AGPULabDemoActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureSetup();
	RunCurrentLesson(DeltaSeconds);
}

void AGPULabDemoActor::RunCurrentLesson(float DeltaSeconds)
{
	if (!RenderTarget)
	{
		return;
	}

	RunningTime += DeltaSeconds;

	switch (Lesson)
	{
	case EGPULabLesson::Lesson01_HelloCompute:
		UGPULabLibrary::Lesson01_HelloCompute(RenderTarget);
		break;

	case EGPULabLesson::Lesson02_Parameters:
		UGPULabLibrary::Lesson02_Parameters(RenderTarget, RunningTime, Scale, ColorA, ColorB);
		break;

	case EGPULabLesson::Lesson03_ThreadIDs:
		UGPULabLibrary::Lesson03_ThreadIDs(RenderTarget, ThreadView);
		break;

	case EGPULabLesson::Lesson04_BufferMath:
	{
		// Only once - see bBufferMathPending in the header for why.
		if (!bBufferMathPending)
		{
			break;
		}
		bBufferMathPending = false;

		TArray<float> Result;
		UGPULabLibrary::Lesson04_BufferMath(BufferMathInput, BufferMathMultiplier, Result);

		UE_LOG(LogGPULab, Log, TEXT("Lesson04: %d values, multiplier %.2f"), BufferMathInput.Num(), BufferMathMultiplier);
		for (int32 i = 0; i < Result.Num(); ++i)
		{
			UE_LOG(LogGPULab, Log, TEXT("Lesson04:   [%d]  %.3f  ->  %.3f"), i, BufferMathInput[i], Result[i]);
		}
		break;
	}

	case EGPULabLesson::Lesson05_MultiPassBlur:
		UGPULabLibrary::Lesson05_MultiPassBlur(RenderTarget, RunningTime, BlurRadius);
		break;

	case EGPULabLesson::Lesson06_FullscreenPS:
		UGPULabLibrary::Lesson06_FullscreenPixelShader(RenderTarget, RunningTime, Tint);
		break;

	case EGPULabLesson::Lesson07_Ripple:
	{
		// Drop a stone somewhere random every DropInterval seconds, so there is
		// always something to look at.
		TimeSinceLastDrop += DeltaSeconds;

		float DropStrength = 0.0f;
		FVector2D DropPosition(0.5, 0.5);

		// Always drop once on the first step, otherwise a freshly placed actor
		// shows a completely flat, completely black surface.
		if (TimeSinceLastDrop >= DropInterval || bRippleNeedsReset)
		{
			TimeSinceLastDrop = 0.0f;
			DropStrength = 0.6f;
			DropPosition = bRippleNeedsReset
				? FVector2D(0.5, 0.5)
				: FVector2D(FMath::FRandRange(0.2, 0.8), FMath::FRandRange(0.2, 0.8));
		}

		UGPULabLibrary::Lesson07_RippleStep(RenderTarget, Damping, DropPosition, DropStrength, bRippleNeedsReset);
		bRippleNeedsReset = false;
		break;
	}

	default:
		break;
	}
}
