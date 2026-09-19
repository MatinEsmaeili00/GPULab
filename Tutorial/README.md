# Build It Yourself

A step-by-step build-along. You start with an empty folder and finish with a
working shader plugin that you wrote.

---

## How this is different from `Docs/`

Two folders, two jobs. Use both.

| | [`Docs/`](../Docs) | `Tutorial/` (you are here) |
|---|---|---|
| Answers | **Why** does this work? | **What** do I type next? |
| Shape | Reference. Read any chapter alone. | Build-along. Read in order, top to bottom. |
| Code | The finished code, explained | Files to create, in the order you create them |
| Use it | When you want to understand a concept | When you want to build the thing |

The recommended loop is: **build a chapter here, then read the matching doc for
why it worked.** Each chapter ends with a link to its doc.

---

## What you are building

A plugin called **`MyShaders`** — your own, from nothing.

It is deliberately *not* called GPULab. You are writing your own version, so:

- you can keep it in the same project as GPULab with no conflict
- at the end of each chapter you can **diff yours against GPULab's** and see where
  you drifted
- when it breaks, it is your code, and debugging your own code is the lesson

---

## The chapters

| # | Chapter | You end up with | Then read |
|---|---|---|---|
| 0 | [Before You Start](00-Before-You-Start.md) | the tools, checked | — |
| 1 | [Create The Plugin](01-Create-The-Plugin.md) | an empty plugin that compiles and loads | [Big Picture](../Docs/00-Big-Picture.md) |
| 2 | [Your First Compute Shader](02-First-Compute-Shader.md) | a shader that paints a texture | [01 Hello Compute](../Docs/01-Hello-Compute.md) |
| 3 | [See It On Screen](03-See-It-On-Screen.md) | it visible on a plane in your level | [08 Setup In Editor](../Docs/08-Setup-In-Editor.md) |
| 4 | [Send Values To The GPU](04-Send-Values-To-The-GPU.md) | an animated, controllable pattern | [02 Parameters](../Docs/02-Parameters.md) |
| 5 | [See The Threads](05-See-The-Threads.md) | the GPU's own numbering, painted | [03 Threads And Groups](../Docs/03-Threads-And-Groups.md) |
| 6 | [Arrays In, Arrays Out](06-Arrays-In-Arrays-Out.md) | GPU maths with results back on the CPU | [04 Buffers And Readback](../Docs/04-Buffers-And-Readback.md) |
| 7 | [Two Passes In One Graph](07-Two-Passes.md) | a blur, and no barriers written by hand | [05 Multi-Pass RDG](../Docs/05-Multi-Pass-RDG.md) |
| 8 | [Draw Instead Of Dispatch](08-Draw-Instead-Of-Dispatch.md) | the same job done the raster way | [06 Pixel Shader Pass](../Docs/06-Pixel-Shader-Pass.md) |
| 9 | [Make It Remember](09-Make-It-Remember.md) | a water simulation that keeps its state | [07 Persistent Simulation](../Docs/07-Persistent-Simulation.md) |
| 10 | [Where To Go Next](10-Where-To-Go-Next.md) | ideas, and how to keep learning | [99 Cheat Sheet](../Docs/99-Cheat-Sheet.md) |

Chapters 1 to 3 are the hard part. That is where most people give up, because
nothing is on screen yet and any one of six small mistakes gives you the same
symptom: nothing happens. Those three chapters are written in the most detail,
with a **checkpoint** at every stage, so you are never more than a few minutes
away from knowing whether you are still on track.

After chapter 3 you have a working loop — edit, build, look — and the rest goes
much faster.

---

## How to read a chapter

Every chapter has the same shape:

**Steps.** Numbered, imperative. "Create this file. Put this in it."

**File trees.** After each step, what your folder should look like. Compare
yours; if it differs, fix it before moving on.

```
MyShaders/
  MyShaders.uplugin          <- NEW this step
  Source/
    MyShaders/
      MyShaders.Build.cs     <- NEW this step
```

**Checkpoints.** Boxed, like this:

> ### ✅ Checkpoint
> Build. It should succeed. If it does not, see the table below.

**Troubleshooting tables.** Symptom on the left, cause and fix on the right.
GPU work fails silently more often than it errors, so these matter more than
usual.

---

## A promise about the code

Every file you are told to create is complete — no `// ... rest of the file`,
no "you know what goes here". You can copy it, or type it, and it will build.

The chapter-1 and chapter-2 code was verified by following these exact
instructions from an empty folder and building it against UE 5.8. Later chapters
are the GPULab lesson code with names changed, and GPULab's version of every one
of them was verified running on D3D12.

---

## If you get stuck

1. Check the **file tree** for the step you are on. Wrong folder is the single
   most common problem.
2. Check the chapter's **troubleshooting table**.
3. Compare your file against GPULab's equivalent — the chapter tells you which
   file that is.
4. Read the matching **doc**. It explains the machinery the chapter only used.

---

→ Start with **[00 - Before You Start](00-Before-You-Start.md)**.
