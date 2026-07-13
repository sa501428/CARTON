# CARTON C++

This directory contains the CARTON desktop application and the reusable C++ `.hic` data-access library.

## Description:
`carton` is a Qt 6 / Qt Quick desktop viewer for Hi-C contact matrices. The app keeps the UI shell in QML and renders the matrix through a custom C++ scene-graph item, so large heatmap content is batched into GPU geometry instead of being represented as QML objects.

The `carton_hic` library provides local/HTTP `.hic` metadata inspection, sparse range reads, normalization support, and block decompression. `straw` is still built as a small debug executable for validating reader behavior from the terminal.

## Installation:
1. Requires CMake 3.13 or higher
2. Requires Qt 6, libcurl, zlib, and zstd development libraries
3. Clone the repository
4. Configure: `cmake -S . -B build-carton`
5. Build: `cmake --build build-carton --target carton straw -j4`
6. Run the desktop app: `open build-carton/carton.app`

## Usage:
The desktop app supports:
1. Opening local `.hic` files
2. Header/metadata inspection
3. Chromosome and resolution switching
4. Observed/OE/expected matrix modes
5. Normalization switching
6. Smooth mouse-wheel zoom and drag pan
7. Asynchronous visible-range loading with a bounded record cache
8. Batched GPU heatmap rendering in a custom Qt Quick item
9. Multiple independent tabs, each with its own file, viewport, cache, and display settings
10. White-to-red default heatmap colors with Viridis, blue-white-red, grayscale, and custom low/high color options
11. Right-click map actions for undo/redo zoom, jump to diagonal, copy genomic position, and layer loading/clearing
12. Top and left 1D track panels for BED/bedGraph-like interval tracks
13. 2D BEDPE-like annotation overlays with intrachromosomal reflection

The debug executable `straw` supports two modes:
1. Standard mode:
`straw [observed/oe/expected] <NONE/VC/VC_SQRT/KR> <hicFile> <chr1>[:x1:x2] <chr2>[:y1:y2] <BP/FRAG/MATRIX> <binsize>`
2. Dump mode (creates slice file):
`straw dump <observed/oe/expected> <NONE/VC/VC_SQRT/KR> <hicFile> <BP/FRAG> <binsize> <outputFile>`

## Examples:
1. Extract specific region:
`straw observed NONE input.hic chr1:0:1000000 chr2:0:1000000 BP 10000`
2. Create slice file at 10kb resolution:
`straw dump observed NONE input.hic BP 10000 output.slc`

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
