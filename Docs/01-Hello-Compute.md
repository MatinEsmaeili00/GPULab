# 01 - Hello Compute

**Files:** [`Lesson01_HelloCompute.usf`](../Shaders/Private/Lesson01_HelloCompute.usf) ·
[`Lesson01_HelloCompute.cpp`](../Source/GPULab/Private/Lesson01_HelloCompute.cpp)

**Blueprint node:** `Lesson 01 Hello Compute (Output RT)`

**Result:** the Render Target fills with a red/green gradient - black in the top-left,
red to the right, green downward, yellow in the bottom-right.

---

## What we are building

One compute shader. It runs once per pixel, and each run writes one colour.

That's it. But it contains every piece you will ever need, so we go through all of
them slowly.

---

## Part 1: the shader (`.usf`)

```hlsl
#include "/Engine/Public/Platform.ush"

RWTexture2D<float4> OutTexture;
uint2 TextureSize;

[numthreads(8, 8, 1)]
void MainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint2 PixelCoord = DispatchThreadId.xy;

    if (PixelCoord.x >= TextureSize.x || PixelCoord.y >= TextureSize.y)
        return;

    float2 UV = (float2(PixelCoord) + 0.5f) / float2(TextureSize);
    OutTexture[PixelCoord] = float4(UV.x, UV.y, 0.0f, 1.0f);
}
```

Line by line:

**`#include "/Engine/Public/Platform.ush"`**
Always start with this. It papers over the differences between DirectX, Vulkan,
Metal and consoles. Note the path starts with a slash - shader includes use
*virtual* paths, not file paths.

**`RWTexture2D<float4> OutTexture;`**
`RW` = read-write. This is the output image. In C++ terms this is a **UAV**. The
`<float4>` says each pixel is four floats (red, green, blue, alpha).

**`uint2 TextureSize;`**
A plain input value. Just a global variable.

**`[numthreads(8, 8, 1)]`**
"One group of work is 8 threads wide, 8 tall, 1 deep." So 64 threads per group.
Lesson 3 goes into what a group is. For now: 64 is a sensible default.

**`uint3 DispatchThreadId : SV_DispatchThreadID`**
The part after the colon is a **semantic** - it tells the hardware to fill this
variable in for us. `SV_DispatchThreadID` is this thread's unique position across
the whole launch. For a 256x256 image, `.x` and `.y` each run 0 to 255.

So `DispatchThreadId.xy` *is* our pixel coordinate. One thread, one pixel.

**The bounds check.**
This is not optional. We launch whole groups only. If the image is 250 pixels
wide, we still launch 32 groups = 256 threads per row. The last 6 threads have no
pixel to write. Without the `return` they would write outside the texture, which
is undefined behaviour - corruption, or a GPU crash.

> **Rule: every compute shader that writes to a fixed-size thing needs a bounds check.**

**`float2 UV = (float2(PixelCoord) + 0.5f) / float2(TextureSize);`**
Turns a pixel index into a 0..1 coordinate. The `+ 0.5` moves us to the *centre*
of the pixel instead of its top-left corner. Pixel 0 covers the area from 0.0 to
1.0, so its centre is at 0.5.

**`OutTexture[PixelCoord] = ...`**
Writing to a UAV looks exactly like writing to an array. No locking, no ordering
guarantees - and we don't need any, because each thread writes to its own pixel
and nobody else's.

---

## Part 2: the C++ shader class

```cpp
class FGPULabHelloComputeCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FGPULabHelloComputeCS);
    SHADER_USE_PARAMETER_STRUCT(FGPULabHelloComputeCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
        SHADER_PARAMETER(FUintVector2, TextureSize)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
```

**This class contains no shader code.** It is a *description* of the shader in the
`.usf` file. Think of it as a header file for the GPU.

**`DECLARE_GLOBAL_SHADER`** registers the type with Unreal's shader system.

**`SHADER_USE_PARAMETER_STRUCT`** says "my inputs are the `FParameters` struct
below - generate all the binding code for me." Without it you would bind each
parameter by hand.

**The parameter struct is the contract.** Look at these two lines side by side:

```
  .usf:   RWTexture2D<float4> OutTexture;
  .cpp:   SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
                                           ^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^
                                           same HLSL type       same name
```

Unreal matches these **by name, as text**. Not by order, not by type. Which means:

> **A typo in a parameter name gives you a shader that compiles perfectly and
> silently does nothing.** There is no error. This is the single most common way
> to lose an afternoon.

**`ShouldCompilePermutation`** decides which platforms get this shader built.
Returning `false` saves build time. `SM5` is a safe floor meaning "has proper
compute support".

---

## Part 3: connecting them

```cpp
IMPLEMENT_GLOBAL_SHADER(
    FGPULabHelloComputeCS,                                  // the class
    "/Plugin/GPULab/Private/Lesson01_HelloCompute.usf",     // the file
    "MainCS",                                               // the entry function
    SF_Compute);                                            // the shader stage
```

`/Plugin/GPULab` is a **virtual path**. It exists because `GPULabModule.cpp` said so
at startup:

```cpp
AddShaderSourceDirectoryMapping(TEXT("/Plugin/GPULab"), ShaderDir);
```

That mapping must happen *before* any shader compiling starts, which is why
`GPULab.uplugin` uses `"LoadingPhase": "PostConfigInit"`. If you forget that, you
get a startup crash saying the virtual shader path was not found.

---

## Part 4: running it

```cpp
void UGPULabLibrary::Lesson01_HelloCompute(UTextureRenderTarget2D* OutputRT)
{
    // --- GAME THREAD ---
    FTextureRenderTargetResource* Resource = GPULab::GetWritableRTResource(OutputRT, TEXT("Lesson01"));
    if (!Resource) return;

    ENQUEUE_RENDER_COMMAND(GPULab_Lesson01)(
        [Resource](FRHICommandListImmediate& RHICmdList)
        {
            // --- RENDER THREAD ---
            FRDGBuilder GraphBuilder(RHICmdList);

            FRDGTextureRef OutputTexture = RegisterExternalTexture(
                GraphBuilder, Resource->GetRenderTargetTexture(), TEXT("GPULab.Lesson01.Output"));

            const FIntPoint Size = OutputTexture->Desc.Extent;

            auto* Parameters = GraphBuilder.AllocParameters<FGPULabHelloComputeCS::FParameters>();
            Parameters->OutTexture  = GraphBuilder.CreateUAV(OutputTexture);
            Parameters->TextureSize = FUintVector2(Size.X, Size.Y);

            TShaderMapRef<FGPULabHelloComputeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("GPULab.Lesson01.HelloCompute"),
                ComputeShader,
                Parameters,
                FComputeShaderUtils::GetGroupCount(Size, 8));

            GraphBuilder.Execute();
        });
}
```

Five things worth stopping on.

### `ENQUEUE_RENDER_COMMAND`

Blueprint calls run on the game thread. GPU work happens on the render thread. This
macro hands a lambda across.

Everything it needs is **captured by value**. Never capture a `UObject*` here - by
the time the render thread runs, it may have been garbage collected. We grab the
`FTextureRenderTargetResource*` on the game thread instead, which is render-thread
owned and safe.

### `RegisterExternalTexture`

Our Render Target already exists - RDG did not create it. Registering tells RDG
"here is a texture you don't own, please include it in your bookkeeping."

The name string only matters for debugging, but use a good one: it is what shows up
in RenderDoc and in `r.RDG.Debug` output, and future-you will be grateful.

### `GraphBuilder.AllocParameters<...>()`

**Not a local variable.** The pass runs later, during `Execute()`, long after this
function has returned. RDG must own the memory so it stays alive. Using a stack
variable here is a use-after-free.

### `CreateUAV`

Turns "a texture" into "a texture the shader may write to". This is where the
"Support UAV" flag on the Render Target asset matters - without it, this fails.

### `GetGroupCount(Size, 8)`

How many 8x8 groups cover the image? For 256x256, that's 32x32 = 1024 groups.

It uses *divide and round up*, and that matters: 250 / 8 = 31.25, and 31 groups
would leave the last two pixel columns unpainted. Rounding up to 32 covers
everything, and the bounds check in the shader handles the overhang.

### `GraphBuilder.Execute()`

**Nothing ran before this line.** Everything above just built a description. This
is where RDG compiles the graph and records real GPU commands.

---

## Try it yourself

1. **Change the colour.** Make it blue and green instead of red and green:
   `float4(0.0f, UV.y, UV.x, 1.0f)`

2. **Break it on purpose.** Rename `TextureSize` to `TextureSizes` in the `.usf`
   *only*. Rebuild shaders (`recompileshaders changed` in the console). The shader
   still compiles - but `TextureSize` is now 0 inside the shader, the bounds check
   rejects every thread, and you get a blank texture. **Get used to this failure
   mode now**, because it will happen to you for real later.

3. **Delete the bounds check** and make the Render Target 250x250 instead of 256.
   See what happens. (Save your work first.)

4. **Draw a circle.** Replace the last line with:
   ```hlsl
   float Dist = length(UV - 0.5f);
   float Circle = Dist < 0.3f ? 1.0f : 0.0f;
   OutTexture[PixelCoord] = float4(Circle, Circle, Circle, 1.0f);
   ```

---

## If it does not work

| Symptom | Likely cause |
|---|---|
| Log says "does not support UAV" | Tick **Support UAV** on the Render Target asset, then save it |
| Texture stays black | Parameter name mismatch between `.usf` and `.cpp`. Check spelling and case. |
| Crash on editor start, "virtual shader path not found" | `LoadingPhase` in the `.uplugin` is not `PostConfigInit` |
| Shader changes have no effect | Console: `recompileshaders changed`. Or set `r.ShaderDevelopmentMode=1` in `ConsoleVariables.ini` |
| Node missing in Blueprint | The module did not compile, or the editor was not restarted after building |

---

## Next

→ **[02 - Parameters](02-Parameters.md)** - make it move.
