#define GRAPE_IMPLEMENTATION
#include <grape.h>
#include <GLFW/glfw3.h>
#include <iostream>

grape::Terminal* global_term = nullptr;

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        uint8_t gmods = grape::ModNone;
        if (mods & GLFW_MOD_SHIFT) gmods |= grape::ModShift;
        if (mods & GLFW_MOD_CONTROL) gmods |= grape::ModCtrl;
        if (mods & GLFW_MOD_ALT) gmods |= grape::ModAlt;

        grape::Key gkey = grape::Key::Unknown;
        switch (key) {
            case GLFW_KEY_UP: gkey = grape::Key::Up; break;
            case GLFW_KEY_DOWN: gkey = grape::Key::Down; break;
            case GLFW_KEY_LEFT: gkey = grape::Key::Left; break;
            case GLFW_KEY_RIGHT: gkey = grape::Key::Right; break;
            case GLFW_KEY_ESCAPE: gkey = grape::Key::Escape; break;
            case GLFW_KEY_ENTER: gkey = grape::Key::Enter; break;
            case GLFW_KEY_TAB: gkey = grape::Key::Tab; break;
            case GLFW_KEY_BACKSPACE: gkey = grape::Key::Backspace; break;
            case GLFW_KEY_PAGE_UP: gkey = grape::Key::PageUp; break;
            case GLFW_KEY_PAGE_DOWN: gkey = grape::Key::PageDown; break;
        }

        if (gkey != grape::Key::Unknown) {
            global_term->sendKey(gkey, gmods);
        } else if (mods & GLFW_MOD_CONTROL && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            global_term->sendKey(grape::Key::Unknown, gmods, 'a' + (key - GLFW_KEY_A));
        }
    }
}

void char_callback(GLFWwindow* window, unsigned int codepoint) {
    if (codepoint < 128) {
        char c = (char)codepoint;
        global_term->writeInput(&c, 1);
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    global_term->scroll(yoffset);
}

int main() {
    if (!glfwInit()) return -1;
    
    // We can use fixed pipeline OpenGL for this visual test since it's just quads
    GLFWwindow* window = glfwCreateWindow(800, 600, "Grape v2.0 - Headless Data Renderer Test", NULL, NULL);
    if (!window) return -1;
    
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetScrollCallback(window, scroll_callback);

    grape::Config cfg;
    grape::Terminal terminal(80, 24, cfg);
    global_term = &terminal;

    // 1. Host Application: Upload the atlas to the GPU
    const auto& atlas = terminal.getAtlas();
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Convert 8-bit alpha mask to RGBA for OpenGL 1.1 fixed pipeline
    std::vector<uint8_t> rgba(atlas.width * atlas.height * 4, 255);
    for (size_t i = 0; i < atlas.pixels.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = atlas.pixels[i];
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width, atlas.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    while (!glfwWindowShouldClose(window)) {
        glfwWaitEventsTimeout(0.016); // Sleep CPU, cap at ~60fps if VSync fails

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

        int cols = width / terminal.getCellWidth();
        int rows = height / terminal.getCellHeight();
        static int last_cols = 0, last_rows = 0;
        if (cols > 0 && rows > 0 && (cols != last_cols || rows != last_rows)) {
            terminal.resize(cols, rows);
            last_cols = cols;
            last_rows = rows;
        }
        
        // Setup Ortho projection matrix
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width, height, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        if (terminal.hasChanged()) {
            const auto& quads = terminal.getQuads(0, 0);
            
            glClearColor(30.0f/255.0f, 30.0f/255.0f, 46.0f/255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glBindTexture(GL_TEXTURE_2D, texture);
            glBegin(GL_QUADS);
            for (const auto& q : quads) {
                if (q.u0 == 0 && q.v0 == 0) {
                    glColor4ub(q.bg.r, q.bg.g, q.bg.b, 255);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(q.x0, q.y0);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(q.x1, q.y0);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(q.x1, q.y1);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(q.x0, q.y1);
                } else {
                    glColor4ub(q.fg.r, q.fg.g, q.fg.b, 255);
                    glTexCoord2f(q.u0, q.v0); glVertex2f(q.x0, q.y0);
                    glTexCoord2f(q.u1, q.v0); glVertex2f(q.x1, q.y0);
                    glTexCoord2f(q.u1, q.v1); glVertex2f(q.x1, q.y1);
                    glTexCoord2f(q.u0, q.v1); glVertex2f(q.x0, q.y1);
                }
            }
            glEnd();

            glfwSwapBuffers(window);
        }
    }
    
    return 0;
}
