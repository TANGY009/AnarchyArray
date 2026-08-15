#include "main.hpp"
#include "input.hpp"
#include "rendering.hpp"
#include <sstream>
#include <vector>
#include <string>
#include <mutex>
#include <unistd.h>
#include <dlfcn.h>

uint32_t EncodeCmpW8Imm_Table(int imm) {
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

uint32_t EncodeMovW1Imm(uint16_t imm) {
    return 0x52800001 | ((static_cast<uint32_t>(imm) & 0xFFFF) << 5);
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

static void ApplyPatch(size_t index, const std::string& patchStr, bool enable) {
    if (!g_PatchesReady || index >= g_PatchAddrs.size() || g_PatchAddrs[index] == 0) return;

    uintptr_t addr = g_PatchAddrs[index];
    const auto& original = g_Originals[index];

    if (!enable) {
        WriteMemory(reinterpret_cast<void*>(addr), const_cast<uint8_t*>(original.data()), original.size(), true);
        return;
    }

    std::vector<PatternByte> patchPattern = ParsePattern(patchStr);
    if (patchPattern.empty()) return;

    std::vector<uint8_t> buffer(patchPattern.size());
    for (size_t i = 0; i < patchPattern.size(); i++) {
        if (patchPattern[i].isWildcard) {
            buffer[i] = (i < original.size()) ? original[i] : 0x00;
        } else {
            buffer[i] = patchPattern[i].data;
        }
    }

    WriteMemory(reinterpret_cast<void*>(addr), buffer.data(), buffer.size(), true);
}

static void ApplyPatchRange(size_t startIdx, size_t count, const std::string& patchStr, bool enable) {
    for (size_t i = startIdx; i < startIdx + count && i < g_PatchAddrs.size(); i++) {
        ApplyPatch(i, patchStr, enable);
    }
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
        "C2 02 00 54 F7 13 40 F9 FF 16 00 F1",
        // 1st CMP W8 #5 (Index 5)
        "1F 15 00 71 E1 01 00 54 00 E4 00 6F 68 02 40 F9",
        // 2nd MOV W1 #5 (Index 6)
        "A1 00 80 52 E2 13 07 94 40 F7 07 36"
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
    ImGui::Begin("AnarchyArray", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    UpdateBounds(0);

    static bool infinitySpread = false;

    // Cardinal Direction states
    // Index 0 = West, Index 1 = North, Index 2 = East, Index 3 = South
    static bool infSpreadW = false;
    static bool infSpreadN = false;
    static bool infSpreadE = false;
    static bool infSpreadS = false;

    static bool spongePlus = false;
    static int absorbTypeVal = 5;
    static int lastAbsorbValue = -1;

    static const char* kAbsorbNames[] = {
        "Air", "Dirt", "Wood", "Metal", "Copper Grates",
        "Water", "Lava", "Leaves", "Plants", "Azalea, Dried Kelp, Solid Plants",
        "Fire, Soul Fire", "Glass", "TNT", "Ice (not blue/packed)", "Powdered Snow",
        "Cactus", "Portals", "Unknown", "Bubble Column", "Unknown",
        "Decorated Pot, Decoration Solids", "ClientRequestPlaceholder", "Structure Void", "Stone, etc, Solids", 
        "Torches, Pot, etc, Non-Solids", "Unknown"
    };

    static const char* kNumberStrings[] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
        "20", "21", "22", "23", "24", "25"
    };

    constexpr int kAbsorbCount = static_cast<int>(sizeof(kAbsorbNames) / sizeof(kAbsorbNames[0])); // 26
    constexpr int kMaxAbsorbIdx = kAbsorbCount - 1; // 25

    // Check if any cardinal direction is currently enabled
    const bool anyDirectionActive = infSpreadW || infSpreadN || infSpreadE || infSpreadS;

    if (anyDirectionActive) ImGui::BeginDisabled();

    if (ImGui::Checkbox("InfinitySpread", &infinitySpread) && g_PatchesReady) {
        ApplyPatchRange(0, 4, "03 00 80 52", infinitySpread);
    }

    if (anyDirectionActive) ImGui::EndDisabled();

    if (infinitySpread) ImGui::BeginDisabled();

    if (ImGui::TreeNode("I.S. Directional Control")) {
        if (ImGui::BeginTable("CompassGrid", 3, ImGuiTableFlags_SizingFixedFit)) {
            const float colWidth = ImGui::GetFontSize() * 2.2f;
            ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, colWidth);
            ImGui::TableSetupColumn("N/S", ImGuiTableColumnFlags_WidthFixed, colWidth);
            ImGui::TableSetupColumn("E", ImGuiTableColumnFlags_WidthFixed, colWidth);

            // Row 1: NORTH
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Checkbox("N##North", &infSpreadN) && g_PatchesReady) {
                ApplyPatch(1, "03 00 80 52", infSpreadN); // Patch #2 = North
            }

            // Row 2: WEST & EAST
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Checkbox("W##West", &infSpreadW) && g_PatchesReady) {
                ApplyPatch(0, "03 00 80 52", infSpreadW); // Patch #1 = West
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Checkbox("E##East", &infSpreadE) && g_PatchesReady) {
                ApplyPatch(2, "03 00 80 52", infSpreadE); // Patch #3 = East
            }

            // Row 3: SOUTH
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Checkbox("S##South", &infSpreadS) && g_PatchesReady) {
                ApplyPatch(3, "03 00 80 52", infSpreadS); // Patch #4 = South
            }

            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (infinitySpread) ImGui::EndDisabled();

    // SpongeRange+
    if (ImGui::Checkbox("SpongeRange+", &spongePlus) && g_PatchesReady) {
        ApplyPatch(4, "1F 20 03 D5 ?? ?? ?? ?? FF 06 00 B1", spongePlus);
    }

    ImGui::Text("Absorb Type");
    ImGui::SameLine();

    const float sliderWidth = ImGui::GetFontSize() * 7.5f;
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderInt("##absorbSlider", &absorbTypeVal, 0, kMaxAbsorbIdx, "%d");
    ImGui::SameLine();

    const float btnSize = ImGui::GetFrameHeight();

    if (ImGui::Button("Grid", ImVec2(btnSize * 1.8f, btnSize))) {
        ImGui::OpenPopup("AbsorbGrid");
    }
    ImGui::SameLine();

    if (ImGui::Button("i", ImVec2(btnSize, btnSize))) {
        ImGui::OpenPopup("AbsorbTypeInfo");
    }

    if (absorbTypeVal >= 0 && absorbTypeVal <= kMaxAbsorbIdx) {
        ImGui::TextColored(ImVec4(0.75f, 0.55f, 0.90f, 1.00f), "Target: %s", kAbsorbNames[absorbTypeVal]);
    }

    if (g_PatchesReady && absorbTypeVal >= 0 && absorbTypeVal <= kMaxAbsorbIdx && absorbTypeVal != lastAbsorbValue) {
        uint32_t cmpInstr = EncodeCmpW8Imm_Table(absorbTypeVal);
        uint32_t movInstr = EncodeMovW1Imm(static_cast<uint16_t>(absorbTypeVal));
    
        if (cmpInstr != 0 && 5 < g_PatchAddrs.size() && g_PatchAddrs[5] != 0) {
            WriteMemory(reinterpret_cast<void*>(g_PatchAddrs[5]), &cmpInstr, sizeof(cmpInstr), true);
        }
    
        if (6 < g_PatchAddrs.size() && g_PatchAddrs[6] != 0) {
            WriteMemory(reinterpret_cast<void*>(g_PatchAddrs[6]), &movInstr, sizeof(movInstr), true);
        }
    
        lastAbsorbValue = absorbTypeVal;
    }

    if (ImGui::BeginPopup("AbsorbGrid", ImGuiWindowFlags_AlwaysAutoResize)) {
        UpdateBounds(2);
        ImGui::Text("Select Absorb Type (0 - %d)", kMaxAbsorbIdx);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnSize);
        if (ImGui::Button("X", ImVec2(btnSize, btnSize))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();

        const float cellWidth = ImGui::GetFontSize() * 2.6f;
        const float rowHeight = ImGui::GetFontSize() * 2.0f;
        const int columns = 5;

        for (int i = 0; i < kAbsorbCount; i++) {
            bool isSelected = (absorbTypeVal == i);
            
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.12f, 0.28f, 1.00f));
            }

            if (ImGui::Button(kNumberStrings[i], ImVec2(cellWidth, rowHeight))) {
                absorbTypeVal = i;
                ImGui::CloseCurrentPopup();
            }

            if (isSelected) {
                ImGui::PopStyleColor();
            }

            if ((i + 1) % columns != 0 && i < kMaxAbsorbIdx) {
                ImGui::SameLine();
            }
        }
        ImGui::EndPopup();
    } else {
        std::lock_guard<std::mutex> lock(g_boundsMutex);
        g_bounds[2].visible = false;
    }

    if (ImGui::BeginPopup("AbsorbTypeInfo", ImGuiWindowFlags_AlwaysAutoResize)) {
        UpdateBounds(1);
        ImGui::Text("Absorb Type Reference");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnSize);
        if (ImGui::Button("X", ImVec2(btnSize, btnSize))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        
        if (ImGui::BeginTable("AbsorbRefTable", 2, ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableNextColumn();
            ImGui::BulletText("0 = Air");
            ImGui::BulletText("1 = Dirt");
            ImGui::BulletText("2 = Wood");
            ImGui::BulletText("3 = Metal");
            ImGui::BulletText("4 = Copper grates");
            ImGui::BulletText("5 = Water");
            ImGui::BulletText("6 = Lava");
            ImGui::BulletText("7 = Leaves");
            ImGui::BulletText("8 = Plants");
            ImGui::BulletText("9 = Azalea, dried kelp, solid plants");
            ImGui::BulletText("10 = Fire, soul fire");
            ImGui::BulletText("11 = Glass");
            ImGui::BulletText("12 = Tnt");

            ImGui::TableNextColumn();
            ImGui::BulletText("13 = Ice (not blue/packed)");
            ImGui::BulletText("14 = Powdered snow");
            ImGui::BulletText("15 = Cactus");
            ImGui::BulletText("16 = Portals");
            ImGui::BulletText("17 = Unknown");
            ImGui::BulletText("18 = Bubble column");
            ImGui::BulletText("19 = Unknown");
            ImGui::BulletText("20 = Decorated pot, decoration solids");
            ImGui::BulletText("21 = ClientRequestPlaceholder");
            ImGui::BulletText("22 = Structure void");
            ImGui::BulletText("23 = Stone, etc, solids");
            ImGui::BulletText("24 = Torches, pot, etc, non-solids");
            ImGui::BulletText("25 = Unknown");
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    } else {
        std::lock_guard<std::mutex> lock(g_boundsMutex);
        g_bounds[1].visible = false;
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