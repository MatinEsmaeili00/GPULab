# 03 - Threads and Groups

**Files:** [`Lesson03_ThreadIDs.usf`](../Shaders/Private/Lesson03_ThreadIDs.usf) ·
[`Lesson03_ThreadIDs.cpp`](../Source/GPULab/Private/Lesson03_ThreadIDs.cpp)

**Blueprint node:** `Lesson 03 Thread IDs (Output RT, View)`

**Result:** four different pictures, one per View setting. Each one shows you a
different piece of the GPU's own bookkeeping.

This is the most important lesson in the plugin. Everything else is detail.

---

## Three layers

```
  DISPATCH  ── you launch this many groups ──┐
                                             │
      ┌──────────┬──────────┬──────────┬─────┘
      │          │          │          │
   ┌──▼──┐    ┌──▼──┐    ┌──▼──┐    ┌──▼──┐
   │GROUP│    │GROUP│    │GROUP│    │GROUP│   ... 1024 of them
   └──┬──┘    └─────┘    └─────┘    └─────┘
      │
      │  each group is [numthreads(8,8,1)] = 64 threads
      │
   ┌──▼─────────────────────┐
   │ T T T T T T T T        │   8 threads across
   │ T T T T T T T T        │   8 threads down
   │ T T T T T T T T        │   = 64 threads
   │ T T T T T T T T        │
   │ T T T T T T T T        │   each one runs your function once
   │ T T T T T T T T        │
   │ T T T T T T T T        │
   │ T T T T T T T T        │
   └────────────────────────┘
```

**THREAD** - one run of your shader function. It handles one pixel, or one array
element, or one particle. There are millions.

**GROUP** - a fixed box of threads, set by `[numthreads(X, Y, Z)]` in the `.usf`.
Threads in the same group are special:

- they can share fast scratch memory (`groupshared`)
- they can wait for each other (`GroupMemoryBarrierWithGroupSync()`)
- they are guaranteed to run on the same part of the chip

Threads in *different* groups can do **none** of that. Assume they run in any
order, or at the same time, or minutes apart. There is no way to make one group
wait for another inside a single dispatch - if you need that, you need two passes
(Lesson 5).

**DISPATCH** - how many groups you launch. Decided in C++.

> **Total threads = group count x threads per group**

---

## The worked example

This is the arithmetic you will redo in your head forever, so do it once properly.

```
  Texture:            256 x 256 pixels        = 65,536 pixels
  [numthreads(8,8,1)] 8 x 8 x 1               = 64 threads per group
  Group count:        256/8 x 256/8 = 32 x 32 = 1,024 groups
  Total threads:      1,024 x 64              = 65,536 threads
                                                ^^^^^^ exactly one per pixel
```

In C++ that division is one line:

```cpp
const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Size, 8);
```

It **divides and rounds up**, and the rounding up is the whole point:

```
  250 pixels / 8 = 31.25
  31 groups  ->  248 pixels covered  ->  last 2 columns never painted  ✗
  32 groups  ->  256 pixels covered  ->  all painted, 6 threads spare  ✓
```

Those 6 spare threads per row are exactly why every shader needs this:

```hlsl
if (PixelCoord.x >= TextureSize.x || PixelCoord.y >= TextureSize.y)
    return;
```

Lesson 3 prints the numbers to the log when you run it, so you can check them
against your own arithmetic.

---

## The four system values

Ask for these in your entry function and the hardware fills them in.

```hlsl
void MainCS(
    uint3 DispatchThreadId : SV_DispatchThreadID,
    uint3 GroupId          : SV_GroupID,
    uint3 GroupThreadId    : SV_GroupThreadID,
    uint  GroupIndex       : SV_GroupIndex)
```

| Semantic | What it is | Range in our example |
|---|---|---|
| `SV_DispatchThreadID` | position across the **whole** launch | 0..255 in x and y |
| `SV_GroupID` | which group am I in | 0..31 in x and y |
| `SV_GroupThreadID` | position **inside** my group | 0..7 in x and y |
| `SV_GroupIndex` | that position flattened to one number | 0..63 |

And the relationship between them:

```
  SV_DispatchThreadID  =  SV_GroupID * numthreads + SV_GroupThreadID
```

```
  SV_GroupIndex  =  GroupThreadId.z * (X*Y) + GroupThreadId.y * X + GroupThreadId.x
```

---

## What each View looks like

Run the lesson with each setting. This is the whole point of it.

**`DispatchThreadID`** - a smooth gradient across the whole image. Every thread
has a different value. **This is the one you want 95% of the time**, because it
maps one thread to one pixel.

**`GroupID`** - chunky blocks, 8x8 pixels each, one flat colour per block. Every
thread in a group returns the *same* `GroupId`. Seeing this makes "the work is
cut into boxes" obvious.

**`GroupThreadID`** - the same little gradient tile repeated over and over, once
per group. Each group runs 0..7 in x and y, independently of every other group.

**`GroupIndex`** - a grey ramp repeating every 64 threads. This is the order
threads sit in when you use `groupshared` memory.

---

## Choosing a group size

Some rules that will keep you out of trouble:

**Make it a multiple of 64.** GPUs do not really run threads one at a time - they
run them in lockstep bundles. NVIDIA calls a bundle a *warp* (32 threads), AMD
calls it a *wavefront* (32 or 64). If your group is 10 threads, the hardware still
runs a full 32-wide bundle and throws 22 of the results away. You just wasted 69%
of that bundle.

So: **64, 128, 256**. Not 10, not 50, not 100.

**Match the shape to the problem.**

| Problem | Group shape | Why |
|---|---|---|
| An image | `[numthreads(8, 8, 1)]` | 64 threads, and neighbours in x and y are in the same group - good for cache |
| A flat array | `[numthreads(64, 1, 1)]` | there is no 2D structure to preserve |
| A 3D volume | `[numthreads(4, 4, 4)]` | 64 threads, cubic |

**Do not go above 1024.** That is the hard limit for X*Y*Z in one group on
current hardware.

**Do not obsess.** 8x8 for images and 64x1x1 for arrays are right almost always.
Tuning group size is a last-resort optimisation you do with a profiler open, not
something to guess at up front.

---

## Keeping the number in one place

Notice that `kThreadGroupSize` appears in the C++ *and* `[numthreads(...)]`
appears in the HLSL. If those two ever disagree, your dispatch covers the wrong
area - too few threads and part of the image stays blank, too many and you waste
work.

Lesson 3 removes the risk entirely:

```cpp
// C++
static constexpr int32 kThreadGroupSize = 8;

static void ModifyCompilationEnvironment(...)
{
    FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
    OutEnvironment.SetDefine(TEXT("GPULAB_THREADS_XY"), kThreadGroupSize);
}
```

```hlsl
// HLSL
#define THREADS GPULAB_THREADS_XY
[numthreads(THREADS, THREADS, 1)]
```

Now there is one number, in one place. **Do this in real code.** It costs three
lines and removes a whole category of bug.

---

## Try it yourself

1. **Run all four views.** Seriously, look at each one. Five minutes here saves
   you hours later.

2. **Change the group size to 16.** Edit `kThreadGroupSize` in the `.cpp` only -
   the `.usf` follows automatically because of the `#define`. Rebuild, run the
   `GroupID` view. The blocks get bigger, and there are fewer of them.

3. **Break the link on purpose.** Hard-code `[numthreads(4, 4, 1)]` in the `.usf`
   while C++ still thinks it is 8. You now launch a quarter of the threads you
   need, and only the top-left quarter of the image gets painted. **This is what
   a group-size mismatch looks like** - learn to recognise it.

4. **Read the log.** Run the lesson and look for the `Lesson03:` line. Check its
   numbers against the arithmetic above.

---

## Where this comes from

**The HLSL side** is standard Direct3D, not Unreal-specific, so Microsoft's
reference is the authority here:

- [`numthreads` attribute](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-numthreads)
  — including the hard limits: `z <= 64`, and `x*y*z <= 1024`.
- [SV_DispatchThreadID](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sv-dispatchthreadid)
  — states the relationship used in this lesson:
  `SV_DispatchThreadID = SV_GroupID * numthreads + SV_GroupThreadID`
- [SV_GroupID](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sv-groupid)
- [SV_GroupThreadID](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sv-groupthreadid)
- [SV_GroupIndex](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sv-groupindex)
- [Compute Shader Overview](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader)
- [Programming guide for HLSL](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-pguide)

**Engine source**

| File | What it defines |
|---|---|
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h` | the five `FComputeShaderUtils::GetGroupCount` overloads, and `GetGroupCountWrapped` for dispatches too large for one dimension |

The "multiple of 64" advice comes from how the hardware actually schedules: NVIDIA
runs threads in 32-wide *warps*, AMD in 32- or 64-wide *wavefronts*. A group of 10
threads still occupies a full bundle and wastes the rest.

> **A note on sources.** This lesson was written by reading the engine source listed
> above directly, not by following a tutorial. The engine paths are the primary
> source - they are on your disk, they match your exact engine version, and they
> cannot go out of date or 404. The links are there for background and for a second
> explanation in someone else's words.

---

## Next

→ **[04 - Buffers and Readback](04-Buffers-And-Readback.md)** - GPU work that is not a picture.
