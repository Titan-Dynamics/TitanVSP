# Windows dev scripts

Prerequisites: Visual Studio 2022 (Desktop C++ workload), CMake, Git. Pandoc is optional (in-app help).

| Script | What it does |
| --- | --- |
| `build.ps1` | Builds Libraries (first time only) + OpenVSP, installs to `..\OpenVSP-build\install`. `-Clean` / `-CleanAll` to rebuild from scratch. |
| `run.ps1` | Incremental build, then launches `vsp.exe`. `-NoBuild` to just launch. |
| `install-shortcut.ps1` | Creates an "OpenVSP (Dev)" shortcut on the Desktop and Start Menu that runs `run.ps1`. |

Run any of them with `powershell -ExecutionPolicy Bypass -File dev\<script>.ps1`.

Syncing with upstream: `git fetch upstream; git merge upstream/main`.
