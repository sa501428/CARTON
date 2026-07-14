CARTON — Contact-map Analysis and Rendering with Track Overlays and Navigation

The desktop application lives in `C++/` and builds as a Qt 6 app:

```sh
cmake -S C++ -B C++/build-carton
cmake --build C++/build-carton --target carton -j4
open C++/build-carton/carton.app
```

The Straw `.hic` reader is compiled directly into the CARTON desktop application through the `carton_hic` library.
