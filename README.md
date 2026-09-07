CARTON — Contact-map Analysis and Rendering with Track Overlays and Navigation

The desktop application lives in `C++/` and builds as a Qt 6 app:

```sh
cmake -S C++ -B C++/build-carton
cmake --build C++/build-carton --target carton -j4
open C++/build-carton/carton.app
```

CARTON links the shared Straw C++ library for `.hic` metadata and matrix queries, including v10 and resolutions derived on demand. CARTON supports single-map, multi-map, multi-region, maps-by-region, and pairwise-region tabs backed by a shared session dataset/cache registry; see [C++/README.md](C++/README.md) for the full feature and usage notes.

Native installers are produced from `C++/logo.svg`: run
`C++/package_dmg.sh` on macOS for a `.dmg`, or
`C++/package_windows.ps1` on Windows for an NSIS installer `.exe`.
