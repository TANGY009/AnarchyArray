#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <mutex>
#include <string>
#include <sstream>
#include <unistd.h>

#include <jni.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "pl/Gloss.h"
#include "pl/c/PreloaderInput.h"

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"

#define LOG_TAG "AnarchyArray"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

inline bool g_Initialized = false;
inline int g_Width = 0;
inline int g_Height = 0;
inline ANativeWindow* g_Window = nullptr;
inline bool g_touchCapturedByGui = false;
inline std::mutex g_boundsMutex;
inline bool g_PatchesReady = false;
inline std::vector<uintptr_t> g_PatchAddrs;
inline std::vector<std::vector<uint8_t>> g_Originals;

inline ANativeWindow* (*orig_ANativeWindow_fromSurface)(JNIEnv* env, jobject surface) = nullptr;
inline EGLBoolean (*orig_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
inline EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

struct WindowBounds {
    float x, y, w, h;
    bool visible;
};

inline WindowBounds g_bounds[3] = {
    {0,0,0,0,false}, // menu
    {0,0,0,0,false}, // info
    {0,0,0,0,false}  // keypad
};

extern void WriteMemory(void* addr, const void* data, size_t size, bool protect);