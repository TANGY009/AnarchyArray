#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <mutex>

#include <jni.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "pl/Hook.h"
#include "pl/Gloss.h"
#include "pl/PreloaderInput.h"

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"

#define LOG_TAG "AnarchyArray"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static bool g_Initialized = false;
static int g_Width = 0;
static int g_Height = 0;
static ANativeWindow* g_Window = nullptr;
static bool g_touchCapturedByGui = false;
static std::mutex g_boundsMutex;
static bool g_PatchesReady = false;
static std::vector<uintptr_t> g_PatchAddrs;
static std::vector<std::vector<uint8_t>> g_Originals;

static ANativeWindow* (*orig_ANativeWindow_fromSurface)(JNIEnv* env, jobject surface) = nullptr;
static EGLBoolean (*orig_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

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

// InputConsumer::initializeMotionEvent
static void (*initMotionEvent)(void*, void*, void*) = nullptr;
static void HookInput1(void* thiz, void* a1, void* a2) {
    if (initMotionEvent) initMotionEvent(thiz, a1, a2);
    if (thiz && g_Initialized) {
        ImGui_ImplAndroid_HandleInputEvent((AInputEvent*)thiz);
    }
}

// InputConsumer::Consume
static int32_t (*Consume)(void*, void*, bool, long, uint32_t*, AInputEvent**) = nullptr;
static int32_t HookInput2(void* thiz, void* a1, bool a2, long a3, uint32_t* a4, AInputEvent** event) {
    int32_t result = Consume ? Consume(thiz, a1, a2, a3, a4, event) : 0;
    if (result == 0 && event && *event && g_Initialized) {
        ImGui_ImplAndroid_HandleInputEvent(*event);
    }
    return result;
}

static void HookLegacyInput() {
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

struct WindowBounds {
    float x, y, w, h;
    bool visible;
};

static WindowBounds g_bounds[3] = {
    {0,0,0,0,false}, // menu
    {0,0,0,0,false}, // info
    {0,0,0,0,false}  // keypad
};

static void UpdateBounds(int index) {
    std::lock_guard<std::mutex> lock(g_boundsMutex);
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    g_bounds[index] = {pos.x, pos.y, size.x, size.y, true};
}

static bool HandleTouchEvent(int action, int pointerId, float x, float y) {
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(x, y);
    bool isTouchInsideGui = false;
    {
        std::lock_guard<std::mutex> lock(g_boundsMutex);
        auto InBounds = [&](const WindowBounds& b) {
            return b.visible && x >= b.x && x <= (b.x + b.w) && y >= b.y && y <= (b.y + b.h);
        };
        for (int i = 0; i < 3; i++) {
            if (InBounds(g_bounds[i])) {
                isTouchInsideGui = true;
                break;
            }
        }
    }
    switch (action & 0xFF) {
        case 0: // DOWN
        {
            io.MouseDown[0] = true;
            if (isTouchInsideGui) {
                g_touchCapturedByGui = true;
                return true; // block game
            }
            g_touchCapturedByGui = false;
            return false;
        }
        case 1: // UP
        {
            io.MouseDown[0] = false;
            bool wasCaptured = g_touchCapturedByGui;
            g_touchCapturedByGui = false;
            return wasCaptured; // block only if GUI owned it
        }
        case 2: // MOVE
            return g_touchCapturedByGui;
    }
    return false;
}

static void RegisterPreloaderTouch() {
    LOGI("Checking for Preloader input support...");
    GHandle hPreloader = GlossOpen("libpreloader.so");
    if (!hPreloader) {
        LOGW("libpreloader.so not found, using legacy input");
        HookLegacyInput();
        return;
    }
    void* sym = (void*)GlossSymbol(hPreloader, "GetPreloaderInput", nullptr);
    if (!sym) {
        LOGW("GetPreloaderInput not found in libpreloader.so, using legacy input");
        HookLegacyInput();
        return;
    }
    PreloaderInput_Interface* (*GetInputFunc)();
    GetInputFunc = reinterpret_cast<PreloaderInput_Interface*(*)()>(sym);
    PreloaderInput_Interface* input = GetInputFunc();
    if (!input || !input->RegisterTouchCallback) {
        LOGW("Preloader input invalid. Falling back to legacy.");
        HookLegacyInput();
        return;
    }
    input->RegisterTouchCallback(HandleTouchEvent);
    LOGI("Using Preloader touch input.");
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

static void ScanSignatures() {
    uintptr_t base = 0;
    size_t size = 0;
    // Wait until libminecraftpe.so is loaded and section is valid, we don't want a bad pointer
    while ((base = GlossGetLibSection("libminecraftpe.so", ".text", &size)) == 0 || size == 0) {
        usleep(1000); // Sleep 1ms between retries
    }
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
    g_PatchAddrs.assign(signatures.size(), 0);
    g_Originals.clear();
    g_Originals.resize(signatures.size());
    for (size_t s = 0; s < signatures.size(); s++) {
        for (size_t i = 0; i + signatures[s].size() <= size; i++) {
            if (memcmp((void*)(base + i), signatures[s].data(), signatures[s].size()) == 0) {
                uintptr_t addr = base + i;
                g_PatchAddrs[s] = addr;
                g_Originals[s].assign((uint8_t*)addr, (uint8_t*)addr + signatures[s].size());
                //LOGI("Signature found at %p", (void*)addr);
                break; // Stop after first match, prevents duplicates
            }
        }
    }
    g_PatchesReady = true;
}

static void DrawMenu() {
    ImGui::Begin("AnarchyArray", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    UpdateBounds(0);
    static bool infinitySpread = false;
    static bool spongePlus = false;
    static bool spongePlusPlus = false;
    static bool netherSponge = false;
    static int absorbTypeVal = 5;
    static int lastAbsorbValue = -1;
    // InfinitySpread
    if (ImGui::Checkbox("InfinitySpread", &infinitySpread) && g_PatchesReady) {
        const uint8_t patch[] = {0x03, 0x00, 0x80, 0x52};
        for (size_t i = 0; i < 4 && i < g_PatchAddrs.size(); i++) {
            if (infinitySpread) {
                WriteMemory((void*)g_PatchAddrs[i], (void*)patch, sizeof(patch), true);
            } else {
                WriteMemory((void*)g_PatchAddrs[i], g_Originals[i].data(), g_Originals[i].size(), true);
            }
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
                WriteMemory((void*)g_PatchAddrs[idx], g_Originals[idx].data(), g_Originals[idx].size(), true);
            }
        }
    }
    // SpongeRange++
    ImGui::BeginDisabled(!spongePlus); // grey out if SpongeRange+ is not active
    if (ImGui::Checkbox("SpongeRange++", &spongePlusPlus) && g_PatchesReady) {
        const uint8_t patchPlusPlus[] = {0x5F, 0xFD, 0x03, 0xF1, 0x8B, 0x2D, 0x0D, 0x9B};
        size_t idx = 5;
        if (idx < g_PatchAddrs.size()) {
            if (spongePlusPlus) {
                WriteMemory((void*)g_PatchAddrs[idx], (void*)patchPlusPlus, sizeof(patchPlusPlus), true);
            } else {
                WriteMemory((void*)g_PatchAddrs[idx], g_Originals[idx].data(), g_Originals[idx].size(), true);
            }
        }
    }
    ImGui::EndDisabled();
    //if (ImGui::Checkbox("NetherSponge", &netherSponge) && g_PatchesReady) {
    //    const uint8_t patchNether[] = {0xSoon};
    //    size_t idx = 8; // choose the correct index after scanning signatures
    //    if (idx < g_PatchAddrs.size()) {
    //       if (netherSponge) {
    //           WriteMemory((void*)g_PatchAddrs[idx], (void*)patchNether, sizeof(patchNether), true);
    //        } else {
    //            WriteMemory((void*)g_PatchAddrs[idx], g_Originals[idx].data(), g_Originals[idx].size(), true);
    //        }
    //    }
    //}
    ImGui::Text("Absorb Type");
    ImGui::SameLine();
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
    // i button
    if (ImGui::Button("i", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
        ImGui::OpenPopup("AbsorbTypeInfo");
    }
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
    if (g_PatchesReady && absorbTypeVal >= 0 && absorbTypeVal <= 575 && absorbTypeVal != lastAbsorbValue) {
        uint32_t instr = EncodeCmpW8Imm_Table(absorbTypeVal);
        if (instr != 0) {
            for (size_t idx : {6, 7}) {
                if (idx < g_PatchAddrs.size()) {
                    WriteMemory((void*)g_PatchAddrs[idx], &instr, 4, true);
                }
            }
            lastAbsorbValue = absorbTypeVal;
        }
    }
    // Info popup
    if (ImGui::BeginPopup("AbsorbTypeInfo", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        UpdateBounds(1);
        ImGui::Text("Absorb Type Reference");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::BeginTable("AbsorbRefTable", 2, ImGuiTableFlags_NoBordersInBody)) {
            // First column
            ImGui::TableNextColumn();
            ImGui::BulletText("0 = air");
            ImGui::BulletText("1 = dirt");
            ImGui::BulletText("2 = wood");
            ImGui::BulletText("3 = metal");
            ImGui::BulletText("4 = copper grates");
            ImGui::BulletText("5 = water");
            ImGui::BulletText("6 = lava");
            ImGui::BulletText("7 = leaves");
            ImGui::BulletText("8 = plants");
            ImGui::BulletText("9 = azalea, dried kelp, solid plants");
            ImGui::BulletText("10 = fire, soul fire");
            ImGui::BulletText("11 = glass");
            ImGui::BulletText("12 = tnt");
            // Second column
            ImGui::TableNextColumn();
            ImGui::BulletText("13 = ice (not blue/packed)");
            ImGui::BulletText("14 = powdered snow");
            ImGui::BulletText("15 = cactus");
            ImGui::BulletText("16 = portals");
            ImGui::BulletText("17 = unknown");
            ImGui::BulletText("18 = bubble column");
            ImGui::BulletText("19 = unknown");
            ImGui::BulletText("20 = decorated pot, decoration solids");
            ImGui::BulletText("21 = n/a");
            ImGui::BulletText("22 = structure void");
            ImGui::BulletText("23 = stone, etc, solids");
            ImGui::BulletText("24 = torches, pot, etc, non-solids");
            ImGui::BulletText("25 = unknown");
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    } else {
        std::lock_guard<std::mutex> lock(g_boundsMutex);
        g_bounds[1].visible = false;
    }
    // Keypad popup window
    if (ImGui::BeginPopup("AbsorbKeypad", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        UpdateBounds(2);
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
    } else {
        std::lock_guard<std::mutex> lock(g_boundsMutex);
        g_bounds[2].visible = false;
    }
    ImGui::End();
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

static void* MainThread(void*) {
    GlossInit(true);
    GHandle hEGL = GlossOpen("libEGL.so");
    if (hEGL) {
        void* swap = (void*)GlossSymbol(hEGL, "eglSwapBuffers", nullptr);
        if (swap) GlossHook(swap, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
        void* makeCurrent = (void*)GlossSymbol(hEGL, "eglMakeCurrent", nullptr);
        if (makeCurrent) GlossHook(makeCurrent, (void*)hook_eglMakeCurrent, (void**)&orig_eglMakeCurrent);
    }
    GHandle hAndroid = GlossOpen("libandroid.so");
    if (hAndroid) {
        void* f = (void*)GlossSymbol(hAndroid, "ANativeWindow_fromSurface", nullptr);
        if (f) GlossHook(f, (void*)hook_ANativeWindow_fromSurface, (void**)&orig_ANativeWindow_fromSurface);
    }
    RegisterPreloaderTouch();
    ScanSignatures();
    LOGI("MainThread finished setup");
    return nullptr;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    LOGI("JNI_OnLoad called");
    pthread_t t;
    pthread_create(&t, nullptr, MainThread, nullptr);
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
}