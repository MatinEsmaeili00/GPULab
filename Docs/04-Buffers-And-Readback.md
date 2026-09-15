# 04 - Buffers and Readback

**Files:** [`Lesson04_BufferMath.usf`](../Shaders/Private/Lesson04_BufferMath.usf) ·
[`Lesson04_BufferMath.cpp`](../Source/GPULab/Private/Lesson04_BufferMath.cpp)

**Blueprint node:** `Lesson 04 Buffer Math (Input, Multiplier) -> Out Result`

**Result:** an array of numbers goes in, the GPU multiplies all of them at once,
and the answers come back so `Print String` can show them.

This is the first lesson with no picture. Not all GPU work is graphics.

---

## Textures vs buffers

| | Texture | Buffer |
|---|---|---|
| Shape | 2D (or 3D, or cube) | a flat array |
| Reading | `.Load()` or `.Sample()` | `Buf[index]` |
| Filtering | yes - bilinear, mips, wrapping | no, it's just memory |
| Good for | images, grids, anything spatial | lists, particles, physics, counters |

If your data is naturally a grid, use a texture and get filtering for free. If it
is a list of things, use a buffer.

---

## SRV and UAV

Two words you will see constantly.

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

A **view** is a permission slip. The same memory can have both an SRV and a UAV;
which one you hand to a shader decides what that shader may do.

"Unordered" in UAV means: threads may write in any order, and the hardware makes
no promises about who finishes first. That is why two threads writing to the same
address is a bug (unless you use atomics).

| | HLSL | C++ macro | C++ creation |
|---|---|---|---|
| Read only | `StructuredBuffer<T>` | `SHADER_PARAMETER_RDG_BUFFER_SRV` | `GraphBuilder.CreateSRV(Buf)` |
| Read/write | `RWStructuredBuffer<T>` | `SHADER_PARAMETER_RDG_BUFFER_UAV` | `GraphBuilder.CreateUAV(Buf)` |

**"Structured"** means the GPU knows how big one element is, so `Buf[7]` works
without you doing byte arithmetic. The other common kind is `ByteAddressBuffer`,
where you index in bytes - useful for packed or mixed data, more fiddly.

---

## The shader

```hlsl
StructuredBuffer<float>   InputValues;
RWStructuredBuffer<float> OutputValues;
float Multiplier;
uint  NumElements;

[numthreads(64, 1, 1)]
void MainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Index = DispatchThreadId.x;
    if (Index >= NumElements) return;

    OutputValues[Index] = InputValues[Index] * Multiplier;
}
```

Note the group is `(64, 1, 1)` - one dimensional, because the problem is one
dimensional. There is no 2D structure worth preserving here.

**There is no loop.** On the CPU you would write `for (i = 0; i < N; ++i)`. On the
GPU the loop *is* the parallelism: you write the body once, and launch N copies of
it. Getting comfortable with that flip is most of learning GPU programming.

---

## Uploading the input

```cpp
FRDGBufferRef InputBuffer = CreateStructuredBuffer(
    GraphBuilder,
    TEXT("GPULab.Lesson04.InputBuffer"),
    sizeof(float),          // how big is one element
    NumElements,            // how many
    InputCopy.GetData(),    // pointer to the CPU data
    sizeof(float) * NumElements);   // total bytes
```

This does two jobs: allocates GPU memory, and schedules the CPU->GPU copy as part
of the graph.

## Creating the output

```cpp
FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
    FRDGBufferDesc::CreateStructuredDesc(sizeof(float), NumElements),
    TEXT("GPULab.Lesson04.OutputBuffer"));
```

This one starts empty. It is **transient** - RDG owns it and recycles its memory
the moment the graph finishes. Which is exactly why we have to copy the answers
out before that happens.

---

## Getting the answers back

This is the part that surprises people.

```
  Game thread      frame 12   ->   frame 13   ->   frame 14
  Render thread               frame 11   ->   frame 12   ->   frame 13
  GPU                                    frame 10   ->   frame 11
```

The GPU is **one to three frames behind**. That lag is deliberate and it is what
keeps everything busy. But it means that when you ask "what was the answer?",
the honest reply is "I haven't started yet."

You have two options.

### Option A: stop and wait (what this lesson does)

```cpp
FRHIGPUBufferReadback Readback(TEXT("GPULab.Lesson04.Readback"));
AddEnqueueCopyPass(GraphBuilder, &Readback, OutputBuffer, sizeof(float) * NumElements);

GraphBuilder.Execute();

RHICmdList.SubmitAndBlockUntilGPUIdle();      // <-- the stall

const float* Data = (const float*)Readback.Lock(sizeof(float) * NumElements);
FMemory::Memcpy(ResultPtr, Data, sizeof(float) * NumElements);
Readback.Unlock();
```

...and on the game thread:

```cpp
FlushRenderingCommands();   // <-- another stall
```

A **readback buffer** is a staging area in memory the CPU can actually see.
`AddEnqueueCopyPass` adds one more pass to the graph that copies our output into
it - and RDG works out by itself that this must happen *after* the compute pass,
because it can see both passes touch `OutputBuffer`.

Then we stall twice: once waiting for the GPU, once waiting for the render thread.

**This is correct and it is also terrible.** You threw away the pipelining. In a
real game those two lines cost several milliseconds every call. Do it once per
frame and you have destroyed your frame rate.

We do it here purely so `Print String` can show the numbers on the same frame.

### Option B: ask now, collect later (what real code does)

1. Kick the work off. Keep the `FRHIGPUBufferReadback` object alive - on a
   subsystem, in a small queue, wherever.
2. On following frames, poll `Readback.IsReady()`.
3. When it returns true, `Lock()`, read, `Unlock()`.

You get the answer two or three frames late and you never stall. Everything in
Unreal that reads GPU results back works this way: occlusion queries, GPU
particle counts, Nanite stats, Lumen probes.

> **Rule of thumb: design so you never need the answer this frame.**

---

## Why `TArray<float> InputCopy = Input;`

The render thread runs *later*. The caller's array may be gone by then. So we copy
it and move the copy into the lambda:

```cpp
[InputCopy = MoveTemp(InputCopy), Multiplier, NumElements, ResultPtr](...)
```

`ResultPtr` is a raw pointer into the caller's output array, which looks
dangerous. It is safe here **only** because `FlushRenderingCommands()` at the
bottom guarantees the render thread has finished before the function returns.
Remove that flush and it becomes a use-after-free. This is a good example of why
Option B is not just faster but structurally safer - it forces you to own the
memory properly.

---

## Try it yourself

1. **Feed it 10 numbers** from a Blueprint Make Array, multiplier 3. Print the
   result.

2. **Feed it 1,000,000 numbers.** Notice it still comes back quickly - that's a
   million multiplications, done all at once. Then notice it is probably *slower*
   than the CPU would be, because the upload, the stall, and the readback dominate.
   **The GPU only wins when the maths is heavy enough to pay for the trip.**

3. **Add a second output.** Give the shader `RWStructuredBuffer<float> OutputSquares`
   and write `InputValues[Index] * InputValues[Index]` into it. You need a second
   buffer, a second UAV, and a second readback.

4. **Change the maths** to something expensive - `sin(cos(x)*100)` fifty times in
   a loop. Now compare against the CPU again. This is the shape of problem the GPU
   is for.

---

## Where this comes from

**Engine source**

| File | What it defines |
|---|---|
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h` | `CreateStructuredBuffer` (several overloads), `AddEnqueueCopyPass` |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | `CreateBuffer`, `CreateSRV`, `CreateUAV` |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphResources.h` | `FRDGBufferDesc::CreateStructuredDesc` |
| `Engine/Source/Runtime/RHI/Public/RHIGPUReadback.h` | `FRHIGPUBufferReadback`, and the `IsReady()` / `Lock()` / `Unlock()` API that the non-stalling version of this lesson would use |

`RHIGPUReadback.h` is worth opening properly. It is short, and reading `IsReady()`
next to `Lock()` makes the "ask now, collect later" pattern obvious in a way prose
does not.

**Official Epic documentation**

- [Render Dependency Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)
  — see "Creating Resources" and "Buffer Uploads".

**Real non-stalling readbacks in the engine.** For a shipped example of polling
instead of blocking, search the engine source for `FRHIGPUBufferReadback` — the GPU
occlusion and GPU-particle systems both keep a small queue of in-flight readbacks
and check them on later frames.

> **A note on sources.** This lesson was written by reading the engine source listed
> above directly, not by following a tutorial. The engine paths are the primary
> source - they are on your disk, they match your exact engine version, and they
> cannot go out of date or 404. The links are there for background and for a second
> explanation in someone else's words.

---

## Next

→ **[05 - Multi-Pass RDG](05-Multi-Pass-RDG.md)** - where RDG starts to earn its keep.
