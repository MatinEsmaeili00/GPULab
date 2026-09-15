# GPULab

Learn low-level GPU programming in Unreal Engine 5, one small working example at a time.

This is a self-contained UE 5.8 plugin. Every lesson is a **compute shader or pixel
shader you can actually run**, paint into a Render Target, and look at. No theory
without a picture at the end of it.

The code is written to be read. Every `.cpp` and `.usf` file is heavily commented,
and the `Docs/` folder explains the *why* behind each one in plain English.

---

## What you will learn

| # | Lesson | The one idea |
|---|--------|--------------|
| 1 | [Hello Compute](Docs/01-Hello-Compute.md) | The three pieces every GPU program in Unreal needs |
| 2 | [Parameters](Docs/02-Parameters.md) | How a number gets from C++ into a shader |
| 3 | [Threads and Groups](Docs/03-Threads-And-Groups.md) | What "parallel" actually means, drawn as a picture |
| 4 | [Buffers and Readback](Docs/04-Buffers-And-Readback.md) | Arrays in, arrays out, and why reading results is slow |
| 5 | [Multi-Pass RDG](Docs/05-Multi-Pass-RDG.md) | Two passes, one graph, and the barriers you didn't write |
| 6 | [Pixel Shader Pass](Docs/06-Pixel-Shader-Pass.md) | The *other* half of the GPU: drawing triangles |
| 7 | [Persistent Simulation](Docs/07-Persistent-Simulation.md) | Keeping state alive across frames (ping-pong) |

Start with **[00 - The Big Picture](Docs/00-Big-Picture.md)**. It is short and it
makes everything after it easier.

Then **[08 - Setting It Up In The Editor](Docs/08-Setup-In-Editor.md)** shows you how
to make the Render Target, the material, and the Blueprint so you can see the output.

Keep **[99 - Cheat Sheet](Docs/99-Cheat-Sheet.md)** open in a tab while you write your own.

---

## Installing

1. Copy the `GPULab` folder into your project's `Plugins/` folder:

   ```
   YourProject/
     YourProject.uproject
     Plugins/
       GPULab/          <- here
   ```

2. Right-click your `.uproject` → **Generate Visual Studio project files**.
3. Build the editor target.
4. Open the project. The plugin is enabled automatically.

Requires **Unreal Engine 5.8** and a C++ project. Tested on Windows / D3D12.

---

## Running a lesson

### The fast way

The plugin ships a **GPULab Demo** actor that builds its own render target and
runs any lesson on a plane, live in the editor viewport.

1. Make the display material once - [Doc 08](Docs/08-Setup-In-Editor.md#making-the-display-material),
   about six clicks.
2. Drag **GPULab Demo** from the Place Actors panel into your level.
3. Set **Display Material**, then pick a **Lesson** from the dropdown.
4. Press **Ctrl + R** for Realtime if you want the animated ones to move.

Every per-lesson knob (colours, blur radius, thread view, damping) is in the
Details panel, so you can poke at the parameters without touching code.

### The manual way

Every lesson is also a plain Blueprint node under the **GPULab** category:

```
Event BeginPlay ──► Lesson 01 Hello Compute (Output RT)
```

Pass it a Render Target with **Support UAV** ticked, put that render target in a
material, and put the material on a plane. [Doc 08](Docs/08-Setup-In-Editor.md)
walks through it with exact clicks - worth doing once, because it is how you will
wire up your own shaders later.

---

## Layout

```
GPULab/
  GPULab.uplugin                  plugin manifest
  Docs/                           the written lessons
  Shaders/Private/*.usf           the HLSL - the code that runs ON the GPU
  Source/GPULab/
    GPULab.Build.cs               which engine modules we depend on
    Public/
      GPULabLibrary.h             the Blueprint nodes - the menu of lessons
      GPULabDemoActor.h           the drop-in actor that shows the output
    Private/
      GPULabModule.cpp            maps /Plugin/GPULab to the Shaders folder
      GPULabCommon.h              shared safety checks
      GPULabDemoActor.cpp         builds a UAV render target in code
      Lesson01..07*.cpp           one file per lesson - the code that DRIVES the GPU
```

Two languages, two places:

- **`.usf` files are HLSL.** This code runs on the graphics card, thousands of copies
  at once. It cannot allocate memory, call the engine, or print.
- **`.cpp` files are C++.** This code runs on your CPU. Its whole job is to set up
  data, describe the work, and hand it to the GPU.

---

## A note on the comments

The comments in this plugin are much denser than you would write in production code.
That is on purpose - this is a textbook you can compile. When you copy a pattern into
your own project, strip them back to the ones that still earn their place.

---

## Where this came from

This was written by reading the **UE 5.8 engine source directly**, not by following a
tutorial. Every API signature here was checked against the headers in the engine
install before it was used.

Each lesson doc ends with a **"Where this comes from"** section listing the exact
engine files behind it, plus links to the official Epic documentation and to good
community write-ups for a second explanation.

The engine paths are the primary source, deliberately. They are on your disk, they
match your exact engine version, and unlike a blog post they cannot rot. When
something in here disagrees with your engine, the engine is right - go read the
header.

The main external references, collected:

- [Creating a New Global Shader as a Plugin in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-a-new-global-shader-as-a-plugin-in-unreal-engine) - Epic
- [Adding Global Shaders to Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine) - Epic
- [Render Dependency Graph in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine) - Epic
- [staticJPL / Render-Dependency-Graph-Documentation](https://github.com/staticJPL/Render-Dependency-Graph-Documentation) - community deep-dive on RDG and the UE5 render pipeline
- [Programming guide for HLSL](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-pguide) - Microsoft, for the HLSL language itself
- [Compute Shader Overview](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader) - Microsoft

All lessons were verified running on D3D12 against UE 5.8.
