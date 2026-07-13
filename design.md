Design a desktop-first, high-performance scientific data visualization application using **C++20/23, Qt 6, Qt Quick/QML, and a custom QRhi-based GPU renderer**. Target **macOS first using Metal**, with future support for **Windows through Direct3D** and **Linux through Vulkan or OpenGL**.

The application should render extremely large local or remote datasets as tiled, GPU-accelerated heat maps, bar charts, and other custom visualizations. It must support smooth pan and zoom, asynchronous range-based data loading, multiresolution tiles, background decoding, bounded CPU/GPU caches, level-of-detail rendering, and minimal copying between storage, memory, and GPU resources.

Use **Qt Quick/QML** for the modern application shell, including tabs, panels, toolbars, dialogs, menus, inspectors, and lightweight overlays. Keep large-scale visualization content out of the QML object hierarchy and render it through **QRhi shaders, textures, buffers, instancing, and GPU compute where appropriate**.

Support interactive annotations and overlays such as rectangles, polygons, labels, selection regions, measurement tools, hover states, and editing handles. Store persistent annotations in data coordinates, use viewport culling and spatial indexing, and render large annotation sets in batches.

Maintain a clean separation between the **data-source layer, tile scheduler, cache system, document model, annotation model, UI layer, and renderer**. Heavy I/O, parsing, decompression, aggregation, and GPU uploads must run outside the UI thread. The system should remain responsive while visualizing datasets much larger than RAM and should be structured for long-term maintainability, extensibility, undo/redo, export, and cross-platform deployment.
