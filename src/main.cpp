#include "main.hpp"
#include "input.hpp"
#include "rendering.hpp"

uint32_t EncodeCmpW8Imm_Table(int imm) { // For absorb type
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

struct PatternByte {
    uint8_t data;
    bool isWildcard;
};

static std::vector<PatternByte> ParsePattern(const std::string& patternStr) {
    std::vector<PatternByte> pattern;
    std::stringstream ss(patternStr);
    std::string token;
    
    while (ss >> token) {
        if (token == "??" || token == "?") {
            pattern.push_back({0, true});
        } else {
            uint8_t byte = (uint8_t)std::strtoul(token.c_str(), nullptr, 16);
            pattern.push_back({byte, false});
        }
    }
    return pattern;
}

static bool MatchPattern(const uint8_t* memory, const std::vector<PatternByte>& pattern) {
    for (size_t i = 0; i < pattern.size(); i++)
        if (!pattern[i].isWildcard && memory[i] != pattern[i].data)
            return false;
    return true;
}

uintptr_t GetLibBase() {
    size_t textSize{};
    uintptr_t text = GlossGetLibSection("libminecraftpe.so", ".text", &textSize);
    Dl_info info{};
    if (dladdr((void*)text, &info))
        return (uintptr_t)info.dli_fbase;
    return 0;
}

static void ScanSignatures() {
    uintptr_t base = GetLibBase();
    uintptr_t txtbase = 0;
    size_t size = 0;
    while ((txtbase = GlossGetLibSection("libminecraftpe.so", ".text", &size)) == 0 || size == 0) {
        usleep(1000);
    }
    const std::vector<std::string> patternStrings = {
        // InfinitySpread (Index 0-3)
        "E3 ?? ?? 2A E4 ?? ?? AA A5 ?? ?? 52 08 ?? ?? 51",
        "E3 ?? ?? 2A 29 ?? ?? 51 E4 ?? ?? AA 65 ?? ?? 52",
        "E3 ?? ?? 2A E4 ?? ?? AA 85 ?? ?? 52 08 ?? ?? 11",
        "E3 ?? ?? 2A 29 ?? ?? 11 E4 ?? ?? AA 45 ?? ?? 52",
        // SpongeLimit+ (Index 4)
        "62 02 00 54 FB 13 40 F9 7F 17 00 F1",
        // SpongeLimit++ (Index 5)
        "5F 51 05 F1 8B 2D 0D 9B",
        // 1st CMP W8 #5 (Index 6)
        "1F 15 00 71 A1 01 00 54 00 E4 00 6F",
        // 2nd CMP W8 #5 (Index 7)
        "1F 15 00 71 01 F8 FF 54 88 02 40 F9"
    };

    g_PatchAddrs.assign(patternStrings.size(), 0);
    g_Originals.clear();
    g_Originals.resize(patternStrings.size());
    
    for (size_t s = 0; s < patternStrings.size(); s++) {
        std::vector<PatternByte> parsedPattern = ParsePattern(patternStrings[s]);
        size_t patternLen = parsedPattern.size();
        if (patternLen == 0 || patternLen > size) continue;
        for (size_t i = 0; i <= size - patternLen; i++) {
            uintptr_t currentAddr = txtbase + i;
            if (MatchPattern(reinterpret_cast<const uint8_t*>(currentAddr), parsedPattern)) {
                g_PatchAddrs[s] = currentAddr;
                g_Originals[s].assign(
                    reinterpret_cast<uint8_t*>(currentAddr), 
                    reinterpret_cast<uint8_t*>(currentAddr) + patternLen
                );
                LOGI("Signature found at binary offset: 0x%lx", (unsigned long)(currentAddr - base));
                break;
            }
        }
    }
    g_PatchesReady = true;
}

void DrawMenu() {
    ImGui::Begin("AnarchyArray", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    UpdateBounds(0);
    static bool infinitySpread = false;
    static bool spongePlus = false;
    static bool spongePlusPlus = false;
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
    ImGui::BeginDisabled(!spongePlus); // Grey out if SpongeRange+ is not active
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

static void* Initialize() {
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
    LOGI("Initialization setup");
    return nullptr;
}

__attribute__((constructor))
void Init() {
    LOGI("AnarchyArray Loaded");
    Initialize();
}