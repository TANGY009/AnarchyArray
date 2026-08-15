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

static void ApplyPurpleTheme(float scale = 1.0f) {
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.ScaleAllSizes(scale * 1.15f);
    
    style.WindowRounding    = 12.0f * scale;
    style.ChildRounding     = 8.0f  * scale;
    style.PopupRounding     = 10.0f * scale;
    style.FrameRounding     = 6.0f  * scale;
    style.ScrollbarRounding = 6.0f  * scale;
    style.GrabRounding      = 6.0f  * scale;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;
    
    ImVec4* colors = style.Colors;
    
    colors[ImGuiCol_WindowBg]             = ImVec4(0.03f, 0.02f, 0.04f, 0.72f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.03f, 0.02f, 0.04f, 0.72f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.08f, 0.02f, 0.09f, 0.45f);
    
    colors[ImGuiCol_Text]                 = ImVec4(0.96f, 0.92f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.40f, 0.52f, 1.00f);
    
    colors[ImGuiCol_TitleBg]              = ImVec4(0.18f, 0.03f, 0.06f, 0.80f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.48f, 0.05f, 0.12f, 0.90f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.10f, 0.02f, 0.04f, 0.60f);
    colors[ImGuiCol_Header]               = ImVec4(0.28f, 0.06f, 0.18f, 0.70f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.52f, 0.08f, 0.32f, 0.85f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.72f, 0.10f, 0.28f, 0.95f);
    
    colors[ImGuiCol_Border]               = ImVec4(0.65f, 0.12f, 0.80f, 0.50f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    colors[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.04f, 0.14f, 0.75f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.06f, 0.24f, 0.85f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.35f, 0.08f, 0.38f, 0.95f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.95f, 0.15f, 0.35f, 1.00f);

    colors[ImGuiCol_Button]               = ImVec4(0.35f, 0.05f, 0.12f, 0.75f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.58f, 0.08f, 0.38f, 0.90f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.82f, 0.12f, 0.28f, 1.00f);
    
    colors[ImGuiCol_Separator]            = ImVec4(0.50f, 0.10f, 0.60f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.75f, 0.15f, 0.45f, 0.85f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.90f, 0.18f, 0.30f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.18f, 0.04f, 0.12f, 0.80f);
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.40f, 0.08f, 0.30f, 0.35f);
    colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.60f, 0.10f, 0.45f, 0.55f);
    
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.80f, 0.12f, 0.35f, 0.90f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.95f, 0.20f, 0.50f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.04f, 0.02f, 0.05f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.32f, 0.06f, 0.28f, 0.75f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.52f, 0.08f, 0.42f, 0.85f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.78f, 0.12f, 0.32f, 1.00f);
}

static void Setup(ANativeWindow* window) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    
    float scale = (float)g_Height / 720.0f;
    if (scale < 0.8f) scale = 0.8f;
    if (scale > 3.0f) scale = 3.0f;
    
    float baseFontSize = 30.0f * scale;
    
    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;
    
    ImFont* font = io.Fonts->AddFontFromFileTTF("/system/fonts/Roboto-Regular.ttf", baseFontSize, &cfg);
    if (!font) {
        cfg.SizePixels = baseFontSize;
        io.Fonts->AddFontDefault(&cfg);
    }
    
    ImGui_ImplAndroid_Init(window);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    
    ApplyPurpleTheme(scale);
    
    g_Initialized = true;
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