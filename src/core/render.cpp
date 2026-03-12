#include <pch.h>

#include <core/render.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <misc/imgui_utility.h>
#include <game/rtech/utils/bsp/bspflags.h>

#include <core/render/dx.h>
#include <core/window.h>
#include <core/input/input.h>

#include <core/filehandling/export.h>

#include <game/rtech/cpakfile.h>
#include <game/rtech/assets/model.h>
#include <game/rtech/assets/texture.h>
#include <game/rtech/assets/settings.h>
#include <core/render/preview/preview.h>

extern CDXParentHandler* g_dxHandler;
extern std::atomic<uint32_t> g_maxConcurrentThreadCount;
extern ExportSettings_t g_ExportSettings;

PreviewSettings_t g_PreviewSettings { .previewCullDistance = PREVIEW_CULL_DEFAULT, .previewMovementSpeed = PREVIEW_SPEED_DEFAULT };

CPreviewDrawData g_currentPreviewDrawData;

std::atomic<bool> inJobAction = false;

enum AssetColumn_t
{
    AC_Type,
    AC_Name,
    AC_GUID,
    AC_File,

    _AC_COUNT,
};

struct AssetCompare_t
{
    const ImGuiTableSortSpecs* sortSpecs;

    // TODO: get rid of pak-specific stuff
    bool operator() (const CGlobalAssetData::AssetLookup_t& a, const CGlobalAssetData::AssetLookup_t& b) const
    {
        const CAsset* assetA = a.m_asset;
        const CAsset* assetB = b.m_asset;

        for (int n = 0; n < sortSpecs->SpecsCount; n++)
        {
            // Here we identify columns using the ColumnUserID value that we ourselves passed to TableSetupColumn()
            // We could also choose to identify columns based on their index (sort_spec->ColumnIndex), which is simpler!
            const ImGuiTableColumnSortSpecs* sort_spec = &sortSpecs->Specs[n];
            __int64 delta = 0;
            switch (sort_spec->ColumnUserID)
            {
            case AssetColumn_t::AC_Type:   delta = static_cast<int64_t>(_byteswap_ulong(assetA->GetAssetType())) - _byteswap_ulong(assetB->GetAssetType());                        break; // no overflow please
            case AssetColumn_t::AC_Name:   delta = _stricmp(assetA->GetAssetName().c_str(), assetB->GetAssetName().c_str());                        break; // no overflow please
            case AssetColumn_t::AC_GUID:   delta = assetA->GetAssetGUID() - assetB->GetAssetGUID();     break;
            case AssetColumn_t::AC_File:   delta = _stricmp(assetA->GetContainerFileName().c_str(), assetB->GetContainerFileName().c_str());     break;
            default: IM_ASSERT(0); break;
            }
            if (delta > 0)
                return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? false : true;
            if (delta < 0)
                return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? true : false;
        }

        return (static_cast<int64_t>(assetA->GetAssetType()) - assetB->GetAssetType()) > 0;
    }
};

void ColouredTextForAssetType(const CAsset* const asset)
{
    bool colouredText = false;

    switch (asset->GetAssetContainerType())
    {
    case CAsset::ContainerType::PAK:
    case CAsset::ContainerType::AUDIO:
    case CAsset::ContainerType::MDL:
    case CAsset::ContainerType::BP_PAK:
    {
        const AssetType_t assetType = static_cast<AssetType_t>(asset->GetAssetType());
        if (s_AssetTypeColours.contains(assetType))
        {
            colouredText = true;

            const Color4& col = s_AssetTypeColours.at(assetType);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(col.r, col.g, col.b, col.a));
        }

        ImGui::Text("%s %s", fourCCToString(asset->GetAssetType()).c_str(), asset->GetAssetVersion().ToString().c_str());
        break;
    }
    default:
    {
        ImGui::Text("unimpl");
        break;
    }
    }

    if (colouredText)
        ImGui::PopStyleColor();
}

// [ Preview ] Render the asset dependencies table in the preview window for the provided asset
void PreviewWnd_AssetDepsTbl(CAsset* asset)
{
    CPakAsset* pakAsset = static_cast<CPakAsset*>(asset);

    std::vector<AssetGuid_t> dependencies;
    pakAsset->getDependencies(dependencies);

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBody
        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

    const ImVec2 outerSize = ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 12.f);

    constexpr int numColumns = 5;

    if (ImGui::TreeNodeEx("Asset Dependencies", ImGuiTreeNodeFlags_SpanAvailWidth))
    {
        if (ImGui::BeginTable("Assets", numColumns, tableFlags, outerSize))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 0);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 1);
            ImGui::TableSetupColumn("Pak", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 2);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 3);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 4);
            ImGui::TableSetupScrollFreeze(1, 1);

            ImGui::TableHeadersRow();

            for (int d = 0; d < dependencies.size(); ++d)
            {
                AssetGuid_t guid = dependencies[d];
                CAsset* depAsset = g_assetData.FindAssetByGUID<CPakAsset>(guid.guid);

#if _DEBUG
                // [rika]: cannot access depAsset if nullptr
                if (depAsset)
                    assert(depAsset->GetAssetContainerType() == CAsset::ContainerType::PAK);
#endif // _DEBUG

                ImGui::PushID(d);

                ImGui::TableNextRow(ImGuiTableRowFlags_None, 0.f);

                // Asset Type
                if (ImGui::TableSetColumnIndex(0))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                        ColouredTextForAssetType(depAsset);
                    else
                        ImGui::TextUnformatted("n/a");
                }

                // Asset GUID
                if (ImGui::TableSetColumnIndex(1))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                        ImGui::TextUnformatted(depAsset->GetAssetName().c_str());
                    else
                        ImGui::Text("%016llX", guid.guid);
                }

                // Asset Container Name (e.g. common.rpak, general_stream.mstr)
                if (ImGui::TableSetColumnIndex(2))
                {
                    ImGui::AlignTextToFramePadding();
                    if (!depAsset)
                        ImGui::TextUnformatted("n/a");
                    else
                        ImGui::TextUnformatted(depAsset->GetContainerFileName().c_str());
                }

                // Export Status
                if (ImGui::TableSetColumnIndex(3))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                    {
                        if (depAsset->GetExportedStatus())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 1.f, 1.f));
                            ImGui::TextUnformatted("Exported");
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                            ImGui::TextUnformatted("Loaded");
                        }
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                        ImGui::TextUnformatted("Not Loaded");
                    }

                    ImGui::PopStyleColor();
                }

                if (ImGui::TableSetColumnIndex(4))
                {
                    ImGui::AlignTextToFramePadding();
                    if (!depAsset)
                        ImGui::BeginDisabled();

                    if (ImGui::Button("Export"))
                        CThread(HandleExportBindingForAsset, depAsset, g_ExportSettings.exportAssetDeps).detach();

                    if (!depAsset)
                        ImGui::EndDisabled();
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::TreePop();
    }
}

void SettingsWnd_Draw(CUIState* uiState)
{
    constexpr uint32_t minThreads = 1u;

    ImGui::SetNextWindowSize(ImVec2(0.f, 0.f), ImGuiCond_Always);
    if (ImGui::Begin("Settings", &uiState->settingsWindowVisible, ImGuiWindowFlags_None))
    {
        ImGui::SeparatorText("General");

        ImGui::Checkbox("Check for updates", &UtilsConfig->checkForUpdates);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("RSX will check for updates against the GitHub repository on startup and let the user know if they should update");
        // ===============================================================================================================
        ImGui::SeparatorText("Export");

        ImGui::Checkbox("Export full asset paths", &g_ExportSettings.exportPathsFull);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Enables exporting of assets to their full path within the export directory, as shown by the listed asset names.\nWhen disabled, assets will be exported into the root-level of a folder named after the asset's type (e.g. \"material/\",\"ui_image/\").");

        ImGui::Checkbox("Export asset dependencies", &g_ExportSettings.exportAssetDeps);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Enables exporting of all dependencies that are associated with any asset that is being exported.");

        ImGui::Checkbox("Disable CacheDB names", &g_ExportSettings.disableCachedNames);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Disables loading names from the cache file, new names will still be added.");

        // texture settings
        ImGui::SeparatorText("Export (Textures)");

        ImGui::Combo("Material Texture Names", reinterpret_cast<int*>(&g_ExportSettings.exportTextureNameSetting), s_TextureExportNameSetting, static_cast<int>(ARRAYSIZE(s_TextureExportNameSetting)));
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Naming scheme for exporting textures via materials options are as follows:\nGUID: exports only using the asset's GUID as a name.\nReal: exports texture using a real name (asset name or guid if no name).\nText: exports the texture with a text name always, generating one if there is none provided.\nSemantic: exports with a generated name all the time, useful for models.");

        ImGui::Combo("Normal Recalc", reinterpret_cast<int*>(&g_ExportSettings.exportNormalRecalcSetting), s_NormalExportRecalcSetting, static_cast<int>(ARRAYSIZE(s_NormalExportRecalcSetting)));
        ImGui::SameLine();
        ImGuiExt::HelpMarker("None: exports the normal as it is stored.\nDirectX: exports with a generated blue channel.\nOpenGL: exports with a generated blue channel and inverts the green channel.");

        ImGui::Checkbox("Export Material Textures", &g_ExportSettings.exportMaterialTextures);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Enables exporting of all textures that are associated with any material asset that is being exported.");

        // model settings
        ImGui::SeparatorText("Export (Models)");

        ImGui::Checkbox("Export Sequences", &g_ExportSettings.exportRigSequences);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Enables exporting of all animation sequences that are associated with any rig or model asset that is being exported.");

        ImGui::Checkbox("Export Skin", &g_ExportSettings.exportModelSkin);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Enables exporting a model with the previewed skin.");

        ImGui::Checkbox("Truncate Materials", &g_ExportSettings.exportModelMatsTruncated);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Truncates material names on SMD.");

        ImGui::Checkbox("Enable QCI Files", &g_ExportSettings.exportQCIFiles);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("QC file will be split into multiple include files.");

        ImGui::PushItemWidth(48.0f);
        ImGui::InputScalar("##QCTargetMajor", ImGuiDataType_U16, reinterpret_cast<uint16_t*>(&g_ExportSettings.qcMajorVersion), nullptr, nullptr, "%u", ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        ImGui::InputScalar("##QCTargetMinor", ImGuiDataType_U16, reinterpret_cast<uint16_t*>(&g_ExportSettings.qcMinorVersion), nullptr, nullptr, "%u", ImGuiInputTextFlags_CharsDecimal);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Text("QC Target Version");
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Desired version for QC files to be compatible with.");

        // physics settings
        ImGui::InputInt("Physics contents filter", reinterpret_cast<int*>(&g_ExportSettings.exportPhysicsContentsFilter), 1, 100, ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Filter physics meshes in or out based on selected contents.");

        ImGui::Checkbox("Physics contents filter exclusive", &g_ExportSettings.exportPhysicsFilterExclusive);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Exclude physics meshes containing any of the specified contents.");

        ImGui::Checkbox("Physics contents filter require all", &g_ExportSettings.exportPhysicsFilterAND);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Filter only physics meshes containing all specified contents.");

        // ===============================================================================================================
        ImGui::SeparatorText("Parsing");

        ImGui::Combo("Compression Level", reinterpret_cast<int*>(&UtilsConfig->compressionLevel), s_CompressionLevelSetting, static_cast<int>(ARRAYSIZE(s_CompressionLevelSetting)));
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Specifies the compression level used when storing parsed assets in memory.\nWARNING: Modify only if you know what you’re doing; otherwise, you may run out of memory.\nNone: no compression.\nSuper Fast: Fastest level with the lowest compression ratio.\nVery Fast: Standard setting; fastest level with a decent compression ratio.\nFast: Fastest level with a good compression ratio.\nNormal: Standard LZ speed with the highest compression ratio.");

        ImGui::SliderScalar("Parse Threads", ImGuiDataType_U32, &UtilsConfig->parseThreadCount, &minThreads, reinterpret_cast<int*>(&g_maxConcurrentThreadCount));
        ImGui::SameLine();
        ImGuiExt::HelpMarker("The number of CPU threads that will be used for loading files.\n\nIn general, the higher the number, the faster RSX will be able to load the selected files.");

        ImGui::SliderScalar("Export Threads", ImGuiDataType_U32, &UtilsConfig->exportThreadCount, &minThreads, reinterpret_cast<int*>(&g_maxConcurrentThreadCount));
        ImGui::SameLine();
        ImGuiExt::HelpMarker("The number of CPU threads that will be used for exporting assets.\n\nA higher number of threads will usually make RSX export assets more quickly, however the increased disk usage may cause decreased performance.");

        // ===============================================================================================================
        ImGui::SeparatorText("Preview");

        ImGui::SliderFloat("Cull Distance", &g_PreviewSettings.previewCullDistance, PREVIEW_CULL_MIN, PREVIEW_CULL_MAX);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Distance at which render of 3D objects will stop. Note: only updated on startup.\n"); // todo: recreate projection matrix ?

        ImGui::SliderFloat("Movement Speed", &g_PreviewSettings.previewMovementSpeed, PREVIEW_SPEED_MIN, PREVIEW_SPEED_MAX);
        ImGui::SameLine();
        ImGuiExt::HelpMarker("Speed at which the camera moves through the 3D scene.\n");

        // ===============================================================================================================
        ImGui::SeparatorText("Export Formats");

        for (auto& it : g_assetData.m_assetTypeBindings)
        {
            if (it.second.e.exportSettingArr)
            {
                ImGui::Combo(fourCCToString(it.first).c_str(), &it.second.e.exportSetting, it.second.e.exportSettingArr, static_cast<int>(it.second.e.exportSettingArrSize));
            }
        }

        ImGui::End();
    }
}
extern void ItemflavWnd_Draw(CUIState*);
extern void LogWnd_Draw(CUIState*);

static std::deque<CAsset*> s_selectedAssets;
static std::vector<CGlobalAssetData::AssetLookup_t> s_filteredAssets;
static CAsset* s_prevRenderInfoAsset = nullptr;

void ApplySelectionRequests(ImGuiMultiSelectIO* ms_io, std::deque<CAsset*>& selectedAssets, std::vector<CGlobalAssetData::AssetLookup_t>& pakAssets)
{
    for (ImGuiSelectionRequest& req : ms_io->Requests)
    {
        if (req.Type == ImGuiSelectionRequestType_SetAll) {
            selectedAssets.clear();
            if (req.Selected) {
                for (int n = 0; n < pakAssets.size(); n++)
                {
                    selectedAssets.insert(selectedAssets.end(), pakAssets[n].m_asset);
                }
            }
        }

        if (req.Type == ImGuiSelectionRequestType_SetRange)
        {
            for (int n = (int)req.RangeFirstItem; n <= (int)req.RangeLastItem; n++)
            {
                auto iter = std::find(selectedAssets.begin(), selectedAssets.end(), pakAssets[n].m_asset);
                const bool isSelected = iter != selectedAssets.end();

                if (!isSelected && req.Selected) selectedAssets.insert(selectedAssets.end(), pakAssets[n].m_asset);
                if (isSelected && !req.Selected) selectedAssets.erase(iter);
            }
        }
    }
}

void MainWnd_MenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        CUIState& uiState = g_dxHandler->GetUIState();
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open"))
            {
                if (!inJobAction)
                {
                    s_selectedAssets.clear();
                    s_filteredAssets.clear();
                    s_prevRenderInfoAsset = nullptr;
                    g_assetData.ClearAssetData();
                    uiState.ClearAssetData();

                    // We kinda leak the thread here but it's okay, we want it to keep executing.
                    CThread(HandleOpenFileDialog, g_dxHandler->GetWindowHandle()).detach();
                }
            }

            if (ImGui::MenuItem("Unload Files"))
            {
                if (!inJobAction)
                {
                    if (g_assetData.v_assets.size() > 0)
                        g_assetData.Log_Info(nullptr, "Unloaded %lld asset%s from %lld container file%s", g_assetData.v_assets.size(), g_assetData.v_assets.size() == 1 ? "" : "s", g_assetData.v_assetContainers.size(), g_assetData.v_assetContainers.size() == 1 ? "" : "s");

                    s_selectedAssets.clear();
                    s_filteredAssets.clear();
                    s_prevRenderInfoAsset = nullptr;
                    g_assetData.ClearAssetData();
                    uiState.ClearAssetData();
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Settings"))
                uiState.ShowSettingsWindow(true);

#if defined(HAS_ITEMFLAV_WINDOW)
            if (ImGui::MenuItem("Itemflavors"))
                uiState.ShowItemflavWindow(true);
#endif

            if (ImGui::MenuItem("Logs"))
                uiState.ShowLogWindow(true);

            ImGui::EndMenu();
        }

#if _DEBUG
        IMGUI_RIGHT_ALIGN_FOR_TEXT("Avg 1.000 ms/frame (100.0 FPS)"); // [rexx]: i hate this actually

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Avg %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
#endif
        ImGui::EndMainMenuBar();
    }
}

void MainWnd_WelcomeBox()
{
    if (!inJobAction && g_assetData.v_assetContainers.empty())
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);

        // [Dialog] Welcome Message
        if (ImGui::Begin("reSource Xtractor", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(RSX_WELCOME_MESSAGE);
            ImGui::PopTextWrapPos();

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.f);

            if (ImGui::Button("Open File..."))
            {
                // Reset selected asset to avoid crash.
                s_selectedAssets.clear();
                s_filteredAssets.clear();
                s_prevRenderInfoAsset = nullptr;
                g_assetData.ClearAssetData();

                // We kinda leak the thread here but it's okay, we want it to keep executing.
                CThread(HandleOpenFileDialog, g_dxHandler->GetWindowHandle()).detach();
            }

            ImGui::End();
        }
    }
}

static ImVec2 s_previousAvailableSizeForPreview(-1, -1);

void HandleRenderFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // create a docking area across the entire viewport
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");

        // If the dockspace hasn't been created yet
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
        {
            DockBuilder(dockspaceId)
                .Window("Scene", true)
                .DockLeft(0.25f)
                    .Window("Asset List")
                    .Done()
                .DockRight(0.25f)
                    .Window("Asset Info");
        }
        //ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::DockSpaceOverViewport(dockspaceId, NULL, ImGuiDockNodeFlags_PassthruCentralNode, 0);

    }

    // while ImGui is using keyboard input, we should not accept any keyboard input, but we should also clear all  
    // existing key states, as otherwise we can end up moving forever (until ImGui loses focus) in model preview if
    // the movement keys are held when ImGui starts capturing keyboard
    if (ImGui::GetIO().WantCaptureKeyboard)
        g_pInput->ClearKeyStates();

    if (g_pInput->mouseCaptured)
        g_pInput->Frame(ImGui::GetIO().DeltaTime);

    g_dxHandler->GetCamera()->Move(ImGui::GetIO().DeltaTime);

    CUIState& uiState = g_dxHandler->GetUIState();

#if defined(DEBUG_IMGUI_DEMO)
    ImGui::ShowDemoWindow();
#endif

    MainWnd_MenuBar();

    MainWnd_WelcomeBox();

    CDXDrawData* previewDrawData = nullptr;

    static size_t prevAssetCount = g_assetData.v_assets.size();

    const bool shouldPopulateAssetWindows = !inJobAction && !g_assetData.v_assetContainers.empty();

    if (ImGui::Begin("Asset List", nullptr, ImGuiWindowFlags_MenuBar) && shouldPopulateAssetWindows)
    {

        std::vector<CGlobalAssetData::AssetLookup_t>& pakAssets = FilterConfig->textFilter.IsActive() ? s_filteredAssets : g_assetData.v_assets;

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Export"))
            {
                const bool multipleAssetsSelected = s_selectedAssets.size() > 1;

                if (ImGui::Selectable(multipleAssetsSelected ? "Export selected assets" : "Export selected asset"))
                {
                    if (!s_selectedAssets.empty())
                    {
                        std::deque<CAsset*> cpyAssets;
                        cpyAssets.insert(cpyAssets.end(), s_selectedAssets.begin(), s_selectedAssets.end());
                        CThread(HandlePakAssetExportList, std::move(cpyAssets), g_ExportSettings.exportAssetDeps).detach();
                        s_selectedAssets.clear();
                    }
                }

                if (ImGui::Selectable("Export all for selected type", false, multipleAssetsSelected ? ImGuiSelectableFlags_Disabled : 0))
                {
                    if (s_selectedAssets.size() == 1)
                    {
                        const uint32_t desiredType = s_selectedAssets[0]->GetAssetType();
                        auto allAssetsOfDesiredType = pakAssets | std::ranges::views::filter([desiredType](const CGlobalAssetData::AssetLookup_t& a)
                            {
                                return a.m_asset->GetAssetType() == desiredType;
                            });

                        std::vector<CGlobalAssetData::AssetLookup_t> allAssets(allAssetsOfDesiredType.begin(), allAssetsOfDesiredType.end());
                        CThread(HandleExportSelectedAssetType, std::move(allAssets), g_ExportSettings.exportAssetDeps).detach();
                        s_selectedAssets.clear();
                    }
                }

                if (ImGui::Selectable("Export all for selected pak", false, multipleAssetsSelected ? ImGuiSelectableFlags_Disabled : 0))
                {
                    if (s_selectedAssets.size() == 1 && s_selectedAssets[0]->GetAssetContainerType() == CAsset::ContainerType::PAK)
                    {
                        const CPakFile* desiredPak = s_selectedAssets[0]->GetContainerFile<const CPakFile>();
                        auto allAssetsOfDesiredType = pakAssets | std::ranges::views::filter([desiredPak](const CGlobalAssetData::AssetLookup_t& a)
                            {
                                return a.m_asset->GetAssetContainerType() == CAsset::ContainerType::PAK && a.m_asset->GetContainerFile<const CPakFile>() == desiredPak;
                            });

                        std::vector<CGlobalAssetData::AssetLookup_t> allAssets(allAssetsOfDesiredType.begin(), allAssetsOfDesiredType.end());
                        CThread(HandleExportSelectedAssetType, std::move(allAssets), g_ExportSettings.exportAssetDeps).detach();
                        s_selectedAssets.clear();
                    }
                }

                if (ImGui::Selectable("Export all for selected pak and type", false, multipleAssetsSelected ? ImGuiSelectableFlags_Disabled : 0))
                {
                    if (s_selectedAssets.size() == 1 && s_selectedAssets[0]->GetAssetContainerType() == CAsset::ContainerType::PAK)
                    {
                        const CPakFile* desiredPak = s_selectedAssets[0]->GetContainerFile<const CPakFile>();
                        const uint32_t desiredType = s_selectedAssets[0]->GetAssetType();
                        auto allAssetsOfDesiredType = pakAssets | std::ranges::views::filter([desiredPak, desiredType](const CGlobalAssetData::AssetLookup_t& a)
                            {
                                return a.m_asset->GetAssetContainerType() == CAsset::ContainerType::PAK
                                    && a.m_asset->GetContainerFile<CPakFile>() == desiredPak
                                    && a.m_asset->GetAssetType() == desiredType;
                            });

                        std::vector<CGlobalAssetData::AssetLookup_t> allAssets(allAssetsOfDesiredType.begin(), allAssetsOfDesiredType.end());
                        CThread(HandleExportSelectedAssetType, std::move(allAssets), g_ExportSettings.exportAssetDeps).detach();
                        s_selectedAssets.clear();
                    }
                }

                if (ImGui::Selectable("Export all"))
                    CThread(HandleExportAllPakAssets, &pakAssets, g_ExportSettings.exportAssetDeps).detach();

                // Exports the names of all assets in the currently shown filtered asset list (i.e., search results)
                if (ImGui::Selectable("Export list of asset names..."))
                    CThread(HandleListExportPakAssets, g_dxHandler->GetWindowHandle(), &pakAssets).detach();

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // OR case if we load a pak and the filter is not cleared yet.
        if (FilterConfig->textFilter.Draw("##Filter", -1.f) || (s_filteredAssets.empty() && FilterConfig->textFilter.IsActive()) || prevAssetCount != g_assetData.v_assets.size())
        {
            s_filteredAssets.clear();
            for (auto& it : g_assetData.v_assets)
            {
                const std::string& assetName = it.m_asset->GetAssetName();

                if (FilterConfig->textFilter.PassFilter(assetName.c_str()))
                    s_filteredAssets.push_back(it);
                else
                {
                    const char* const inputText = FilterConfig->textFilter.inputBuf.c_str();
                    const size_t inputLen = FilterConfig->textFilter.inputBuf.size();

                    char* end;
                    const uint64_t guid = strtoull(inputText, &end, 0);

                    if (end == &inputText[inputLen])
                    {
                        if (guid == RTech::StringToGuid(assetName.c_str()))
                            s_filteredAssets.push_back(it);
                    }
                }
            }

            // Shrink capacity to match new size.
            s_filteredAssets.shrink_to_fit();
        }

        constexpr int numColumns = AssetColumn_t::_AC_COUNT;
        if (ImGui::BeginTable("Assets", numColumns, ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 0, AssetColumn_t::AC_Type);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0, AssetColumn_t::AC_Name);
            ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 0, AssetColumn_t::AC_GUID);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 0, AssetColumn_t::AC_File);

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();

            if (sortSpecs && sortSpecs->SpecsDirty && pakAssets.size() > 1)
            {
                std::sort(pakAssets.begin(), pakAssets.end(), AssetCompare_t(sortSpecs));
                sortSpecs->SpecsDirty = false;
            }

            ImGuiMultiSelectFlags flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
            ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(flags, static_cast<int>(s_selectedAssets.size()), static_cast<int>(pakAssets.size()));

            ApplySelectionRequests(ms_io, s_selectedAssets, pakAssets);

            // arbitrary large number that will never happen
            static unsigned int lastSelectionSize = UINT32_MAX;

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(pakAssets.size()));
            while (clipper.Step())
            {
                for (int rowNum = clipper.DisplayStart; rowNum < clipper.DisplayEnd; rowNum++)
                {
                    CAsset* const asset = pakAssets[rowNum].m_asset;

                    // previously this was GUID_pakCRC but realistically the pak filename also works instead of the crc (though it may be slower for lookup?)
                    ImGui::PushID(std::format("{:X}_{}", asset->GetAssetGUID(), asset->GetContainerFileName()).c_str());

                    ImGui::TableNextRow();

                    if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_Type))
                    {
                        ColouredTextForAssetType(asset);

                        auto typeBinding = g_assetData.m_assetTypeBindings.find(asset->GetAssetType());
                        if (typeBinding != g_assetData.m_assetTypeBindings.end())
                            ImGuiExt::Tooltip(typeBinding->second.name);
                    }

                    if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_Name))
                    {
                        const bool isSelected = std::find(s_selectedAssets.begin(), s_selectedAssets.end(), asset) != s_selectedAssets.end();
                        ImGui::SetNextItemSelectionUserData(rowNum);
                        if (ImGui::Selectable(asset->GetAssetName().c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                // if the double clicked asset is not in the list, add it
                                // this is a very strange case but the only way you can export multiple assets by double clicking is by
                                // holding shift or ctrl while double clicking, which may cause the hovered asset to be deselected before export
                                if (!isSelected)
                                    s_selectedAssets.insert(s_selectedAssets.end(), asset);

                                CThread(HandlePakAssetExportList, std::move(s_selectedAssets), g_ExportSettings.exportAssetDeps).detach();

                                s_selectedAssets.clear();
                            }
                        }
                    }

                    if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_GUID))
                        ImGui::Text("%016llX", asset->GetAssetGUID());

                    if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_File))
                        ImGui::TextUnformatted(asset->GetContainerFileName().c_str());

                    ImGui::PopID();
                }
            }
            ms_io = ImGui::EndMultiSelect();

            ApplySelectionRequests(ms_io, s_selectedAssets, pakAssets);

            ImGui::EndTable();
        }
    }
    ImGui::End();

    // This window must be executed before "Scene", as the scene relies on previewDrawData already being set from here
    if (ImGui::Begin("Asset Info", nullptr, ImGuiWindowFlags_MenuBar) && shouldPopulateAssetWindows)
    {
        CAsset* const firstAsset = s_selectedAssets.empty() ? nullptr : *s_selectedAssets.begin();
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::BeginMenu("Export"))
                {
                    // File > Export > Quick Export
                    // Option to "quickly" export the asset to the exported_files directory
                    // in the format defined by the "Export Options" menu.
                    if (ImGui::MenuItem("Quick Export"))
                        CThread(HandleExportBindingForAsset, std::move(firstAsset), g_ExportSettings.exportAssetDeps).detach();

                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        const std::string assetName = !firstAsset ? "(none)" : firstAsset->GetAssetName();
        const std::string assetGuidStr = !firstAsset ? "(none)" : std::format("{:X}", firstAsset->GetAssetGUID());

        ImGuiExt::ConstTextInputLeft("Asset Name", assetName.c_str(), 70);
        ImGuiExt::ConstTextInputLeft("Asset GUID", assetGuidStr.c_str(), 70);

        // Dependency information only exists for PAK assets
        if (firstAsset && firstAsset->GetAssetContainerType() == CAsset::ContainerType::PAK)
        {
            const PakAsset_t* const pakAsset = static_cast<CPakAsset*>(firstAsset)->data();

            ImGui::Text("Number of Dependencies: %hu", pakAsset->dependenciesCount);
            ImGuiExt::HelpMarker("This is the number of assets in the same .rpak file as this asset that are required by this asset");

            ImGui::Text("Number of Dependent Assets: %hu", pakAsset->dependentsCount);
            ImGuiExt::HelpMarker("This is the number of assets in the same .rpak file as this asset that use this asset");

            PreviewWnd_AssetDepsTbl(firstAsset);
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.f);

        ImGui::Separator();

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.f);
        ImGui::Dummy(ImVec2(0, 0));
        if (firstAsset)
        {
            const uint32_t type = firstAsset->GetAssetType();
            if (auto it = g_assetData.m_assetTypeBindings.find(type); it != g_assetData.m_assetTypeBindings.end())
            {
                if (it->second.previewFunc)
                {
                    // First frame is a special case, we wanna reset some settings for the preview function.
                    const bool firstFrameForAsset = firstAsset != s_prevRenderInfoAsset;

                    previewDrawData = reinterpret_cast<CDXDrawData*>(it->second.previewFunc(static_cast<CPakAsset*>(firstAsset), firstFrameForAsset));
                    s_prevRenderInfoAsset = firstAsset;

                }
                else ImGui::Text("Asset type '%s' does not currently support Asset Preview.", fourCCToString(type).c_str());
            }
            else ImGui::Text("Asset type '%s' is not currently supported.", fourCCToString(type).c_str());
        }
        else ImGui::TextUnformatted("No asset selected.");
    }
    ImGui::End();

    // The "scene" preview window must always be in the center.
    // Setting NoMove seems to be the best way to stop it from being undocked
    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoMove))
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();

        if (avail != s_previousAvailableSizeForPreview || !g_dxHandler->GetPreviewRTV())
        {
            g_dxHandler->CleanupForPreviewResize();
            g_dxHandler->CreateViewForSceneWindow(static_cast<uint16_t>(avail.x), static_cast<uint16_t>(avail.y));
        }

        s_previousAvailableSizeForPreview = avail;

        // Preview rendering
        ID3D11Device* const device = g_dxHandler->GetDevice();
        CDXScene& scene = g_dxHandler->GetScene();
        ID3D11DeviceContext* const ctx = g_dxHandler->GetDeviceContext();

        // TODO: UPDATE THIS FOR PREVIEW SHIT.
        ID3D11RenderTargetView* const previewRTV = g_dxHandler->GetPreviewRTV();
        static constexpr float clear_color_with_alpha[4] = { 0.01f, 0.01f, 0.01f, 1.00f };

        ctx->OMSetRenderTargets(1, &previewRTV, g_dxHandler->GetPreviewDSV());
        ctx->ClearRenderTargetView(previewRTV, clear_color_with_alpha);
        ctx->ClearDepthStencilView(g_dxHandler->GetPreviewDSV(), D3D11_CLEAR_DEPTH, 1, 0);

        const D3D11_VIEWPORT vp = {
            0, 0,
            static_cast<float>(avail.x),
            static_cast<float>(avail.y),
            0, 1
        };

        ctx->RSSetViewports(1u, &vp);
        ctx->RSSetState(g_dxHandler->GetRasterizerState());
        ctx->OMSetDepthStencilState(g_dxHandler->GetDepthStencilState(), 1u);

#if defined(ADVANCED_MODEL_PREVIEW)
        // Update CBufCommonPerCamera
        g_dxHandler->GetCamera()->CommitCameraDataBufferUpdates();

        scene.UpdateHardwareLights();
        scene.UpdateCubemapSamples();

        if (scene.NeedsLightingUpdate())
            scene.MapAndUpdateLightBuffer(device, ctx);

        if (scene.NeedsCubemapSmpUpdate())
            scene.MapAndUpdateCubemapSamplesBuffer(device, ctx);
#else
        UNUSED(device);
#endif

        if (previewDrawData)
        {
            previewDrawData->SetPSResource(PSRSRC_CUBEMAP, g_dxHandler->GetCubemapSRV());
            previewDrawData->SetPSResource(PSRSRC_CSMDEPTHATLASSAMPLER, g_dxHandler->GetCSMDepthAtlasSamplerSRV());
            previewDrawData->SetPSResource(PSRSRC_SHADOWMAP, g_dxHandler->GetShadowMapSRV());
            previewDrawData->SetPSResource(PSRSRC_CLOUDMASK, g_dxHandler->GetCloudMaskSRV());
            previewDrawData->SetPSResource(PSRSRC_STATICSHADOWTEXTURE, g_dxHandler->GetStaticShadowTexSRV());

            CDXCamera* const camera = g_dxHandler->GetCamera();

            if (previewDrawData->vertexShader && previewDrawData->pixelShader) LIKELY
            {
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                assertm(previewDrawData->transformsBuffer, "uh oh something very bad happened!!!!!!");

                ctx->VSSetConstantBuffers(0u, 1u, &previewDrawData->transformsBuffer); // VS_TransformConstants/CBufModelInstance

                UINT offset = 0u;

                for (size_t i = 0; i < previewDrawData->meshBuffers.size(); ++i)
                {
                    const DXMeshDrawData_t& meshDrawData = previewDrawData->meshBuffers[i];

                    if (!meshDrawData.visible || !meshDrawData.vertexShader || !meshDrawData.pixelShader)
                        continue;

                    ctx->RSSetState(meshDrawData.wireframe ? g_dxHandler->GetRasterizerStateWireFrame() : g_dxHandler->GetRasterizerState());

                    assertm(meshDrawData.vertexShader != nullptr, "No vertex shader?");
                    assertm(meshDrawData.pixelShader != nullptr, "No pixel shader?");

                    ctx->IASetInputLayout(meshDrawData.inputLayout);
                    ctx->VSSetShader(meshDrawData.vertexShader, nullptr, 0u);

                    ID3D11Buffer* sharedConstBuffers[] = {
                        camera->bufCommonPerCamera,           // CBufCommonPerCamera - b2
                        previewDrawData->modelInstanceBuffer, // CBufModelInstance   - b3
                    };

                    for (auto& rsrc : previewDrawData->vertexShaderResources)
                    {
                        ctx->VSSetShaderResources(rsrc.first, 1u, &rsrc.second);
                    }

                    // [AMP]
                    if (meshDrawData.hasGameShaders)
                    {
                        // VertexShader: CBufCommonPerCamera, CBufModelInstance
                        ctx->VSSetConstantBuffers(2u, ARRSIZE(sharedConstBuffers), sharedConstBuffers);

                        // VertexShader: g_boneMatrix, g_boneMatrixPrevFrame
                        ctx->VSSetShaderResources(VSRSRC_BONE_MATRIX, 1u, &previewDrawData->boneMatrixSRV);
                        ctx->VSSetShaderResources(VSRSRC_BONE_MATRIX_PREV_FRAME, 1u, &previewDrawData->boneMatrixSRV);
                    }

                    ctx->IASetVertexBuffers(0u, 1u, &meshDrawData.vertexBuffer, &meshDrawData.vertexStride, &offset);
                    // ==============================================================================

                    assertm(meshDrawData.pixelShader != nullptr, "No pixel shader?");

                    ctx->PSSetShader(meshDrawData.pixelShader, nullptr, 0u);

                    ID3D11SamplerState* const samplerState = g_dxHandler->GetSamplerState();

                    // [AMP] Samplers, Lights, CBufs
                    if (meshDrawData.hasGameShaders)
                    {
                        ID3D11SamplerState* samplers[] = {
                            g_dxHandler->GetSamplerComparisonState(),
                            samplerState,
                            samplerState,
                        };
                        ctx->PSSetSamplers(0, ARRSIZE(samplers), samplers);

                        if (meshDrawData.uberStaticBuf)
                            ctx->PSSetConstantBuffers(0u, 1u, &meshDrawData.uberStaticBuf);

                        if (meshDrawData.uberDynamicBuf)
                            ctx->PSSetConstantBuffers(1u, 1u, &meshDrawData.uberDynamicBuf);

                        // PixelShader: CBufCommonPerCamera, CBufModelInstance
                        ctx->PSSetConstantBuffers(2u, ARRSIZE(sharedConstBuffers), sharedConstBuffers);

                        // PixelShader: s_globalLights
                        ctx->PSSetShaderResources(PSRSRC_GLOBAL_LIGHTS, 1u, &scene.globalLightsSRV);
                        ctx->PSSetShaderResources(PSRSRC_CUBEMAP_SAMPLES, 1u, &scene.cubemapSamplesSRV);
                    }
                    else
                        ctx->PSSetSamplers(0, 1, &samplerState);

                    // Bind texture resources for this mesh's material
                    for (auto& tex : meshDrawData.textures)
                    {
                        ID3D11ShaderResourceView* const textureSRV = tex.texture
                            ? tex.texture.get()->GetSRV()
                            : nullptr;

                        ctx->PSSetShaderResources(tex.resourceBindPoint, 1u, &textureSRV);
                    }

                    // Bind pixel shader resources
                    for (auto& rsrc : previewDrawData->pixelShaderResources)
                    {
                        ctx->PSSetShaderResources(rsrc.first, 1u, &rsrc.second);
                    }

                    // ==============================================================================
                    ctx->IASetIndexBuffer(meshDrawData.indexBuffer, meshDrawData.indexFormat, 0u);
                    ctx->DrawIndexed(static_cast<UINT>(meshDrawData.numIndices), 0u, 0u);
                }
            }
            else assertm(0, "Failed to load shaders for model preview.");

            if (g_dxHandler->GetPreviewFrameBuffer())
            {
                ImGui::Image(g_dxHandler->GetPreviewFrameBufferSRV(), avail);
            }
        }
    }
    ImGui::End();

    if (uiState.settingsWindowVisible)
        SettingsWnd_Draw(&uiState);

#if defined(HAS_ITEMFLAV_WINDOW)
    if (uiState.itemflavWindowVisible)
        ItemflavWnd_Draw(&uiState);
#endif

    if (uiState.logWindowVisible)
        LogWnd_Draw(&uiState);

    g_pImGuiHandler->HandleProgressBar();

    ImGui::Render();

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // Preview rendering
    ID3D11Device* const device = g_dxHandler->GetDevice();
    CDXScene& scene = g_dxHandler->GetScene();
    ID3D11DeviceContext* const ctx = g_dxHandler->GetDeviceContext();

#if !defined(ADVANCED_MODEL_PREVIEW)
    UNUSED(device);
    UNUSED(scene);
#endif


    

    prevAssetCount = g_assetData.v_assets.size();

    ID3D11RenderTargetView* const mainView = g_dxHandler->GetMainView();
    static constexpr float clear_color_with_alpha[4] = { 0.01f, 0.01f, 0.01f, 1.00f };

    ctx->OMSetRenderTargets(1, &mainView, g_dxHandler->GetDepthStencilView());
    ctx->ClearRenderTargetView(mainView, clear_color_with_alpha);
    ctx->ClearDepthStencilView(g_dxHandler->GetDepthStencilView(), D3D11_CLEAR_DEPTH, 1, 0);

    LONG width = 0ul;
    LONG height = 0ul;
    g_dxHandler->GetWindowSize(&width, &height);

    const D3D11_VIEWPORT vp = {
        0, 0,
        static_cast<float>(width),
        static_cast<float>(height),
        0, 1
    };

    ctx->RSSetViewports(1u, &vp);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_dxHandler->GetSwapChain()->Present(1u, 0u);
}
