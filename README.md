# arrayplot

Live plotting for C++ arrays and Eigen matrices while debugging -- no VSIX, no fragile debugger-visualizer API. A standalone viewer app renders whatever you send it, and you send data either from normal code or (on Windows, in Visual Studio) straight out of the Watch/Immediate window while stopped at a breakpoint. Runs on both Windows and Linux.

## Why this exists

Visual Studio's native debugger doesn't have a built-in way to graph array/matrix contents, and the extensions that try to add one (e.g. ArrayPlotter64) do so via VS's Concord debugger-engine API, which is fragile and sparsely documented -- that's the likely reason those extensions tend to be unstable.

This takes a different approach: a tiny header-only client streams data over a local IPC channel to a plain, separately-running viewer app built on Dear ImGui + ImPlot. Nothing hooks into VS (or gdb/lldb) itself, so there's no extension to break across debugger or VS versions.

## Features

- 1D line plots for raw arrays, `std::vector<double>` / `std::vector<float>`
- 2D heatmaps with a single-hue (dark-to-light blue) colormap, index-labeled axes, and a colorbar showing the actual value range
- Eigen support: `MatrixXd`/`MatrixXf`, `VectorXd`/`VectorXf`, `ArrayXXd`/`ArrayXXf`, and arbitrary expressions (blocks, transposes, products, ...) via a generic template
- Complex Eigen matrices (`MatrixXcd`/`MatrixXcf`, `VectorXcd`/`VectorXcf`): magnitude, phase, real, and imaginary views, sent as separate named windows
- Windows: callable from ordinary code, the Immediate/Watch window mid-debug, or a conditional breakpoint for continuous updates as a loop runs
- Linux: callable from ordinary code (gdb/lldb function-call support varies -- see [Linux notes](#linux-notes))
- Viewer is a plain executable -- no VS extension install, works regardless of VS/debugger version

## Repository layout

```
arrayplot/
  CMakeLists.txt
  AI_DISCLAIMER.md
  common/
    protocol.h                        # wire format shared by client and viewer
  client/include/arrayplot/
    arrayplot.h                       # header-only client: plot1d/plot2d, std::vector overloads
    arrayplot_eigen.h                 # Eigen support, incl. complex matrices
  viewer/
    plot_store.h                      # shared: data store, message parsing, ImGui/ImPlot draw code
    main_win32.cpp                    # Windows: Win32 + DirectX11 + named-pipe server
    main_linux.cpp                    # Linux: GLFW + OpenGL3 + Unix-domain-socket server
  examples/
    example.cpp                       # sine wave + real Eigen matrix
    example_eigen.cpp                 # real + complex Eigen matrix demo
```

## Building

Common requirements: CMake 3.20+, and internet access on first configure (CMake `FetchContent` pulls Dear ImGui, ImPlot, and (on Linux) GLFW from their repos, plus Eigen if it isn't already found via `find_package`).

Build targets on both platforms:

- `arrayplot_viewer` -- the standalone viewer app
- `arrayplot_example` -- sine wave + real Eigen matrix demo
- `arrayplot_example_eigen` -- real + complex Eigen matrix demo

### Windows

Requirements: Visual Studio with the C++ workload. Open the folder via `File > Open > CMake...` and point it at `CMakeLists.txt` -- VS's built-in CMake integration configures and generates automatically.

### Linux

Requirements: a C++17 compiler, and X11/OpenGL development headers for GLFW to build against, e.g. on Debian/Ubuntu:
```bash
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
```
Then:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## Try it

```bash
./arrayplot_viewer      # arrayplot_viewer.exe on Windows
```
then, in another terminal:
```bash
./arrayplot_example_eigen
```
You should see line plots and heatmaps update live as the demo runs.

## Using it in your own project

Add `client/include` and `common` to your include path (or `add_subdirectory(arrayplot)` and link the `arrayplot_client` interface target), then:

```cpp
#include <arrayplot/arrayplot.h>

aplot::plot1d("residual", data.data(), data.size());
aplot::plot1d("wave", myVector);          // std::vector<double> / <float>
aplot::plot2d("grid", ptr, rows, cols, /*rowMajor=*/true);
```

For Eigen (include Eigen's own headers first):

```cpp
#include <Eigen/Dense>
#include <arrayplot/arrayplot_eigen.h>

aplot::plot("J", jacobian);               // MatrixXd/Xf, VectorXd/Xf, ArrayXXd/Xf, or any expression

// complex matrices -- plot() sends magnitude + phase by default:
aplot::plot("H", complexMatrix);          // MatrixXcd/Xcf, VectorXcd/Xcf
aplot::plot_real("H", complexMatrix);     // or pick a specific view directly
aplot::plot_imag("H", complexMatrix);
aplot::plot_magnitude("H", complexMatrix);
aplot::plot_phase("H", complexMatrix);
```

Calls silently no-op if `arrayplot_viewer` isn't running.

## Plotting from the Watch/Immediate window (Windows/Visual Studio)

You can call these functions interactively while stopped at a breakpoint, but VS's native expression evaluator has two limitations worth knowing about:

1. **It can only call functions that already exist as compiled, non-inlined symbols.** If a given overload is never actually called anywhere in your program, it may never get emitted and you'll see `identifier is undefined`. If you need one that isn't otherwise used, force it to compile with a no-op call near your breakpoint: `if (false) aplot::plot1d("warmup", myVector);`.
2. **It does not deduce template arguments.** A bare call like `aplot::plot1d("x", ptr, n)` will fail; you'd need `aplot::plot1d<double>("x", ptr, n)`. This is also why `arrayplot.h`/`arrayplot_eigen.h` provide concrete (non-template) overloads for `std::vector<double>`, `Eigen::MatrixXd`, and similar common types -- prefer those from the Watch window, e.g. `aplot::plot1d("x", myVector)` or `aplot::plot("m", myMatrix)`, since they need no explicit template argument and don't require the evaluator to call trivial STL/Eigen accessor functions directly.

For a live-updating plot without single-stepping, set a breakpoint's **condition** to a call with a side effect that always evaluates false, so it never actually stops execution:

```
aplot::plot1d("x", myVector), false
```

## Linux notes

The client and viewer work the same way as on Windows, just over a Unix domain socket instead of a named pipe -- `aplot::plot1d(...)` / `aplot::plot(...)` calls from ordinary code work unchanged.

The Watch/Immediate-window and conditional-breakpoint tricks above are VS-specific. gdb and lldb *can* call functions interactively (gdb: `call aplot::plot1d("x", myVector)`; lldb: `expr aplot::plot1d("x", myVector)`), and the same two caveats apply (the function must already be a real compiled symbol, and template arguments aren't deduced) -- but this hasn't been tried against real gdb/lldb sessions, only reasoned about, so treat it as a starting point rather than a verified workflow.

## Protocol

Client and viewer talk over a local IPC channel defined in `common/protocol.h`: a Windows named pipe (`\\.\pipe\arrayplot`) on Windows, a Unix domain socket (`/tmp/arrayplot.sock`) on Linux. Message format is identical on both: a small binary header (name length, dtype, rows, cols, row-major flag, payload size) followed by the name and the raw data. Each distinct name becomes its own window in the viewer, updated in place on every call.

## Notes

- Windows and Linux only (the viewer uses Win32 + DirectX11 on Windows, GLFW + OpenGL3 on Linux).
- Neither transport has authentication -- this is built for local, single-user debugging, not as a network-facing service.
- The Linux viewer/socket path is new and, unlike the Windows build, has not been compiled or run by the author -- see [AI_DISCLAIMER.md](AI_DISCLAIMER.md).

## License

MIT -- see [LICENSE](LICENSE). Dear ImGui, ImPlot, and Eigen are fetched separately at build time and remain under their own licenses (MIT, MIT, and MPL2 respectively).
