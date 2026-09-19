# 03 - See It On Screen

**Goal:** actually look at what your shader painted.

**Time:** about 15 minutes. No C++, no building — this is all clicking in the
editor.

Your shader from chapter 2 works. It paints into a Render Target. A Render
Target is just a texture in memory, though, so nothing shows it to you. This
chapter builds the last three links in the chain:

```
   your shader  ──►  Render Target  ──►  Material  ──►  a plane in the level
      (ch 2)            (step 1)          (step 2)         (step 3)
```

---

## Step 1 — Make a Render Target

1. In the **Content Browser**, right-click in empty space.
2. Choose **Textures → Render Target**.
3. Name it **`RT_MyShaders`**.
4. **Double-click it** to open its settings.
5. Set these four:

| Setting | Value | Why |
|---|---|---|
| **Size X** | `512` | |
| **Size Y** | `512` | |
| **Render Target Format** | `RTF RGBA16f` | floating point, so values are not clamped to 0..1 |
| **Support UAV** | ☑ **ticked** | **this is the one everybody forgets** |

6. **Save it.** (Ctrl+S, or right-click → Save.)

> ### ⚠️ Support UAV
>
> Your compute shader writes through a UAV. Unreal only creates that view if the
> asset was told to allow it, and the flag is read when the texture is first
> built.
>
> Miss it and chapter 2's code prints exactly this, which is why you wrote that
> check:
>
> ```
> DrawGradient: Render Target 'RT_MyShaders' does not support UAV.
> ```
>
> If you already used the render target before ticking the box, restart the
> editor so it gets rebuilt.

---

## Step 2 — Make a material to show it

1. Content Browser → right-click → **Material**. Name it **`M_MyShaders`**.
2. Double-click to open the Material Editor.
3. Click on empty graph space so nothing is selected. In the **Details** panel
   on the left, set:
   - **Shading Model** → **Unlit**
   - **Two Sided** → ☑ ticked

   *Unlit* means "show me these exact colours", with no lighting applied. You
   want to see what your shader wrote, not what a light did to it. *Two Sided*
   just means you can see it from behind.

4. Right-click in the graph → search **`TextureSampleParameter2D`** → add it.
5. Select the new node. In **Details**, set:
   - **Parameter Name** → **`MyTexture`**
   - **Sampler Type** → **`Linear Color`**
   - **Texture** → **`RT_MyShaders`** (your render target)
6. Drag from the node's **RGB** output pin to the **Emissive Color** input on the
   big result node.
7. Click **Apply**, then **Save**.

Your graph should look like this:

```
   ┌──────────────────────────┐
   │ TextureSampleParameter2D │
   │   Name:    MyTexture     │          ┌──────────────────┐
   │   Sampler: Linear Color  │          │   M_MyShaders    │
   │   Texture: RT_MyShaders  │          │                  │
   │                     RGB ●┼─────────►│ Emissive Color   │
   │                       R ○│          │                  │
   │                       G ○│          │ Shading: Unlit   │
   │                       B ○│          │ Two Sided: yes   │
   │                       A ○│          └──────────────────┘
   └──────────────────────────┘
```

> **Why Emissive and not Base Color?** Emissive means "this surface gives off
> this colour". Combined with Unlit, it shows your texture exactly, with no
> lighting maths in between. Base Color would be tinted by whatever lights are
> in the scene.

> **Why "Linear Color" and not "Color"?** Your render target is `RGBA16f`, which
> is linear. Setting the sampler to `Color` tells Unreal to apply an sRGB
> conversion that your data does not need, and everything comes out looking
> washed out.

---

## Step 3 — Put it in the level

1. In the **Place Actors** panel, find **Plane** (under Shapes). Drag it into
   your level.
2. With the plane selected, in **Details** set the **Scale** to something like
   `3, 3, 3` so it is big enough to see.
3. Rotate it to stand upright — set **Rotation → Roll** to `90`.
4. Drag **`M_MyShaders`** from the Content Browser onto the plane.

The plane should now be solid black. That is correct — your render target is
empty until something paints it.

---

## Step 4 — Call your shader

1. In the main toolbar: **Blueprints → Open Level Blueprint**.
2. Find the **Event BeginPlay** node (add it if it isn't there: right-click →
   search "BeginPlay").
3. Drag off its white output pin → search **`Draw Gradient`** → add it.
4. On the node's **Output RT** pin, click the dropdown and pick
   **`RT_MyShaders`**.
5. **Compile**, then **Save**.

```
   ┌──────────────────┐        ┌────────────────────────┐
   │ Event BeginPlay  │───────►│  Draw Gradient         │
   │                ▷ │        │   Output RT:           │
   └──────────────────┘        │     RT_MyShaders  ▼    │
                               └────────────────────────┘
```

---

## Step 5 — Press Play

> ### ✅ Checkpoint — the whole thing works
>
> The plane shows a gradient: dark in one corner, red along one axis, green
> along another, yellow in the opposite corner.
>
> ```
>    ┌─────────────────────────┐
>    │▓▓▓▓▓▓▓▓▒▒▒▒▒▒░░░░░░     │
>    │▓▓▓▓▓▓▒▒▒▒▒▒░░░░░░░░     │
>    │▓▓▓▓▒▒▒▒▒▒░░░░░░░░░░     │
>    │▓▓▒▒▒▒▒▒░░░░░░░░░░░░     │
>    │▒▒▒▒▒▒░░░░░░░░░░░░░░     │
>    └─────────────────────────┘
> ```
>
> **That is your code, running on your graphics card, 262,144 times at once.**
> (512 x 512 pixels.)
>
> The gradient's orientation may not match the picture exactly — that depends on
> how the plane's UVs are laid out and which side you are looking from. Two axes
> of colour is the thing to check.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Plane stays black | shader never ran | check the Output Log for your own error messages from chapter 2 |
| Log: `does not support UAV` | the flag | tick **Support UAV**, save, restart the editor |
| Plane is invisible from one side | single-sided material | tick **Two Sided** |
| Everything washed out / too bright | sampler type | set **Linear Color**, not Color |
| Plane shows grey checkerboard | material not applied | drag `M_MyShaders` onto the plane again |
| Blueprint node missing | module not loaded | restart the editor after building |
| Colours are there but very dark | Shading Model | set it to **Unlit**, and plug into **Emissive Color** |

---

## Make it live-updating (optional but recommended)

`Event BeginPlay` runs once. For the animated lessons coming up you want it
running every frame.

Swap `Event BeginPlay` for **`Event Tick`**. That is all — chapter 4 uses this.

---

## What you have now

A complete, working pipeline you can reuse for everything that follows:

```
   Gradient.usf          your HLSL
        │
   Gradient.cpp          C++ class + RDG pass
        │
   Blueprint node        Draw Gradient
        │
   RT_MyShaders          the texture it paints
        │
   M_MyShaders           a material that shows that texture
        │
   a plane               something to look at
```

From here on, **only the top two boxes change.** Every remaining chapter writes
a new `.usf` and a new `.cpp`, and reuses the render target, material, plane and
Blueprint you just made.

That is why these three chapters were the hard part.

---

## Try it yourself

Small changes, quick loop. Edit `Gradient.usf`, then in the editor console
(press `` ` ``) type **`recompileshaders changed`** — **you do not need to
rebuild C++** for shader-only edits.

1. **Change the colours.** Swap the last line for
   `float4(0.0f, UV.y, UV.x, 1.0f)` — now it's green and blue.

2. **Draw a circle.**
   ```hlsl
   float Dist = length(UV - 0.5f);
   float Circle = Dist < 0.3f ? 1.0f : 0.0f;
   OutTexture[PixelCoord] = float4(Circle, Circle, Circle, 1.0f);
   ```

3. **Break it on purpose.** Rename `TextureSize` to `TextureSizes` in the
   **`.usf` only**. Recompile shaders. The shader still compiles — but
   `TextureSize` is now 0 inside it, so the bounds check rejects every thread and
   you get a black texture.

   **Do this one.** Unreal matches parameters by name, silently. This exact
   failure will cost you an afternoon one day, and having seen it once is worth
   more than any warning.

---

→ **Now read [Docs/08 - Setting It Up In The Editor](../Docs/08-Setup-In-Editor.md)**
for more on render targets, materials, and the shader iteration loop.

→ Next: **[04 - Send Values To The GPU](04-Send-Values-To-The-GPU.md)** — make it
move.
