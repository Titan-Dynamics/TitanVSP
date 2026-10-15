# Changes in TitanVSP

TitanVSP is [OpenVSP](https://github.com/OpenVSP/OpenVSP) 3.55.0 with the changes below. Its
history starts with one commit importing OpenVSP 3.55.0 (upstream tag `OpenVSP_3.55.0`); every
commit after that is TitanVSP's own.

File formats, the `openvsp` Python module and the API are unchanged apart from the new `FILLET`
component, so OpenVSP models and scripts work as they are.

- [Releases](#releases)
- [Fillet component](#fillet-component)
- [Dark theme](#dark-theme)
- [Perspective view](#perspective-view)
- [Windows file dialog](#windows-file-dialog)
- [VSPAERO results plots](#vspaero-results-plots)
- [TitanVSP name](#titanvsp-name)
- [Windows build scripts and Python API](#windows-build-scripts-and-python-api)
- [Removed](#removed)

## Releases

| Release | What's in it |
| --- | --- |
| [v3.55.0-titan.3](https://github.com/Titan-Dynamics/TitanVSP/releases/tag/v3.55.0-titan.3) | Everything on this page. |
| v3.55.0-titan.2 (withdrawn) | Everything except the VSPAERO results plot changes. It was replaced by titan.3. |
| v3.55.0-titan.1 (withdrawn) | The Fillet component, the TitanVSP name, and the Windows build scripts. It was replaced by titan.2. |

## Fillet component

A new component type, **FILLET**, blends one part into another with a smooth, concave
rolling-ball fillet: the shape a ball of fixed radius sweeps as it rolls along the junction
touching both parts.

| Add the Fillet to | It blends into | Shape |
| --- | --- | --- |
| a **Wing** | the Fuselage, Pod or Stack its root is buried in | Runs round the wing root from the lower trailing edge to the upper one, and tapers to a point at each end of the trailing edge |
| a **Fuselage, Pod or Stack** | the Fuselage, Pod, Stack or Wing it overlaps most | A closed ring round each loop where they meet; a body passing right through another gets two rings |

- **A separate body.** The fillet is its own closed, watertight body overlapping both parts, and
  it leaves them untouched. Exported to STL, it's a closed, outward-facing 2-manifold, so it
  unions cleanly with the parts in Blender and other mesh tools.
- **Finds its partner.** The other part is found on its own and saved with the model. The fillet
  rebuilds whenever either part moves or changes.
- **Built in place.** Its position, rotation and symmetry are fixed and greyed out, because it's
  built in world coordinates. Each mirrored copy of a symmetric part gets its own fillet.
- **Parameters** (all in the `Design` group):

  | Parameter | Default | What it does |
  | --- | --- | --- |
  | `Radius` | 0 (auto) | Ball radius. At 0 the first build picks 8% of the wing's root chord, or 8% of the slimmer body's thickness, and fills it in. |
  | `NumStations` | 41 (9–401) | Cross-sections round the junction. More are added wherever the fillet turns quickly. |
  | `TETaper` | 0.15 (0–1) | Wing roots only: the fraction of each side over which the radius tapers to nothing at the trailing edge. 0 and greyed out on body fillets. |
  | `Sink` | 1.0 | How far the fillet's face carries on past each contact, easing under the surface, so it fades in instead of ending on a line. In model units, capped at 4 × Radius. |

- **Status line.** The Fillet's **Design** tab says what it's blending into, or why it couldn't
  build: the root isn't in a body, or the ball doesn't fit all the way round a junction.
- **API.** `AddGeom("FILLET", parent_id)` from Python, AngelScript or C++. It fails if the parent
  isn't a Wing, Fuselage, Pod or Stack. Scaling the model scales `Radius` and `Sink`.
- **Speed.** A rebuild takes about 0.1–1.5 s, depending on the junction.

How it works is described in [AGENTS.md](AGENTS.md#fillet-component) and at the top of
`src/geom_core/FilletGeom.cpp`.

**Limits:** a wing fillet only blends the wing's root (its first section) into a body. A body
join must be a closed loop that doesn't cross a flat end cap or a sharp edge.

## Dark theme

The whole interface is dark.

- **Windows and controls:** dark backgrounds, darker inputs and lists, light text and blue
  selection. Section divider bars, read-only fields, tooltips, the console, the attribute tree
  and the CompGeom and DegenGeom result panels all follow the theme.
- **Title bars:** every window asks Windows for a dark title bar (Windows 10 and later).
- **3D view:** the default background is dark slate. Axis labels and colour-scale labels are
  light. **Reset** in Background settings goes back to the dark default.
- **Line colours:** new components draw in light grey (#aaa) instead of blue. Cross-section and
  sub-surface lines that were black are #aaa too. The selected component's outline, its active
  cross-section, the active sub-surface, the picking hover highlight and the selected viewport's
  border are white instead of red. A wing's active-section box stays green.
- **XSec View:** the airfoil window opened by **Show** draws its curve in white, and its
  **Reset Defaults** button (and the Curve Editor's) goes back to white.

Models keep the colours saved in them, so only new components pick up #aaa.

**VSPAERO Viewer** (`vspviewer.exe`) is dark too: its panels, menus, buttons and title bar use the
same palette, and its 3D view opens on the same dark slate background, with light text and
legend. Trailing wakes draw light blue. **View → Use White Background** switches the view to
white, with the original colours, for printing or screenshots.

The VSPAERO Viewer's 3D view also takes the mouse like TitanVSP's main 3D view: left-drag
rotates; right-drag or Alt+left-drag pans, with the model following the cursor; middle-drag,
Ctrl+left-drag or left+right-drag zooms; and the mouse wheel zooms about the point under the
cursor.

## Perspective view

**View → Perspective** (key **5**) switches every viewport between orthographic and perspective
(30° field of view). The perspective eye stands back far enough that the view keeps the same
framing, so rotate, pan and zoom carry over and switching back and forth doesn't jump. The screen
grid, rulers, probes and picking work the same in both modes.

The perspective code already in OpenVSP passed degrees where radians were needed. It has been
replaced.

## Windows file dialog

Open, save, import and export use the operating system's own file dialog by default, instead of
OpenVSP's built-in chooser. The built-in one, with its favourite folders, is still available under
**File → Preferences… → File Chooser**. A choice already saved there is kept.

## VSPAERO results plots

The plots in **Results Manager - VSPAERO** (Convergence, Load Dist., Sweep, Cp Slice and
Unsteady) can be explored with the mouse:

- **Pan:** drag the plot.
- **Zoom:** roll the mouse wheel to zoom in or out about the point under the cursor. Hold
  **Ctrl** to zoom only along x, or **Shift** to zoom only along y.
- **Reset:** double-click to go back to the automatic fit.
- **Read values:** hovering shows a crosshair and the x and y under the cursor in the plot's
  top-right corner.

Panning or zooming sets the plot's Xmin, Xmax, Ymin and Ymax under **Actions** to manual at the
new limits, so the view stays put when the plot refreshes. Unticking them, or double-clicking,
hands them back to the automatic fit.

The lines x = 0 and y = 0 draw light over the grid, and the grid is shaded for the dark
background. This applies to every plot that uses the same graph widget, such as Wave Drag.

The greyed-out Time Step box could show -2147483648 instead of 0 when there are no unsteady
results. That upstream bug, an overflowing conversion of the box's limits, is fixed.

## TitanVSP name

The app calls itself TitanVSP 3.55.0 everywhere a user sees a name: window titles, the `-help`
banner, `GetVSPVersion()`, the headers of exported STEP, IGES, NASTRAN, Calculix and DXF files,
preferences and messages, and the release zip (`TitanVSP-3.55.0-win64.zip`).

Kept as they were, because code, files or settings depend on them: exe and library names, the
`openvsp` Python module and API names, file format markers (`.vsp3`, `OPENVSP_XSEC_FILE_V1`
and so on), the `FC_OPENVSP` enum, the preferences store, links to the OpenVSP wiki and help,
and the macOS and Linux package names.

## Windows build scripts and Python API

- `dev\build.ps1` builds the third-party libraries (first time only) and TitanVSP, and installs
  it next to the repo. `-Clean` and `-CleanAll` rebuild from scratch.
- `dev\run.ps1` does an incremental build and launches `vsp.exe`. `-NoBuild` just launches it.
- `dev\install-shortcut.ps1` adds a one-click "TitanVSP (Dev)" shortcut that runs `run.ps1`.
- The Windows build enables the Python API and sets up a venv with the `openvsp` package installed
  in editable mode, so it picks up rebuilt bindings automatically.
- The packaged Python `environment.yml` pins the Python version the bindings were built for.
  Upstream left it blank, so conda could build an environment the bindings couldn't load.

See [README.md](README.md#build-and-run) and [dev/README.md](dev/README.md).

## Removed

Nothing from upstream OpenVSP 3.55.0 is removed. An early TitanVSP command, **Model → Blend Wings to
Fuselages** (`BlendWingsAndFuselages` in the API), inserted thin airfoil sections near each wing
root. The Fillet component replaced it before the first release.
