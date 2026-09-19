# 10 - Where To Go Next

You built a shader plugin from an empty folder. Here is what you actually
learned, and what to do with it.

---

## What you can now do

- Create a plugin that hosts your own HLSL
- Write a compute shader and run it from Blueprint
- Send values from C++ into a shader, and know which types map to which
- Reason about threads, groups and dispatch sizes — and spot a mismatch
- Move arrays to the GPU and get results back, and explain why that's slow
- Chain passes in RDG and let it handle the barriers
- Use the raster path when it suits better, and say why
- Keep state alive across frames with ping-pong and extraction

That's the whole core of low-level GPU work in Unreal. Everything past this
point is depth, not new categories.

---

## Fix the things the tutorial cut corners on

The tutorial optimised for getting you to a working thing. Now make it good.
Each of these is a real improvement to your `MyShaders` plugin:

**1. Your own log category.** Every chapter used `LogTemp`, which is the
engine's junk drawer. Make `LogMyShaders` — two lines, and your messages become
filterable.

```cpp
// in a header
DECLARE_LOG_CATEGORY_EXTERN(LogMyShaders, Log, All);
// in one .cpp
DEFINE_LOG_CATEGORY(LogMyShaders);
```

GPULab does this in [`GPULabCommon.h`](../Source/GPULab/Private/GPULabCommon.h).

**2. Pull the repeated validation out.** Every chapter re-typed the same
null-check and UAV-check. Move it into one shared helper. GPULab's version is
`GPULab::GetWritableRTResource`.

**3. Get the global out of chapter 9.** `static TRefCountPtr<IPooledRenderTarget>`
means one simulation for the entire process. Move it onto a `UWorldSubsystem` or
the component that owns the effect.

**4. Make chapter 6's readback non-blocking.** Keep the
`FRHIGPUBufferReadback` alive, poll `IsReady()` on later frames, and delete both
stalls. This is the single biggest real-world difference in the whole tutorial.

**5. Make the blur separable.** Two 1D passes instead of one 2D pass. Four times
cheaper at radius 4, sixteen times at radius 16.

---

## Build something of your own

Pick one. They all reuse exactly what you have.

**Easy — a few hours**

- **Heat diffusion.** Chapter 9's structure, simpler maths: each cell moves
  toward the average of its neighbours. Paint heat with the mouse.
- **Conway's Game of Life.** Chapter 9 with different rules. The exercise at the
  end of that chapter.
- **A mask painter.** Click in the world, write a splat into a render target,
  read that texture in a material to blend between two surfaces. This is how
  puddle, snow and damage masks work.

**Medium — a weekend**

- **Reaction-diffusion.** Two chemicals, two channels of your state texture.
  Produces coral, leopard spots, fingerprint patterns. Astonishing output for
  about forty lines of HLSL.
- **A GPU particle system.** Positions and velocities in a `StructuredBuffer`,
  one compute pass to integrate, then draw them. Chapters 6, 7 and 9 combined.
- **Your own post-process effect.** An `FSceneViewExtension` lets you inject
  passes into the real render pipeline, with the scene colour and depth as
  inputs.

**Harder — worth it**

- **Terrain deformation.** Write footprints into a height texture, read it in a
  landscape material for displacement. Chapter 9's persistence plus world-space
  coordinate mapping.
- **A compute-driven mesh.** Write vertex positions from a compute shader into a
  buffer a mesh reads. Cloth, water surfaces, destruction.

---

## Learn to see what the GPU did

You have been debugging by looking at colours. There are better tools.

**`r.RDG.Debug 1`** — logs every pass by the name you gave `RDG_EVENT_NAME`.
Answers "did my pass even run?"

**RenderDoc.** Enable the **RenderDoc** plugin, then capture a frame from the
viewport toolbar. You get every pass, every resource, every parameter value, and
you can click a pixel and see what wrote it. When a shader silently does the
wrong thing, this is how you find out why.

**`DumpGPU`** — writes a whole frame's RDG resources to disk.

Your `RDG_EVENT_NAME` strings are what you read in all three. That is why every
chapter used names like `MyShaders.Ripple.Simulate` rather than `Pass2`.

---

## Read the engine

You now know enough to read Unreal's own rendering code, which is the best
resource there is and the only one that can't go out of date.

| Path | What's there |
|---|---|
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | the whole RDG API |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h` | `AddPass`, buffer helpers, clears |
| `Engine/Source/Runtime/RenderCore/Public/ShaderParameterMacros.h` | every `SHADER_PARAMETER_*` macro |
| `Engine/Source/Runtime/Renderer/Private/PostProcess/` | real full-screen passes, well written |
| `Engine/Shaders/Private/` | the engine's own `.usf` files |

Start here:

```
grep -rn "IMPLEMENT_GLOBAL_SHADER" "C:/Program Files/Epic Games/UE_5.8/Engine/Source"
```

Several hundred worked examples of the exact pattern you just learned.

---

## Keep these two open

- **[Docs/99 - Cheat Sheet](../Docs/99-Cheat-Sheet.md)** — the skeleton of a
  compute shader, every parameter macro, the type table, and the twelve mistakes
  that cost an afternoon.
- **[Docs/00 - The Big Picture](../Docs/00-Big-Picture.md)** — worth re-reading
  now. It'll land differently than it did before chapter 1.

---

## The one habit worth keeping

GPU code fails **silently** more often than it errors. A misspelled parameter
compiles fine and does nothing. A group-size mismatch paints a quarter of your
image. A missing `QueueTextureExtraction` gives you one frame and then nothing.

So: **change one thing, look at it, change the next.** The temptation is to write
three passes and then debug them together. Resist it. The loop you built in
chapter 3 — edit, `recompileshaders changed`, look — is fast for a reason. Use it
constantly.

And when something doesn't work, before you read any code: **make it fail louder.**
Paint the value you suspect straight into the texture. Log the dispatch
arithmetic. Set the output to solid red at the top of the shader to prove it runs
at all. Half of GPU debugging is just making the invisible visible.

---

You're done. Go build something.
