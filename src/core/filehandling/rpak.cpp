#include <pch.h>

#include <thirdparty/imgui/misc/imgui_utility.h>

#include <core/filehandling/load.h>
#include <core/filehandling/export.h>

#include <core/discord_presence.h>

#include <game/rtech/cpakfile.h>

namespace
{
    thread_local const CAsset* g_CurrentExportRootAsset = nullptr;
    thread_local std::vector<const CAsset*> g_ExportAssetStack;

    class ScopedExportRootContext
    {
    public:
        explicit ScopedExportRootContext(const CAsset* newRoot)
            : m_previousRoot(g_CurrentExportRootAsset), m_applied(newRoot != nullptr)
        {
            if (m_applied)
                g_CurrentExportRootAsset = newRoot;
        }

        ~ScopedExportRootContext()
        {
            if (m_applied)
                g_CurrentExportRootAsset = m_previousRoot;
        }

    private:
        const CAsset* m_previousRoot;
        bool m_applied;
    };

    class ScopedExportAssetContext
    {
    public:
        explicit ScopedExportAssetContext(const CAsset* asset)
            : m_applied(asset != nullptr)
        {
            if (m_applied)
                g_ExportAssetStack.push_back(asset);
        }

        ~ScopedExportAssetContext()
        {
            if (m_applied)
                g_ExportAssetStack.pop_back();
        }

    private:
        bool m_applied;
    };
}

const CAsset* GetCurrentExportRootAsset()
{
    return g_CurrentExportRootAsset;
}

const CAsset* GetCurrentExportParentAsset()
{
    return g_ExportAssetStack.size() > 1 ? g_ExportAssetStack[g_ExportAssetStack.size() - 2] : nullptr;
}

namespace
{
    constexpr uint32_t kEfctAssetType = static_cast<uint32_t>(AssetType_t::EFCT);

    inline bool IsEfctAsset(const CAsset* asset)
    {
        return asset && asset->GetAssetType() == kEfctAssetType;
    }
}

void HandlePakLoad(std::vector<std::string> filePaths)
{
    // Original pak load behavior (no global UI progress counters used here).

    if (g_assetData.m_pakPatchMaster)
        delete g_assetData.m_pakPatchMaster;

    g_assetData.m_patchMasterEntries.clear();
    g_assetData.m_pakLoadStatusMap.clear();

    for (const std::string& path : filePaths)
    {
        std::filesystem::path fsPath = path;

        // If pak is not located in drive root (shouldn't happen but we might as well check)
        if (!g_assetData.m_pakPatchMaster && fsPath.has_parent_path())
        {
            const std::filesystem::path dirPath = fsPath.parent_path();
            const std::filesystem::path patchMasterPath = dirPath / "patch_master.rpak";

            if (std::filesystem::exists(patchMasterPath))
            {
                // [rika]: prevent double load on patch_master and catch if it fails to load
                g_assetData.m_pakPatchMaster = new CPakFile();
                if (static_cast<CPakFile*>(g_assetData.m_pakPatchMaster)->ParseFileBuffer(patchMasterPath.string()))
                {
                    const CPakFile* const pak = static_cast<CPakFile*>(g_assetData.m_pakPatchMaster);

                    if (pak->header()->crc != 0)
                        g_assetData.m_pakLoadStatusMap.emplace(pak->header()->crc, true);
                }
                else
                {
                    assertm(false, "Parsing patch_master from file failed.");
                    delete g_assetData.m_pakPatchMaster;
                    g_assetData.m_pakPatchMaster = nullptr;
                }

                //Log("[PTCH] Found %lld patch entries.\n", g_assetData.m_patchMasterEntries.size());           
            }
        }

        if (g_assetData.m_pakPatchMaster)
        {
            const std::string pakStem = GetPakFileStemNoPatchNum(path);
            const std::string basePakName = pakStem + ".rpak";

            auto& patchMap = g_assetData.m_patchMasterEntries;
            auto it = patchMap.find(basePakName);

            if (it != patchMap.end())
            {
                const uint8_t patchVersion = patchMap.at(basePakName);

                const std::string topPatchFileName = std::format("{}({:02}).rpak", pakStem, patchVersion);
                fsPath.replace_filename(topPatchFileName);

                Log("Loading highest patch '%s' instead of requested file '%s'\n", topPatchFileName.c_str(), path.c_str());
            }
        }

        if (CPakFile* const pak = new CPakFile(); pak->ParseFileBuffer(fsPath.string()))
        {
            if(pak->header()->crc != 0)
                g_assetData.m_pakLoadStatusMap.emplace(pak->header()->crc, true);

            g_assetData.v_assetContainers.emplace_back(pak);
            // Update Discord presence to show which pak was just loaded (if enabled)
            if (UtilsConfig->discordPresenceEnabled)
                DiscordGamePresence::UpdatePresence(fsPath.filename().string(), "Loaded pak");
        }
        else
        {
            //assertm(false, "Parsing pak from file failed.");
            delete pak;
        }
    }
}

static void TraverseAssetDependencies(CPakAsset* const asset, std::deque<CPakAsset*>& cpyAssets, std::deque<CPakAsset*>& parentAssets, CPakAsset* const parent)
{
    std::vector<AssetGuid_t> dependencies;
    asset->getDependencies(dependencies);

    for (int d = 0; d < dependencies.size(); ++d)
    {
        const AssetGuid_t guid = dependencies[d];
        CPakAsset* const depAsset = g_assetData.FindAssetByGUID<CPakAsset>(guid.guid);

        if (!depAsset)
            continue;

        if (IsEfctAsset(depAsset))
            continue;

        if (std::find(cpyAssets.begin(), cpyAssets.end(), depAsset) != cpyAssets.end())
            continue; // Already in the list.

        assert(depAsset != asset);
        parentAssets.emplace_back(asset);
        cpyAssets.emplace_back(depAsset);

        // Add the dependencies of this asset into the list as well.
        TraverseAssetDependencies(depAsset, cpyAssets, parentAssets, depAsset);
    }

    // Add the root asset itself to the list.
    if (std::find(cpyAssets.begin(), cpyAssets.end(), asset) == cpyAssets.end())
    {
        parentAssets.emplace_back(parent);
        cpyAssets.emplace_back(asset);
    }
}

static void HandleExportBindingForAssetEx(CAsset* const asset)
{
    ScopedExportAssetContext assetContext(asset);

    if (auto it = g_assetData.m_assetTypeBindings.find(asset->GetAssetType()); it != g_assetData.m_assetTypeBindings.end())
    {
        if (it->second.e.exportFunc)
        {
            const bool exported = it->second.e.exportFunc(asset, it->second.e.exportSetting);
            asset->SetExportedStatus(exported);
        }
    }
}

FORCEINLINE void HandleExportBindingForAsset(CAsset* const asset, const bool exportDependencies)
{
    const bool allowDependencies = exportDependencies && !IsEfctAsset(asset);

    // only pak assets have dependencies so don't try to export them with other types
    if (asset->GetAssetContainerType() == CAsset::ContainerType::PAK && allowDependencies)
    {
        std::deque<CPakAsset*> cpyAssets;
        std::deque<CPakAsset*> parentAssets;
        TraverseAssetDependencies(static_cast<CPakAsset*>(asset), cpyAssets, parentAssets, nullptr);

        ScopedExportRootContext rootContext(asset);

        for (size_t idx = 0; idx < cpyAssets.size(); ++idx)
        {
            CPakAsset* const dependency = cpyAssets[idx];
            CAsset* const parent = parentAssets[idx];

            ScopedExportAssetContext parentContext(parent);
            HandleExportBindingForAssetEx(dependency);
        }
    }
    else
        HandleExportBindingForAssetEx(asset);
}

void HandlePakAssetExportList(std::deque<CAsset*> selectedAssets, const bool exportDependencies)
{
    assertm(selectedAssets.size() > 0, "selectedAssets is empty.");

    CParallelTask parallelProcessTask(UtilsConfig->exportThreadCount);

    for (auto& asset : selectedAssets)
    {
        parallelProcessTask.addTask([&asset, exportDependencies]
            {
                HandleExportBindingForAsset(asset, exportDependencies);
            }, 1u);
    }

    const ProgressBarEvent_t* const exportAssetListEvent = g_pImGuiHandler->AddProgressBarEvent("Exporting asset list..", parallelProcessTask.getRemainingTasks(), &parallelProcessTask, PB_FNCLASS_TO_VOID(&CParallelTask::getRemainingTasks));
    parallelProcessTask.execute();
    parallelProcessTask.wait();
    g_pImGuiHandler->FinishProgressBarEvent(exportAssetListEvent);
}

void HandleExportAllPakAssets(std::vector<CGlobalAssetData::AssetLookup_t>* const pakAssets, const bool exportDependencies)
{
    assertm(g_assetData.v_assetContainers.size() > 0, "No paks loaded.");
    assertm(pakAssets->size() > 0, "No assets?");

    CParallelTask parallelProcessTask(UtilsConfig->exportThreadCount);

    for (auto& asset : *pakAssets)
    {
        parallelProcessTask.addTask([asset, exportDependencies]
            {
                HandleExportBindingForAsset(asset.m_asset, exportDependencies);
            }, 1u);
    }

    const ProgressBarEvent_t* const exportAllAssetsEvent = g_pImGuiHandler->AddProgressBarEvent("Exporting all assets..", parallelProcessTask.getRemainingTasks(), &parallelProcessTask, PB_FNCLASS_TO_VOID(&CParallelTask::getRemainingTasks));
    parallelProcessTask.execute();
    parallelProcessTask.wait();
    g_pImGuiHandler->FinishProgressBarEvent(exportAllAssetsEvent);
}

void HandleExportSelectedAssetType(std::vector<CGlobalAssetData::AssetLookup_t> pakAssets, const bool exportDependencies)
{
    HandleExportAllPakAssets(&pakAssets, exportDependencies);
}