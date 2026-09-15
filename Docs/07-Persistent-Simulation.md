# 07 - Persistent Simulation

**Files:** [`Lesson07_Ripple.usf`](../Shaders/Private/Lesson07_Ripple.usf) ·
[`Lesson07_Ripple.cpp`](../Source/GPULab/Private/Lesson07_Ripple.cpp)

**Blueprint node:** `Lesson 07 Ripple Step (Output RT, Damping, Drop Position, Drop Strength, Reset)`

**Result:** water. Drop stones in it and watch the rings spread and bounce off
the edges.

---

## The one idea

Every lesson so far started from nothing. A simulation cannot: this frame's water
needs last frame's water.

Two problems stand in the way, and they have two separate fixes.

---

## Problem 1: you cannot read and write the same texture

Threads run in an unpredictable order. Suppose one texture holds the water, and
each thread reads its neighbours and writes itself:

```
  thread A reads its neighbour B's height   ─┐
                                             ├── who goes first? nobody knows
  thread B writes its own new height        ─┘
```

Thread A gets either the old value or the new value depending on pure luck - and
the luck is different every frame, on every GPU, at every resolution. Your
simulation dissolves into noise.

### The fix: ping-pong

Keep **two** textures. Read from one, write to the other, swap next frame.

```
   frame 1:   [ A ] ──read──►  shader  ──write──►  [ B ]
   frame 2:   [ B ] ──read──►  shader  ──write──►  [ A ]
   frame 3:   [ A ] ──read──►  shader  ──write──►  [ B ]
```

Every texture is read-only or write-only within a pass, never both. No race.

This file does it with a slight simplification: it keeps **one** stored pointer,
reads from it, writes into a brand new texture, and stores that one instead. Same
effect, less bookkeeping.

```cpp
StateIn  = GraphBuilder.RegisterExternalTexture(GRippleState, TEXT("...StatePrev"));
StateOut = GraphBuilder.CreateTexture(StateDesc, TEXT("...StateNext"));
// simulate: StateIn -> StateOut
GraphBuilder.QueueTextureExtraction(StateOut, &GRippleState);   // becomes next frame's StateIn
```

---

## Problem 2: RDG throws its textures away

That is normally the whole point (see Lesson 5). Here we need one to survive.

### The fix: `QueueTextureExtraction`

```cpp
static TRefCountPtr<IPooledRenderTarget> GRippleState;

// ... inside the graph ...
GraphBuilder.QueueTextureExtraction(StateOut, &GRippleState);
```

This says: "when the graph finishes, do not recycle this one - hand it to me."
You get back an `IPooledRenderTarget`, a reference-counted texture that lives
outside any graph. Next frame you feed it back in with `RegisterExternalTexture`.

> **Forget this line and the symptom is very specific:** you see a single flash on
> the first frame and then nothing, because every frame starts from blank water.

### About that global

```cpp
static TRefCountPtr<IPooledRenderTarget> GRippleState;
```

A file-static usually deserves a raised eyebrow. It is acceptable here for one
reason: it is only ever touched inside `ENQUEUE_RENDER_COMMAND` lambdas, so only
the render thread ever sees it, so there is no race.

In a real project put it somewhere with a proper lifetime - a
`UWorldSubsystem`, a `FSceneViewExtension`, or a member of whatever component
owns the effect. A global means one simulation for the entire process, which
breaks the moment you want two.

---

## The first frame

```cpp
const bool bHaveState = GRippleState.IsValid() && GRippleState->GetDesc().Extent == Size;

if (bReset || !bHaveState)
{
    StateIn = GraphBuilder.CreateTexture(StateDesc, TEXT("...StateInit"));
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StateIn), FLinearColor::Black);
}
else
{
    StateIn = GraphBuilder.RegisterExternalTexture(GRippleState, TEXT("...StatePrev"));
}
```

Three cases to handle, and all three bite in real code:

1. **First ever run** - nothing stored yet.
2. **Reset requested** - the user pressed a button.
3. **The Render Target was resized** - the stored texture is the wrong size now.
   Checking `GetDesc().Extent == Size` catches this. Skip that check and you get
   a crash or garbage the first time someone changes the resolution.

`AddClearUAVPass` is RDG's "fill this with a value" helper. It is a real pass in
the graph like any other.

---

## The format

```cpp
FRDGTextureDesc::Create2D(Size, PF_G32R32F, FClearValueBinding::Black,
                          TexCreate_ShaderResource | TexCreate_UAV);
```

`PF_G32R32F` = **two 32-bit floats per pixel**. We are not storing a colour here,
we are storing physics:

```
  .x = height     how high the water is at this point
  .y = velocity   how fast it is moving up or down
```

**Use full 32-bit precision for simulation state.** A 16-bit float format would
save memory, but the tiny rounding error on every step accumulates. After a few
hundred frames the surface drifts, oscillates, or freezes. This is a real bug
people hit, and it looks like "the physics is wrong" rather than "the format is
wrong".

The *display* render target can happily stay 16-bit - it is just a picture.

---

## The physics

```hlsl
float Laplacian = (Left + Right + Up + Down) - 4.0f * Height;

Velocity += Laplacian * 0.25f;
Velocity *= Damping;
Height   += Velocity;
```

Three lines, and they are the wave equation.

**The Laplacian** sounds frightening and means something simple: *how far am I
from the average of my neighbours?*

- If all four neighbours are at the same height as me, it is 0 - nothing happens.
- If I am lower than everyone around me, it is positive, and I get pushed **up**.
- If I am a peak, it is negative, and I get pulled **down**.

That is surface tension. Water is always trying to flatten itself out.

**Then it is just Newton.** Acceleration changes velocity, velocity changes
position - the same three lines you would write for a falling rock, except here
every pixel is its own rock and its acceleration comes from its neighbours.

**Damping** is the only artificial bit. Without it the ripples never stop, and
worse, small numerical errors compound until the whole thing explodes into
flashing garbage. `0.997` means each step keeps 99.7% of its energy.

> Try setting Damping to 1.0 in the demo actor and watch it slowly go unstable.
> This is what "numerically unstable" looks like, and it is worth seeing once.

---

## Two passes again

```
  StateIn ──► SimulateCS ──► StateOut ──► VisualiseCS ──► your Render Target
                                 │
                                 └──► QueueTextureExtraction ──► next frame
```

The simulation and the display are separate on purpose:

- the state is `float2` physics, the display is `float4` colour
- you might want to simulate at 512x512 and display at 1024x1024
- you might want to simulate several steps per frame for stability
- you might want to display it three different ways without re-simulating

**Keeping simulation separate from presentation is good architecture
everywhere**, not just on the GPU.

The `VisualiseCS` shader does a nice trick worth stealing: it works out the
*slope* of the surface by comparing left-vs-right and up-vs-down heights, then
shades based on that. That is exactly what a normal map does, computed on the
fly.

---

## Try it yourself

1. **Set Damping to 1.0.** Watch it slowly go unstable. Then 0.9 - ripples die
   almost instantly.

2. **Change the wave speed.** `Velocity += Laplacian * 0.25f` → try `0.1` (slow,
   syrupy) and `0.5` (fast, sharp). Go to `0.6` and it explodes - you have
   exceeded the stability limit of this integration scheme.

3. **Delete the `QueueTextureExtraction` line.** See the single flash. Put it back.
   Now you will recognise that symptom forever.

4. **Resize while it runs.** Change Resolution in the demo actor. The size check
   catches it and restarts cleanly. Then comment out the `GetDesc().Extent == Size`
   part of the check and try again.

5. **Simulate twice per frame.** Add both passes to the graph a second time,
   feeding the first step's output into the second step's input. Ripples move
   twice as fast and stay stable. This is *substepping*, and it is how real
   simulations get speed without losing stability.

6. **Make it a real Game of Life.** Same structure, different rules - swap
   `PF_G32R32F` for `PF_R32_FLOAT`, count live neighbours, apply Conway's rules.
   Everything else in this file stays exactly the same. That is the sign you have
   learned the pattern rather than the example.

---

## You have finished the lessons

Go to **[99 - Cheat Sheet](99-Cheat-Sheet.md)** and keep it open while you write
your own.
