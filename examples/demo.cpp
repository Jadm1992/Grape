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

#include <vector>

struct Vertex {
    float x, y;
    float u, v;
    unsigned char r, g, b, a;
};

int main() {
    if (!glfwInit()) {
        std::cerr << "ERROR: Failed to initialize GLFW.\n";
        return -1;
    }
    
    // We can use fixed pipeline OpenGL for this visual test since it's just quads
    GLFWwindow* window = glfwCreateWindow(800, 600, "Grape v2.0 - Headless Data Renderer Test", NULL, NULL);
    if (!window) {
        std::cerr << "ERROR: Failed to create GLFW window. Are you running over an SSH connection without a display?\n";
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetScrollCallback(window, scroll_callback);

    grape::Config cfg;
    grape::Terminal terminal(80, 24, cfg);
    terminal.onUpdate = []() {
        glfwPostEmptyEvent(); // Instantly wake up the main thread from glfwWaitEventsTimeout
    };
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
        glfwWaitEvents(); // Sleep CPU indefinitely until an event arrives (like onUpdate or input)

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

        int cols = width / terminal.getCellWidth();
        int rows = height / terminal.getCellHeight();
        static int last_cols = 0, last_rows = 0;
        if (cols != last_cols || rows != last_rows) {
            if (cols > 0 && rows > 0) {
                terminal.resize(cols, rows);
            }
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

            static std::vector<Vertex> vertices;
            vertices.clear();
            vertices.reserve(quads.size() * 4);

            for (const auto& q : quads) {
                bool isBg = (q.u0 == 0 && q.v0 == 0);
                grape::Color c = isBg ? q.bg : q.fg;
                float u0 = isBg ? 0.0f : q.u0;
                float v0 = isBg ? 0.0f : q.v0;
                float u1 = isBg ? 0.0f : q.u1;
                float v1 = isBg ? 0.0f : q.v1;
                
                vertices.push_back({ q.x0, q.y0, u0, v0, c.r, c.g, c.b, 255 });
                vertices.push_back({ q.x1, q.y0, u1, v0, c.r, c.g, c.b, 255 });
                vertices.push_back({ q.x1, q.y1, u1, v1, c.r, c.g, c.b, 255 });
                vertices.push_back({ q.x0, q.y1, u0, v1, c.r, c.g, c.b, 255 });
            }

            if (!vertices.empty()) {
                glEnableClientState(GL_VERTEX_ARRAY);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glEnableClientState(GL_COLOR_ARRAY);

                glVertexPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].x);
                glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].u);
                glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &vertices[0].r);

                glDrawArrays(GL_QUADS, 0, (GLsizei)vertices.size());

                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                glDisableClientState(GL_VERTEX_ARRAY);
            }

            glfwSwapBuffers(window);
        }
    }
    
    return 0;
}
