#include <pch.h>

#include <core/render.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <misc/imgui_utility.h>
#include <game/rtech/utils/bsp/bspflags.h>
#include <cctype>

#include <core/render/dx.h>
#include <core/window.h>
#include <core/input/input.h>

#include <core/filehandling/export.h>

#include <core/discord_presence.h>

#include <game/rtech/cpakfile.h>
#include <game/rtech/assets/model.h>
#include <game/rtech/assets/texture.h>

extern CDXParentHandler* g_dxHandler;
extern std::atomic<uint32_t> maxConcurrentThreads;

ExportSettings_t g_ExportSettings{ .exportNormalRecalcSetting = eNormalExportRecalc::NML_RECALC_NONE, .exportTextureNameSetting = eTextureExportName::TXTR_NAME_TEXT, .exportMaterialTextures = true,
    .exportPathsFull = false, .exportAssetDeps = false, .disableCachedNames = false, .previewedSkinIndex = 0, .qcMajorVersion = 49, .qcMinorVersion = 0, .exportRigSequences = true, .exportModelSkin = false, .exportModelMatsTruncated = false, .exportQCIFiles = false, .exportPhysicsContentsFilter = static_cast<uint32_t>(TRACE_MASK_ALL) };
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

struct AsyncAssetFilterState
{
    std::mutex mutex;
    std::vector<CGlobalAssetData::AssetLookup_t> pendingResults;
    std::string pendingFilterText;
    uint64_t pendingJobId = 0;
    bool hasPendingResults = false;
    std::atomic<uint64_t> lastRequestedJobId{0};
    std::atomic<int32_t> runningJobs{0};
    std::atomic<uint32_t> activeJobId{0};
    std::atomic<bool> progressBarVisible{false};
    std::atomic<uint32_t> progressProcessed{0};
    std::atomic<uint32_t> progressTotal{0};
    const ProgressBarEvent_t* progressEvent = nullptr;
};

static AsyncAssetFilterState g_AsyncAssetFilterState;

static std::string TrimFilterToken(const std::string& token)
{
    const char* const whitespace = " \t\r\n";
    const size_t start = token.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return {};
    const size_t end = token.find_last_not_of(whitespace);
    return token.substr(start, end - start + 1);
}

static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;

    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b)
        {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });

    return it != haystack.end();
}

static void ParseFilterTokens(const std::string& filterText, std::vector<std::string>& includeTerms, std::vector<std::string>& excludeTerms)
{
    includeTerms.clear();
    excludeTerms.clear();

    size_t start = 0;
    while (start < filterText.length())
    {
        const size_t end = filterText.find(',', start);
        const size_t len = end == std::string::npos ? std::string::npos : end - start;
        std::string token = filterText.substr(start, len);
        token = TrimFilterToken(token);

        if (!token.empty())
        {
            if (token.front() == '-')
            {
                std::string term = TrimFilterToken(token.substr(1));
                if (!term.empty())
                    excludeTerms.emplace_back(std::move(term));
            }
            else
            {
                includeTerms.emplace_back(std::move(token));
            }
        }

        if (end == std::string::npos)
            break;
        start = end + 1;
    }
}

static std::vector<CGlobalAssetData::AssetLookup_t> BuildFilteredAssetListSnapshot(const std::string& filterText, std::atomic<uint32_t>* progressCounter, uint32_t progressTotal)
{
    std::vector<CGlobalAssetData::AssetLookup_t> matches;

    if (filterText.empty())
    {
        if (progressCounter)
            progressCounter->store(progressTotal, std::memory_order_relaxed);
        return matches;
    }

    std::vector<std::string> includeTerms;
    std::vector<std::string> excludeTerms;
    ParseFilterTokens(filterText, includeTerms, excludeTerms);

    const bool filterActive = !includeTerms.empty() || !excludeTerms.empty();

    char* end = nullptr;
    const uint64_t parsedGuid = strtoull(filterText.c_str(), &end, 0);
    const bool filterIsGuid = end == (filterText.c_str() + filterText.length());

    if (!filterActive && !filterIsGuid)
    {
        if (progressCounter)
            progressCounter->store(progressTotal, std::memory_order_relaxed);
        return matches;
    }

    matches.reserve(std::min<size_t>(g_assetData.v_assets.size(), 4096));

    for (const auto& lookup : g_assetData.v_assets)
    {
        const std::string& assetName = lookup.m_asset->GetAssetName();
        bool added = false;

        if (filterActive)
        {
            bool includeMatched = includeTerms.empty();
            if (!includeTerms.empty())
            {
                for (const std::string& term : includeTerms)
                {
                    if (ContainsCaseInsensitive(assetName, term))
                    {
                        includeMatched = true;
                        break;
                    }
                }
            }

            bool excluded = false;
            if (includeMatched)
            {
                for (const std::string& term : excludeTerms)
                {
                    if (ContainsCaseInsensitive(assetName, term))
                    {
                        excluded = true;
                        break;
                    }
                }
            }

            if (includeMatched && !excluded)
            {
                matches.push_back(lookup);
                added = true;
            }
        }

        if (!added && filterIsGuid && parsedGuid == RTech::StringToGuid(assetName.c_str()))
            matches.push_back(lookup);

        if (progressCounter)
            progressCounter->fetch_add(1, std::memory_order_relaxed);
    }

    matches.shrink_to_fit();
    if (progressCounter)
        progressCounter->store(progressTotal, std::memory_order_relaxed);
    return matches;
}

static void SubmitAsyncAssetFilterJob(const std::string& filterText)
{
    if (filterText.empty())
        return;

    const uint64_t jobId = g_AsyncAssetFilterState.lastRequestedJobId.fetch_add(1) + 1;
    g_AsyncAssetFilterState.runningJobs.fetch_add(1);
    g_AsyncAssetFilterState.activeJobId.store(static_cast<uint32_t>(jobId));
    const uint32_t assetCount = static_cast<uint32_t>(g_assetData.v_assets.size());
    g_AsyncAssetFilterState.progressTotal.store(assetCount, std::memory_order_relaxed);
    g_AsyncAssetFilterState.progressProcessed.store(0, std::memory_order_relaxed);

    constexpr size_t kSearchProgressThreshold = 256;
    const bool shouldShowProgressBar = filterText.length() > kSearchProgressThreshold;

    if (shouldShowProgressBar && !g_AsyncAssetFilterState.progressBarVisible.exchange(true))
    {
        // use inverted mode so the handler treats our processed counter as the displayed value (fills left-to-right)
        g_AsyncAssetFilterState.progressEvent = g_pImGuiHandler->AddProgressBarEvent("Searching assets", assetCount, &g_AsyncAssetFilterState.progressProcessed, true);

        if (!g_AsyncAssetFilterState.progressEvent)
            g_AsyncAssetFilterState.progressBarVisible.store(false);
    }

    CThread([filterText, jobId]()
    {
        auto results = BuildFilteredAssetListSnapshot(filterText, &g_AsyncAssetFilterState.progressProcessed, g_AsyncAssetFilterState.progressTotal.load());

        if (jobId == g_AsyncAssetFilterState.lastRequestedJobId.load())
        {
            std::scoped_lock lock(g_AsyncAssetFilterState.mutex);
            g_AsyncAssetFilterState.pendingResults = std::move(results);
            g_AsyncAssetFilterState.pendingFilterText = filterText;
            g_AsyncAssetFilterState.pendingJobId = jobId;
            g_AsyncAssetFilterState.hasPendingResults = true;
        }

        g_AsyncAssetFilterState.progressProcessed.store(g_AsyncAssetFilterState.progressTotal.load());

        if (g_AsyncAssetFilterState.progressEvent)
        {
            g_pImGuiHandler->FinishProgressBarEvent(g_AsyncAssetFilterState.progressEvent);
            g_AsyncAssetFilterState.progressEvent = nullptr;
            g_AsyncAssetFilterState.progressBarVisible.store(false);
        }

        g_AsyncAssetFilterState.runningJobs.fetch_sub(1);
    }).detach();
}

static bool ConsumeCompletedAssetFilterResults(std::vector<CGlobalAssetData::AssetLookup_t>& filteredAssets, uint64_t& lastAppliedJobId, std::string& lastAppliedFilterText)
{
    std::scoped_lock lock(g_AsyncAssetFilterState.mutex);
    if (!g_AsyncAssetFilterState.hasPendingResults)
        return false;

    filteredAssets = g_AsyncAssetFilterState.pendingResults;
    filteredAssets.shrink_to_fit();
    lastAppliedJobId = g_AsyncAssetFilterState.pendingJobId;
    lastAppliedFilterText = g_AsyncAssetFilterState.pendingFilterText;
    g_AsyncAssetFilterState.hasPendingResults = false;

    g_AsyncAssetFilterState.progressProcessed.store(g_AsyncAssetFilterState.progressTotal.load());
    if (g_AsyncAssetFilterState.progressEvent)
    {
        g_pImGuiHandler->FinishProgressBarEvent(g_AsyncAssetFilterState.progressEvent);
        g_AsyncAssetFilterState.progressEvent = nullptr;
        g_AsyncAssetFilterState.progressBarVisible.store(false);
    }

    return true;
}


// TODO: Add shortcut handling for copying asset names with CTRL+C when assets are selected and asset list is focused.
//void HandleImGuiShortcuts()
//{
//
//}

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

void CreatePakAssetDependenciesTable(CAsset* asset)
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

                if (ImGui::TableSetColumnIndex(0))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                        ColouredTextForAssetType(depAsset);
                    else
                        ImGui::TextUnformatted("n/a");
                }

                if (ImGui::TableSetColumnIndex(1))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                        ImGui::TextUnformatted(depAsset->GetAssetName().c_str());
                    else
                        ImGui::Text("%016llX", guid.guid);
                }

                if (ImGui::TableSetColumnIndex(2))
                {
                    ImGui::AlignTextToFramePadding();
                    if (!depAsset)
                        ImGui::TextUnformatted("n/a");
                    else
                        ImGui::TextUnformatted(depAsset->GetContainerFileName().c_str());
                }

                if (ImGui::TableSetColumnIndex(3))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                    {
                        if (depAsset->GetExportedStatus())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 1.f, 1.f));
                            ImGui::TextUnformatted("Exported");
                            ImGui::PopStyleColor();
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                            ImGui::TextUnformatted("Loaded");
                            ImGui::PopStyleColor();
                        }
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                        ImGui::TextUnformatted("Not Loaded");
                        ImGui::PopStyleColor();
                    }
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

void CreatePakAssetDependentsTable(CAsset* asset)
{
    CPakAsset* pakAsset = static_cast<CPakAsset*>(asset);

    std::vector<AssetGuid_t> dependents;
    pakAsset->getDependents(dependents);

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBody
        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

    const ImVec2 outerSize = ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 12.f);

    constexpr int numColumns = 5;

    if (ImGui::TreeNodeEx("Asset Dependents", ImGuiTreeNodeFlags_SpanAvailWidth))
    {
        if (dependents.empty())
        {
            ImGui::TextUnformatted("No dependents found.");
        }
        else if (ImGui::BeginTable("AssetDependents", numColumns, tableFlags, outerSize))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 0);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 1);
            ImGui::TableSetupColumn("Pak", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 2);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 3);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.f, 4);
            ImGui::TableSetupScrollFreeze(1, 1);

            ImGui::TableHeadersRow();

            for (int d = 0; d < dependents.size(); ++d)
            {
                CPakAsset* depAsset = g_assetData.FindAssetByGUID<CPakAsset>(dependents[d].guid);

                ImGui::PushID(d);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 0.f);

                if (ImGui::TableSetColumnIndex(0))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                        ColouredTextForAssetType(depAsset);
                    else
                        ImGui::TextUnformatted("n/a");
                }

                if (ImGui::TableSetColumnIndex(1))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                        ImGui::TextUnformatted(depAsset->GetAssetName().c_str());
                    else
                        ImGui::Text("%016llX", dependents[d].guid);
                }

                if (ImGui::TableSetColumnIndex(2))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                        ImGui::TextUnformatted(depAsset->GetContainerFileName().c_str());
                    else
                        ImGui::TextUnformatted("n/a");
                }

                if (ImGui::TableSetColumnIndex(3))
                {
                    ImGui::AlignTextToFramePadding();
                    if (depAsset)
                    {
                        if (depAsset->GetExportedStatus())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 1.f, 1.f));
                            ImGui::TextUnformatted("Exported");
                            ImGui::PopStyleColor();
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                            ImGui::TextUnformatted("Loaded");
                            ImGui::PopStyleColor();
                        }
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                        ImGui::TextUnformatted("Unavailable");
                        ImGui::PopStyleColor();
                    }
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

void HandleRenderFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // create a docking area across the entire viewport
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode, 0);
    }

    // while ImGui is using keyboard input, we should not accept any keyboard input, but we should also clear all  
    // existing key states, as otherwise we can end up moving forever (until ImGui loses focus) in model preview if
    // the movement keys are held when ImGui starts capturing keyboard
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        g_pInput->ClearKeyStates();

        //HandleImGuiShortcuts();
    }

    if (g_pInput->mouseCaptured)
        g_pInput->Frame(ImGui::GetIO().DeltaTime);

    g_dxHandler->GetCamera()->Move(ImGui::GetIO().DeltaTime);

    CUIState& uiState = g_dxHandler->GetUIState();

    //ImGui::ShowDemoWindow();

    static std::deque<CAsset*> selectedAssets;
    static std::vector<CGlobalAssetData::AssetLookup_t> filteredAssets;
    static CAsset* prevRenderInfoAsset = nullptr;
    static uint64_t lastAppliedFilterJobId = 0;
    static std::string lastSubmittedFilterText;
    static std::string lastAppliedFilterText;
    static size_t lastSubmittedAssetCountSnapshot = 0;

    CDXDrawData* previewDrawData = nullptr;
    if (ImGui::BeginMainMenuBar())
    {

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open"))
            {
                if (!inJobAction)
                {
                    // Reset selected asset to avoid crash.
                    selectedAssets.clear();
                    filteredAssets.clear();
                    prevRenderInfoAsset = nullptr;
                    g_assetData.ClearAssetData();

                    // We kinda leak the thread here but it's okay, we want it to keep executing.
                    CThread(HandleOpenFileDialog, g_dxHandler->GetWindowHandle()).detach();
                }
            }

            if (ImGui::MenuItem("Unload Files"))
            {
                if (!inJobAction)
                {
                    selectedAssets.clear();
                    filteredAssets.clear();
                    prevRenderInfoAsset = nullptr;
                    g_assetData.ClearAssetData();
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            //if (ImGui::MenuItem("Copy Asset Names", "CTRL+C"))
            //{
            // 
            //}

            if (ImGui::MenuItem("Settings"))
                uiState.ShowSettingsWindow(true);

            ImGui::EndMenu();
        }

#if _DEBUG
        IMGUI_RIGHT_ALIGN_FOR_TEXT("Avg 1.000 ms/frame (100.0 FPS)"); // [rexx]: i hate this actually

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Avg %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
#endif
        ImGui::EndMainMenuBar();
    }

    // [rexx]: yes, these branches could be structured a bit better
    //         but otherwise we get very deep indentation which looks ugly
    if (!inJobAction && g_assetData.v_assetContainers.empty())
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);

        if (ImGui::Begin("reSource Xtractor", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(RSX_WELCOME_MESSAGE);
            ImGui::PopTextWrapPos();

            ImGui::Dummy(ImVec2(0.0f, 5.0f));

            if (ImGui::Button("Open File..."))
            {
                // Reset selected asset to avoid crash.
                selectedAssets.clear();
                filteredAssets.clear();
                prevRenderInfoAsset = nullptr;
                g_assetData.ClearAssetData();

                // We kinda leak the thread here but it's okay, we want it to keep executing.
                CThread(HandleOpenFileDialog, g_dxHandler->GetWindowHandle()).detach();
            }

            ImGui::End();
        }
    }


    // Only render window if we have a pak loaded and if we aren't currently in pakload.
    if (!inJobAction && !g_assetData.v_assetContainers.empty())
    {
        ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Asset List", nullptr, ImGuiWindowFlags_MenuBar))
        {
            std::vector<CGlobalAssetData::AssetLookup_t>& pakAssets = FilterConfig->textFilter.IsActive() ? filteredAssets : g_assetData.v_assets;

            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("Export"))
                {
                    const bool multipleAssetsSelected = selectedAssets.size() > 1;

                    if (ImGui::Selectable("Export selected"))
                    {
                        if (!selectedAssets.empty())
                        {
                            std::deque<CAsset*> cpyAssets;
                            cpyAssets.insert(cpyAssets.end(), selectedAssets.begin(), selectedAssets.end());
                            CThread(HandlePakAssetExportList, std::move(cpyAssets), g_ExportSettings.exportAssetDeps).detach();
                            selectedAssets.clear();
                        }
                    }

                    // Option is only valid if one asset is selected
                    if (ImGui::Selectable("Export all for selected type", false, multipleAssetsSelected ? ImGuiSelectableFlags_Disabled : 0))
                    {
                        if (selectedAssets.size() == 1)
                        {
                            const uint32_t desiredType = selectedAssets[0]->GetAssetType();
                            auto allAssetsOfDesiredType = pakAssets | std::ranges::views::filter([desiredType](const CGlobalAssetData::AssetLookup_t& a)
                                {
                                    return a.m_asset->GetAssetType() == desiredType;
                                });

                            std::vector<CGlobalAssetData::AssetLookup_t> allAssets(allAssetsOfDesiredType.begin(), allAssetsOfDesiredType.end());
                            CThread(HandleExportSelectedAssetType, std::move(allAssets), g_ExportSettings.exportAssetDeps).detach();
                            selectedAssets.clear();
                        }
                    }

                    // Option is only valid if one asset is selected
                    // TODO: rename to "for selected file" to allow for all audio assets to be exported?
                    if (ImGui::Selectable("Export all for selected pak", false, multipleAssetsSelected ? ImGuiSelectableFlags_Disabled : 0))
                    {
                        if (selectedAssets.size() == 1 && selectedAssets[0]->GetAssetContainerType() == CAsset::ContainerType::PAK)
                        {
                            const CPakFile* desiredPak = selectedAssets[0]->GetContainerFile<const CPakFile>();
                            auto allAssetsOfDesiredType = pakAssets | std::ranges::views::filter([desiredPak](const CGlobalAssetData::AssetLookup_t& a)
                                {
                                    return a.m_asset->GetAssetContainerType() == CAsset::ContainerType::PAK && a.m_asset->GetContainerFile<const CPakFile>() == desiredPak;
                                });

                            std::vector<CGlobalAssetData::AssetLookup_t> allAssets(allAssetsOfDesiredType.begin(), allAssetsOfDesiredType.end());
                            CThread(HandleExportSelectedAssetType, std::move(allAssets), g_ExportSettings.exportAssetDeps).detach();
                            selectedAssets.clear();
                        }
                    }

                    // Option is only valid if one asset is selected
                    // TODO: rename to "for selected file" to allow for all audio assets to be exported?
                    if (ImGui::Selectable("Export all for selected pak and type", false, multipleAssetsSelected ? ImGuiSelectableFlags_Disabled : 0))
                    {
                        if (selectedAssets.size() == 1 && selectedAssets[0]->GetAssetContainerType() == CAsset::ContainerType::PAK)
                        {
                            const CPakFile* desiredPak = selectedAssets[0]->GetContainerFile<const CPakFile>();
                            const uint32_t desiredType = selectedAssets[0]->GetAssetType();
                            auto allAssetsOfDesiredType = pakAssets | std::ranges::views::filter([desiredPak, desiredType](const CGlobalAssetData::AssetLookup_t& a)
                                {
                                    return a.m_asset->GetAssetContainerType() == CAsset::ContainerType::PAK
                                        && a.m_asset->GetContainerFile<CPakFile>() == desiredPak
                                        && a.m_asset->GetAssetType() == desiredType;
                                });

                            std::vector<CGlobalAssetData::AssetLookup_t> allAssets(allAssetsOfDesiredType.begin(), allAssetsOfDesiredType.end());
                            CThread(HandleExportSelectedAssetType, std::move(allAssets), g_ExportSettings.exportAssetDeps).detach();
                            selectedAssets.clear();
                        }
                    }

                    if (ImGui::Selectable("Export all"))
                    {
                        CThread(HandleExportAllPakAssets, &pakAssets, g_ExportSettings.exportAssetDeps).detach();
                    }

                    // Exports the names of all assets in the currently shown filtered asset list (i.e., search results)
                    if (ImGui::Selectable("Export list of assets"))
                    {
                        CThread(HandleListExportPakAssets, g_dxHandler->GetWindowHandle(), &pakAssets).detach();
                    }

                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            const bool filterInputChanged = FilterConfig->textFilter.Draw("##Filter", -1.f);
            const bool appliedFilterResults = ConsumeCompletedAssetFilterResults(filteredAssets, lastAppliedFilterJobId, lastAppliedFilterText);
            if (appliedFilterResults)
            {
                lastSubmittedAssetCountSnapshot = g_assetData.v_assets.size();
                lastSubmittedFilterText = lastAppliedFilterText;
            }

            const bool filterActive = FilterConfig->textFilter.IsActive();
            const std::string currentFilterText = FilterConfig->textFilter.InputBuf;

            const bool awaitingResults = !currentFilterText.empty() && currentFilterText == lastSubmittedFilterText && currentFilterText != lastAppliedFilterText;

            if (filterActive)
            {
                const bool filterTextChanged = filterInputChanged && currentFilterText != lastSubmittedFilterText;
                const bool filterNeverSubmitted = !currentFilterText.empty() && lastSubmittedFilterText != currentFilterText && lastAppliedFilterText != currentFilterText;
                const bool assetsChangedSinceSubmission = !currentFilterText.empty() && currentFilterText == lastAppliedFilterText && g_assetData.v_assets.size() != lastSubmittedAssetCountSnapshot;
                const bool resultsMissing = awaitingResults && g_AsyncAssetFilterState.runningJobs.load() == 0;

                if ((filterTextChanged || filterNeverSubmitted || assetsChangedSinceSubmission || resultsMissing) && !currentFilterText.empty())
                {
                    lastSubmittedFilterText = currentFilterText;
                    lastSubmittedAssetCountSnapshot = g_assetData.v_assets.size();
                    SubmitAsyncAssetFilterJob(currentFilterText);
                }

                if (awaitingResults || g_AsyncAssetFilterState.runningJobs.load() > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Searching...");
                }
            }
            else
            {
                filteredAssets.clear();
                lastSubmittedFilterText.clear();
                lastAppliedFilterText.clear();
                lastAppliedFilterJobId = 0;
                lastSubmittedAssetCountSnapshot = 0;
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
                ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(flags, static_cast<int>(selectedAssets.size()), static_cast<int>(pakAssets.size()));

                ApplySelectionRequests(ms_io, selectedAssets, pakAssets);

                // arbitrary large number that will never happen
                static unsigned int lastSelectionSize = UINT32_MAX;

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(pakAssets.size()));
                while (clipper.Step())
                {
                    for (int rowNum = clipper.DisplayStart; rowNum < clipper.DisplayEnd; rowNum++)
                    {
                        CAsset* const asset = pakAssets[rowNum].m_asset;

                        // temp just to deal with compile issue while the class is modified

                        // previously this was GUID_pakCRC but realistically the pak filename also works instead of the crc (though it may be slower for lookup?)
                        ImGui::PushID(std::format("{:X}_{}", asset->GetAssetGUID(), asset->GetContainerFileName()).c_str());

                        ImGui::TableNextRow();

                        if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_Type))
                        {
                            ColouredTextForAssetType(asset);
                        }

                        if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_Name))
                        {
                            const bool isSelected = std::find(selectedAssets.begin(), selectedAssets.end(), asset) != selectedAssets.end();
                            ImGui::SetNextItemSelectionUserData(rowNum);
                            if (ImGui::Selectable(asset->GetAssetName().c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                            {
                                // Update Discord presence to show which asset was clicked and its pak (if enabled)
                                if (UtilsConfig->discordPresenceEnabled)
                                    DiscordGamePresence::UpdatePresence(asset->GetAssetName(), asset->GetContainerFileName());

                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                {
                                    // if the double clicked asset is not in the list, add it
                                    // this is a very strange case but the only way you can export multiple assets by double clicking is by
                                    // holding shift or ctrl while double clicking, which may cause the hovered asset to be deselected before export
                                    if (!isSelected)
                                        selectedAssets.insert(selectedAssets.end(), asset);

                                    CThread(HandlePakAssetExportList, std::move(selectedAssets), g_ExportSettings.exportAssetDeps).detach();
                                
                                    selectedAssets.clear();
                                }
                            }
                        }

                        if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_GUID))
                            ImGui::Text("%016llX", asset->GetAssetGUID());

                        if (ImGui::TableSetColumnIndex(AssetColumn_t::AC_File))
                        {
                            if (asset->IsPatched())
                                ImGui::Text("%s (Patched)", asset->GetContainerFileName().c_str());
                            else
                                ImGui::TextUnformatted(asset->GetContainerFileName().c_str());
                        }

                        ImGui::PopID();
                    }
                }
                ms_io = ImGui::EndMultiSelect();

                ApplySelectionRequests(ms_io, selectedAssets, pakAssets);

                ImGui::EndTable();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        CAsset* const firstAsset = selectedAssets.empty() ? nullptr : *selectedAssets.begin();
        
        if (ImGui::Begin("Asset Info", nullptr, ImGuiWindowFlags_MenuBar))
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::BeginMenu("Export"))
                    {
                        if (ImGui::MenuItem("Quick Export"))
                        {
                            // Option to "quickly" export the asset to the exported_files directory
                            // in the format defined by the "Export Options" menu.

                            CThread(HandleExportBindingForAsset, std::move(firstAsset), g_ExportSettings.exportAssetDeps).detach();
                        }

                        if (ImGui::MenuItem("Export as..."))
                        {
                            // Option to export the selected asset as a different format to the format
                            // selected in "Export Options" to a different location to usual.
                        }
                        ImGui::EndMenu();
                    }
                    //ShowExampleMenuFile();
                    ImGui::EndMenu();
                }
                //if (ImGui::BeginMenu("Edit"))
                //{
                //    if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
                //    if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled item
                //    ImGui::Separator();
                //    if (ImGui::MenuItem("Cut", "CTRL+X")) {}
                //    if (ImGui::MenuItem("Copy", "CTRL+C")) {}
                //    if (ImGui::MenuItem("Paste", "CTRL+V")) {}
                //    ImGui::EndMenu();
                //}
                ImGui::EndMenuBar();
            }

            const std::string assetName = !firstAsset ? "(none)" : firstAsset->GetAssetName(); // std::format("{} ({:X})", , firstAsset->data()->guid);
            const std::string assetGuidStr = !firstAsset ? "(none)" : std::format("{:X}", firstAsset->GetAssetGUID());

            ImGuiConstTextInputLeft("Asset Name", assetName.c_str(), 70);
            ImGuiConstTextInputLeft("Asset GUID", assetGuidStr.c_str(), 70);

            if (firstAsset && firstAsset->GetAssetContainerType() == CAsset::ContainerType::PAK)
            {
                const PakAsset_t* const pakAsset = static_cast<CPakAsset*>(firstAsset)->data();

                ImGui::Text("remainingDependencyCount: %hu", pakAsset->remainingDependencyCount);
                ImGui::Text("dependenciesCount: %hu", pakAsset->dependenciesCount);

                ImGui::Text("dependenciesIndex: %u", pakAsset->dependenciesIndex);
                ImGui::Text("dependentsIndex: %u", pakAsset->dependentsIndex);

                CreatePakAssetDependenciesTable(firstAsset);
                CreatePakAssetDependentsTable(firstAsset);
            }

            ImGui::Dummy(ImVec2(0.0f, 5.0f));

            ImGui::Separator();

            ImGui::Dummy(ImVec2(0.0f, 5.0f));

            if (firstAsset)
            {

                const uint32_t type = firstAsset->GetAssetType();
                if (auto it = g_assetData.m_assetTypeBindings.find(type); it != g_assetData.m_assetTypeBindings.end())
                {
                    if (it->second.previewFunc)
                    {
                        // First frame is a special case, we wanna reset some settings for the preview function.
                        const bool firstFrameForAsset = firstAsset != prevRenderInfoAsset;

                        previewDrawData = reinterpret_cast<CDXDrawData*>(it->second.previewFunc(static_cast<CPakAsset*>(firstAsset), firstFrameForAsset));
                        prevRenderInfoAsset = firstAsset;
                    }
                    else
                    {
                        ImGui::Text("Asset type '%s' does not currently support Asset Preview.", fourCCToString(type).c_str());
                    }
                }
                else
                {
                    ImGui::Text("Asset type '%s' is not currently supported.", fourCCToString(type).c_str());
                }
            }
            else
            {
                ImGui::TextUnformatted("No asset selected.");
            }
        }

        ImGui::End();
    }

    constexpr uint32_t minThreads = 1u;

    if (uiState.settingsWindowVisible)
    {
        ImGui::SetNextWindowSize(ImVec2(0.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("Settings", &uiState.settingsWindowVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
        {
            // ===============================================================================================================
            ImGui::SeparatorText("Export");

            ImGui::Checkbox("Export full asset paths", &g_ExportSettings.exportPathsFull);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Enables exporting of assets to their full path within the export directory, as shown by the listed asset names.\nWhen disabled, assets will be exported into the root-level of a folder named after the asset's type (e.g. \"material/\",\"ui_image/\").");
            
            ImGui::Checkbox("Export asset dependencies", &g_ExportSettings.exportAssetDeps);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Enables exporting of all dependencies that are associated with any asset that is being exported.");
            // Discord presence toggle
            ImGui::SameLine();
            ImGui::Checkbox("Enable Discord Presence", &UtilsConfig->discordPresenceEnabled);
            
            ImGui::Checkbox("Disable CacheDB names", &g_ExportSettings.disableCachedNames);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Disables loading names from the cache file, new names will still be added.");

            // texture settings
            ImGui::SeparatorText("Export (Textures)");

            ImGui::Combo("Material Texture Names", reinterpret_cast<int*>(&g_ExportSettings.exportTextureNameSetting), s_TextureExportNameSetting, static_cast<int>(ARRAYSIZE(s_TextureExportNameSetting)));
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Naming scheme for exporting textures via materials options are as follows:\nGUID: exports only using the asset's GUID as a name.\nReal: exports texture using a real name (asset name or guid if no name).\nText: exports the texture with a text name always, generating one if there is none provided.\nSemantic: exports with a generated name all the time, useful for models.");

            ImGui::Combo("Normal Recalc", reinterpret_cast<int*>(&g_ExportSettings.exportNormalRecalcSetting), s_NormalExportRecalcSetting, static_cast<int>(ARRAYSIZE(s_NormalExportRecalcSetting)));
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("None: exports the normal as it is stored.\nDirectX: exports with a generated blue channel.\nOpenGL: exports with a generated blue channel and inverts the green channel.");

            ImGui::Checkbox("Export Material Textures", &g_ExportSettings.exportMaterialTextures);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Enables exporting of all textures that are associated with any material asset that is being exported.");

            // model settings
            ImGui::SeparatorText("Export (Models)");

            ImGui::Checkbox("Export Sequences", &g_ExportSettings.exportRigSequences);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Enables exporting of all animation sequences that are associated with any rig or model asset that is being exported.");

            ImGui::Checkbox("Export Skin", &g_ExportSettings.exportModelSkin);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Enables exporting a model with the previewed skin.");

            ImGui::Checkbox("Truncate Materials", &g_ExportSettings.exportModelMatsTruncated);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Truncates material names on SMD.");

            ImGui::Checkbox("Enable QCI Files", &g_ExportSettings.exportQCIFiles);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("QC file will be split into multiple include files.");

            ImGui::PushItemWidth(48.0f);
            ImGui::InputScalar("##QCTargetMajor", ImGuiDataType_U16, reinterpret_cast<uint16_t*>(&g_ExportSettings.qcMajorVersion), nullptr, nullptr, "%u", ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine();
            ImGui::InputScalar("##QCTargetMinor", ImGuiDataType_U16, reinterpret_cast<uint16_t*>(&g_ExportSettings.qcMinorVersion), nullptr, nullptr, "%u", ImGuiInputTextFlags_CharsDecimal);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::Text("QC Target Version");
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Desired version for QC files to be compatible with.");

            // physics settings
            ImGui::InputInt("Physics contents filter", reinterpret_cast<int*>(&g_ExportSettings.exportPhysicsContentsFilter), 1, 100, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Filter physics meshes in or out based on selected contents.");

            ImGui::Checkbox("Physics contents filter exclusive", &g_ExportSettings.exportPhysicsFilterExclusive);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Exclude physics meshes containing any of the specified contents.");

            ImGui::Checkbox("Physics contents filter require all", &g_ExportSettings.exportPhysicsFilterAND);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Filter only physics meshes containing all specified contents.");

            // ===============================================================================================================
            ImGui::SeparatorText("Parsing");

            ImGui::Combo("Compression Level", reinterpret_cast<int*>(&UtilsConfig->compressionLevel), s_CompressionLevelSetting, static_cast<int>(ARRAYSIZE(s_CompressionLevelSetting)));
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Specifies the compression level used when storing parsed assets in memory.\nWARNING: Modify only if you know what you�re doing; otherwise, you may run out of memory.\nNone: no compression.\nSuper Fast: Fastest level with the lowest compression ratio.\nVery Fast: Standard setting; fastest level with a decent compression ratio.\nFast: Fastest level with a good compression ratio.\nNormal: Standard LZ speed with the highest compression ratio.");

            ImGui::SliderScalar("Parse Threads", ImGuiDataType_U32, &UtilsConfig->parseThreadCount, &minThreads, reinterpret_cast<int*>(&maxConcurrentThreads));
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("The number of CPU threads that will be used for loading files.\n\nIn general, the higher the number, the faster RSX will be able to load the selected files.");

            ImGui::SliderScalar("Export Threads", ImGuiDataType_U32, &UtilsConfig->exportThreadCount, &minThreads, reinterpret_cast<int*>(&maxConcurrentThreads));
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("The number of CPU threads that will be used for exporting assets.\n\nA higher number of threads will usually make RSX export assets more quickly, however the increased disk usage may cause decreased performance.");

            // ===============================================================================================================
            ImGui::SeparatorText("Preview");

            ImGui::SliderFloat("Cull Distance", &g_PreviewSettings.previewCullDistance, PREVIEW_CULL_MIN, PREVIEW_CULL_MAX);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Distance at which render of 3D objects will stop. Note: only updated on startup.\n"); // todo: recreate projection matrix ?

            ImGui::SliderFloat("Movement Speed", &g_PreviewSettings.previewMovementSpeed, PREVIEW_SPEED_MIN, PREVIEW_SPEED_MAX);
            ImGui::SameLine();
            g_pImGuiHandler->HelpMarker("Speed at which the camera moves through the 3D scene.\n");
            
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

    g_pImGuiHandler->HandleProgressBar();

    ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
    if (previewDrawData)
    {
        //const CDXCamera* camera = g_dxHandler->GetCamera();
        //bgDrawList->AddText(
        //    ImGui::GetFont(), 15.f,
        //    ImVec2(10, 20), 0xFFFFFFFF,
        //    std::format("pos: {:.3f} {:.3f} {:.3f}\nrot: {:.3f} {:.3f} {:.3f}",
        //        camera->position.x, camera->position.y, camera->position.z,
        //        camera->rotation.x, camera->rotation.y, camera->rotation.z
        //    ).c_str());
        //bgDrawList->AddText(ImGui::GetFont(), 20.f, ImVec2(10, 50), 0xFF0000FF, previewDrawData->modelName);

        bgDrawList->AddText(
            ImGui::GetFont(), 15.f,
            ImVec2(10, 20), 0xFFFFFFFF,
            std::format("right click to toggle preview mouse control").c_str());
    }

    ImGui::Render();
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    ID3D11RenderTargetView* const mainView = g_dxHandler->GetMainView();
    static constexpr float clear_color_with_alpha[4] = { 0.01f, 0.01f, 0.01f, 1.00f };
    g_dxHandler->GetDeviceContext()->OMSetRenderTargets(1, &mainView, g_dxHandler->GetDepthStencilView());
    g_dxHandler->GetDeviceContext()->ClearRenderTargetView(mainView, clear_color_with_alpha);
    g_dxHandler->GetDeviceContext()->ClearDepthStencilView(g_dxHandler->GetDepthStencilView(), D3D11_CLEAR_DEPTH, 1, 0);

    LONG width = 0ul;
    LONG height = 0ul;
    g_dxHandler->GetWindowSize(&width, &height);

    const D3D11_VIEWPORT vp = {
        0, 0,
        static_cast<float>(width),
        static_cast<float>(height),
        0, 1
    };
    ID3D11Device* const device = g_dxHandler->GetDevice();
    ID3D11DeviceContext* const ctx = g_dxHandler->GetDeviceContext();

#if !defined(ADVANCED_MODEL_PREVIEW)
    UNUSED(device);
#endif

    g_dxHandler->GetDeviceContext()->RSSetViewports(1u, &vp);
    g_dxHandler->GetDeviceContext()->RSSetState(g_dxHandler->GetRasterizerState());
    g_dxHandler->GetDeviceContext()->OMSetDepthStencilState(g_dxHandler->GetDepthStencilState(), 1u);

#if defined(ADVANCED_MODEL_PREVIEW)
    g_dxHandler->GetCamera()->CommitCameraDataBufferUpdates();

    CDXScene& scene = g_dxHandler->GetScene();

    if (scene.globalLights.size() == 0)
    {
        HardwareLight& light = scene.globalLights.emplace_back();

        light.pos = { 0, 10.f, 0 };
        light.rcpMaxRadius = 1 / 100.f;
        light.rcpMaxRadiusSq = 1 / (light.rcpMaxRadius * light.rcpMaxRadius);
        light.attenLinear = -1.95238f;
        light.attenQuadratic = 0.95238f;
        light.specularIntensity = 1.f;
        light.color = { 1.f, 1.f, 1.f };
    }

    if (scene.NeedsLightingUpdate())
        g_dxHandler->GetScene().CreateOrUpdateLights(device, ctx);
#endif

    if (previewDrawData)
    {
        CDXCamera* const camera = g_dxHandler->GetCamera();

        if (previewDrawData->vertexShader && previewDrawData->pixelShader)
        {
            CShader* vertexShader = previewDrawData->vertexShader;
            CShader* pixelShader = previewDrawData->pixelShader;

            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            ctx->IASetInputLayout(vertexShader->GetInputLayout());

            if (vertexShader)
                ctx->VSSetShader(vertexShader->Get<ID3D11VertexShader>(), nullptr, 0u);

            if (pixelShader)
                ctx->PSSetShader(pixelShader->Get<ID3D11PixelShader>(), nullptr, 0u);

            assertm(previewDrawData->transformsBuffer, "uh oh something very bad happened!!!!!!");
            ID3D11Buffer* const transformsBuffer = previewDrawData->transformsBuffer;

            ctx->VSSetConstantBuffers(0u, 1u, &transformsBuffer);

            UINT offset = 0u;

            for (size_t i = 0; i < previewDrawData->meshBuffers.size(); ++i)
            {
                const DXMeshDrawData_t& meshDrawData = previewDrawData->meshBuffers[i];

                // if this mesh is not visible, don't draw it!
                if (!meshDrawData.visible)
                    continue;

                if (meshDrawData.doFrustumCulling)
                {
                    // todo
                }

#if defined(ADVANCED_MODEL_PREVIEW)
                const bool useAdvancedModelPreview = meshDrawData.vertexShader && meshDrawData.pixelShader;
#else
                constexpr bool useAdvancedModelPreview = false;
#endif

                ID3D11Buffer* sharedConstBuffers[] = {
                    camera->bufCommonPerCamera,
                    previewDrawData->transformsBuffer,
                };

                if (meshDrawData.vertexShader)
                {
                    ctx->IASetInputLayout(meshDrawData.inputLayout);
                    ctx->VSSetShader(meshDrawData.vertexShader, nullptr, 0u);
                }
                else
                    ctx->VSSetShader(vertexShader->Get<ID3D11VertexShader>(), nullptr, 0u);

                // if we have a custom vertex shader and/or pixel shader for this mesh from the draw data, use it
                // otherwise we can fall back on the base shaders
                if (useAdvancedModelPreview)
                {
                    ctx->VSSetConstantBuffers(2u, ARRSIZE(sharedConstBuffers), sharedConstBuffers);

                    ctx->VSSetShaderResources(60u, 1u, &previewDrawData->boneMatrixSRV);
                    ctx->VSSetShaderResources(62u, 1u, &previewDrawData->boneMatrixSRV);
                }

                for (auto& rsrc : previewDrawData->vertexShaderResources)
                {
                    ctx->VSSetShaderResources(rsrc.bindPoint, 1u, &rsrc.resourceView);
                }

                ctx->IASetVertexBuffers(0u, 1u, &meshDrawData.vertexBuffer, &meshDrawData.vertexStride, &offset);

                if (meshDrawData.pixelShader)
                    ctx->PSSetShader(meshDrawData.pixelShader, nullptr, 0u);
                else
                    ctx->PSSetShader(pixelShader->Get<ID3D11PixelShader>(), nullptr, 0u);

                ID3D11SamplerState* const samplerState = g_dxHandler->GetSamplerState();

                if (useAdvancedModelPreview)
                {
                    ID3D11SamplerState* samplers[] = {
                        g_dxHandler->GetSamplerComparisonState(),
                        samplerState,
                        samplerState,
                    };
                    ctx->PSSetSamplers(0, ARRSIZE(samplers), samplers);

                    if (meshDrawData.uberStaticBuf)
                        ctx->PSSetConstantBuffers(0u, 1u, &meshDrawData.uberStaticBuf);
                    ctx->PSSetConstantBuffers(2u, ARRSIZE(sharedConstBuffers), sharedConstBuffers);

#if defined(ADVANCED_MODEL_PREVIEW)
                    scene.BindLightsSRV(ctx);
#endif
                }
                else
                    ctx->PSSetSamplers(0, 1, &samplerState);

                for (auto& tex : meshDrawData.textures)
                {
                    ID3D11ShaderResourceView* const textureSRV = tex.texture
                        ? tex.texture.get()->GetSRV()
                        : nullptr;

                    ctx->PSSetShaderResources(tex.resourceBindPoint, 1u, &textureSRV);
                }

                for (auto& rsrc : previewDrawData->pixelShaderResources)
                {
                    ctx->PSSetShaderResources(rsrc.bindPoint, 1u, &rsrc.resourceView);
                }

                ctx->IASetIndexBuffer(meshDrawData.indexBuffer, meshDrawData.indexFormat, 0u);
                ctx->DrawIndexed(static_cast<UINT>(meshDrawData.numIndices), 0u, 0u);
            }
        }
        else
        {
            assertm(0, "Failed to load shaders for model preview.");
        }
    }

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_dxHandler->GetSwapChain()->Present(1u, 0u);
}