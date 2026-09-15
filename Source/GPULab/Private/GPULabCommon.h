// Small helpers shared by every lesson. Nothing clever here - it is just the
// boring safety checks pulled out so each lesson file stays readable.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGPULab, Log, All);

namespace GPULab
{
	/**
	 * Checks a Render Target is usable by a compute shader and hands back the
	 * render-thread object that owns the actual GPU texture.
	 *
	 * Returns nullptr and explains the problem in the log if something is wrong.
	 *
	 * The "Support UAV" check matters: a compute shader writes through a UAV
	 * (Unordered Access View - "a view that lets any thread write anywhere").
	 * Unreal only creates that view if the asset was told to allow it, and the
	 * flag is read when the Render Target is first built, so ticking it later in
	 * the editor is fine but ticking it from code after the fact is not.
	 */
	inline FTextureRenderTargetResource* GetWritableRTResource(UTextureRenderTarget2D* RT, const TCHAR* LessonName)
	{
		if (!RT)
		{
			UE_LOG(LogGPULab, Error, TEXT("%s: no Render Target was given."), LessonName);
			return nullptr;
		}

		if (!RT->bCanCreateUAV)
		{
			UE_LOG(LogGPULab, Error,
				TEXT("%s: Render Target '%s' does not support UAV. Open the asset and tick 'Support UAV' ")
				TEXT("(under Texture Render Target 2D), then save it."),
				LessonName, *RT->GetName());
			return nullptr;
		}

		FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			UE_LOG(LogGPULab, Error, TEXT("%s: Render Target '%s' has no GPU resource yet."), LessonName, *RT->GetName());
			return nullptr;
		}

		return Resource;
	}

	/** Same as above but for lessons that only DRAW into the target (Lesson 6), where no UAV is needed. */
	inline FTextureRenderTargetResource* GetDrawableRTResource(UTextureRenderTarget2D* RT, const TCHAR* LessonName)
	{
		if (!RT)
		{
			UE_LOG(LogGPULab, Error, TEXT("%s: no Render Target was given."), LessonName);
			return nullptr;
		}

		FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			UE_LOG(LogGPULab, Error, TEXT("%s: Render Target '%s' has no GPU resource yet."), LessonName, *RT->GetName());
			return nullptr;
		}

		return Resource;
	}
}
