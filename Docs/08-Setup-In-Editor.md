# 08 - Setting It Up In The Editor

How to actually see a lesson's output. Two routes: the fast one and the manual one.

---

## Route A: the demo actor (30 seconds)

The plugin ships an actor that builds everything for you.

1. In the Content Browser, make the display material once - **[see below](#making-the-display-material)**.
   You only ever do this once per project.
2. In the **Place Actors** panel, search for **GPULab Demo**. Drag it into the
   level.
3. In the Details panel:
   - **Display Material** → your material
   - **Lesson** → pick one
4. That's it. The plane shows the result immediately.

To see the animated lessons move, turn on **Realtime** in the viewport:
press **Ctrl + R**, or click the arrow in the top-left of the viewport and tick
*Realtime*. Without it the editor only redraws when something changes, so
animation looks frozen.

Lesson 04 has no picture. Pick it, then open **Window → Output Log** and look for
lines starting with `LogGPULab`.

---

## Route B: your own Render Target and Blueprint

Worth doing once, because it is how you will wire your own shaders up later.

### Step 1 - make a Render Target

1. Content Browser → right-click → **Textures → Render Target**.
2. Name it `RT_GPULab`.
3. Double-click it and set:

   | Setting | Value | Why |
   |---|---|---|
   | **Size X / Size Y** | 512 / 512 | must be set before anything uses it |
   | **Render Target Format** | `RTF RGBA16f` | floating point, so values are not clamped to 0..1 |
   | **Support UAV** | **ticked** | compute shaders write through a UAV. Lessons 1-5 and 7 fail without this. |

4. **Save it.**

> **The most common mistake in this whole plugin** is forgetting *Support UAV*.
> The log tells you clearly:
> `Lesson01: Render Target 'RT_GPULab' does not support UAV.`
> Lesson 6 is the exception - it draws instead of writing a UAV, so it works either way.

### Step 2 - make the display material

<a name="making-the-display-material"></a>

1. Content Browser → right-click → **Material**. Name it `M_GPULabDisplay`.
2. Double-click to open it.
3. In the Details panel, set **Shading Model** to **Unlit**. You want to see the
   texture exactly as the shader wrote it, not lit by the scene.
   Tick **Two Sided** as well so it is visible from behind.
4. Right-click in the graph → add a **TextureSampleParameter2D**.
5. Name the parameter **`GPULabTexture`**.
6. Set its **Sampler Type** to **Linear Color** (not Color - the render target is
   already linear, and Color would apply an sRGB conversion you do not want).
7. Drag the **RGB** output into **Emissive Color**.
8. Click **Apply** and **Save**.

```
   ┌──────────────────────────┐
   │ TextureSampleParameter2D │
   │   Name: GPULabTexture    │        ┌─────────────────┐
   │   Sampler: Linear Color  │        │  M_GPULabDisplay│
   │                          │        │                 │
   │                     RGB ●┼───────►│ Emissive Color  │
   │                       R ○│        │                 │
   │                       G ○│        │ Shading: Unlit  │
   │                       B ○│        │ Two Sided: yes  │
   │                       A ○│        └─────────────────┘
   └──────────────────────────┘
```

> If the material refuses to compile with *"Sampler type is Linear Color, should
> be Color"*, it is complaining about the placeholder texture sitting in the node,
> not about your render target. Either drop `RT_GPULab` into the node as its
> default, or pick any linear/HDR texture.

### Step 3 - put it on something

1. Drag a **Plane** into the level from Place Actors.
2. Make a **Material Instance** of `M_GPULabDisplay` (right-click the material →
   *Create Material Instance*).
3. Open the instance, tick **GPULabTexture**, and set it to `RT_GPULab`.
4. Assign the instance to the plane.

### Step 4 - call the lesson

Open the **Level Blueprint** (Blueprints → Open Level Blueprint):

```
   Event BeginPlay ──────► Lesson 01 Hello Compute
                             Output RT: RT_GPULab
```

For an animated lesson:

```
   Event Tick ────────────► Lesson 02 Parameters
                              Output RT: RT_GPULab
        ┌──────────────┐        Time:  ◄─┐
        │ Get Game Time│──────────────────┘
        │  in Seconds  │        Scale: 8.0
        └──────────────┘        Color A / Color B: pick two
```

Press **Play**.

---

## Iterating on a shader

You do **not** need to rebuild C++ to change a `.usf` file.

1. Edit the `.usf`.
2. In the editor console (backtick key, `` ` ``), type:

   ```
   recompileshaders changed
   ```

3. Run the lesson again.

To make that even smoother, add this to
`Config/ConsoleVariables.ini` in your project:

```ini
r.ShaderDevelopmentMode=1
```

You now get proper error messages with line numbers when a shader fails to
compile, and a retry prompt instead of a silent failure.

You **do** need to rebuild C++ when you:
- add or rename a `SHADER_PARAMETER`
- add a new shader class or lesson
- change anything in a `.h` or `.cpp`

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `does not support UAV` in the log | Render Target flag | Tick **Support UAV**, save the asset |
| Plane is black | RT never written, or material not Unlit | Check the lesson node actually fired; check Shading Model |
| Plane is invisible | Backface | Tick **Two Sided** on the material |
| Nothing animates | Editor not redrawing | **Ctrl + R** for Realtime, or press Play |
| Blueprint node missing | Module not built, or editor not restarted | Rebuild, then restart the editor |
| Shader edits do nothing | Shader cache | `recompileshaders changed` |
| Crash at startup, *virtual shader path not found* | Plugin loads too late | `"LoadingPhase": "PostConfigInit"` in the `.uplugin` |
| Texture shows but colours look washed out | Sampler type | Set **Linear Color**, not Color |

---

## Looking at what the GPU actually did

Two tools worth knowing:

**`r.RDG.Debug 1`** - logs every RDG pass, by the name you gave it in
`RDG_EVENT_NAME`. Good for "did my pass even run?"

**RenderDoc** - enable the **RenderDoc** plugin in your project, then press the
capture button in the viewport toolbar. You get the whole frame: every pass,
every resource, every parameter value, and the ability to click a pixel and see
which threads wrote it. When a shader is silently doing the wrong thing, this is
how you find out why.

Your `RDG_EVENT_NAME` strings are what you will be reading in both. This is why
the lessons all use names like `GPULab.Lesson05.PassB_Blur` rather than `Pass2`.
