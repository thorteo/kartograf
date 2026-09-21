# Building

## Current state of this machine

Checked 2026-09-21:

| Tool | Status |
|---|---|
| g++ 15.2 (Linux) | present |
| git | present |
| CMake | **missing** |
| Ninja | **missing** |
| Clang | **missing** |
| MSVC / Visual Studio (Windows side) | **missing** |
| WSL Windows interop (`cmd.exe`, `/mnt/c`) | working |
| Escape from Tarkov install | present, with live logs and screenshots |

So nothing compiles yet. The toolchain has to be set up first, and there is a choice to
make about how.

## Two routes to a Windows binary

### Route A — build on Windows with MSVC

Install **Visual Studio 2022 Build Tools** with the "Desktop development with C++"
workload and the Windows 10/11 SDK, plus CMake and Ninja. Build from a Developer
prompt, or from WSL by invoking the Windows toolchain through interop.

Pros: the reference configuration, full SDK, real debugger, no surprises with D3D11 or
DirectComposition headers.
Cons: several GB of install; iteration happens on the Windows side.

### Route B — cross-compile from WSL with clang-cl + xwin

[`xwin`](https://github.com/Jake-Shadle/xwin) downloads the genuine Microsoft CRT and
Windows SDK headers and libraries, after which `clang-cl` cross-compiles to
`x86_64-pc-windows-msvc` from Linux.

Pros: the whole build lives on the Linux side where development happens; fast
iteration; easy to wire into CI.
Cons: setup is manual; the MinGW route is **not** a substitute — MinGW-w64's `dcomp.h`
and `d3d11.h` have historically been incomplete, with DirectComposition interfaces
added by patches over time. Only the real SDK headers, which is what `xwin` fetches,
are dependable here.

**Recommendation:** Route B as the primary path, because it keeps development where the
developer is, with Route A available when a real Windows debugger is needed.

Either way, the Linux core build below needs only CMake, Ninja and g++.

## Planned presets

```sh
# Core + tests, Linux, no Windows headers involved
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev

# Core + tests with ThreadSanitizer — worth running regularly, see ADR 0004
cmake --preset linux-tsan && ctest --preset linux-tsan

# Full overlay for Windows
cmake --preset windows-release
cmake --build --preset windows-release
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
