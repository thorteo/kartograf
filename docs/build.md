# Building

## Current state of this machine

Checked 2026-09-21:

| Tool | Status |
|---|---|
| g++ 15.2 (Linux) | present |
| git | present |
| CMake 4.2.3 | installed |
| Ninja 1.13.2 | installed |
| Clang 21.1.8 | installed |
| MSVC / Visual Studio (Windows side) | not installed, and not needed — see Route B |
| xwin 0.10.0 + Windows SDK at `~/.xwin-sdk` | installed, 630 MB |
| WSL Windows interop (`cmd.exe`, `/mnt/c`) | working |
| Escape from Tarkov install | present, with live logs and screenshots |

Both builds work today. Route B below is set up and is the one in use: the Windows
overlay is cross-compiled from Linux and runs on Windows through WSL interop, so the
whole edit-build-run loop stays in one shell.

## Two routes to a Windows binary

### Route A — build on Windows with MSVC

Install **Visual Studio 2022 Build Tools** with the "Desktop development with C++"
workload and the Windows 10/11 SDK, plus CMake and Ninja. Build from a Developer
prompt, or from WSL by invoking the Windows toolchain through interop.

Pros: the reference configuration, full SDK, real debugger, no surprises with D3D11 or
DirectComposition headers.
Cons: several GB of install; iteration happens on the Windows side.

### Route B — cross-compile from WSL with clang-cl + xwin  ← in use

[`xwin`](https://github.com/Jake-Shadle/xwin) downloads the genuine Microsoft CRT and
Windows SDK headers and libraries, after which `clang-cl` cross-compiles to
`x86_64-pc-windows-msvc` from Linux.

Pros: the whole build lives on the Linux side where development happens; fast
iteration; easy to wire into CI.
Cons: setup is manual; the MinGW route is **not** a substitute — MinGW-w64's `dcomp.h`
and `d3d11.h` have historically been incomplete, with DirectComposition interfaces
added by patches over time. Only the real SDK headers, which is what `xwin` fetches,
are dependable here.

**In use:** Route B. Route A stays available for when a real Windows debugger is
needed.

Set up once:

```sh
xwin --accept-license --arch x86_64 --variant desktop splat --output ~/.xwin-sdk
```

That fetches the genuine Microsoft CRT and Windows SDK (630 MB), including `dcomp.h`,
`d3d11.h` and `dxgi1_2.h`, and creates the lowercase symlinks that make case-sensitive
Linux filesystems work with the SDK's mixed-case filenames. `cmake/windows-clang-cl.cmake`
picks it up from `~/.xwin-sdk`, or from `XWIN_ROOT` if set.

The toolchain pins the **static release CRT** in every configuration. xwin's splat
carries no debug CRT, and static linking is what we want regardless: the overlay ships
as a single .exe that needs no Visual C++ redistributable.

Note that WSL can execute the cross-compiled binary directly, so a build can be tested
without leaving the shell:

```sh
cmake --build --preset windows-dev && ./build/windows-dev/tarkov_help.exe
```

## Presets

```sh
# Core + tests, Linux, no Windows headers involved
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev

# Core + tests with ThreadSanitizer — worth running regularly, see ADR 0004
cmake --preset linux-tsan && ctest --preset linux-tsan

# Full overlay for Windows, cross-compiled from Linux
cmake --preset windows-dev
cmake --build --preset windows-dev
```

## Dependencies

Vendored in `third_party/`, no package manager:

| Library | Form |
|---|---|
| Dear ImGui | source tree, with the `win32` and `dx11` backends |
| nlohmann/json | single header |
| SQLite | amalgamation (`sqlite3.c` + `sqlite3.h`) |
| doctest | single header, tests only |

WinHTTP, Direct3D 11 and DirectComposition come from the Windows SDK.

## Running

The game must run in **borderless** or **windowed** mode. In exclusive fullscreen,
Windows will not composite the overlay and nothing will appear. The application should
detect this and say so, rather than looking broken.
