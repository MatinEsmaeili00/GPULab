# 02 - Parameters

**Files:** [`Lesson02_Parameters.usf`](../Shaders/Private/Lesson02_Parameters.usf) ·
[`Lesson02_Parameters.cpp`](../Source/GPULab/Private/Lesson02_Parameters.cpp)

**Blueprint node:** `Lesson 02 Parameters (Output RT, Time, Scale, Color A, Color B)`

**Result:** a swirling pattern of rings and spokes. Feed it a rising `Time` on Tick
and it animates.

---

## The one idea

Lesson 1's shader was hard-coded. Real shaders take inputs. This lesson is about
exactly how a value travels from a Blueprint pin into an HLSL variable.

---

## The journey of a single float

```
  Blueprint pin  "Scale = 8.0"
         │
         ▼
  C++ function argument               float Scale
         │  captured by value into the lambda
         ▼
  Render thread                       Parameters->Scale = Scale;
         │  RDG packs the whole FParameters struct...
         ▼
  A uniform buffer                    a small block of GPU memory
         │  uploaded with the dispatch
         ▼
  HLSL global variable                float Scale;
```

A **uniform buffer** (also called a constant buffer) is a small block of values
that **every thread can read and no thread can write**. That is what makes it
cheap: the hardware can cache it aggressively because it knows it never changes
during the dispatch.

This is why parameters are for *small* data. Thousands of values belong in a
buffer instead - that's Lesson 4.

---

## The type table

The C++ type you write decides the HLSL type you get. Get this wrong and your
values arrive scrambled.

| C++ | HLSL | Notes |
|---|---|---|
| `float` | `float` | |
| `int32` | `int` | |
| `uint32` | `uint` | |
| `FVector2f` | `float2` | |
| `FVector3f` | `float3` | see the padding warning below |
| `FVector4f` | `float4` | |
| `FLinearColor` | `float4` | r, g, b, a |
| `FIntPoint` | `int2` | |
| `FIntVector` | `int3` | |
| `FUintVector2` | `uint2` | |
| `FMatrix44f` | `float4x4` | |

### Always use the `f` types

Shaders work in single precision. Unreal's gameplay `FVector` and `FMatrix` are
**double** precision (since UE5). Use `FVector3f`, `FVector4f`, `FMatrix44f` in
shader parameter structs - never the plain ones.

If you have a gameplay `FVector2D` and need to pass it, convert explicitly:

```cpp
const FVector2f Drop(static_cast<float>(In.X), static_cast<float>(In.Y));
```

### The `float3` padding trap

GPUs lay out constant buffers in 16-byte rows, and a value is not allowed to
straddle a row boundary. So this:

```cpp
SHADER_PARAMETER(FVector3f, Position)   // 12 bytes
SHADER_PARAMETER(float,     Radius)     // 4 bytes
```

packs nicely into one 16-byte row. But this:

```cpp
SHADER_PARAMETER(float,     Radius)     // 4 bytes
SHADER_PARAMETER(FVector3f, Position)   // 12 bytes - would straddle, so it gets pushed
```

wastes 12 bytes of padding.

Unreal's macros handle the alignment correctly either way, so **your data will not
be corrupted**. But if you ever hand-write a struct or share one with a
`.ush` file, this bites. The habit that avoids it entirely:

> **Put big things first, small things last.** Matrices, then float4s, then float2s,
> then single floats and ints.

---

## `ModifyCompilationEnvironment`

New in this lesson:

```cpp
static void ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
    OutEnvironment.SetDefine(TEXT("GPULAB_THREADS_XY"), 8);
}
```

This runs at **shader compile time**, not at run time. It injects `#define` lines
into the HLSL before the compiler sees it.

That means:

```hlsl
#define THREADS GPULAB_THREADS_XY
[numthreads(THREADS, THREADS, 1)]
```

...now gets its group size from C++. One place to change it instead of two. Lesson
3 actually uses this.

The other big use is **permutations** - building the same `.usf` several different
ways (`#if QUALITY_HIGH`), and picking one at run time. That is beyond this lesson,
but `ModifyCompilationEnvironment` is where it starts.

**Important:** `SetDefine` values are baked in when the shader compiles. They are
**not** something you can change per frame. Anything that changes at run time must
be a `SHADER_PARAMETER`.

---

## Reading the shader maths

```hlsl
float2 Centered = UV - 0.5f;          // move origin to the middle
float Radius = length(Centered);      // distance from centre  -> rings
float Angle  = atan2(Centered.y, Centered.x);  // angle around centre -> spokes

float Wave = sin(Radius * Scale * 6.2831853f - Time * 2.0f + Angle * 3.0f);
float T    = Wave * 0.5f + 0.5f;      // sin gives -1..1, we want 0..1

OutTexture[PixelCoord] = float4(lerp(ColorA.rgb, ColorB.rgb, T), 1.0f);
```

Four functions worth knowing by heart:

- **`length(v)`** - how long is this vector. Distance, if you subtracted two points.
- **`atan2(y, x)`** - the angle of a direction, in radians, from -pi to +pi.
- **`sin(x)`** - waves between -1 and 1. `* 0.5 + 0.5` remaps it to 0..1.
- **`lerp(a, b, t)`** - `a` when `t` is 0, `b` when `t` is 1, a blend in between.

`6.2831853` is 2*pi. Multiplying by it turns "how many full cycles" into radians,
so `Scale = 8` means 8 rings.

Subtracting `Time` slides the pattern outward. Adding `Angle * 3.0` twists it into
a 3-armed spiral.

---

## Animating it

The node takes `Time` - it does not read the clock itself. That is deliberate: a
shader that is handed its time is easy to pause, scrub, and test.

In Blueprint:

```
Event Tick
   │
   ├─> Get Game Time in Seconds ──┐
   │                              ▼
   └───────────────> Lesson 02 Parameters (Output RT, Time, Scale, A, B)
```

---

## Try it yourself

1. **Swap the colours.** Feed different Color A / Color B from Blueprint. No
   rebuild needed - they're parameters.

2. **Add a new parameter.** Give it a `float Twist` and use it instead of the
   hard-coded `3.0` on `Angle`. You need **all three** edits:
   - `SHADER_PARAMETER(float, Twist)` in the struct
   - `float Twist;` in the `.usf`
   - `Parameters->Twist = Twist;` where you fill it in

   Plus the function argument and the `UFUNCTION` signature. Miss the `.usf` line
   and the shader will not compile. Miss the assignment and you get garbage.

3. **Make only the rings.** Delete `+ Angle * 3.0f`. Now only `Radius` matters, so
   you get clean concentric circles.

4. **Make only the spokes.** Delete `Radius * Scale * 6.2831853f` instead.

---

## Next

→ **[03 - Threads and Groups](03-Threads-And-Groups.md)** - see the parallelism.
