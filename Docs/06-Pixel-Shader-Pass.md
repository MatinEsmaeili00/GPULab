# 06 - Pixel Shader Pass

**Files:** [`Lesson06_FullscreenPS.usf`](../Shaders/Private/Lesson06_FullscreenPS.usf) ·
[`Lesson06_FullscreenPS.cpp`](../Source/GPULab/Private/Lesson06_FullscreenPS.cpp)

**Blueprint node:** `Lesson 06 Fullscreen Pixel Shader (Output RT, Time, Tint)`

**Result:** a checkerboard with a bright diagonal band sliding across it.

---

## The one idea

Everything so far was **compute**. This is a **draw**.

|  | Compute | Raster (vertex + pixel) |
|---|---|---|
| What you say | "run this function N times" | "draw these triangles" |
| Who picks the pixels | you do, from the thread ID | the triangle's shape does |
| Output goes to | a UAV you bound | the bound render target |
| Needs "Support UAV" | **yes** | **no** |
| C++ helper | `FComputeShaderUtils` | `FPixelShaderUtils` |
| Shader stage | `SF_Compute` | `SF_Vertex` + `SF_Pixel` |
| Depth test, blending, stencil | not available | available |

Both paths run on the same hardware. Raster just adds fixed-function machinery
around your code: it turns triangles into pixel coverage, it can test and write
depth, and it can blend your output with what was already there.

---

## The one big triangle

To cover a whole render target you could draw two triangles forming a quad. Almost
nobody does. Instead you draw **one triangle that is bigger than the screen**:

```
        (-1, 3)
           |\
           | \
           |  \
           |   \
   ┌───────┼────\──────┐
   │       │     \     │   <- the render target
   │       │      \    │
   │       │       \   │
   └───────┼────────\──┘
           |         \
        (-1,-1)───────(3,-1)
```

Why? Along the diagonal of a two-triangle quad, the hardware processes pixels
twice - once for each triangle - because it always shades in 2x2 quads and the
edge cuts through them. One big triangle has no internal edge, so nothing is
shaded twice. It is a small win, it is free, and it is what the whole industry
does.

**You do not write this.** Unreal has a vertex shader for exactly this job:
`FScreenVertexShaderVS`. `FPixelShaderUtils::AddFullscreenPass` picks it up for
you.

---

## The pixel shader

```hlsl
void MainPS(
    float4 SvPosition : SV_POSITION,
    out float4 OutColor : SV_Target0)
{
    float2 UV = SvPosition.xy / float2(OutputSize);
    ...
    OutColor = float4(Color * Tint.rgb, 1.0f);
}
```

**`SV_POSITION` as an input** to a pixel shader is the pixel's position on
screen, and it is **already at the pixel centre**. A 256-wide target gives you
0.5, 1.5, 2.5 ... 255.5.

That is why there is no `+ 0.5` here, unlike every compute lesson. `SV_DispatchThreadID`
gives whole numbers; `SV_POSITION` gives centres. Mixing them up shifts your image
by half a pixel, which is just visible enough to be annoying and just subtle
enough to be hard to find.

**`SV_Target0`** means "write this to the first bound render target". A raster
pass can have up to eight bound at once (`SV_Target0` .. `SV_Target7`) - that is
how a deferred renderer writes colour, normal, roughness and so on in one pass.

**We do not get a UV from the vertex shader.** `FScreenVertexShaderVS` only
outputs position, so we compute the UV ourselves from `SvPosition` and the target
size. That's why `OutputSize` is a parameter.

---

## `RENDER_TARGET_BINDING_SLOTS()`

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    SHADER_PARAMETER(FLinearColor, Tint)
    SHADER_PARAMETER(float,        Time)
    SHADER_PARAMETER(FUintVector2, OutputSize)

    RENDER_TARGET_BINDING_SLOTS()      // <-- this is what makes it a raster pass
END_SHADER_PARAMETER_STRUCT()
```

This macro adds hidden members describing which textures the pass draws into and
what happens to whatever was already there. RDG refuses to build a raster pass
without it.

There is no compute equivalent, because compute has no "bound render target"
concept at all - it just writes through UAVs.

Then you fill it in:

```cpp
Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);
```

### Load actions

| Action | What happens to the existing contents | When to use |
|---|---|---|
| `ELoad` | kept - you draw on top | you are blending, or only drawing part of it |
| `EClear` | wiped to the texture's clear colour first | you want a clean slate |
| `ENoAction` | undefined - "I promise to overwrite every pixel" | fastest, when that promise is true |

`ENoAction` is a genuine performance win on tiled mobile GPUs, where `ELoad`
means copying the whole tile in from main memory. But if you lie, you get
whatever garbage happened to be in that memory. We use `ELoad` because it is
always safe.

---

## Adding the pass

```cpp
FPixelShaderUtils::AddFullscreenPass(
    GraphBuilder,
    GetGlobalShaderMap(GMaxRHIFeatureLevel),   // needed to find the vertex shader
    RDG_EVENT_NAME("GPULab.Lesson06.FullscreenPS"),
    PixelShader,
    Parameters,
    FIntRect(0, 0, Size.X, Size.Y));           // the viewport
```

Compare with the compute version:

```cpp
FComputeShaderUtils::AddPass(
    GraphBuilder,
    RDG_EVENT_NAME("..."),
    ComputeShader,
    Parameters,
    GroupCount);                               // <-- group count, not a viewport
```

The shape is identical. The difference is the last argument: compute takes a
**group count**, raster takes a **viewport** - the rectangle of the target the
triangle is allowed to touch.

`AddFullscreenPass` also takes optional blend, rasterizer and depth-stencil
states. Leave them null for "opaque, no depth test, fill the whole thing", which
is what we want. Passing `TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI()`
would give you ordinary alpha blending instead.

---

## So which should I use?

**Use compute when:**
- the work is not really pixels (arrays, particles, physics, sorting)
- you need to write to scattered locations
- you want `groupshared` memory to let threads cooperate
- you want to run at a different resolution to anything being drawn

**Use raster when:**
- you are drawing actual geometry
- you want depth testing, stencil, or hardware blending
- you want the hardware to do the coverage work for you
- you are on mobile, where compute is often slower

For "fill a texture with a procedural pattern", **both work**, and the compute
version is usually marginally faster. This lesson uses raster mostly so you have
written one.

---

## Try it yourself

1. **Notice the Render Target does not need "Support UAV".** Make a second RT
   without it and run Lesson 6 on it - it works. Then run Lesson 1 on it and read
   the error in the log.

2. **Change the checker size.** `floor(UV * 8.0f)` → try 4, 16, 32.

3. **Add blending.** Pass `TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI()`
   as the blend state and set the load action to `ELoad`. Now each call *adds* to
   the target instead of replacing it, and repeated calls blow out to white.

4. **Write to two targets.** Add `out float4 OutColor2 : SV_Target1` to the shader
   and bind a second texture to `RenderTargets[1]`. This is a mini G-buffer.

5. **Port it to compute.** Rewrite this lesson as a compute shader. It is about
   fifteen minutes and it will fix the compute/raster distinction in your head
   permanently.

---

## Where this comes from

**Engine source**

| File | What it defines |
|---|---|
| `Engine/Source/Runtime/RenderCore/Public/PixelShaderUtils.h` | `FPixelShaderUtils::AddFullscreenPass`, `DrawFullscreenTriangle`, `InitFullscreenPipelineState` |
| `Engine/Source/Runtime/RenderCore/Private/PixelShaderUtils.cpp` | shows `InitFullscreenPipelineState` picking up `FScreenVertexShaderVS` for you |
| `Engine/Source/Runtime/RenderCore/Public/CommonRenderResources.h` | `FScreenVertexShaderVS` — the vertex shader you do not have to write |
| `Engine/Shaders/Private/Tools/FullscreenVertexShader.usf` | the actual HLSL of that vertex shader. Worth opening: it explains why you only get `SV_POSITION` and no UV, which is why this lesson computes its own. |
| `Engine/Source/Runtime/RenderCore/Public/ShaderParameterMacros.h` | `RENDER_TARGET_BINDING_SLOTS` |
| `Engine/Source/Runtime/RenderCore/Public/RHIStaticStates.h` | `TStaticBlendState`, `TStaticRasterizerState`, `TStaticDepthStencilState` for the optional arguments |

**Official Epic documentation**

- [Render Dependency Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)
  — see "Raster Passes" for how `RENDER_TARGET_BINDING_SLOTS` and load actions work.

**On the single oversized triangle.** The reason it beats a two-triangle quad is
that GPUs shade in 2x2 pixel quads, so the diagonal seam of a quad gets shaded
twice. Background:

- [Compute Shader Overview](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader)
  — for the contrast with the compute path.

> **A note on sources.** This lesson was written by reading the engine source listed
> above directly, not by following a tutorial. The engine paths are the primary
> source - they are on your disk, they match your exact engine version, and they
> cannot go out of date or 404. The links are there for background and for a second
> explanation in someone else's words.

---

## Next

→ **[07 - Persistent Simulation](07-Persistent-Simulation.md)** - state that survives the frame.
