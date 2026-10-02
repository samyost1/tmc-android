/* DrawRibbonDisplayTab — extracted from port_imgui_menu.cpp so the
 * per-tab UI code stays in smaller, navigable chunks. This file is
 * included into the same translation unit and intentionally relies on
 * the surrounding ImGui/Port_* declarations already visible there. */
extern "C" { typedef void (*PortStepCycle)(int); }
typedef const char* (*PortStepValue)(char* buf, size_t cap);

/* One Display-tab settings row: label, then a <value> stepper flanked by
 * < / > buttons. getValue() is read *between* the buttons so a click's new
 * value shows the same frame, matching the hand-written rows. */
static void StepperRow(const char* label, const char* id,
                       PortStepValue getValue, PortStepCycle onCycle) {
    char tag[24];
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::Text("%s", label);
    ImGui::TableSetColumnIndex(1);
    std::snprintf(tag, sizeof(tag), "<##%s", id);
    if (ImGui::Button(tag)) onCycle(-1);
    ImGui::SameLine();
    char buf[24];
    ImGui::Text("%s", getValue(buf, sizeof(buf)));
    ImGui::SameLine();
    std::snprintf(tag, sizeof(tag), ">##%s", id);
    if (ImGui::Button(tag)) onCycle(+1);
}

static void DrawRibbonDisplayTab(void) {
    const bool gpuActive = Port_GPU_IsActive();
    /* Label column sized to the longest row label so it never clips at any
     * font scale; the indented blocks below the table align to its value
     * column (label width + the cell padding on both sides). */
    const float labelW = std::max(150.0f, ImGui::CalcTextSize("Decouple game speed").x);
    const float valueX = labelW + ImGui::GetStyle().CellPadding.x * 2.0f;

    if (ImGui::BeginTable("##display_settings_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelW);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        // Window scale - value captured before the buttons (read-before).
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Window scale");
        ImGui::TableSetColumnIndex(1);
        int scale = (int)Port_PPU_WindowScale();
        if (ImGui::Button("<##scale")) Port_PPU_CycleWindowScale(-1);
        ImGui::SameLine(); ImGui::Text("%dx", scale); ImGui::SameLine();
        if (ImGui::Button(">##scale")) Port_PPU_CycleWindowScale(+1);

        StepperRow("Filter", "filter",
                   [](char*, size_t) { return Port_PPU_PresentationModeName(); },
                   Port_PPU_CyclePresentationMode);

        StepperRow("Target FPS", "fps",
                   [](char* b, size_t n) -> const char* {
                       unsigned fps = Port_Config_TargetFps();
                       if (fps == 0) return "uncapped";
                       std::snprintf(b, n, "%u", fps);
                       return b;
                   },
                   Port_Config_CycleTargetFps);
        RandoUi_HelpTooltip(
            "How often frames are presented to the display. With 'Decouple game "
            "speed' on (default) this only changes display smoothness - the game "
            "always runs at normal speed, and 'uncapped' acts as a persistent "
            "fast-forward. With it off, this paces the engine directly, so 120 "
            "runs the game at 2x speed.");

        /* Decoupled pacing: game logic on a fixed tick grid, presents on the
         * Target FPS grid (port_bios.c). Off = legacy coupled loop. */
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Decouple game speed");
        ImGui::TableSetColumnIndex(1);
        {
            bool dec = Port_Config_GetDecoupleRender();
            if (ImGui::Checkbox("##decouple_render", &dec)) {
                Port_Config_SetDecoupleRender(dec);
            }
        }
        RandoUi_HelpTooltip(
            "Runs game logic at a fixed 60 Hz (or the exact GBA rate in "
            "Console-Parity mode) no matter what Target FPS or VSync do. "
            "Fast-forward gets faster too: the engine skips rendering the "
            "frames it doesn't show.");

        /* On-screen FPS/TPS counter (DrawFpsOverlay, port_imgui_menu.cpp). */
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Show FPS counter");
        ImGui::TableSetColumnIndex(1);
        {
            bool sf = Port_Config_GetShowFps();
            if (ImGui::Checkbox("##show_fps", &sf)) {
                Port_Config_SetShowFps(sf);
            }
        }
        RandoUi_HelpTooltip(
            "Top-right overlay. FPS = frames shown per second (render rate); "
            "TPS = game logic ticks per second. With decoupling on, TPS "
            "staying at 60 while FPS follows your Target FPS is the split "
            "working. TPS turns red when the game runs off-speed "
            "(fast-forward, or the machine can't keep up).");

        StepperRow("Internal scale", "iscale",
                   [](char* b, size_t n) -> const char* {
                       std::snprintf(b, n, "%ux", (unsigned)Port_Config_InternalScale());
                       return b;
                   },
                   Port_Config_CycleInternalScale);
        RandoUi_HelpTooltip(
            "Renders the GBA frame at this integer multiple internally before "
            "the final upscale. It only changes sampling quality when a shader "
            "or the widescreen path resamples the image - with the default "
            "nearest-neighbour 2D output it has no visible effect. Leave at 1x "
            "unless a shader you're using benefits from supersampling.");

        StepperRow("Aspect ratio", "aspect",
                   [](char*, size_t) { return Port_Config_AspectModeName(Port_Config_AspectMode()); },
                   Port_Config_CycleAspectMode);

        StepperRow("Renderer", "renderer",
                   [](char*, size_t) { return Port_Config_RenderBackendName(Port_Config_RenderBackend()); },
                   Port_Config_CycleRenderBackend);
        ImGui::SameLine(); ImGui::TextDisabled("(restart)");

        /* GPU PPU rasterizer toggle (docs/gpu-rasterizer-design.md). Only
         * meaningful on the SDL_GPU backend; the CPU rasterizer is the
         * automatic fallback. Shown greyed-out otherwise. */
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("GPU rasterizer");
        ImGui::TableSetColumnIndex(1);
        {
            bool raster = Port_Config_GetGpuRaster();
            ImGui::BeginDisabled(!gpuActive);
            if (ImGui::Checkbox("##gpu_raster", &raster)) {
                Port_Config_SetGpuRaster(raster);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled(gpuActive ? "(CPU fallback)" : "(needs GPU backend)");
        }

        /* GLES compute rasterizer — opt-in fallback for devices without Vulkan.
         * ~5x slower than CPU on weak GLES GPUs (Adreno 405), so default off and
         * only relevant when the SDL_GPU (Vulkan) backend is NOT active. */
        if (!gpuActive) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("GLES raster (exp.)");
            ImGui::TableSetColumnIndex(1);
            bool gles = Port_Config_GetGpuRasterGles();
            if (ImGui::Checkbox("##gpu_raster_gles", &gles)) {
                Port_Config_SetGpuRasterGles(gles);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(often slower than CPU)");
        }

        StepperRow("Background", "bgfill",
                   [](char*, size_t) { return Port_Config_BgFillName(Port_Config_BgFill()); },
                   Port_Config_CycleBgFill);

        StepperRow("CRT filter", "crt",
                   [](char*, size_t) { return Port_PPU_FilterName(); },
                   Port_PPU_CycleFilter);

        ImGui::EndTable();
    }

    if (Port_Config_BgFill() == PORT_BG_FILL_SOLID_COLOR) {
        uint8_t r8 = 0, g8 = 0, b8 = 0;
        Port_Config_BgFillColor(&r8, &g8, &b8);
        int rgb[3] = { (int)r8, (int)g8, (int)b8 };
        bool changed = false;
        ImGui::Indent(valueX);
        ImGui::PushItemWidth(120.0f);
        if (ImGui::SliderInt("R##bgcol_r", &rgb[0], 0, 255)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SliderInt("G##bgcol_g", &rgb[1], 0, 255)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SliderInt("B##bgcol_b", &rgb[2], 0, 255)) {
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImVec4 sw = ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f);
        ImGui::ColorButton("##bgcol_preview", sw,
                           ImGuiColorEditFlags_NoPicker |
                           ImGuiColorEditFlags_NoTooltip,
                           ImVec2(22, 22));
        ImGui::Unindent(valueX);
        if (changed) {
            Port_Config_SetBgFillColor((uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2]);
        }
    }

    const bool glslpSupported = gpuActive && Port_GPU_SupportsGlslpRuntime();
    if (!glslpSupported) {
        ImGui::TextUnformatted("Shader preset");
        ImGui::Indent(valueX);
        ImGui::SameLine(ImGui::GetCursorPosX());
        ImGui::TextDisabled("%s", gpuActive ? "(SPIR-V backend required)" : "(GPU backend required)");
        ImGui::Unindent(valueX);
    } else {
        static std::vector<std::string> sPresetPaths;
        static std::string              sActivePreset;
        static bool                     sScannedOnce = false;

        auto refresh = [&] {
            sPresetPaths.clear();
            std::error_code ec;
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
                /* skip_permission_denied + the non-throwing increment(ec)
                 * overload: the range-for/`operator++` form throws
                 * filesystem_error mid-recursion on an unreadable entry,
                 * and nothing here catches it -> std::terminate. */
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
            /* Reflect the preset loaded from config at startup (#146) so the
             * list shows it selected and "active: ..." renders. */
            if (sActivePreset.empty() && Port_GlslpRuntime_IsActive()) {
                const char* sp = Port_Config_GetShaderPreset();
                if (sp && sp[0]) sActivePreset = sp;
            }
            sScannedOnce = true;
        }

        /* Label in the table's label column (under "CRT filter"); everything
         * else, including wrapped rows and the list, in the value column. */
        ImGui::TextUnformatted("Shader preset");
        ImGui::Indent(valueX);
        ImGui::SameLine(ImGui::GetCursorPosX());
        if (ImGui::Button("Off##preset")) {
            Port_GlslpRuntime_Unload();
            sActivePreset.clear();
            Port_Config_SetShaderPreset("");                  /* persist (#146) */
            Port_DebugMenu_ToastFromExternal("Shader preset off");
        }
        SameLineIfFits(ButtonWidth("Rescan##preset"));
        if (ImGui::Button("Rescan##preset")) refresh();
        char found[32];
        std::snprintf(found, sizeof(found), "(%zu found)", sPresetPaths.size());
        SameLineIfFits(ImGui::CalcTextSize(found).x);
        ImGui::TextDisabled("%s", found);

        const bool active = Port_GlslpRuntime_IsActive() != 0;
        if (active && !sActivePreset.empty()) {
            ImGui::TextDisabled("  active: %s", sActivePreset.c_str());
        }

        if (!sPresetPaths.empty()) {
            if (ImGui::BeginListBox("##preset_list",
                    ImVec2(ImGui::GetContentRegionAvail().x - 4.0f, ImGui::GetTextLineHeightWithSpacing() * 3.5f))) {
                for (size_t i = 0; i < sPresetPaths.size(); ++i) {
                    bool selected = (sPresetPaths[i] == sActivePreset)
                                    && Port_GlslpRuntime_IsActive();
                    std::string label = std::filesystem::path(sPresetPaths[i]).filename().string();
                    label += "    "; label += sPresetPaths[i];
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        if (selected) {
                            Port_GlslpRuntime_Unload();
                            sActivePreset.clear();
                            Port_Config_SetShaderPreset("");          /* persist (#146) */
                            Port_DebugMenu_ToastFromExternal("Shader preset off");
                        } else if (Port_GlslpRuntime_Load(sPresetPaths[i].c_str())) {
                            sActivePreset = sPresetPaths[i];
                            Port_Config_SetShaderPreset(sActivePreset.c_str()); /* persist (#146) */
                            Port_DebugMenu_ToastFromExternal("Preset loaded");
                        } else {
                            Port_DebugMenu_ToastFromExternal("Preset load FAILED");
                        }
                    }
                }
                ImGui::EndListBox();
            }
        }
        ImGui::Unindent(valueX);
    }

    ImGui::Separator();

#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
    {
        bool ws = Port_Config_WidescreenEnabled();
        if (ImGui::Checkbox("Widescreen (WIP)", &ws)) {
            Port_Config_SetWidescreenEnabled(ws);
            Port_PPU_ApplyWindowScale();
            Port_DebugMenu_ToastFromExternal(ws ? "Widescreen enabled" : "Widescreen disabled");
        }
        SameLineIfFits(CheckboxWidth("Fullscreen"));
    }
#endif

    bool fs = Port_PPU_IsFullscreen();
    if (ImGui::Checkbox("Fullscreen", &fs)) {
        Port_PPU_ToggleFullscreen();
        Port_Config_SetFullscreen(Port_PPU_IsFullscreen());   /* persist (#146) */
    }
    SameLineIfFits(CheckboxWidth("Hide cursor in fullscreen"));

    bool hideCur = Port_Config_GetFullscreenHideCursor();
    if (ImGui::Checkbox("Hide cursor in fullscreen", &hideCur)) {
        Port_Config_SetFullscreenHideCursor(hideCur);         /* persist */
        Port_PPU_ApplyCursorVisibility();                     /* live, if already fullscreen */
    }
    SameLineIfFits(CheckboxWidth("VSync"));

    bool vsync = Port_PPU_VSyncEnabled();
    if (ImGui::Checkbox("VSync", &vsync)) {
        Port_PPU_SetVSync(vsync);
        Port_Config_SetVSync(vsync);                          /* persist (#146) */
    }
    SameLineIfFits(CheckboxWidth("Discord RPC"));

    bool drp = Port_DiscordRpc_IsEnabled();
    if (ImGui::Checkbox("Discord RPC", &drp)) {
        Port_DiscordRpc_SetEnabled(drp);
        Port_Config_SetDiscordRpc(drp);                       /* persist (#146) */
    }
    RandoUi_HelpTooltip(
        "Publishes your current area and heart/rupee count to Discord as a "
        "Rich Presence status.\n\n"
        "Requires a Discord application ID compiled into the build "
        "(discord_app_id.txt, not shipped by default), so it stays inactive in "
        "release builds that ship without one. Also currently a no-op on "
        "Windows (the named-pipe IPC client is not yet implemented).");

    bool colourCorrect = Port_PPU_ColorCorrectionEnabled();
    if (ImGui::Checkbox("GBA colour correction", &colourCorrect)) {
        Port_PPU_SetColorCorrection(colourCorrect);           /* live */
        Port_Config_SetColorCorrection(colourCorrect);        /* persist */
    }
    RandoUi_HelpTooltip(
        "Remaps the raw 15-bit palette through the GBA's dim, high-gamma "
        "reflective-LCD response so colours match the handheld instead of "
        "looking over-bright on a modern sRGB display. Default on.");

    bool lcdPersist = Port_Config_GetLcdPersistence();
    if (ImGui::Checkbox("LCD persistence", &lcdPersist)) {
        Port_Config_SetLcdPersistence(lcdPersist);            /* persist */
        Port_PPU_SetPersistence(lcdPersist, Port_Config_GetLcdPersistenceRho());
    }
    RandoUi_HelpTooltip(
        "Reproduces the slow pixel response (ghosting) of the GBA LCD by "
        "blending each frame into the previous one. Some flicker/dither "
        "transparency effects were designed around it. Default off.");
    if (lcdPersist) {
        float rho = Port_Config_GetLcdPersistenceRho();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("Persistence strength", &rho, 0.0f, 0.95f, "%.2f")) {
            Port_Config_SetLcdPersistenceRho(rho);            /* persist */
            Port_PPU_SetPersistence(true, rho);               /* live */
        }
    }

    bool holdAdv = Port_Config_GetHoldToAdvanceText();
    if (ImGui::Checkbox("Hold to advance text", &holdAdv)) {
        Port_Config_SetHoldToAdvanceText(holdAdv);
    }
    RandoUi_HelpTooltip(
        "When on, holding an advance button (A / B / R / D-pad) keeps text "
        "pages turning instead of needing a fresh press each page. Default off "
        "preserves GBA-accurate timing for timed cutscenes.");
}
