# Architecture Overview

## System Diagram

```text
[Host Application] ---> [grape::Terminal]
       |                      |
       v                      v
   [OpenGL/GPU] <--- [TextureAtlas & Quads]
                              |
                     [libtsm (State Machine)] <--> [PTY (Bash/PowerShell)]
                              |
                    [FontEngine (stb_truetype)]
```

## Entry Points and Request Flow
1. **Initialization**: The host application creates a `grape::Terminal` instance. This spawns a background thread running a Pseudo-Terminal (PTY) — using `forkpty` on Unix or `CreatePseudoConsole` on Windows.
2. **Font Loading**: `FontEngine` loads a TTF font and generates a grayscale `TextureAtlas`.
3. **Input Handling**: Keyboard and mouse events from the host are forwarded to the `Terminal` via `sendKey()`, `writeInput()`, and `scroll()`. These are sent to the PTY.
4. **State Update**: The PTY output is parsed by `libtsm`, updating the internal grid of characters and attributes.
5. **Rendering**: The host queries `getQuads()`. The terminal maps the grid cells to textured quads using the `TextureAtlas` and returns them for the host to render.

## Module Responsibilities
- **Terminal**: The central API. Manages the PTY thread, the terminal state machine, and orchestrates rendering.
- **FontEngine**: Handles loading TrueType fonts and rasterizing glyphs into a single texture atlas.
- **libtsm (External)**: Processes ANSI escape sequences and maintains the logical terminal grid.
- **Host Application**: Responsible for window creation, OpenGL context management, input event capturing, and drawing the actual geometry returned by Grape.

## External Dependencies and Integrations
- **libtsm**: Core dependency for VT100/Xterm emulation.
- **OS PTY APIs**: Uses `forkpty` (Unix) and `ConPTY` (Windows) to spawn the shell.
- **stb_truetype**: Used for text rendering into the texture atlas.

## Data Flow
1. Shell (e.g., bash) writes text to PTY.
2. Background thread reads PTY, feeds text into `libtsm`.
3. Host calls `getQuads()`. `libtsm` triggers draw callbacks.
4. Callbacks generate `Quad` structs containing screen coordinates, texture coordinates, and colors.
5. Host uploads `TextureAtlas` to GPU and issues draw calls using the generated `Quad`s.
