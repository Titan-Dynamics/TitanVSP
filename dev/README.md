# Windows dev scripts

Prerequisites: Visual Studio 2022 (Desktop C++ workload), CMake, Git, SWIG, Python 3 with numpy.
Pandoc is optional (in-app help).

**Compile and open TitanVSP:** `powershell -ExecutionPolicy Bypass -File dev\run.ps1` (or the "TitanVSP (Dev)" shortcut).

| Script | What it does |
| --- | --- |
| `build.ps1` | Builds Libraries (first time only) + TitanVSP, installs to `..\OpenVSP-build\install`. `-Clean` / `-CleanAll` to rebuild from scratch. |
| `run.ps1` | Incremental build, then launches `vsp.exe`. `-NoBuild` to just launch. |
| `install-shortcut.ps1` | Creates a "TitanVSP (Dev)" shortcut on the Desktop and Start Menu that runs `run.ps1`. |

Run any of them with `powershell -ExecutionPolicy Bypass -File dev\<script>.ps1`.

Releases aren't built here: every push to `main` builds the zip on GitHub Actions
(`.github/workflows/release.yml`) and publishes it as the next release, replacing the previous one.

## Python API

The build creates a venv with the `openvsp` package installed in editable mode from the build tree,
so after changing the API (`src/geom_api/`) and rebuilding, the venv uses the new bindings automatically.

```powershell
..\OpenVSP-build\vsp\venv\Scripts\Activate.ps1
python -c "import openvsp as vsp; print(vsp.GetVSPVersion())"
```

## Syncing with upstream

TitanVSP's history starts from a squashed import of OpenVSP 3.55.0, so upstream releases are applied
as patches rather than merged. See "Keeping up with upstream" in the main [README](../README.md).
