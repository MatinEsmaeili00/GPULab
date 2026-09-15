# 00 - The Big Picture

Read this once. Everything else will make more sense afterwards.

---

## 1. What a GPU actually is

Your CPU has maybe 8 or 16 cores. Each one is clever and fast and can do anything.

Your GPU has **thousands** of tiny cores. Each one is slow and simple. They are
happiest when they all run *the same instructions* at *the same time* on
*different data*.

So the question you must always ask is not "how do I make this fast?" but:

> **Can I split this job into thousands of pieces that don't need to talk to each other?**

If yes, the GPU will crush it. If no, leave it on the CPU.

Painting a 256x256 image is 65,536 pixels, and no pixel needs to know what its
neighbour is doing. That is a perfect GPU job, which is why every lesson here
paints an image.

---

## 2. The two kinds of GPU program

The GPU can run your code in two very different ways.

### Raster (the old way)

You hand it triangles. The hardware works out which pixels each triangle covers,
then runs your **pixel shader** once per covered pixel.

- You do **not** choose which pixels run. The triangle's shape does.
- Output goes to the bound **render target**.
- Two shaders involved: a **vertex shader** (where do the corners go?) and a
  **pixel shader** (what colour is this pixel?).
- This is how literally every mesh in your game gets drawn.

Lesson 6 uses this path.

### Compute (the flexible way)

You say "run this function N times". That's it. No triangles, no geometry.

- You choose N.
- Each copy gets a number, and uses it to decide what to work on.
- Output goes wherever you point it - a texture, a buffer, anything.
- One shader involved: a **compute shader**.

Lessons 1-5 and 7 use this path. Most modern GPU work is compute.

---

## 3. The three pieces of an Unreal GPU program

Every single lesson in this plugin is the same three pieces. Once you see the
pattern, you can read any rendering code in the engine.

```
   ┌─────────────────────┐
   │  1. THE .usf FILE   │   HLSL. Runs on the GPU.
   │                     │   "What should each thread do?"
   └─────────────────────┘
              ▲
              │  linked by name, at compile time,
              │  via IMPLEMENT_GLOBAL_SHADER
              ▼
   ┌─────────────────────┐
   │  2. THE C++ CLASS   │   An FGlobalShader subclass.
   │                     │   "What inputs does that shader take?"
   └─────────────────────┘
              ▲
              │  used by
              ▼
   ┌─────────────────────┐
   │  3. THE RDG PASS    │   Runs on the render thread.
   │                     │   "Fill in those inputs and run it."
   └─────────────────────┘
```

**Piece 1 - the `.usf` file.** Plain HLSL with a `[numthreads(...)]` line and an
entry function. It declares global variables for its inputs.

**Piece 2 - the C++ shader class.** It does not contain any shader code. It is a
*description*: "there is a shader at this path, its entry point is called this,
and here are the inputs it expects." The inputs are listed in a
`BEGIN_SHADER_PARAMETER_STRUCT` block, and **the names must match the `.usf`
exactly** - Unreal links them by string.

**Piece 3 - the RDG pass.** Actual running code. It fills in the parameter
struct, works out how many threads to launch, and adds the work to the graph.

---

## 4. What "global shader" means

You will meet three families of shader in Unreal:

| Family | What it is | Example |
|---|---|---|
| **Global shader** | A standalone program. Not tied to any material or mesh. You run it whenever you want. | Everything in this plugin. Also: bloom, tone mapping, SSAO. |
| **Material shader** | Generated from a Material asset's node graph. One per material. | The material on your character |
| **Vertex factory shader** | Handles the different ways mesh vertices are laid out (static, skinned, instanced) | Engine internals |

You want **global shaders** when you want to write HLSL by hand and control
exactly what runs. That is what `FGlobalShader` means, and it is what this whole
plugin is about.

---

## 5. Threads (the render thread problem)

This trips up everyone once.

Unreal runs your game on several threads at the same time:

```
  Game thread      frame 12   ->   frame 13   ->   frame 14
  Render thread               frame 11   ->   frame 12   ->   frame 13
  GPU                                    frame 10   ->   frame 11
```

The render thread is roughly **one frame behind** the game thread, and the GPU is
behind that. This is deliberate - it keeps everything busy.

The consequences for you:

1. **Blueprint and gameplay code run on the game thread.** You cannot issue GPU
   commands there.
2. **You hand work over with `ENQUEUE_RENDER_COMMAND`.** It takes a lambda that
   runs later, on the render thread.
3. **Everything that lambda needs must be copied in by value.** Never capture a
   `UObject*` and use it inside - by the time the render thread gets there, the
   object may have been garbage collected. Copy the numbers you need.

You will see this exact shape in every lesson:

```cpp
void SomeBlueprintFunction(UTextureRenderTarget2D* RT, float MyValue)
{
    // --- game thread ---
    FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();

    ENQUEUE_RENDER_COMMAND(SomeName)(
        [Resource, MyValue](FRHICommandListImmediate& RHICmdList)
        {
            // --- render thread, some time later ---
        });
}
```

---

## 6. What RDG is and why it exists

RDG = **Render Dependency Graph**. `FRDGBuilder`.

Older graphics code said "do this, now do that" straight to the driver. Modern
APIs (D3D12, Vulkan) made that painful, because you became responsible for:

- **Barriers.** Telling the GPU "this texture was being written, now it will be
  read, please finish the writes first." Forget one and you get flickering,
  garbage, or a crash - often only on one brand of card.
- **Memory.** Temporary textures are big. You want to reuse the same memory for
  different things at different points in the frame.

RDG fixes both by making you **describe the work before running it**.

```
  You:  "Pass A writes texture X."
  You:  "Pass B reads texture X and writes Y."
  You:  "Go."
  RDG:  looks at the whole list...
        - B needs A's output, so order them
        - insert the write->read barrier on X
        - X is dead after B, so its memory can be reused
        - now record the real GPU commands
```

Nothing runs until you call `GraphBuilder.Execute()`. Everything before that is
just building a to-do list.

This is why RDG feels like extra ceremony in Lesson 1 (one pass, nothing to
schedule) and starts to feel obviously worth it in Lesson 5 (two passes, and you
never wrote a barrier).

---

## 7. The words you will keep meeting

| Word | Plain English |
|---|---|
| **Shader** | A small program that runs on the GPU |
| **HLSL** | The language shaders are written in. Looks like C. |
| **`.usf`** | Unreal Shader File. A file of HLSL. |
| **`.ush`** | Unreal Shader Header. HLSL you `#include` into a `.usf`. |
| **Dispatch** | Launching a compute shader. "Run this N times." |
| **Thread** | One copy of your shader function |
| **Thread group** | A small fixed box of threads that can cooperate |
| **UAV** | Unordered Access View. A view that lets shaders **write** anywhere. |
| **SRV** | Shader Resource View. A view that lets shaders **read** only. |
| **View** | A permission slip. The memory is the resource; the view says how you may touch it. |
| **Render target** | A texture you can draw into |
| **Uniform / constant buffer** | A small block of values every thread reads, none writes |
| **Barrier** | "Wait, and change how this resource is being used" |
| **Transient** | Exists only inside one RDG graph, then the memory is reused |
| **Readback** | Copying GPU results back to CPU memory. Slow. |

---

## Next

→ **[01 - Hello Compute](01-Hello-Compute.md)** - build the smallest one that works.
