#include <pch.h>
#include <core/crashhandler.h>

#include <thirdparty/imgui/imgui.h>
#include <thirdparty/imgui/backends/imgui_impl_win32.h>
#include <thirdparty/imgui/backends/imgui_impl_dx11.h>

#include <thirdparty/imgui/misc/imgui_utility.h>

#include <core/render/dx.h>
#include <core/input/input.h>
#include <core/cache/cachedb.h>
#include <core/utils/cli_parser.h>
#include <core/utils/exportsettings.h>
#include <core/filehandling/load.h>

#include <core/splash.h>
#include <core/window.h>
#include <core/render.h>
#include <iostream>
#include <game/rtech/utils/bsp/bspflags.h>

CDXParentHandler* g_dxHandler;
std::atomic<uint32_t> maxConcurrentThreads = 1u;

CBufferManager g_BufferManager; // called constructor on init.

ExportSettings_t g_ExportSettings{ .exportNormalRecalcSetting = eNormalExportRecalc::NML_RECALC_NONE, .exportTextureNameSetting = eTextureExportName::TXTR_NAME_TEXT,
    .exportMaterialTextures = true, .exportPathsFull = false, .exportAssetDeps = false, .disableCachedNames = false, .previewedSkinIndex = 0,
    .qcMajorVersion = 49, .qcMinorVersion = 0, .exportRigSequences = true, .exportModelSkin = false, .exportModelMatsTruncated = false,
    .exportQCIFiles = false, .exportPhysicsContentsFilter = static_cast<uint32_t>(TRACE_MASK_ALL), .exportDirectory = ""
};

// Handle CLI to only init certain asset types.
static void HandleAssetRegistration(const CCommandLine* const cli)
{
    UNUSED(cli);

    // import func
    // model
    extern void InitModelAssetType();
    extern void InitAnimRigAssetType();
    extern void InitAnimSeqAssetType();
    extern void InitAnimSeqDataAssetType();
    extern void InitAnimRecordingAssetType();

    extern void InitSourceModelAssetType();
    extern void InitSourceSequenceAssetType();

    // texture/material
    extern void InitMaterialAssetType();
    extern void InitMaterialSnapshotAssetType();
    // mt4a
    extern void InitTextureAssetType();
    extern void InitTextureAnimationAssetType();
    extern void InitTextureListAssetType();
    extern void InitUIImageAtlasAssetType();
    extern void InitUIImageAssetType(); 
    extern void InitUIFontAtlasAssetType();

    // particle (texture)
    extern void InitEffectAssetType();
    extern void InitParticleScriptAssetType();
    
    // dx/shader
    extern void InitShaderAssetType();
    extern void InitShaderSetAssetType();
    
    // ui
    extern void InitUIAssetType();

    // rlcd
    extern void InitLcdScreenEffectAssetType();
    extern void InitRTKAssetType();
    
    // pak
    extern void InitPatchMasterAssetType();
    // vers
    
    // descriptors (stats, specs, etc)
    extern void InitDatatableAssetType();
    extern void InitSettingsAssetType();
    extern void InitSettingsLayoutAssetType();
    extern void InitRSONAssetType();
    extern void InitSubtitlesAsset();
    extern void InitLoclAssetType();

    extern void InitODLAssetType();
    
    // vpk
    extern void InitWrapAssetType();
    extern void InitWeaponDefinitionAssetType();
    extern void InitImpactAssetType();
    
    // map
    extern void InitMapAssetType();
    // llyr
    
    // audio
    extern void InitAudioSourceAssetType();

    // bluepoint
    extern void InitBluepointWrappedFileAssetType();


    // call func
    // model
    InitModelAssetType();
    InitAnimRigAssetType();
    InitAnimSeqAssetType();
    InitAnimSeqDataAssetType();
    InitAnimRecordingAssetType();

    InitSourceModelAssetType();
    InitSourceSequenceAssetType();

    // texture/material
    InitMaterialAssetType();
    InitMaterialSnapshotAssetType();

    // mt4a
    InitTextureAssetType();
    InitTextureAnimationAssetType();
    InitTextureListAssetType();
    InitUIImageAtlasAssetType();
    InitUIImageAssetType();
    InitUIFontAtlasAssetType();

    // particle (texture)
    InitEffectAssetType();
    InitParticleScriptAssetType();

    // dx/shader
    InitShaderAssetType();
    InitShaderSetAssetType();

    // ui
    InitUIAssetType();
    // hsys
    InitLcdScreenEffectAssetType();
    InitRTKAssetType();

    // pak
    InitPatchMasterAssetType();
    // vers

    // descriptors (stats, specs, etc)
    InitDatatableAssetType();
    InitSettingsAssetType();
    InitSettingsLayoutAssetType();
    InitRSONAssetType();
    InitSubtitlesAsset();
    InitLoclAssetType();

    InitODLAssetType();

    // vpk
    InitWrapAssetType();
    InitWeaponDefinitionAssetType();
    InitImpactAssetType();

    // map
    InitMapAssetType();
    // llyr

    // audio
    InitAudioSourceAssetType();

    // bluepoint
    InitBluepointWrappedFileAssetType();
}

#if defined(NDEBUG) && defined(_WIN32)
// https://stackoverflow.com/a/57241985
void CreateConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    if (!AllocConsole())
        return;

    // std::cout, std::clog, std::cerr, std::cin
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    std::cout.clear();
    std::clog.clear();
    std::cerr.clear();
    std::cin.clear();

    // std::wcout, std::wclog, std::wcerr, std::wcin
    HANDLE hConOut = CreateFile(_T("CONOUT$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hConIn = CreateFile(_T("CONIN$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
    SetStdHandle(STD_ERROR_HANDLE, hConOut);
    SetStdHandle(STD_INPUT_HANDLE, hConIn);
    std::wcout.clear();
    std::wclog.clear();
    std::wcerr.clear();
    std::wcin.clear();
}
#endif



int main(int argc, char* argv[])
{
    CCommandLine cli(argc, argv);

#if defined(BUILD_NOGUI)
    constexpr bool noGui = true;
#else
    const bool noGui = cli.HasParam("-nogui");
#endif

#if defined(NDEBUG) && defined(_WIN32)
    if (noGui)
        CreateConsole();
#endif

    // we want the visual studio debugger to be able to control the working directory
#if defined(NDEBUG) && !defined(BUILD_NOGUI)
    // this is needed to properly support drag'n'drop, it changes the current working directory to the file you drag into the exe
    // HOWEVER: this should not apply when nogui is specified, as it is expected that shorter, relative file paths can be used when running CLI
    if (!noGui && !RestoreCurrentWorkingDirectory())
    {
        return EXIT_FAILURE;
    }
#endif // #if defined(NDEBUG)

    // Set default export directory.
    // TODO: If/when an option for this is added to the UI, this line will need to be removed and replaced with something that reads the value from imgui.ini
    g_ExportSettings.SetExportDirectory(std::filesystem::current_path() / EXPORT_DIRECTORY_NAME);

    if (noGui)
        g_ExportSettings.SetFromCLI(&cli);

#if defined(_WIN32)
    // https://github.com/microsoft/DirectXTex/wiki/DirectXTex#initialization
    // [rika]: supposed to be done per thread but it Just Works so I'm not messing with it
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        assertm(false, "failed to initialize COM");
        return EXIT_FAILURE;
    }
#endif

#ifdef EXCEPTION_HANDLER
    g_CrashHandler.Init();
#endif

    g_cacheDBManager.LoadFromFile((std::filesystem::current_path() / "rsx_cache_db.bin").string());

    // init pak asset types
    HandleAssetRegistration(&cli);

    // get max con-current threads.
    maxConcurrentThreads = std::max(1u, CThread::GetConCurrentThreads());

#if defined(SPLASHSCREEN)
    if(!noGui)
        DrawSplashScreen(); // draw splashscreen now for 2~ seconds
#endif // #if defined(SPLASHSCREEN)

#if !defined(BUILD_NOGUI)
    if (!noGui)
    {
        const HWND windowHandle = SetupWindow();

        g_dxHandler = new CDXParentHandler(windowHandle);
        if (!g_dxHandler->SetupDeviceD3D())
        {
            assertm(false, _T("Failed to setup D3D."));
            delete g_dxHandler;
            return EXIT_FAILURE;
        }
        g_pInput->Init(windowHandle);

        ShowWindow(windowHandle, SW_SHOWDEFAULT);
        UpdateWindow(windowHandle);

        ImGui::CreateContext();
        g_pImGuiHandler->SetupHandler();
        g_pImGuiHandler->SetStyle();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard
            /*| ImGuiConfigFlags_NavEnableGamepad*/
            | ImGuiConfigFlags_DockingEnable
            | ImGuiConfigFlags_ViewportsEnable
            ;

        GImGui->NavCursorVisible = false; // was NavDisableHighlight

        ImGui_ImplWin32_Init(windowHandle);
        ImGui_ImplDX11_Init(g_dxHandler->GetDevice(), g_dxHandler->GetDeviceContext());
    }
    else
#endif
    {
        g_dxHandler = new CDXParentHandler(NULL);

        g_pImGuiHandler->SetNoImGui(true);

        const uint32_t totalThreadCount = CThread::GetConCurrentThreads();

        if (const char* const numParseThreads = cli.GetParamValue("--parsethreads"))
            UtilsConfig->parseThreadCount = clamp(static_cast<uint32_t>(atoi(numParseThreads)), 1u, totalThreadCount);

        if (const char* const numExportThreads = cli.GetParamValue("--exportthreads"))
            UtilsConfig->exportThreadCount = clamp(static_cast<uint32_t>(atoi(numExportThreads)), 1u, totalThreadCount);
    }

    // call after initializing dx and gui otherwise you will crash
    HandleLoadFromCommandLine(&cli);

#if !defined(BUILD_NOGUI)
    if (!noGui)
    {
        bool quit = false;
        while (!quit)
        {
            MSG msg;
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);

                if (msg.message == WM_QUIT)
                    quit = true;
            }
            if (quit)
                break;

            HandleRenderFrame();
        }
    }
#endif

    g_cacheDBManager.SaveToFile((std::filesystem::current_path() / "rsx_cache_db.bin").string());

#if !defined(BUILD_NOGUI)
    if (!noGui)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
#endif

    delete g_dxHandler;

	return EXIT_SUCCESS;
}

int WINAPI WinMain(
    _In_ HINSTANCE /*hInstance*/,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPSTR     /*lpCmdLine*/,
    _In_ int       /*nCmdShow*/
)
{
    return main(__argc, __argv);
};