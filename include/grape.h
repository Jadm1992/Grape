#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <mutex>

namespace grape {

struct Color {
    uint8_t r, g, b;
};

struct Config {

#ifdef _WIN32
    std::string font_family = "C:\\Windows\\Fonts\\consola.ttf";
#else
    std::string font_family = "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf";
#endif
    int font_size = 14;

    Color background = {30, 30, 46};
    Color foreground = {205, 214, 244};

    // 16-color ANSI Palette (Catppuccin Mocha)
    Color palette[16] = {
        {69, 71, 90},    {243, 139, 168}, {166, 227, 161}, {249, 226, 175},
        {137, 180, 250}, {245, 194, 231}, {148, 226, 213}, {186, 194, 222},
        {88, 91, 112},   {243, 139, 168}, {166, 227, 161}, {249, 226, 175},
        {137, 180, 250}, {245, 194, 231}, {148, 226, 213}, {166, 173, 200}
    };
};

struct Quad {
    float x0, y0, x1, y1; // Screen coordinates
    float u0, v0, u1, v1; // Texture coordinates
    Color fg;
    Color bg;
};

// Represents the raw texture atlas containing the font glyphs
struct TextureAtlas {
    int width, height;
    std::vector<unsigned char> pixels; // 8-bit alpha mask
};

enum class Key {
    Unknown, Up, Down, Left, Right, Escape, Enter, Tab,
    Backspace, Insert, Delete, Home, End, PageUp, PageDown
};

const uint8_t ModNone  = 0;
const uint8_t ModShift = 1 << 0;
const uint8_t ModAlt   = 1 << 1;
const uint8_t ModCtrl  = 1 << 2;
const uint8_t ModLogo  = 1 << 3;

class Terminal {
public:
    Terminal(int cols, int rows, const Config& config = Config());
    ~Terminal();

    // Resize the terminal grid
    void resize(int cols, int rows);

    // Forward raw UTF-8 input text
    void writeInput(const char* data, size_t len);

    // Forward special keys and modified keystrokes
    void sendKey(Key key, uint8_t modifiers = ModNone, uint32_t unicode_char = 0);

    // Forward mouse wheel scroll events
    void scroll(double yoffset);

    // Get the generated font texture atlas (Host must upload this to GPU once, or when it changes)
    const TextureAtlas& getAtlas() const;

    // Get the quads to render for the current frame
    // x, y specify the top-left pixel position to render the grid
    const std::vector<Quad>& getQuads(float x, float y);

    // Returns true if the terminal needs to be redrawn
    bool hasChanged() const;

    int getCellWidth() const;
    int getCellHeight() const;

private:
    class Impl;
    Impl* pimpl;
};

} // namespace grape

// ============================================================================
// IMPLEMENTATION
// ============================================================================
#ifdef GRAPE_IMPLEMENTATION

#include <libtsm.h>
#include <thread>

// XKB Keysym Definitions (Used natively by libtsm)
#define GRAPE_XKB_KEY_NoSymbol 0
#define GRAPE_XKB_KEY_BackSpace 0xff08
#define GRAPE_XKB_KEY_Tab 0xff09
#define GRAPE_XKB_KEY_Return 0xff0d
#define GRAPE_XKB_KEY_Escape 0xff1b
#define GRAPE_XKB_KEY_Home 0xff50
#define GRAPE_XKB_KEY_Left 0xff51
#define GRAPE_XKB_KEY_Up 0xff52
#define GRAPE_XKB_KEY_Right 0xff53
#define GRAPE_XKB_KEY_Down 0xff54
#define GRAPE_XKB_KEY_Page_Up 0xff55
#define GRAPE_XKB_KEY_Page_Down 0xff56
#define GRAPE_XKB_KEY_End 0xff57
#define GRAPE_XKB_KEY_Insert 0xff63
#define GRAPE_XKB_KEY_Delete 0xffff
#include <atomic>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <cstring>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pty.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#endif

namespace grape {

struct GlyphInfo {
    float tx, ty, tw, th; // texture coords
    int advance;
    int bearingX, bearingY;
    int width, height;
};

class FontEngine {
public:
    stbtt_fontinfo font;
    std::vector<unsigned char> fontBuffer;
    TextureAtlas atlas;
    std::unordered_map<uint32_t, GlyphInfo> glyphs;
    int cellWidth, cellHeight;
    int baselineY;

    bool init(const std::string& path, int fontSize) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        fontBuffer.resize(size);
        if (!file.read((char*)fontBuffer.data(), size)) return false;

        if (!stbtt_InitFont(&font, fontBuffer.data(), 0)) return false;

        float scale = stbtt_ScaleForPixelHeight(&font, (float)fontSize);
        
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
        ascent = (int)std::round(ascent * scale);
        descent = (int)std::round(descent * scale);
        
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, 'M', &advance, &lsb);
        cellWidth = (int)std::round(advance * scale);
        cellHeight = ascent - descent;
        baselineY = ascent;

        atlas.width = 1024;
        atlas.height = 1024;
        atlas.pixels.resize(atlas.width * atlas.height, 0);
        atlas.pixels[0] = 255; // Reserve (0,0) as a solid white pixel for solid colored quads

        int penX = 1;
        int penY = 1;
        int shelfHeight = 0;

        // Ranges: ASCII, Latin-1, Box Drawing, Block Elements, Braille Patterns
        const uint32_t ranges[][2] = {
            {32, 127}, {160, 255}, {0x2500, 0x257F}, {0x2580, 0x259F}, {0x2800, 0x28FF}
        };

        for (auto range : ranges) {
            for (uint32_t c = range[0]; c <= range[1]; ++c) {
                int width = 0, height = 0, xoff = 0, yoff = 0;
                unsigned char* bitmap = stbtt_GetCodepointBitmap(&font, 0, scale, c, &width, &height, &xoff, &yoff);
                
                if (bitmap) {
                    if (penX + width + 1 >= atlas.width) {
                        penX = 1;
                        penY += shelfHeight + 1;
                        shelfHeight = 0;
                    }

                    if (penY + height + 1 < atlas.height) {
                        for (int row = 0; row < height; ++row) {
                            for (int col = 0; col < width; ++col) {
                                atlas.pixels[(penY + row) * atlas.width + (penX + col)] = bitmap[row * width + col];
                            }
                        }
                        
                        GlyphInfo g;
                        g.tx = (float)penX / atlas.width;
                        g.ty = (float)penY / atlas.height;
                        g.tw = (float)width / atlas.width;
                        g.th = (float)height / atlas.height;
                        g.width = width;
                        g.height = height;
                        g.bearingX = xoff;
                        g.bearingY = yoff;
                        g.advance = cellWidth;
                        
                        glyphs[c] = g;
                    }
                    
                    stbtt_FreeBitmap(bitmap, nullptr);
                    penX += width + 1;
                    if (height > shelfHeight) shelfHeight = height;
                }
            }
        }
        return true;
    }
};

class Terminal::Impl {
public:
    Config config;
    FontEngine fontEngine;
    std::vector<Quad> frameQuads;
    
    struct tsm_screen *screen = nullptr;
    struct tsm_vte *vte = nullptr;

    std::thread pty_thread;
    std::atomic<bool> running{true};
    std::atomic<bool> dirty{true};

#ifdef _WIN32
    HPCON hPC = INVALID_HANDLE_VALUE;
    HANDLE hAppRead = INVALID_HANDLE_VALUE;
    HANDLE hAppWrite = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {0};
#else
    int pty_master_fd = -1;
    pid_t pty_child_pid = -1;
#endif

    std::mutex tsm_mutex;


    float renderX = 0;
    float renderY = 0;

    Impl(int cols, int rows, const Config& cfg) : config(cfg) {
        if (!fontEngine.init(config.font_family, config.font_size)) {
            std::cerr << "ERROR: Failed to load font: " << config.font_family << "\n";
        }
        
        tsm_screen_new(&screen, tsm_log, this);
        tsm_vte_new(&vte, screen, tsm_write_cb, this, tsm_log, this);
        tsm_screen_resize(screen, cols, rows);
        tsm_screen_set_max_sb(screen, 10000);

#ifdef _WIN32
        HANDLE hPtyRead = INVALID_HANDLE_VALUE;
        HANDLE hPtyWrite = INVALID_HANDLE_VALUE;
        CreatePipe(&hPtyRead, &hAppWrite, NULL, 0); // AppWrite -> PtyRead
        CreatePipe(&hAppRead, &hPtyWrite, NULL, 0); // PtyWrite -> AppRead
        COORD size = {(SHORT)cols, (SHORT)rows};
        CreatePseudoConsole(size, hPtyRead, hPtyWrite, 0, &hPC);
        STARTUPINFOEXW siEx{0};
        siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        size_t bytesRequired;
        InitializeProcThreadAttributeList(NULL, 1, 0, &bytesRequired);
        siEx.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, bytesRequired);
        InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &bytesRequired);
        UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(hPC), NULL, NULL);
        wchar_t cmd[] = L"powershell.exe";
        CreateProcessW(NULL, cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &siEx.StartupInfo, &pi);
        CloseHandle(hPtyRead);
        CloseHandle(hPtyWrite);
        HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
#else
        pty_child_pid = forkpty(&pty_master_fd, NULL, NULL, NULL);
        if (pty_child_pid == 0) {
            setenv("TERM", "xterm-256color", 1);
            execlp(getenv("SHELL") ? getenv("SHELL") : "/bin/bash", getenv("SHELL") ? getenv("SHELL") : "/bin/bash", NULL);
            exit(1);
        }
#endif
        running = true;
        pty_thread = std::thread(&Terminal::Impl::ptyThreadFunc, this);
    }

    ~Impl() {
        running = false;
        if (pty_thread.joinable()) pty_thread.join();
#ifdef _WIN32
        if (hPC != INVALID_HANDLE_VALUE) ClosePseudoConsole(hPC);
        if (hAppRead != INVALID_HANDLE_VALUE) CloseHandle(hAppRead);
        if (hAppWrite != INVALID_HANDLE_VALUE) CloseHandle(hAppWrite);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
#else
        if (pty_master_fd != -1) close(pty_master_fd);
        if (pty_child_pid > 0) {
            int status;
            waitpid(pty_child_pid, &status, 0);
        }
#endif
        if (vte) tsm_vte_unref(vte);
        if (screen) tsm_screen_unref(screen);
    }

    static void tsm_log(void *data, const char *file, int line, const char *func, const char *subs, unsigned int sev, const char *format, va_list args) {}

    static void tsm_write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data) {
        Impl* term = static_cast<Impl*>(data);
#ifdef _WIN32
        if (term->hAppWrite != INVALID_HANDLE_VALUE) {
            DWORD bytesWritten;
            WriteFile(term->hAppWrite, u8, (DWORD)len, &bytesWritten, NULL);
        }
#else
        if (term->pty_master_fd != -1) {
            write(term->pty_master_fd, u8, len);
        }
#endif
    }

    static int draw_cb(struct tsm_screen *con, uint64_t id, const uint32_t *ch, size_t len, unsigned int width, unsigned int posx, unsigned int posy, const struct tsm_screen_attr *attr, tsm_age_t age, void *data) {
        Impl* term = static_cast<Impl*>(data);
        
        Color fg = term->config.foreground;
        Color bg = term->config.background;
        
        if (attr->fccode >= 0 && attr->fccode < 16) fg = term->config.palette[attr->fccode];
        else if (attr->fccode < 0) fg = {(uint8_t)attr->fr, (uint8_t)attr->fg, (uint8_t)attr->fb};

        if (attr->bccode >= 0 && attr->bccode < 16) bg = term->config.palette[attr->bccode];
        else if (attr->bccode < 0) bg = {(uint8_t)attr->br, (uint8_t)attr->bg, (uint8_t)attr->bb};

        if (attr->inverse) std::swap(fg, bg);

        uint32_t codepoint = (len > 0 && ch) ? ch[0] : ' ';
        
        // Background Quad (Solid)
        Quad bgQuad;
        bgQuad.x0 = term->renderX + posx * term->fontEngine.cellWidth;
        bgQuad.y0 = term->renderY + posy * term->fontEngine.cellHeight;
        bgQuad.x1 = bgQuad.x0 + term->fontEngine.cellWidth;
        bgQuad.y1 = bgQuad.y0 + term->fontEngine.cellHeight;
        bgQuad.u0 = bgQuad.v0 = bgQuad.u1 = bgQuad.v1 = 0; // Top-left of atlas (assumes blank pixel)
        bgQuad.fg = bg;
        bgQuad.bg = bg;
        term->frameQuads.push_back(bgQuad);

        // Foreground Glyph Quad
        if (codepoint != ' ') {
            auto it = term->fontEngine.glyphs.find(codepoint);
            if (it != term->fontEngine.glyphs.end()) {
                const GlyphInfo& g = it->second;
                Quad fgQuad;
                fgQuad.x0 = bgQuad.x0 + g.bearingX;
                fgQuad.y0 = bgQuad.y0 + term->fontEngine.baselineY + g.bearingY;
                fgQuad.x1 = fgQuad.x0 + g.width;
                fgQuad.y1 = fgQuad.y0 + g.height;
                fgQuad.u0 = g.tx;
                fgQuad.v0 = g.ty;
                fgQuad.u1 = g.tx + g.tw;
                fgQuad.v1 = g.ty + g.th;
                fgQuad.fg = fg;
                fgQuad.bg = bg;
                term->frameQuads.push_back(fgQuad);
            }
        }
        if (attr->underline) {
            Quad uQuad;
            uQuad.x0 = bgQuad.x0;
            uQuad.x1 = bgQuad.x1;
            uQuad.y1 = bgQuad.y1 - 2; // Bottom of cell
            uQuad.y0 = uQuad.y1 - 2;  // 2 pixels thick
            uQuad.u0 = 0.0f; uQuad.v0 = 0.0f; uQuad.u1 = 0.0f; uQuad.v1 = 0.0f;
            uQuad.fg = fg; uQuad.bg = fg;
            term->frameQuads.push_back(uQuad);
        }

        return 0;
    }

    void ptyThreadFunc() {
#ifdef _WIN32
        char buf[4096];
        DWORD bytesRead;
        while (running && ReadFile(hAppRead, buf, sizeof(buf), &bytesRead, NULL)) {
            std::lock_guard<std::mutex> lock(tsm_mutex);
            tsm_vte_input(vte, buf, bytesRead);
            dirty = true;
        }
#else
        char buf[4096];
        while (running) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(pty_master_fd, &read_fds);
            struct timeval timeout = {0, 10000};
            int ret = select(pty_master_fd + 1, &read_fds, NULL, NULL, &timeout);
            if (ret > 0 && FD_ISSET(pty_master_fd, &read_fds)) {
                int bytes = read(pty_master_fd, buf, sizeof(buf));
                if (bytes <= 0) break;
                std::lock_guard<std::mutex> lock(tsm_mutex);
                tsm_vte_input(vte, buf, bytes);
                dirty = true;
            }
        }
#endif
    }
};

Terminal::Terminal(int cols, int rows, const Config& config) {
    pimpl = new Impl(cols, rows, config);
}

Terminal::~Terminal() {
    delete pimpl;
}

void Terminal::resize(int cols, int rows) {
    std::lock_guard<std::mutex> lock(pimpl->tsm_mutex);
    if (cols <= 0 || rows <= 0) return;
    
    tsm_screen_resize(pimpl->screen, cols, rows);
#ifdef _WIN32
    if (pimpl->hPC != INVALID_HANDLE_VALUE) {
        COORD size = {(SHORT)cols, (SHORT)rows};
        ResizePseudoConsole(pimpl->hPC, size);
    }
#else
    if (pimpl->pty_master_fd != -1) {
        struct winsize ws;
        ws.ws_col = cols;
        ws.ws_row = rows;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl(pimpl->pty_master_fd, TIOCSWINSZ, &ws);
    }
#endif
    pimpl->dirty = true;
}

void Terminal::writeInput(const char* data, size_t len) {
#ifdef _WIN32
    if (pimpl->hAppWrite != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        WriteFile(pimpl->hAppWrite, data, (DWORD)len, &bytesWritten, NULL);
    }
#else
    if (pimpl->pty_master_fd != -1) {
        write(pimpl->pty_master_fd, data, len);
    }
#endif
    pimpl->dirty = true;
}

void Terminal::sendKey(Key key, uint8_t modifiers, uint32_t unicode_char) {
    uint32_t keysym = GRAPE_XKB_KEY_NoSymbol;
    switch (key) {
        case Key::Up: keysym = GRAPE_XKB_KEY_Up; break;
        case Key::Down: keysym = GRAPE_XKB_KEY_Down; break;
        case Key::Left: keysym = GRAPE_XKB_KEY_Left; break;
        case Key::Right: keysym = GRAPE_XKB_KEY_Right; break;
        case Key::Escape: keysym = GRAPE_XKB_KEY_Escape; break;
        case Key::Enter: keysym = GRAPE_XKB_KEY_Return; break;
        case Key::Tab: keysym = GRAPE_XKB_KEY_Tab; break;
        case Key::Backspace: keysym = GRAPE_XKB_KEY_BackSpace; break;
        case Key::Insert: keysym = GRAPE_XKB_KEY_Insert; break;
        case Key::Delete: keysym = GRAPE_XKB_KEY_Delete; break;
        case Key::Home: keysym = GRAPE_XKB_KEY_Home; break;
        case Key::End: keysym = GRAPE_XKB_KEY_End; break;
        case Key::PageUp: keysym = GRAPE_XKB_KEY_Page_Up; break;
        case Key::PageDown: keysym = GRAPE_XKB_KEY_Page_Down; break;
        default: break;
    }
    
    unsigned int tsm_mods = 0;
    if (modifiers & ModShift) tsm_mods |= TSM_SHIFT_MASK;
    if (modifiers & ModCtrl)  tsm_mods |= TSM_CONTROL_MASK;
    if (modifiers & ModAlt)   tsm_mods |= TSM_ALT_MASK;
    if (modifiers & ModLogo)  tsm_mods |= TSM_LOGO_MASK;

    std::lock_guard<std::mutex> lock(pimpl->tsm_mutex);
    if (pimpl->vte) {
        tsm_vte_handle_keyboard(pimpl->vte, keysym, unicode_char, tsm_mods, unicode_char);
    }
    pimpl->dirty = true;
}

void Terminal::scroll(double yoffset) {
    std::lock_guard<std::mutex> lock(pimpl->tsm_mutex);
    if (yoffset > 0) tsm_screen_sb_up(pimpl->screen, 3);
    else if (yoffset < 0) tsm_screen_sb_down(pimpl->screen, 3);
    pimpl->dirty = true;
}

const TextureAtlas& Terminal::getAtlas() const {
    return pimpl->fontEngine.atlas;
}

const std::vector<Quad>& Terminal::getQuads(float x, float y) {
    std::lock_guard<std::mutex> lock(pimpl->tsm_mutex);
    if (pimpl->dirty) {
        pimpl->renderX = x;
        pimpl->renderY = y;
        pimpl->frameQuads.clear();
        tsm_screen_draw(pimpl->screen, Impl::draw_cb, pimpl);
        pimpl->dirty = false;
        
        // Draw Cursor
        unsigned int cx = tsm_screen_get_cursor_x(pimpl->screen);
        unsigned int cy = tsm_screen_get_cursor_y(pimpl->screen);
        
        Quad cursorQuad;
        cursorQuad.x0 = x + cx * pimpl->fontEngine.cellWidth;
        cursorQuad.y0 = y + cy * pimpl->fontEngine.cellHeight;
        cursorQuad.x1 = cursorQuad.x0 + pimpl->fontEngine.cellWidth;
        cursorQuad.y1 = cursorQuad.y0 + pimpl->fontEngine.cellHeight;
        cursorQuad.u0 = cursorQuad.v0 = cursorQuad.u1 = cursorQuad.v1 = 0;
        cursorQuad.fg = pimpl->config.foreground;
        cursorQuad.bg = pimpl->config.foreground;
        pimpl->frameQuads.push_back(cursorQuad);
    }
    return pimpl->frameQuads;
}

bool Terminal::hasChanged() const {
    return pimpl->dirty.load();
}

int Terminal::getCellWidth() const {
    return pimpl->fontEngine.cellWidth;
}

int Terminal::getCellHeight() const {
    return pimpl->fontEngine.cellHeight;
}

} // namespace grape
#endif // GRAPE_IMPLEMENTATION
