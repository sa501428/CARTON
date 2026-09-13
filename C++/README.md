# CARTON C++

This directory contains the CARTON desktop application and the reusable C++ `.hic` data-access library.

## Description:
`carton` is a Qt 6 / Qt Quick desktop viewer for Hi-C contact matrices. The app keeps the UI shell in QML and renders the matrix through a custom C++ scene-graph item, so large heatmap content is batched into GPU geometry instead of being represented as QML objects.

The shared Straw C++ library provides local/HTTP `.hic` metadata inspection, sparse range reads, normalization support, block decompression, v10 support, and resolutions derived on demand. Set `STRAW_SOURCE_DIR` to its `C++` directory when it is not available at the default sibling path. Genomics annotation, signal, and interaction files are read through the separately reusable `igv-cpp` library.

## Building

The build requires CMake 3.24 or newer; Qt 6.5 or newer with Core, Gui, Quick,
Quick Controls 2, Concurrent, and SVG; libcurl; zlib; zstd; HTSlib 1.17+; and
pkg-config. Clone CARTON and `igv-cpp` as sibling repositories, or install
`igv-cpp` so CMake can find its package configuration.

The app icon is generated from `logo.svg` during the build. macOS receives an
`.icns` bundle icon, while Windows receives a multi-resolution `.ico` embedded
in both the executable and installer.

### macOS

```sh
cmake -S . -B build-carton -DCMAKE_BUILD_TYPE=Release
cmake --build build-carton --target carton --parallel
open build-carton/carton.app
```

To erase the build directory and rebuild everything from scratch, then run the
smoke tests:

```sh
./rebuild.sh
```

Pass `--debug` for a debug build, `--skip-tests` to stop after building, or
`--reset-settings` to also clear saved preferences (recent files, bookmarks,
display options) for a genuine first-run state.

To build the deployable, smoke-tested, ad-hoc-signed disk image:

```sh
./package_dmg.sh
```

The installer is written to `build-carton/CARTON-<version>-macOS.dmg`.

### Windows

Install the C++ build dependencies and Qt for the selected compiler, and install
[NSIS 3.03+](https://nsis.sourceforge.io/) so `makensis.exe` is in `PATH`. From
PowerShell, run:

```powershell
.\package_windows.ps1
```

The script builds in Release mode, runs Qt's Windows deployment tool, stages
non-Qt runtime DLLs, smoke-tests the deployed app, and creates
`build-carton-windows\CARTON-<version>-Windows.exe`. Extra CMake definitions,
such as a vcpkg toolchain file, can be passed with `-CMakeArgs`:

```powershell
.\package_windows.ps1 -CMakeArgs @(
  "-DCMAKE_TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake"
)
```

## Usage:
The desktop app supports:
1. Opening local `.hic` files
2. Header/metadata inspection
3. Chromosome and resolution switching
4. Observed/log/OE/expected matrix modes, plus VS mode with a loaded control `.hic`
5. Normalization switching
6. Smooth mouse-wheel zoom and drag pan
7. Asynchronous visible-range loading with a bounded record cache
8. Batched GPU heatmap rendering in a custom Qt Quick item
9. Five tab layouts: single-map, multi-map, multi-region, maps-by-region, and pairwise region matrix
10. Juicebox-compatible coloring: observed/expected use a linear white-to-red scale with an automatic 95th-percentile range, log is an explicit separate display mode, and OE uses a log-ratio blue-white-red scale with default threshold 5
11. Right-click map actions for undo/redo zoom, jump to diagonal, copy genomic position, and layer loading/clearing
12. Top and left 1D track panels for BED/bedGraph-like interval tracks, with 100 px default height and multi-file loading
13. 2D BEDPE-like annotation overlays with per-layer color overrides and both/above/below-diagonal placement controls
14. Direct interaction layer for wheel zoom, drag pan, double-click zoom, and Shift-drag region zoom
15. Coordinate lookup/jump fields and desktop menus for file, navigation, display, and layer actions
16. A session-scoped dataset registry: `.hic` metadata, parsed tracks, parsed annotations, and the bounded tile cache are shared when the same resource is used by multiple tabs or cells
17. Responsive multi-map grids with independently assigned maps, 1D tracks, and 2D annotations per cell
18. BEDPE-centered multi-region grids with a configurable fixed window, including interchromosomal regions
19. Transposable maps-by-region grids with shared, map-, region-, or cell-scoped layers
20. BED/BEDPE-derived pairwise N × N region matrices, with primary/control placement around the diagonal and either split-VS or blank diagonal cells
21. Versioned workspace import/export for all tab types and pooled resource references

The default tab is the single-map layout and retains the complete primary-map plus optional-control workflow. Multi-map tabs default to cell-scoped layers; the other layouts expose the scope selector appropriate to their shared axes. Editing a reused annotation layer uses copy-on-write, so the edited tab or cell receives a unique custom resource while the original pooled annotation remains unchanged.

For development and layout smoke testing, the application accepts `--tab-type=single|multi-map|multi-region|map-region|pairwise`, `--regions=<path>`, and `--regions-format=bed|bedpe|bedpe-as-bed`.

## Slice Format:
The slice format (.slc) is a binary format that contains:
1. Magic string "HICSLICE"
2. Resolution (int32)
3. Number of chromosomes (int32)
4. Chromosome mapping (name lengths, names, and keys)
5. Contact records (chr1Key, binX, chr2Key, binY, value)

## Reading Slice Files:
A C++ reader is provided in the slice_reader directory. It provides methods to:
1. Read basic file information (resolution, chromosomes)
2. Read all contact records
3. Read records for specific chromosome pairs
The reader automatically handles the chromosome ordering convention.

## Notes:
The simplified slice format is only intended for repeated analysis on a high resolution slice of the matrix. Otherwise, the original hic file format is more efficient.

The current renderer uses Qt Quick scene-graph geometry, which is QRhi-backed in Qt 6. CARTON explicitly requests Metal on macOS. The data-source, cache, and controller layers are separated so a lower-level explicit QRhi texture renderer can replace the scene-graph geometry item without changing the `.hic` reader or QML shell.

## Bug Reports or Feature Requests:
For bug reports or feature requests, please open an issue on the repository.
