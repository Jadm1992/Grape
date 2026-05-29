# Include Module

## Purpose
This directory contains the public API for the Grape library. It defines the core interfaces and structures required to embed and interact with the terminal emulator.

## Key Files
- `grape.h`: The primary header file containing the library's public API, data structures, and the single-header implementation (if `GRAPE_IMPLEMENTATION` is defined).

## Public API

### Structs
- **`grape::Color`**
  - Description: Represents an RGB color.
- **`grape::Config`**
  - Description: Configuration settings for the terminal, including font family, size, and the 16-color ANSI palette.
- **`grape::Quad`**
  - Description: Represents a renderable character quad with screen and texture coordinates, foreground, and background colors.
- **`grape::TextureAtlas`**
  - Description: Contains the raw 8-bit alpha mask texture data generated from the font glyphs.

### Classes
- **`grape::Terminal`**
  - Description: The main terminal emulator class.
  - **`Terminal(int cols, int rows, const Config& config)`**: Initializes the terminal with specific dimensions and configuration.
  - **`void resize(int cols, int rows)`**: Resizes the terminal grid.
  - **`void writeInput(const char* data, size_t len)`**: Forwards raw UTF-8 input text to the PTY.
  - **`void sendKey(Key key, uint8_t modifiers, uint32_t unicode_char)`**: Forwards special keys and keystrokes.
  - **`void scroll(double yoffset)`**: Forwards mouse wheel scroll events.
  - **`const TextureAtlas& getAtlas() const`**: Retrieves the generated font texture atlas.
  - **`const std::vector<Quad>& getQuads(float x, float y)`**: Gets the quads to render for the current frame at the specified screen coordinates.
  - **`bool hasChanged() const`**: Returns true if the terminal needs to be redrawn.
  - **`int getCellWidth() const`**: Returns the pixel width of a single character cell.
  - **`int getCellHeight() const`**: Returns the pixel height of a single character cell.

## Dependencies
- Standard C++ Library (`<string>`, `<vector>`, `<cstdint>`, `<cstddef>`)
- Internal implementation dependencies (when `GRAPE_IMPLEMENTATION` is defined): `libtsm`, `stb_truetype.h`.

## Usage Examples
```cpp
grape::Config cfg;
grape::Terminal terminal(80, 24, cfg);
const auto& atlas = terminal.getAtlas();
// Upload atlas to GPU...
if (terminal.hasChanged()) {
    const auto& quads = terminal.getQuads(0, 0);
    // Render quads...
}
```
