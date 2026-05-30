# Grape

Grape is a zero-dependency, GPU-accelerated terminal emulator library for C++.

It provides a lightweight, performant terminal emulator component that can be easily integrated into any C++ application. It handles the PTY, ANSI escape sequences, and font rasterization natively, outputting raw geometry (`std::vector<Quad>`) that you can draw using OpenGL, Vulkan, DirectX, or any custom engine renderer.

## Quick Start

### Prerequisites
To use the core library in your own project, you only need:
- C++17 compliant compiler
- `libtsm` (Terminal State Machine)

*Note: `glfw3` and OpenGL are only required if you want to build the example demo.*

### Usage
Grape is designed to be as easy to integrate as possible. To use it in your project:
1. Drop `include/grape.h` into your source tree.
2. Drop the `vendor/libtsm` directory into your source tree and compile its `.c` files alongside your application.

In exactly **one** C++ file, define the implementation macro before including the header:

```cpp
#define GRAPE_IMPLEMENTATION
#include "grape.h"

int main() {
    grape::Config cfg;
    cfg.font_size = 18;
    
    // Initialize the terminal (80 columns, 24 rows)
    grape::Terminal terminal(80, 24, cfg);
    
    // In your game/render loop:
    if (terminal.hasChanged()) {
        const auto& quads = terminal.getQuads(0, 0);
        // Feed the raw quads to your graphics API!
    }
    return 0;
}
```

### Building the Demo
If you want to test the included OpenGL hardware-accelerated demo (`examples/demo.cpp`), use CMake:
```bash
mkdir build
cd build
cmake ..
make
./demo
```

## Project Structure Overview
- `include/`: Contains the C++ interface and engine (`grape.h`).
- `vendor/libtsm/`: Vendored C implementation of the VT100 parser.
- `examples/`: Example applications demonstrating how to integrate Grape into a host application window.

## Key Internal Dependencies
- **libtsm**: Core state machine logic and VT100/ANSI parsing (compiled separately as C code).
- **stb_truetype**: Embedded directly inside `grape.h` for font loading and texture atlas rasterization.

## License
MIT License
