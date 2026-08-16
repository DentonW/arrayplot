# AI Disclaimer

The code in this repository was written by an AI coding assistant (Claude, Anthropic) in an interactive session with the repository owner, who provided the requirements and iterative feedback that shaped it.

## What this means

- All source under `client/`, `viewer/`, `examples/`, and `common/`, plus `CMakeLists.txt`, was AI-generated.
- The assistant had no compiler or CMake available in its working environment. This has been compiled and verified as working by the user.
- Dear ImGui, ImPlot, and Eigen are fetched from their upstream repositories at build time (CMake `FetchContent`) and are unmodified; their own licenses apply.

## Before relying on this code

Build it, run it, and review it yourself before using it for anything where correctness matters -- especially the debugger-integration workflow (Watch/Immediate window calls) and the named-pipe protocol between the client and viewer, which accepts local connections without any authentication and was written for single-user local debugging, not as a hardened service.
