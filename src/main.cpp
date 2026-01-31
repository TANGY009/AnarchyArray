#include <cstdint>
#include <cstdlib>
#include <vector>

#include <jni.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>

#include "pl/Hook.h"
#include "pl/Gloss.h"

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AnarchyArray", __VA_ARGS__)

static bool g_Initialized = false;
static int g_Width = 0, g_Height = 0;

static ANativeWindow* g_Window = nullptr;
static ANativeWindow* (*orig_ANativeWindow_fromSurface)(JNIEnv* env, jobject surface) = nullptr;
static EGLBoolean (*orig_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
static EGLSurface (*orig_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = nullptr;

static bool g_PatchesReady = false;
static std::vector<uintptr_t> g_PatchAddrs;
static std::vector<std::array<uint8_t,4>> g_Originals;

// InputConsumer::initializeMotionEvent
static void (*initMotionEvent)(void*, void*, void*) = nullptr;
static void HookInput1(void* thiz, void* a1, void* a2) {
    if (initMotionEvent) initMotionEvent(thiz, a1, a2);
    if (thiz && g_Initialized) {
        //LOGI("[HookInput1] AInputEvent addr=%p", thiz);
        ImGui_ImplAndroid_HandleInputEvent((AInputEvent*)thiz);
    }
}

// InputConsumer::Consume
static int32_t (*Consume)(void*, void*, bool, long, uint32_t*, AInputEvent**) = nullptr;
static int32_t HookInput2(void* thiz, void* a1, bool a2, long a3, uint32_t* a4, AInputEvent** event) {
    int32_t result = Consume ? Consume(thiz, a1, a2, a3, a4, event) : 0;
    if (result == 0 && event && *event && g_Initialized) {
        //LOGI("[HookInput2] AInputEvent addr=%p", *event);
        ImGui_ImplAndroid_HandleInputEvent(*event);
    }
    return result;
}

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

static uint32_t EncodeCmpW8Imm_Table(int imm) { // for absorb type
    if (imm < 0 || imm > 575) return 0;
    uint32_t instr = 0x7100001F;
    int block = imm / 64;
    int offset = imm % 64;
    uint8_t immByte = 0x01 + (offset * 0x04);
    uint8_t* p = reinterpret_cast<uint8_t*>(&instr);
    p[1] = immByte;
    p[2] = (uint8_t)block;
    return instr;
}

static void DrawMenu() {
    ImGui::Begin("AnarchyArray", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    static bool infinitySpread = false;
    static bool spongePlus = false;
    static bool spongePlusPlus = false;
    static int absorbTypeVal = 5;
    // InfinitySpread
    if (ImGui::Checkbox("InfinitySpread", &infinitySpread) && g_PatchesReady) {
        const uint8_t patch[] = {0x03, 0x00, 0x80, 0x52};
        for (size_t i = 0; i < 4 && i < g_PatchAddrs.size(); i++) {
            WriteMemory((void*)g_PatchAddrs[i], infinitySpread ? (void*)patch : (void*)g_Originals[i].data(), 4, true);
        }
    }
    // SpongeRange+
    if (ImGui::Checkbox("SpongeRange+", &spongePlus) && g_PatchesReady) {
        const uint8_t patchPlus[] = {0x1F, 0x20, 0x03, 0xD5, 0xFB, 0x13, 0x40, 0xF9, 0x7F, 0x07, 0x00, 0xB1};
        size_t idx = 4;
        if (idx < g_PatchAddrs.size()) {
            if (spongePlus) {
                WriteMemory((void*)g_PatchAddrs[idx], (void*)patchPlus, sizeof(patchPlus), true);
            } else {
                WriteMemory((void*)g_PatchAddrs[idx], (void*)g_Originals[idx].data(), 4, true);
            }
        }
    }
    // SpongeRange++
    if (ImGui::Checkbox("SpongeRange++", &spongePlusPlus) && g_PatchesReady) {
        const uint8_t patchPlusPlus[] = {0x5F, 0xFD, 0x03, 0xF1, 0x8B, 0x2D, 0x0D, 0x9B};
        size_t idx = 5;
        if (idx < g_PatchAddrs.size()) {
            if (spongePlusPlus) {
                WriteMemory((void*)g_PatchAddrs[idx], (void*)patchPlusPlus, sizeof(patchPlusPlus), true);
                spongePlus = true;
            } else {
                WriteMemory((void*)g_PatchAddrs[idx], (void*)g_Originals[idx].data(), 4, true);
            }
        }
    }
    // Absorb Type
    ImGui::Text("Absorb Type"); ImGui::SameLine();
    // Number display
    ImGui::SetNextItemWidth(50);
    ImGui::InputInt("##absorbDisplay", &absorbTypeVal, 0, 0, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    // K button + square gap + minus/plus arrows
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    // Keypad button
    if (ImGui::Button("K", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
        ImGui::OpenPopup("AbsorbKeypad");
    }
    ImGui::SameLine();
    // Square gap
    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
    ImGui::SameLine();
    // Minus button
    if (ImGui::Button("-", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
        if (absorbTypeVal > 0) absorbTypeVal--;
    }
    ImGui::SameLine();
    // Plus button
    if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
        if (absorbTypeVal < 575) absorbTypeVal++;
    }
    ImGui::PopStyleVar(3);
    // Apply patch when value changes
    if (g_PatchesReady && absorbTypeVal >= 0 && absorbTypeVal <= 575) {
        for (size_t idx : {6, 7}) {
            if (idx < g_PatchAddrs.size()) {
                uint32_t instr = EncodeCmpW8Imm_Table(absorbTypeVal);
                if (instr != 0) {
                    WriteMemory((void*)g_PatchAddrs[idx], &instr, 4, true);
                }
            }
        }
    }
    // Keypad popup window
    if (ImGui::BeginPopup("AbsorbKeypad", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
    // Title bar with a close X button at top-right
    ImGui::Text("Keypad");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
    if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    // Fixed keypad grid size
    const float cellWidth = 60.0f;
    const float rowHeight = 50.0f;
    // 1 2 3
    for (int i = 1; i <= 3; i++) {
        if (ImGui::Button(std::to_string(i).c_str(), ImVec2(cellWidth, rowHeight))) {
            absorbTypeVal = absorbTypeVal * 10 + i;
        }
        if (i < 3) ImGui::SameLine();
    }
    // 4 5 6
    for (int i = 4; i <= 6; i++) {
        if (ImGui::Button(std::to_string(i).c_str(), ImVec2(cellWidth, rowHeight))) {
            absorbTypeVal = absorbTypeVal * 10 + i;
        }
        if (i < 6) ImGui::SameLine();
    }
    // 7 8 9
    for (int i = 7; i <= 9; i++) {
        if (ImGui::Button(std::to_string(i).c_str(), ImVec2(cellWidth, rowHeight))) {
            absorbTypeVal = absorbTypeVal * 10 + i;
        }
        if (i < 9) ImGui::SameLine();
    }
    // blank 0 <-
    ImGui::Dummy(ImVec2(cellWidth, rowHeight));
    ImGui::SameLine();
    if (ImGui::Button("0", ImVec2(cellWidth, rowHeight))) {
        absorbTypeVal = absorbTypeVal * 10;
    }
    ImGui::SameLine();
    if (ImGui::Button("<-", ImVec2(cellWidth, rowHeight))) { // backspace arrow
        absorbTypeVal /= 10;
    }
    ImGui::EndPopup();
    }
    ImGui::End();
}

static void ScanSignatures() {
    uintptr_t base = GlossGetLibSection("libminecraftpe.so", ".text", nullptr);
    size_t size = 0;
    GlossGetLibSection("libminecraftpe.so", ".text", &size);
    // Signature sets
    const std::vector<std::vector<uint8_t>> signatures = {
        // InfinitySpread
        {0xE3,0x03,0x19,0x2A,0xE4,0x03,0x14,0xAA,0xA5,0x00,0x80,0x52,0x08,0x05,0x00,0x51},
        {0xE3,0x03,0x19,0x2A,0x29,0x05,0x00,0x51,0xE4,0x03,0x14,0xAA,0x65,0x00,0x80,0x52},
        {0xE3,0x03,0x19,0x2A,0xE4,0x03,0x14,0xAA,0x85,0x00,0x80,0x52,0x08,0x05,0x00,0x11},
        {0xE3,0x03,0x19,0x2A,0x29,0x05,0x00,0x11,0xE4,0x03,0x14,0xAA,0x45,0x00,0x80,0x52},
        // SpongeLimit+
        {0x62,0x02,0x00,0x54,0xFB,0x13,0x40,0xF9,0x7F,0x17,0x00,0xF1},
        // SpongeLimit++
        {0x5F,0x51,0x05,0xF1,0x8B,0x2D,0x0D,0x9B},
        // 1st CMP W8 #5
        {0x1F,0x15,0x00,0x71,0xA1,0x01,0x00,0x54,0x00,0xE4,0x00,0x6F},
        // 2nd CMP W8 #5
        {0x1F,0x15,0x00,0x71,0x01,0xF8,0xFF,0x54,0x88,0x02,0x40,0xF9},
    };
    for (auto& sig : signatures) {
        for (size_t i = 0; i + sig.size() < size; i++) {
            if (!memcmp((void*)(base + i), sig.data(), sig.size())) {
                uintptr_t addr = base + i;
                g_PatchAddrs.push_back(addr);
                std::array<uint8_t,4> orig;
                memcpy(orig.data(), (void*)addr, 4);
                g_Originals.push_back(orig);
                //LOGI("Signature found at %p", (void*)addr);
            }
        }
    }
    g_PatchesReady = true;
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
    cfg.SizePixels = 18.0f * scale;
    io.Fonts->AddFontDefault(&cfg);
    ImGui_ImplAndroid_Init(window);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale * 0.65f);
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

static EGLSurface hook_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list) {
    EGLSurface surf = orig_eglCreateWindowSurface(dpy, config, win, attrib_list);
    if (!g_Initialized && win) {
        Setup((ANativeWindow*)win);
    }
    return surf;
}

static ANativeWindow* hook_ANativeWindow_fromSurface(JNIEnv* env, jobject surface) {
    ANativeWindow* win = orig_ANativeWindow_fromSurface(env, surface);
    g_Window = win;
    return win;
}

static EGLBoolean hook_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    EGLBoolean result = orig_eglMakeCurrent(dpy, draw, read, ctx);
    if (!g_Initialized && g_Window && draw != EGL_NO_SURFACE) {
        EGLint w=0,h=0;
        eglQuerySurface(dpy, draw, EGL_WIDTH, &w);
        eglQuerySurface(dpy, draw, EGL_HEIGHT, &h);
        g_Width = w;
        g_Height = h;
        Setup(g_Window);
    }
    return result;
}

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
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

static void HookInput() {
    void* sym1 = (void*)GlossSymbol(GlossOpen("libinput.so"),
        "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE", nullptr);
    if (sym1) {
        GHook h = GlossHook(sym1, (void*)HookInput1, (void**)&initMotionEvent);
        if (h) {
            LOGI("HookInput1: successfully hooked InputConsumer::initializeMotionEvent");
        }
    }
    void* sym2 = (void*)GlossSymbol(GlossOpen("libinput.so"),
        "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE", nullptr);
    if (sym2) {
        GHook h = GlossHook(sym2, (void*)HookInput2, (void**)&Consume);
        if (h) {
            LOGI("HookInput2: successfully hooked InputConsumer::consume");
        }
    }
}

static void* MainThread(void*) {
    GlossInit(true);
    GHandle hEGL = GlossOpen("libEGL.so");
    if (hEGL) {
        void* swap = (void*)GlossSymbol(hEGL, "eglSwapBuffers", nullptr);
        if (swap) GlossHook(swap, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
        void* create = (void*)GlossSymbol(hEGL, "eglCreateWindowSurface", nullptr);
        if (create) GlossHook(create, (void*)hook_eglCreateWindowSurface, (void**)&orig_eglCreateWindowSurface);
        void* makeCurrent = (void*)GlossSymbol(hEGL, "eglMakeCurrent", nullptr);
        if (makeCurrent) GlossHook(makeCurrent, (void*)hook_eglMakeCurrent, (void**)&orig_eglMakeCurrent);
    }
    GHandle hAndroid = GlossOpen("libandroid.so");
    if (hAndroid) {
        void* f = (void*)GlossSymbol(hAndroid, "ANativeWindow_fromSurface", nullptr);
        if (f) GlossHook(f, (void*)hook_ANativeWindow_fromSurface, (void**)&orig_ANativeWindow_fromSurface);
    }
    HookInput();
    ScanSignatures();
    LOGI("MainThread finished setup");
    return nullptr;
}

__attribute__((constructor))
void AnarchyArray_Init() {
    LOGI("AnarchyArray_Init called");
    pthread_t t;
    pthread_create(&t, nullptr, MainThread, nullptr);
}