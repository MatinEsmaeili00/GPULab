# 00 - Before You Start

Five minutes of checking now saves an hour of confusion later.

---

## What you need

**1. Unreal Engine 5.8.**

Other 5.x versions will mostly work, but some API names have moved between
versions. If you are on 5.3 or earlier, expect to fix a few compile errors —
`FUintVector2` and some RDG helpers are newer.

**2. A C++ Unreal project.** Not a Blueprint-only one.

Blueprint-only projects have no `Source/` folder and cannot compile plugins. If
yours is Blueprint-only, the quickest fix is **Tools → New C++ Class**, make any
empty class, and let the editor convert the project.

**3. Visual Studio 2022** with the **Game development with C++** workload.

You do not need to use Visual Studio as your editor — every build in this
tutorial is one command line. But the compiler and Windows SDK it installs are
required.

**4. Somewhere to type commands.** PowerShell, Command Prompt, or Git Bash.

---

## Check your setup

Open a terminal and run this, adjusting the version if yours differs:

```
dir "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat"
```

If that file exists, you are ready. **Write down that path** — you will use it
in every chapter.

If your engine is somewhere else (an Epic Games Launcher install on another
drive, or a source build), find `Build.bat` under
`<YourEngine>\Engine\Build\BatchFiles\` and use that path instead.

---

## The build command

This is the command you will run at the end of almost every chapter. Learn it
now:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
    YourProjectEditor Win64 Development ^
    -Project="D:\Path\To\YourProject.uproject" ^
    -WaitMutex
```

All on one line, or use `^` at the end of each line in Command Prompt / `` ` ``
in PowerShell to continue.

Three things to substitute:

| Placeholder | Replace with |
|---|---|
| `YourProjectEditor` | your project's name with `Editor` stuck on the end. Project `Foo` → target `FooEditor` |
| the `-Project=` path | the full path to your `.uproject` file |
| the Build.bat path | wherever your engine lives |

A successful build ends with:

```
Result: Succeeded
```

> ### ⚠️ The trap that will bite you
>
> **Close the Unreal editor before you build.**
>
> If the editor is open, the build fails in about three seconds with:
>
> ```
> Unable to build while Live Coding is active.
> ```
>
> Worse: closing the editor is not always enough. A process called
> `LiveCodingConsole.exe` can outlive it and keep holding the lock. If you get
> that message with no editor open, check for it:
>
> ```
> tasklist | findstr LiveCoding
> ```
>
> and kill it:
>
> ```
> taskkill /F /IM LiveCodingConsole.exe
> ```
>
> This will happen to you. Now you know what it is.

---

## What you are about to build

By the end of chapter 3 you will have this on screen:

```
   ┌─────────────────────────┐
   │▓▓▓▓▓▓▓▓▒▒▒▒▒▒░░░░░░     │     a red/green gradient,
   │▓▓▓▓▓▓▒▒▒▒▒▒░░░░░░░░     │     painted by a program
   │▓▓▓▓▒▒▒▒▒▒░░░░░░░░░░     │     you wrote, running on
   │▓▓▒▒▒▒▒▒░░░░░░░░░░░░     │     your graphics card
   │▒▒▒▒▒▒░░░░░░░░░░░░░░     │
   └─────────────────────────┘
```

It looks like nothing. It is not nothing: getting one custom compute shader to
run and show its output in Unreal touches the plugin system, the shader
compiler, the render thread, the render dependency graph, and the material
system. Once that pipeline works end to end, everything else is a variation.

By the end of chapter 9 you will have a water simulation that keeps running
frame after frame, entirely on the GPU.

---

## The vocabulary you need right now

Just five words. The rest you will pick up as you go.

| Word | Plain English |
|---|---|
| **Shader** | a small program that runs on the graphics card |
| **HLSL** | the language shaders are written in. Looks like C. |
| **`.usf`** | Unreal Shader File — a file of HLSL |
| **Compute shader** | a shader that is not drawing triangles, just doing work |
| **Render target** | a texture you can draw into, and then show on something |

If you want the full picture before you start typing, read
[Docs/00 - The Big Picture](../Docs/00-Big-Picture.md) first. It is short. But
you can also just start, and read it after chapter 2 when the words mean
something.

---

> ### ✅ Checkpoint
>
> Before moving on, you should be able to say yes to all of these:
>
> - [ ] I found `Build.bat` and wrote down its full path
> - [ ] My project is a C++ project (it has a `Source/` folder)
> - [ ] I know my project's target name (project name + `Editor`)
> - [ ] I know where my `.uproject` file is
> - [ ] I understand I must close the editor before building

---

→ Next: **[01 - Create The Plugin](01-Create-The-Plugin.md)**
