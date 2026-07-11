#pragma once
#include "main.hpp"

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