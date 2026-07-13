CARTON — Contact-map Analysis and Rendering with Track Overlays and Navigation

The desktop application lives in `C++/` and builds as a Qt 6 app:

```sh
cmake -S C++ -B C++/build-carton
cmake --build C++/build-carton --target carton straw -j4
open C++/build-carton/carton.app
```

`carton` is the main desktop viewer. `straw` remains a debug utility for validating `.hic` reader output.
