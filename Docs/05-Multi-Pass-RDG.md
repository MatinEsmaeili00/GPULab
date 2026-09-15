# 05 - Multi-Pass RDG

**Files:** [`Lesson05_MultiPass.usf`](../Shaders/Private/Lesson05_MultiPass.usf) ·
[`Lesson05_MultiPass.cpp`](../Source/GPULab/Private/Lesson05_MultiPass.cpp)

**Blueprint node:** `Lesson 05 Multi Pass Blur (Output RT, Time, Blur Radius)`

**Result:** five coloured dots orbiting the centre, softly blurred.

---

## The one idea

RDG felt like pointless paperwork in Lessons 1-4, because one pass has nothing to
schedule. This is the lesson where it pays you back.

```
   ┌──────────────┐                      ┌──────────────┐
   │   PASS A     │  writes   ┌──────┐   │   PASS B     │  writes
   │   DotsCS     │──────────►│Scratch│──►│   BlurCS     │────────► Render Target
   └──────────────┘           │texture│   └──────────────┘
                              └──────┘     reads
```

Pass B must not start until pass A has finished. The GPU must also be told that
the scratch texture has changed role, from "something being written" to
"something being read".

**You will not find that code below.** That is the point.

---

## What a barrier is, and why you want RDG to write it

Modern GPUs do not run your passes one after another politely. They overlap them,
reorder them, and keep hundreds of writes in flight in caches that other parts of
the chip cannot see yet.

A **barrier** is you telling the driver:

> "Stop. Finish every write to this texture, flush the caches, and change its
> state from writable to readable. Then carry on."

Writing them by hand, the classic bugs are:

| Mistake | What you see |
|---|---|
| Forgot a barrier | Flickering, garbage, or last frame's data - and often only on one vendor's card |
| Barrier too early | Same as forgetting it |
| Too many barriers | Correct, but slow - you serialised work that could have overlapped |
| Wrong state | Validation-layer spam, or a device removal crash |

These are miserable bugs. They are timing-dependent, hardware-dependent, and
frequently invisible on the machine you are developing on.

RDG removes the whole category. You declared:

```cpp
// pass A
Parameters->ScratchOut = GraphBuilder.CreateUAV(ScratchTexture);   // WRITE

// pass B
Parameters->BlurInput  = ScratchTexture;                           // READ
```

That is all the information it needed. `CreateUAV` means write; passing the
texture straight in means read. RDG sees both passes touch the same resource in
different ways, orders them, and inserts exactly one barrier.

---

## Transient resources

```cpp
const FRDGTextureDesc ScratchDesc = FRDGTextureDesc::Create2D(
    Size,
    PF_FloatRGBA,
    FClearValueBinding::Black,
    TexCreate_ShaderResource | TexCreate_UAV);

FRDGTextureRef ScratchTexture = GraphBuilder.CreateTexture(ScratchDesc, TEXT("GPULab.Lesson05.Scratch"));
```

This texture:

- is created when the graph runs
- stops existing when the graph finishes
- never becomes an asset and never shows up in the Content Browser
- **shares its memory with other passes' temporaries**

That last one is the real win. A frame of a modern renderer has dozens of
temporary full-screen textures. If they all had their own memory you would run
out. Because RDG knows the exact lifetime of each one, it hands the same physical
memory to several of them in turn.

### The flags matter

```cpp
TexCreate_ShaderResource | TexCreate_UAV
```

| Flag | Needed when |
|---|---|
| `TexCreate_UAV` | a compute shader writes to it |
| `TexCreate_ShaderResource` | any shader reads or samples it |
| `TexCreate_RenderTargetable` | a raster pass draws into it (Lesson 6) |
| `TexCreate_DepthStencilTargetable` | it is a depth buffer |

Leave one out and RDG asserts the moment you try to use it that way. The error
message names the texture, which is another reason to give them good names.

---

## Two entry points in one file

```cpp
IMPLEMENT_GLOBAL_SHADER(FGPULabDotsCS, ".../Lesson05_MultiPass.usf", "DotsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGPULabBlurCS, ".../Lesson05_MultiPass.usf", "BlurCS", SF_Compute);
```

Same file, different entry function. One `.usf` can hold a whole little library of
related shaders, which keeps shared helper functions in one place.

---

## Read-only vs read-write, in the macros

This is the bit worth memorising:

```cpp
// pass A - I will WRITE this
SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ScratchOut)

// pass B - I will only READ this
SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BlurInput)
```

| Macro | HLSL | Access |
|---|---|---|
| `SHADER_PARAMETER_RDG_TEXTURE` | `Texture2D` | read only |
| `SHADER_PARAMETER_RDG_TEXTURE_UAV` | `RWTexture2D` | read and write |
| `SHADER_PARAMETER_RDG_BUFFER_SRV` | `StructuredBuffer` | read only |
| `SHADER_PARAMETER_RDG_BUFFER_UAV` | `RWStructuredBuffer` | read and write |

> **Choosing the macro is how you declare the dependency.** Use a UAV where an SRV
> would do and RDG has to assume you might write, so it inserts barriers you did
> not need and serialises passes that could have run together.

---

## Samplers

New in this lesson:

```cpp
Parameters->BlurSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
```

A **sampler** describes *how* to read a texture, separately from *which* texture
you read.

| Setting | Options | Meaning |
|---|---|---|
| Filter | `SF_Point`, `SF_Bilinear`, `SF_Trilinear`, `SF_AnisotropicLinear` | how to blend between texels |
| Address | `AM_Clamp`, `AM_Wrap`, `AM_Mirror`, `AM_Border` | what happens past the edge |

We use **Bilinear** so the blur is smooth, and **Clamp** so it does not wrap
around and pull colours from the opposite side of the image.

`TStaticSamplerState<...>::GetRHI()` returns a shared, engine-owned object. There
is no cost to asking for one and nothing to clean up.

### `SampleLevel`, not `Sample`

```hlsl
Sum += BlurInput.SampleLevel(BlurSampler, UV + Offset, 0);
```

In a **pixel** shader you can call `Sample()`, and the hardware picks a mip level
for you by comparing the UVs of neighbouring pixels (they run in 2x2 quads).

A **compute** shader has no quads and no neighbours, so it cannot do that. You
must say which mip you want. `0` is the largest.

> Calling `Sample()` in a compute shader is a compile error. `SampleLevel()` is
> the fix, and it is not a compromise - you nearly always know the mip you want.

### Load vs Sample

```hlsl
Tex.Load(int3(Coord, 0))              // exact texel, integer coords, no sampler
Tex.SampleLevel(Samp, UV, 0)          // filtered, 0..1 coords, needs a sampler
```

Use `Load` when you want exactly texel (12, 7). Use `SampleLevel` when you want
"whatever colour is 30% of the way across", with blending. Lesson 7 uses `Load`
because a physics simulation must read exact values.

---

## About this blur

The blur here is a **box blur**: average every texel in a square.

```hlsl
for (int y = -BlurRadius; y <= BlurRadius; ++y)
for (int x = -BlurRadius; x <= BlurRadius; ++x)
    Sum += BlurInput.SampleLevel(BlurSampler, UV + float2(x, y) * TexelSize, 0);
```

That is `(2r+1)^2` reads per pixel. At radius 4 that is 81. At radius 16 it is
1089 - which is why the C++ clamps it.

**The right way is a separable blur:** blur horizontally into a temporary, then
vertically. That gives the same result with `2(2r+1)` reads instead of `(2r+1)^2`
- 18 instead of 81 at radius 4, 66 instead of 1089 at radius 16.

That is a genuinely great exercise now that you know how to add a second pass.
See below.

---

## Try it yourself

1. **Set Blur Radius to 0.** Now you see pass A's raw output - hard-edged dots.
   Walk it up to 8 and watch it soften.

2. **Comment out pass B entirely** and point pass A at the Render Target instead
   of the scratch texture. Confirm the dots still appear. Then put it back. This
   proves to you that the passes really are independent pieces.

3. **Make the blur separable.** This is the big one:
   - add a second scratch texture
   - split `BlurCS` into `BlurHorizontalCS` and `BlurVerticalCS`
   - pass A → scratch1, horizontal → scratch2, vertical → output

   You now have a three-pass graph and you still never wrote a barrier.

4. **Look at the graph.** Run `r.RDG.Debug 1` in the console and watch the log.
   You will see your passes by the names you gave them in `RDG_EVENT_NAME`, which
   is exactly why good names matter.

5. **Deliberately break a flag.** Remove `TexCreate_ShaderResource` from the
   scratch description. RDG will assert when pass B tries to read it, and the
   message will name `GPULab.Lesson05.Scratch`. Learn what that error looks like.

---

## Where this comes from

**Engine source**

| File | What it defines |
|---|---|
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | `CreateTexture`, and the setup-vs-execute split that makes barriers automatic |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphResources.h` | `FRDGTextureDesc::Create2D` and the `TexCreate_*` flags |
| `Engine/Source/Runtime/RenderCore/Public/ShaderParameterMacros.h` | `SHADER_PARAMETER_RDG_TEXTURE` vs `..._TEXTURE_UAV` — the read/write distinction RDG reads dependencies from |
| `Engine/Source/Runtime/RenderCore/Public/RHIStaticStates.h` | `TStaticSamplerState`, and every filter and address mode |

**Official Epic documentation**

- [Render Dependency Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)
  — this is the page to actually read once you have this lesson working. It covers
  transient resource allocation, async compute scheduling, and the validation layer.

**Community deep-dive**

- [staticJPL / Render-Dependency-Graph-Documentation](https://github.com/staticJPL/Render-Dependency-Graph-Documentation)
  — a long community write-up of how RDG fits into the wider UE5 rendering pipeline,
  with a worked `FSceneViewExtension` triangle-shader example. Written against 5.1,
  so a few details have moved, but the architecture explanation holds up well.

**Real multi-pass code.** `Engine/Source/Runtime/Renderer/Private/PostProcess/` is
full of well-written examples — bloom is a chain of downsample and upsample passes
and is very close in shape to the separable-blur exercise above.

**On `SampleLevel` vs `Sample`**

- [Programming guide for HLSL](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-pguide)

> **A note on sources.** This lesson was written by reading the engine source listed
> above directly, not by following a tutorial. The engine paths are the primary
> source - they are on your disk, they match your exact engine version, and they
> cannot go out of date or 404. The links are there for background and for a second
> explanation in someone else's words.

---

## Next

→ **[06 - Pixel Shader Pass](06-Pixel-Shader-Pass.md)** - the other half of the GPU.
