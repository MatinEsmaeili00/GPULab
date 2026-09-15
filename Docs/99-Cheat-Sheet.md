# 99 - Cheat Sheet

Keep this open while you write your own. Everything here appears in the lessons.

---

## The skeleton of a compute shader

**`Shaders/Private/MyShader.usf`**

```hlsl
#include "/Engine/Public/Platform.ush"

RWTexture2D<float4> OutTexture;
uint2 TextureSize;
float MyValue;

[numthreads(8, 8, 1)]
void MainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint2 Coord = DispatchThreadId.xy;
    if (Coord.x >= TextureSize.x || Coord.y >= TextureSize.y) return;

    float2 UV = (float2(Coord) + 0.5f) / float2(TextureSize);
    OutTexture[Coord] = float4(UV, MyValue, 1.0f);
}
```

**`Source/MyModule/Private/MyShader.cpp`**

```cpp
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"

class FMyShaderCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMyShaderCS);
    SHADER_USE_PARAMETER_STRUCT(FMyShaderCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTexture)
        SHADER_PARAMETER(FUintVector2, TextureSize)
        SHADER_PARAMETER(float,        MyValue)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

IMPLEMENT_GLOBAL_SHADER(FMyShaderCS, "/Plugin/MyPlugin/Private/MyShader.usf", "MainCS", SF_Compute);

void RunIt(UTextureRenderTarget2D* RT, float Value)
{
    FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();

    ENQUEUE_RENDER_COMMAND(RunMyShader)(
        [Resource, Value](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);

            FRDGTextureRef Output = RegisterExternalTexture(
                GraphBuilder, Resource->GetRenderTargetTexture(), TEXT("MyOutput"));

            const FIntPoint Size = Output->Desc.Extent;

            auto* Parameters = GraphBuilder.AllocParameters<FMyShaderCS::FParameters>();
            Parameters->OutTexture  = GraphBuilder.CreateUAV(Output);
            Parameters->TextureSize = FUintVector2(Size.X, Size.Y);
            Parameters->MyValue     = Value;

            TShaderMapRef<FMyShaderCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

            FComputeShaderUtils::AddPass(
                GraphBuilder, RDG_EVENT_NAME("MyShader"), Shader, Parameters,
                FComputeShaderUtils::GetGroupCount(Size, 8));

            GraphBuilder.Execute();
        });
}
```

---

## Parameter macros

| Macro | HLSL | Access |
|---|---|---|
| `SHADER_PARAMETER(Type, Name)` | plain value | read |
| `SHADER_PARAMETER_RDG_TEXTURE(Texture2D, N)` | `Texture2D` | read |
| `SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, N)` | `RWTexture2D` | read/write |
| `SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, N)` | `StructuredBuffer` | read |
| `SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, N)` | `RWStructuredBuffer` | read/write |
| `SHADER_PARAMETER_SAMPLER(SamplerState, N)` | `SamplerState` | - |
| `SHADER_PARAMETER_TEXTURE(Texture2D, N)` | `Texture2D` | non-RDG texture |
| `RENDER_TARGET_BINDING_SLOTS()` | - | required for raster passes |

**The macro you pick declares the dependency.** UAV = write, plain = read. That is
how RDG orders passes and inserts barriers.

---

## C++ → HLSL types

| C++ | HLSL |
|---|---|
| `float` | `float` |
| `int32` / `uint32` | `int` / `uint` |
| `FVector2f` | `float2` |
| `FVector3f` | `float3` |
| `FVector4f` / `FLinearColor` | `float4` |
| `FIntPoint` / `FIntVector` | `int2` / `int3` |
| `FUintVector2` | `uint2` |
| `FMatrix44f` | `float4x4` |

Always the `f` types. `FVector` and `FMatrix` are double precision; shaders are not.

---

## Creating resources

```cpp
// transient texture (dies with the graph)
FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
    Size, PF_FloatRGBA, FClearValueBinding::Black,
    TexCreate_ShaderResource | TexCreate_UAV);
FRDGTextureRef Tex = GraphBuilder.CreateTexture(Desc, TEXT("Name"));

// register a texture that already exists
FRDGTextureRef Ext = RegisterExternalTexture(GraphBuilder, RHITexture, TEXT("Name"));

// buffer with data uploaded from the CPU
FRDGBufferRef In = CreateStructuredBuffer(
    GraphBuilder, TEXT("In"), sizeof(float), Count, Data, sizeof(float) * Count);

// empty buffer
FRDGBufferRef Out = GraphBuilder.CreateBuffer(
    FRDGBufferDesc::CreateStructuredDesc(sizeof(float), Count), TEXT("Out"));

// views
Parameters->Thing = GraphBuilder.CreateUAV(Tex);   // write
Parameters->Thing = GraphBuilder.CreateSRV(In);    // read (buffers)
Parameters->Thing = Tex;                           // read (textures - no call needed)

// clear
AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Tex), FLinearColor::Black);

// keep past the end of the graph
GraphBuilder.QueueTextureExtraction(Tex, &MyPersistentPtr);
```

### Texture creation flags

| Flag | Needed when |
|---|---|
| `TexCreate_UAV` | a compute shader writes it |
| `TexCreate_ShaderResource` | any shader reads or samples it |
| `TexCreate_RenderTargetable` | a raster pass draws into it |

---

## Adding passes

```cpp
// compute
FComputeShaderUtils::AddPass(
    GraphBuilder, RDG_EVENT_NAME("Name"), Shader, Parameters, GroupCount);

// raster (needs RENDER_TARGET_BINDING_SLOTS in the struct)
Parameters->RenderTargets[0] = FRenderTargetBinding(Tex, ERenderTargetLoadAction::ELoad);
FPixelShaderUtils::AddFullscreenPass(
    GraphBuilder, GetGlobalShaderMap(GMaxRHIFeatureLevel),
    RDG_EVENT_NAME("Name"), PixelShader, Parameters, FIntRect(0, 0, W, H));
```

### Group counts

```cpp
FComputeShaderUtils::GetGroupCount(Size, 8)                              // 2D, 8x8 groups
FComputeShaderUtils::GetGroupCount(FIntVector(N,1,1), FIntVector(64,1,1)) // 1D, 64-wide
```

Always divides and rounds up. Always pair it with a bounds check in the shader.

---

## HLSL system values

| Semantic | Meaning |
|---|---|
| `SV_DispatchThreadID` | position across the whole dispatch |
| `SV_GroupID` | which group |
| `SV_GroupThreadID` | position inside the group |
| `SV_GroupIndex` | flat index inside the group |
| `SV_POSITION` (pixel shader in) | pixel position, **already centred** |
| `SV_Target0..7` | pixel shader outputs |

```
SV_DispatchThreadID = SV_GroupID * numthreads + SV_GroupThreadID
```

---

## Reading textures in HLSL

```hlsl
Tex.Load(int3(Coord, 0))            // exact texel, integer coords, no sampler
Tex.SampleLevel(Samp, UV, 0)        // filtered, 0..1 coords, needs a sampler
Tex.Sample(Samp, UV)                // PIXEL SHADERS ONLY - auto mip selection
```

`Sample()` in a compute shader is a compile error. Use `SampleLevel`.

---

## Samplers

```cpp
TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
```

| Filter | Address |
|---|---|
| `SF_Point`, `SF_Bilinear`, `SF_Trilinear`, `SF_AnisotropicLinear` | `AM_Clamp`, `AM_Wrap`, `AM_Mirror`, `AM_Border` |

---

## Useful HLSL functions

| Function | Does |
|---|---|
| `saturate(x)` | clamp to 0..1 |
| `lerp(a, b, t)` | blend |
| `step(edge, x)` | 0 below edge, 1 above |
| `smoothstep(a, b, x)` | soft 0→1 ramp between a and b |
| `length(v)`, `distance(a,b)` | vector length / distance |
| `normalize(v)` | unit-length vector |
| `dot(a, b)` | dot product |
| `frac(x)`, `floor(x)`, `fmod(a,b)` | fractional part, round down, remainder |
| `abs`, `min`, `max`, `clamp`, `pow`, `sqrt` | as you'd expect |
| `sin`, `cos`, `atan2` | trig, radians |
| `ddx(x)`, `ddy(x)` | rate of change - **pixel shaders only** |

---

## Plugin setup

**`MyPlugin.uplugin`**

```json
"Modules": [{ "Name": "MyModule", "Type": "Runtime", "LoadingPhase": "PostConfigInit" }]
```

`PostConfigInit` is required - the shader directory must be mapped before any
shader compiling starts.

**`MyModule.Build.cs`**

```cs
PrivateDependencyModuleNames.AddRange(new string[] { "Projects", "RHI", "RenderCore", "Renderer" });
```

**`MyModule.cpp`**

```cpp
virtual void StartupModule() override
{
    const FString Dir = FPaths::Combine(
        IPluginManager::Get().FindPlugin(TEXT("MyPlugin"))->GetBaseDir(), TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/MyPlugin"), Dir);
}
```

---

## Console commands

| Command | Does |
|---|---|
| `recompileshaders changed` | rebuild edited `.usf` files without restarting |
| `recompileshaders global` | rebuild all global shaders |
| `r.RDG.Debug 1` | log every RDG pass by name |
| `r.ShaderDevelopmentMode 1` | proper shader errors with line numbers (set in `ConsoleVariables.ini`) |
| `DumpGPU` | dump a full frame's RDG resources to disk |

---

## The mistakes that will cost you an afternoon

1. **Parameter name typo.** The shader compiles, runs, and silently does nothing.
   There is no error. Check spelling and case against the `.usf` first, always.

2. **Forgot the bounds check.** Writes past the end of the texture. May look fine
   for months, then corrupt something.

3. **`[numthreads]` and the C++ group size disagree.** Part of your image is
   blank, or you do 4x the work you needed. Push the number in with
   `ModifyCompilationEnvironment` and the problem cannot happen.

4. **Forgot `bCanCreateUAV` / "Support UAV".** Compute lessons refuse to run.
   The flag is read when the texture is created, so setting it later does nothing.

5. **Missing texture creation flag.** RDG asserts and names the texture - which
   is why you give them good names.

6. **Used a stack variable instead of `GraphBuilder.AllocParameters`.** The pass
   runs after your function returned. Use-after-free.

7. **Captured a `UObject*` in the render command.** It may be garbage collected
   before the render thread gets there. Copy values, grab the resource on the
   game thread.

8. **Reading and writing the same texture.** Race condition, looks like noise.
   Ping-pong instead.

9. **`Sample()` in a compute shader.** Compile error. Use `SampleLevel()`.

10. **Forgot `QueueTextureExtraction`.** Your simulation resets every frame - one
    flash, then nothing.

11. **Used `FVector`/`FMatrix` in a parameter struct.** Double precision. Use the
    `f` variants.

12. **Blocking readback every frame.** Correct, and it destroys your frame rate.
    Poll `IsReady()` on a later frame instead.

---

## Where to look in the engine

Reading engine code is the fastest way to learn more. Good starting points:

| Path | What's there |
|---|---|
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | the whole RDG API |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h` | `AddPass`, buffer helpers, clears |
| `Engine/Source/Runtime/RenderCore/Public/ShaderParameterMacros.h` | every `SHADER_PARAMETER_*` macro |
| `Engine/Source/Runtime/Renderer/Private/PostProcess/` | real full-screen passes, well written |
| `Engine/Shaders/Private/` | the engine's own `.usf` files |

Searching the engine for `IMPLEMENT_GLOBAL_SHADER` gives you several hundred
worked examples of exactly the pattern in this plugin.
