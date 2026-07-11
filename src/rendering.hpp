#pragma once
#include "main.hpp"

struct GLState {
    GLint program;
    GLint vao;
    GLint fbo;
    GLint viewport[4];
    GLint scissor[4];
    GLboolean blend;
    GLboolean scissorTest;
};

static void SaveGL(GLState& s) {
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
    glGetIntegerv(GL_VIEWPORT, s.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, s.scissor);
    s.blend = glIsEnabled(GL_BLEND);
    s.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
}

static void RestoreGL(const GLState& s) {
    glUseProgram(s.program);
    glBindVertexArray(s.vao);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glViewport(
        s.viewport[0], s.viewport[1],
        s.viewport[2], s.viewport[3]
    );
    glScissor(
        s.scissor[0], s.scissor[1],
        s.scissor[2], s.scissor[3]
    );
    s.blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    s.scissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
}

extern void DrawMenu();

inline void UpdateBounds(int index) {
    std::lock_guard<std::mutex> lock(g_boundsMutex);
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    g_bounds[index] = {pos.x, pos.y, size.x, size.y, true};
}

static void Setup(ANativeWindow* window) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    
    float scale = (float)g_Height / 720.0f;
    if (scale < 1.5f) scale = 1.5f;
    if (scale > 4.0f) scale = 4.0f;
    
    ImFontConfig cfg;
    cfg.SizePixels = 20.0f * scale;
    io.Fonts->AddFontDefault(&cfg);
    
    ImGui_ImplAndroid_Init(window);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale * 0.75f);
    style.Alpha = 1.0f;
    
    g_Initialized = true;
    LOGI("ImGui initialized successfully");
}

static void Render() {
    if (!g_Initialized) return;
    static int lastW = 0, lastH = 0;
    ImGuiIO& io = ImGui::GetIO();
    
    if (g_Width != lastW || g_Height != lastH) {
        io.DisplaySize = ImVec2((float)g_Width, (float)g_Height);
        lastW = g_Width;
        lastH = g_Height;
    }
    
    GLState gl;
    SaveGL(gl);
    
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();
    
    DrawMenu();
    
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    RestoreGL(gl);
}

inline ANativeWindow* hook_ANativeWindow_fromSurface(JNIEnv* env, jobject surface) {
    ANativeWindow* win = orig_ANativeWindow_fromSurface(env, surface);
    g_Window = win;
    return win;
}

inline EGLBoolean hook_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    EGLBoolean result = orig_eglMakeCurrent(dpy, draw, read, ctx);
    if (!g_Initialized && g_Window && draw != EGL_NO_SURFACE) {
        EGLint w=0, h=0;
        eglQuerySurface(dpy, draw, EGL_WIDTH, &w);
        eglQuerySurface(dpy, draw, EGL_HEIGHT, &h);
        g_Width = w;
        g_Height = h;
        Setup(g_Window);
    }
    return result;
}

inline EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    if (!orig_eglSwapBuffers) return EGL_FALSE;
    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) return orig_eglSwapBuffers(dpy, surf);
    
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    g_Width = w;
    g_Height = h;
    
    if (g_Initialized) Render();
    return orig_eglSwapBuffers(dpy, surf);
}