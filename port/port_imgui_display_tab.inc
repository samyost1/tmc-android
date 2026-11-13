/* DrawRibbonDisplayTab — extracted from port_imgui_menu.cpp so the
 * per-tab UI code stays in smaller, navigable chunks. This file is
 * included into the same translation unit and intentionally relies on
 * the surrounding ImGui/Port_* declarations already visible there. */
extern "C" { typedef void (*PortStepCycle)(int); }
typedef const char* (*PortStepValue)(char* buf, size_t cap);

/* Aligned stepper row: label on column 0, and [<] [ centered value ] [>] on column 1. */
static void StepperRow(const char* label, const char* id,
                       PortStepValue getValue, PortStepCycle onCycle,
                       const char* subtitle = nullptr) {
    char tag[32];
    const float em = ImGui::GetFontSize();
    const float btnW = em * 1.85f;
    const float valW = em * 8.5f;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (subtitle && subtitle[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.62f, 0.56f, 1.0f));
        ImGui::TextWrapped("%s", subtitle);
        ImGui::PopStyleColor();
    }

    ImGui::TableSetColumnIndex(1);
    std::snprintf(tag, sizeof(tag), "<##%s", id);
    if (ImGui::Button(tag, ImVec2(btnW, 0))) onCycle(-1);
    ImGui::SameLine(0.0f, 8.0f);

    char buf[32];
    const char* valStr = getValue(buf, sizeof(buf));
    const float curX = ImGui::GetCursorPosX();
    const float textW = ImGui::CalcTextSize(valStr).x;
    const float offset = (valW > textW) ? (valW - textW) * 0.5f : 0.0f;
    ImGui::SetCursorPosX(curX + offset);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.85f, 0.96f, 0.87f, 1.0f), "%s", valStr);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(curX + valW + 8.0f);

    std::snprintf(tag, sizeof(tag), ">##%s", id);
    if (ImGui::Button(tag, ImVec2(btnW, 0))) onCycle(+1);
}

static void ToggleRow(const char* label, const char* id, bool* value, const char* subtitle = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (subtitle && subtitle[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.62f, 0.56f, 1.0f));
        ImGui::TextWrapped("%s", subtitle);
        ImGui::PopStyleColor();
    }
    ImGui::TableSetColumnIndex(1);
    return;
}

static void DrawRibbonDisplayTab(void) {
    const bool gpuActive = Port_GPU_IsActive();
    const float em = ImGui::GetFontSize();
    const float labelColW = em * 13.5f;
    const ImGuiTableFlags tblFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;

    ImGui::SeparatorText(UiTr("Screen & Aspect Ratio", "Schermo & Proporzioni"));
    if (ImGui::BeginTable("##display_screen_table", 2, tblFlags)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelColW);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
        {
            bool ws = Port_Config_WidescreenEnabled();
            ToggleRow("Widescreen (16:9)", "##ws", &ws,
                      UiTr("Extends horizontal view to 384x160 pixels",
                           "Estende la visuale orizzontale a 384x160 pixel"));
            if (ImGui::Checkbox("##ws_chk", &ws)) {
                Port_Config_SetWidescreenEnabled(ws);
                Port_PPU_ApplyWindowScale();
                Port_DebugMenu_ToastFromExternal(ws ? UiTr("Widescreen enabled", "Widescreen attivato")
                                                    : UiTr("Widescreen disabled", "Widescreen disattivato"));
            }
        }
#endif

        StepperRow(UiTr("Aspect Ratio", "Proporzioni (Aspect Ratio)"), "aspect",
                   [](char*, size_t) { return Port_Config_AspectModeName(Port_Config_AspectMode()); },
                   Port_Config_CycleAspectMode,
                   UiTr("Fit image to screen or keep original aspect ratio",
                        "Adatta l'immagine allo schermo o mantieni il formato originale"));

        StepperRow(UiTr("Scaling Filter", "Filtro Immagine"), "filter",
                   [](char*, size_t) { return Port_PPU_PresentationModeName(); },
                   Port_PPU_CyclePresentationMode,
                   UiTr("Crisp pixel-art (Nearest) or smooth (Linear)",
                        "Nitidezza pixel-perfect (Nearest) o morbido (Linear)"));

        StepperRow(UiTr("CRT / Scanline Effect", "Effetto CRT / Scanline"), "crt",
                   [](char*, size_t) { return Port_PPU_FilterName(); },
                   Port_PPU_CycleFilter);

        StepperRow(UiTr("Internal Resolution", "Risoluzione Interna"), "iscale",
                   [](char* b, size_t n) -> const char* {
                       std::snprintf(b, n, "%ux", (unsigned)Port_Config_InternalScale());
                       return b;
                   },
                   Port_Config_CycleInternalScale);

        StepperRow(UiTr("Letterbox Background", "Sfondo Cornici"), "bgfill",
                   [](char*, size_t) { return Port_Config_BgFillName(Port_Config_BgFill()); },
                   Port_Config_CycleBgFill);

#ifndef __ANDROID__
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(UiTr("Window Scale", "Scala Finestra"));
        ImGui::TableSetColumnIndex(1);
        {
            const float btnW = em * 1.85f;
            int scale = (int)Port_PPU_WindowScale();
            if (ImGui::Button("<##scale", ImVec2(btnW, 0))) Port_PPU_CycleWindowScale(-1);
            ImGui::SameLine();
            ImGui::Text(" %dx ", scale);
            ImGui::SameLine();
            if (ImGui::Button(">##scale", ImVec2(btnW, 0))) Port_PPU_CycleWindowScale(+1);
        }

        {
            bool fs = Port_PPU_IsFullscreen();
            ToggleRow(UiTr("Fullscreen", "Schermo Intero (Fullscreen)"), "##fs", &fs);
            if (ImGui::Checkbox("##fs_chk", &fs)) {
                Port_PPU_ToggleFullscreen();
                Port_Config_SetFullscreen(Port_PPU_IsFullscreen());
            }
        }
#endif
        ImGui::EndTable();
    }

    if (Port_Config_BgFill() == PORT_BG_FILL_SOLID_COLOR) {
        uint8_t r8 = 0, g8 = 0, b8 = 0;
        Port_Config_BgFillColor(&r8, &g8, &b8);
        int rgb[3] = { (int)r8, (int)g8, (int)b8 };
        bool changed = false;
        ImGui::PushItemWidth(em * 5.5f);
        if (ImGui::SliderInt("R##bgcol_r", &rgb[0], 0, 255)) changed = true;
        ImGui::SameLine();
        if (ImGui::SliderInt("G##bgcol_g", &rgb[1], 0, 255)) changed = true;
        ImGui::SameLine();
        if (ImGui::SliderInt("B##bgcol_b", &rgb[2], 0, 255)) changed = true;
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImVec4 sw = ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f);
        ImGui::ColorButton("##bgcol_preview", sw, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip,
                           ImVec2(em * 1.4f, em * 1.4f));
        if (changed) {
            Port_Config_SetBgFillColor((uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2]);
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(UiTr("Colors & GBA Accuracy", "Colori & Fedelt\xC3\xA0 GBA"));
    if (ImGui::BeginTable("##display_color_table", 2, tblFlags)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelColW);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        {
            bool colourCorrect = Port_PPU_ColorCorrectionEnabled();
            ToggleRow(UiTr("GBA Color Correction", "Correzione Colori GBA"), "##colcorr", &colourCorrect,
                      UiTr("Balances the oversaturated raw GBA palette",
                           "Bilancia la saturazione eccessiva tipica della palette GBA grezza"));
            if (ImGui::Checkbox("##colcorr_chk", &colourCorrect)) {
                Port_PPU_SetColorCorrection(colourCorrect);
                Port_Config_SetColorCorrection(colourCorrect);
            }
        }

        {
            bool lcdPersist = Port_Config_GetLcdPersistence();
            ToggleRow(UiTr("LCD Persistence (Ghosting)", "Persistenza LCD (Ghosting)"), "##lcdp", &lcdPersist,
                      UiTr("Simulates GBA LCD response trail for flicker transparency effects",
                           "Simula la scia del display GBA per gli effetti di trasparenza"));
            if (ImGui::Checkbox("##lcdp_chk", &lcdPersist)) {
                Port_Config_SetLcdPersistence(lcdPersist);
                Port_PPU_SetPersistence(lcdPersist, Port_Config_GetLcdPersistenceRho());
            }
            if (lcdPersist) {
                ImGui::SameLine();
                float rho = Port_Config_GetLcdPersistenceRho();
                ImGui::SetNextItemWidth(em * 7.0f);
                if (ImGui::SliderFloat("##lcd_rho", &rho, 0.0f, 0.95f, "%.2f")) {
                    Port_Config_SetLcdPersistenceRho(rho);
                    Port_PPU_SetPersistence(true, rho);
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText(UiTr("Performance & Rendering", "Prestazioni & Rendering"));
    if (ImGui::BeginTable("##display_perf_table", 2, tblFlags)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelColW);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        {
            bool sf = Port_Config_GetShowFps();
            ToggleRow(UiTr("Show FPS / TPS Counter", "Mostra Contatore FPS / TPS"), "##show_fps", &sf,
                      UiTr("Displays frame rate and game speed in the top-right corner",
                           "Mostra in alto a destra il frame rate e la velocit\xC3\xA0 di gioco"));
            if (ImGui::Checkbox("##show_fps_chk", &sf)) {
                Port_Config_SetShowFps(sf);
            }
        }

        StepperRow("Target FPS", "fps",
                   [](char* b, size_t n) -> const char* {
                       unsigned fps = Port_Config_TargetFps();
                       if (fps == 0) return UiTr("Uncapped", "Sbloccato");
                       std::snprintf(b, n, "%u FPS", fps);
                       return b;
                   },
                   Port_Config_CycleTargetFps);

        {
            bool dec = Port_Config_GetDecoupleRender();
            ToggleRow(UiTr("Decoupled Game Speed", "Velocit\xC3\xA0 Gioco Disaccoppiata"), "##decouple", &dec,
                      UiTr("Keeps game logic at a fixed 60 Hz and accelerates Fast-Forward",
                           "Mantiene la logica a 60 Hz fissi e accelera il Fast-Forward"));
            if (ImGui::Checkbox("##decouple_render", &dec)) {
                Port_Config_SetDecoupleRender(dec);
            }
        }

        {
            bool raster = Port_Config_GetGpuRaster();
            ToggleRow(UiTr("GPU Rasterizer", "Rasterizzatore GPU"), "##gpu_raster", &raster,
                      gpuActive ? UiTr("Renders PPU via GPU shader (disable for CPU fallback)",
                                       "Disegna la PPU via shader GPU (disattiva per fallback CPU)")
                                : UiTr("Requires GPU backend", "Richiede backend GPU"));
            ImGui::BeginDisabled(!gpuActive);
            if (ImGui::Checkbox("##gpu_raster_chk", &raster)) {
                Port_Config_SetGpuRaster(raster);
            }
            ImGui::EndDisabled();
        }

        StepperRow(UiTr("Renderer Backend (restart)", "Backend Rendering (riavvio)"), "renderer",
                   [](char*, size_t) { return Port_Config_RenderBackendName(Port_Config_RenderBackend()); },
                   Port_Config_CycleRenderBackend,
                   UiTr("Applies on next app launch", "Richiede il riavvio dell'applicazione"));

#ifndef __ANDROID__
        {
            bool vsync = Port_PPU_VSyncEnabled();
            ToggleRow("VSync", "##vsync", &vsync);
            if (ImGui::Checkbox("##vsync_chk", &vsync)) {
                Port_PPU_SetVSync(vsync);
                Port_Config_SetVSync(vsync);
            }
        }
#endif
        ImGui::EndTable();
    }

    const bool glslpSupported = gpuActive && Port_GPU_SupportsGlslpRuntime();
    if (glslpSupported && ImGui::CollapsingHeader("Shader Presets (.glslp)")) {
        static std::vector<std::string> sPresetPaths;
        static std::string              sActivePreset;
        static bool                     sScannedOnce = false;

        auto refresh = [&] {
            sPresetPaths.clear();
            std::vector<std::string> roots;
            if (const char* base = SDL_GetBasePath()) {
                std::string b = base;
                roots.push_back(b + "shaders");
                roots.push_back(b + "glslp");
                roots.push_back(b);
            }
            roots.emplace_back("shaders");
            roots.emplace_back("glslp");
            roots.emplace_back(".");
            for (const auto& root : roots) {
                std::error_code dec;
                if (!std::filesystem::is_directory(root, dec)) continue;
                std::filesystem::recursive_directory_iterator it(
                    root, std::filesystem::directory_options::skip_permission_denied, dec);
                const std::filesystem::recursive_directory_iterator end;
                for (; !dec && it != end; it.increment(dec)) {
                    std::error_code fec;
                    if (it->is_regular_file(fec) && it->path().extension() == ".glslp") {
                        sPresetPaths.push_back(it->path().string());
                    }
                }
            }
            std::sort(sPresetPaths.begin(), sPresetPaths.end());
            sPresetPaths.erase(std::unique(sPresetPaths.begin(), sPresetPaths.end()),
                               sPresetPaths.end());
        };
        if (!sScannedOnce) {
            refresh();
            if (sActivePreset.empty() && Port_GlslpRuntime_IsActive()) {
                const char* sp = Port_Config_GetShaderPreset();
                if (sp && sp[0]) sActivePreset = sp;
            }
            sScannedOnce = true;
        }

        if (ImGui::Button("Disattiva Shader##preset")) {
            Port_GlslpRuntime_Unload();
            sActivePreset.clear();
            Port_Config_SetShaderPreset("");
            Port_DebugMenu_ToastFromExternal("Shader preset disattivato");
        }
        ImGui::SameLine();
        if (ImGui::Button("Ricarica Lista##preset")) refresh();
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu trovati)", sPresetPaths.size());

        if (!sPresetPaths.empty()) {
            if (ImGui::BeginListBox("##preset_list",
                    ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 4.0f))) {
                for (size_t i = 0; i < sPresetPaths.size(); ++i) {
                    bool selected = (sPresetPaths[i] == sActivePreset) && Port_GlslpRuntime_IsActive();
                    std::string label = std::filesystem::path(sPresetPaths[i]).filename().string();
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        if (selected) {
                            Port_GlslpRuntime_Unload();
                            sActivePreset.clear();
                            Port_Config_SetShaderPreset("");
                            Port_DebugMenu_ToastFromExternal("Shader preset disattivato");
                        } else if (Port_GlslpRuntime_Load(sPresetPaths[i].c_str())) {
                            sActivePreset = sPresetPaths[i];
                            Port_Config_SetShaderPreset(sActivePreset.c_str());
                            Port_DebugMenu_ToastFromExternal("Shader caricato");
                        } else {
                            Port_DebugMenu_ToastFromExternal("Errore caricamento shader");
                        }
                    }
                }
                ImGui::EndListBox();
            }
        }
    }
}

