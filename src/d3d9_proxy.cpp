#include <windows.h>
#include <d3d9.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace
{
    HMODULE g_realD3D9 = nullptr;
    HINSTANCE g_instance = nullptr;
    char g_logPath[MAX_PATH] = {};
    char g_configPath[MAX_PATH] = {};
    char g_hudStatusPath[MAX_PATH] = {};

    enum class LogLevel
    {
        Quiet = 0,
        Normal = 1,
        Debug = 2
    };

    struct Config
    {
        LogLevel logging = LogLevel::Normal;
        bool textureDetail = true;
        bool waterReflection = true;
        bool dynamicLighting = true;
        bool metalSheen = true;
        bool surfaceDetail = true;
        bool volumetricFog = false;
        bool extendRenderDistance = true;
        bool protectAlphaLights = true;
        bool softAlphaFix = true;
        bool suppressShadowPlanes = true;
        bool frameSummaries = false;
        bool drawSampling = false;
        bool waterDiagnostics = false;
        bool chainLoadD3D9 = false;
        bool hudEnabled = false;
        char hudMode[32] = "crawler";
        int hudX = 18;
        int hudY = 92;
        BYTE hudOpacity = 220;
        bool hudShowDetails = true;
        char hudPerks[192] = "";
        char hudBonuses[192] = "";
        char hudProgress[128] = "";
        char hudPending[128] = "";
        char chainD3D9Path[MAX_PATH] = "reshade_d3d9.dll";
        UINT shadowMaxPrimitiveCount = 2;
        UINT detailMinPrimitiveCount = 4;
        DWORD detailAnisotropy = 16;
        float detailMipBias = -0.85f;
        UINT waterMinPrimitiveCount = 80;
        UINT waterMaxPrimitiveCount = 220;
        float waterStrength = 0.55f;
        float surfaceDetailStrength = 0.35f;
        UINT lightMinPrimitiveCount = 4;
        UINT lightMaxPrimitiveCount = 96;
        UINT metalMinPrimitiveCount = 4;
        UINT metalMaxPrimitiveCount = 48;
        float metalSpecular = 0.62f;
        float metalSpecularBlue = 0.70f;
        float metalPower = 72.0f;
        BYTE ambientR = 42;
        BYTE ambientG = 42;
        BYTE ambientB = 50;
        float materialSpecular = 0.38f;
        float materialSpecularBlue = 0.44f;
        float materialPower = 12.0f;
        float diffuseStrength = 0.90f;
        float specularStrength = 0.90f;
        float lightFlickerStrength = 0.08f;
        float lightMotionStrength = 0.10f;
        DWORD softAlphaRef = 10;
        float fogStartScale = 1.05f;
        float fogEndScale = 1.25f;
        float fogDensityScale = 0.15f;
        float minFogEnd = 40000.0f;
        float farPlane = 500000.0f;
    };

    Config g_config;
    bool g_configLoaded = false;
    DWORD g_lastHudStatusReadTick = 0;
    void InitPaths()
    {
        if (g_logPath[0] != '\0' && g_configPath[0] != '\0' && g_hudStatusPath[0] != '\0')
            return;

        char basePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, basePath, MAX_PATH);
        char* slash = strrchr(basePath, '\\');
        if (slash != nullptr)
            *(slash + 1) = '\0';
        else
            basePath[0] = '\0';

        strcpy_s(g_logPath, basePath);
        strncat_s(g_logPath, "ac_d3d9_proxy.log", _TRUNCATE);
        strcpy_s(g_configPath, basePath);
        strncat_s(g_configPath, "derethfx.ini", _TRUNCATE);
        strcpy_s(g_hudStatusPath, basePath);
        strncat_s(g_hudStatusPath, "derethfx_hud.ini", _TRUNCATE);
    }

    bool ReadBool(const char* section, const char* key, bool fallback)
    {
        return GetPrivateProfileIntA(section, key, fallback ? 1 : 0, g_configPath) != 0;
    }

    UINT ReadUInt(const char* section, const char* key, UINT fallback, UINT minValue, UINT maxValue)
    {
        UINT value = static_cast<UINT>(GetPrivateProfileIntA(section, key, static_cast<int>(fallback), g_configPath));
        if (value < minValue)
            value = minValue;
        if (value > maxValue)
            value = maxValue;
        return value;
    }

    float ReadFloat(const char* section, const char* key, float fallback, float minValue, float maxValue)
    {
        char text[64] = {};
        char fallbackText[64] = {};
        std::snprintf(fallbackText, sizeof(fallbackText), "%.3f", fallback);
        GetPrivateProfileStringA(section, key, fallbackText, text, sizeof(text), g_configPath);
        float value = static_cast<float>(std::atof(text));
        if (value < minValue)
            value = minValue;
        if (value > maxValue)
            value = maxValue;
        return value;
    }

    void ReadString(const char* section, const char* key, const char* fallback, char* output, DWORD outputSize)
    {
        if (output == nullptr || outputSize == 0)
            return;
        GetPrivateProfileStringA(section, key, fallback != nullptr ? fallback : "", output, outputSize, g_configPath);
    }

    void OverrideStringFromHudStatus(const char* section, const char* key, char* output, DWORD outputSize)
    {
        if (output == nullptr || outputSize == 0)
            return;
        DWORD attrs = GetFileAttributesA(g_hudStatusPath);
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return;
        char current[256] = {};
        strncpy_s(current, output, _TRUNCATE);
        GetPrivateProfileStringA(section, key, current, output, outputSize, g_hudStatusPath);
    }

    void RefreshHudStatusConfig()
    {
        DWORD now = GetTickCount();
        if (g_lastHudStatusReadTick != 0 && now - g_lastHudStatusReadTick < 500)
            return;

        g_lastHudStatusReadTick = now;
        OverrideStringFromHudStatus("CrawlerHUD", "perks", g_config.hudPerks, sizeof(g_config.hudPerks));
        OverrideStringFromHudStatus("CrawlerHUD", "bonuses", g_config.hudBonuses, sizeof(g_config.hudBonuses));
        OverrideStringFromHudStatus("CrawlerHUD", "progress", g_config.hudProgress, sizeof(g_config.hudProgress));
        OverrideStringFromHudStatus("CrawlerHUD", "pending", g_config.hudPending, sizeof(g_config.hudPending));
    }

    void LoadConfig()
    {
        if (g_configLoaded)
            return;

        InitPaths();

        char level[32] = {};
        GetPrivateProfileStringA("Logging", "level", "normal", level, sizeof(level), g_configPath);
        if (_stricmp(level, "quiet") == 0 || _stricmp(level, "off") == 0)
            g_config.logging = LogLevel::Quiet;
        else if (_stricmp(level, "debug") == 0)
            g_config.logging = LogLevel::Debug;
        else
            g_config.logging = LogLevel::Normal;

        g_config.textureDetail = ReadBool("Effects", "textureDetail", g_config.textureDetail);
        g_config.waterReflection = ReadBool("Effects", "waterReflection", g_config.waterReflection);
        g_config.dynamicLighting = ReadBool("Effects", "dynamicLighting", g_config.dynamicLighting);
        g_config.metalSheen = ReadBool("Effects", "metalSheen", g_config.metalSheen);
        g_config.surfaceDetail = ReadBool("Effects", "surfaceDetail", g_config.surfaceDetail);
        g_config.volumetricFog = ReadBool("Effects", "volumetricFog", g_config.volumetricFog);
        g_config.extendRenderDistance = ReadBool("Effects", "extendRenderDistance", g_config.extendRenderDistance);
        g_config.protectAlphaLights = ReadBool("Effects", "protectAlphaLights", g_config.protectAlphaLights);
        g_config.softAlphaFix = ReadBool("Effects", "softAlphaFix", g_config.softAlphaFix);
        g_config.suppressShadowPlanes = ReadBool("Effects", "suppressShadowPlanes", g_config.suppressShadowPlanes);

        g_config.frameSummaries = ReadBool("Logging", "frameSummaries", g_config.logging == LogLevel::Debug);
        g_config.drawSampling = ReadBool("Logging", "drawSampling", g_config.logging == LogLevel::Debug);
        g_config.waterDiagnostics = ReadBool("Logging", "waterDiagnostics", g_config.logging == LogLevel::Debug);
        g_config.chainLoadD3D9 = ReadBool("ReShade", "chainLoad", g_config.chainLoadD3D9);
        GetPrivateProfileStringA("ReShade", "dll", g_config.chainD3D9Path, g_config.chainD3D9Path, sizeof(g_config.chainD3D9Path), g_configPath);
        g_config.shadowMaxPrimitiveCount = ReadUInt("Shadows", "maxPrimitiveCount", g_config.shadowMaxPrimitiveCount, 1, 64);
        g_config.hudEnabled = ReadBool("HUD", "enabled", g_config.hudEnabled);
        GetPrivateProfileStringA("HUD", "mode", g_config.hudMode, g_config.hudMode, sizeof(g_config.hudMode), g_configPath);
        g_config.hudX = static_cast<int>(ReadUInt("HUD", "x", static_cast<UINT>(g_config.hudX), 0, 4096));
        g_config.hudY = static_cast<int>(ReadUInt("HUD", "y", static_cast<UINT>(g_config.hudY), 0, 4096));
        g_config.hudOpacity = static_cast<BYTE>(ReadUInt("HUD", "opacity", g_config.hudOpacity, 40, 255));
        g_config.hudShowDetails = ReadBool("CrawlerHUD", "showDetails", g_config.hudShowDetails);
        ReadString("CrawlerHUD", "perks", g_config.hudPerks, g_config.hudPerks, sizeof(g_config.hudPerks));
        ReadString("CrawlerHUD", "bonuses", g_config.hudBonuses, g_config.hudBonuses, sizeof(g_config.hudBonuses));
        ReadString("CrawlerHUD", "progress", g_config.hudProgress, g_config.hudProgress, sizeof(g_config.hudProgress));
        ReadString("CrawlerHUD", "pending", g_config.hudPending, g_config.hudPending, sizeof(g_config.hudPending));
        OverrideStringFromHudStatus("CrawlerHUD", "perks", g_config.hudPerks, sizeof(g_config.hudPerks));
        OverrideStringFromHudStatus("CrawlerHUD", "bonuses", g_config.hudBonuses, sizeof(g_config.hudBonuses));
        OverrideStringFromHudStatus("CrawlerHUD", "progress", g_config.hudProgress, sizeof(g_config.hudProgress));
        OverrideStringFromHudStatus("CrawlerHUD", "pending", g_config.hudPending, sizeof(g_config.hudPending));

        g_config.detailMinPrimitiveCount = ReadUInt("TextureDetail", "minPrimitiveCount", g_config.detailMinPrimitiveCount, 1, 10000);
        g_config.detailAnisotropy = ReadUInt("TextureDetail", "anisotropy", g_config.detailAnisotropy, 1, 16);
        g_config.detailMipBias = ReadFloat("TextureDetail", "mipBias", g_config.detailMipBias, -3.0f, 1.0f);

        g_config.waterMinPrimitiveCount = ReadUInt("Water", "minPrimitiveCount", g_config.waterMinPrimitiveCount, 1, 10000);
        g_config.waterMaxPrimitiveCount = ReadUInt("Water", "maxPrimitiveCount", g_config.waterMaxPrimitiveCount, g_config.waterMinPrimitiveCount, 10000);
        g_config.waterStrength = ReadFloat("Water", "strength", g_config.waterStrength, 0.0f, 2.0f);
        g_config.surfaceDetailStrength = ReadFloat("SurfaceDetail", "strength", g_config.surfaceDetailStrength, 0.0f, 1.0f);

        g_config.lightMinPrimitiveCount = ReadUInt("DynamicLighting", "minPrimitiveCount", g_config.lightMinPrimitiveCount, 1, 10000);
        g_config.lightMaxPrimitiveCount = ReadUInt("DynamicLighting", "maxPrimitiveCount", g_config.lightMaxPrimitiveCount, g_config.lightMinPrimitiveCount, 10000);
        g_config.ambientR = static_cast<BYTE>(ReadUInt("DynamicLighting", "ambientR", g_config.ambientR, 0, 255));
        g_config.ambientG = static_cast<BYTE>(ReadUInt("DynamicLighting", "ambientG", g_config.ambientG, 0, 255));
        g_config.ambientB = static_cast<BYTE>(ReadUInt("DynamicLighting", "ambientB", g_config.ambientB, 0, 255));
        g_config.materialSpecular = ReadFloat("DynamicLighting", "materialSpecular", g_config.materialSpecular, 0.0f, 1.0f);
        g_config.materialSpecularBlue = ReadFloat("DynamicLighting", "materialSpecularBlue", g_config.materialSpecularBlue, 0.0f, 1.0f);
        g_config.materialPower = ReadFloat("DynamicLighting", "materialPower", g_config.materialPower, 1.0f, 128.0f);
        g_config.diffuseStrength = ReadFloat("DynamicLighting", "diffuseStrength", g_config.diffuseStrength, 0.0f, 2.0f);
        g_config.specularStrength = ReadFloat("DynamicLighting", "specularStrength", g_config.specularStrength, 0.0f, 2.0f);
        g_config.lightFlickerStrength = ReadFloat("DynamicLighting", "lightFlickerStrength", g_config.lightFlickerStrength, 0.0f, 0.35f);
        g_config.lightMotionStrength = ReadFloat("DynamicLighting", "lightMotionStrength", g_config.lightMotionStrength, 0.0f, 0.50f);

        g_config.metalMinPrimitiveCount = ReadUInt("Metal", "minPrimitiveCount", g_config.metalMinPrimitiveCount, 1, 10000);
        g_config.metalMaxPrimitiveCount = ReadUInt("Metal", "maxPrimitiveCount", g_config.metalMaxPrimitiveCount, g_config.metalMinPrimitiveCount, 10000);
        g_config.metalSpecular = ReadFloat("Metal", "specular", g_config.metalSpecular, 0.0f, 1.0f);
        g_config.metalSpecularBlue = ReadFloat("Metal", "specularBlue", g_config.metalSpecularBlue, 0.0f, 1.0f);
        g_config.metalPower = ReadFloat("Metal", "power", g_config.metalPower, 1.0f, 128.0f);
        g_config.softAlphaRef = ReadUInt("Alpha", "softAlphaRef", g_config.softAlphaRef, 0, 255);

        g_config.fogStartScale = ReadFloat("Atmosphere", "fogStartScale", g_config.fogStartScale, 0.1f, 10.0f);
        g_config.fogEndScale = ReadFloat("Atmosphere", "fogEndScale", g_config.fogEndScale, 0.1f, 20.0f);
        g_config.fogDensityScale = ReadFloat("Atmosphere", "fogDensityScale", g_config.fogDensityScale, 0.05f, 5.0f);
        g_config.minFogEnd = ReadFloat("Atmosphere", "minFogEnd", g_config.minFogEnd, 1000.0f, 1000000.0f);
        g_config.farPlane = ReadFloat("RenderDistance", "farPlane", g_config.farPlane, 1000.0f, 2000000.0f);

        g_configLoaded = true;
    }

    void WriteLogLine(const char* fmt, va_list args)
    {
        FILE* file = nullptr;
        if (fopen_s(&file, g_logPath, "a") != 0 || file == nullptr)
            return;

        SYSTEMTIME st {};
        GetLocalTime(&st);
        std::fprintf(file, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        std::vfprintf(file, fmt, args);
        std::fprintf(file, "\n");
        std::fclose(file);
    }

    void Log(const char* fmt, ...)
    {
        LoadConfig();
        if (g_config.logging == LogLevel::Quiet)
            return;

        va_list args;
        va_start(args, fmt);
        WriteLogLine(fmt, args);
        va_end(args);
    }

    void DebugLog(const char* fmt, ...)
    {
        LoadConfig();
        if (g_config.logging != LogLevel::Debug)
            return;

        va_list args;
        va_start(args, fmt);
        WriteLogLine(fmt, args);
        va_end(args);
    }
    HMODULE LoadRealD3D9()
    {
        if (g_realD3D9 != nullptr)
            return g_realD3D9;

        LoadConfig();

        if (g_config.chainLoadD3D9 && g_config.chainD3D9Path[0] != '\0')
        {
            char chainPath[MAX_PATH] = {};
            const bool absolutePath =
                (g_config.chainD3D9Path[0] != '\0' && g_config.chainD3D9Path[1] == ':') ||
                (g_config.chainD3D9Path[0] == '\\' && g_config.chainD3D9Path[1] == '\\');

            if (absolutePath)
            {
                strcpy_s(chainPath, g_config.chainD3D9Path);
            }
            else
            {
                strcpy_s(chainPath, g_configPath);
                char* slash = strrchr(chainPath, '\\');
                if (slash != nullptr)
                    *(slash + 1) = '\0';
                else
                    chainPath[0] = '\0';
                strncat_s(chainPath, g_config.chainD3D9Path, _TRUNCATE);
            }

            const char* fileName = strrchr(chainPath, '\\');
            fileName = fileName != nullptr ? fileName + 1 : chainPath;
            if (_stricmp(fileName, "d3d9.dll") == 0)
            {
                Log("Skipping ReShade chainload from %s to avoid loading DerethFX recursively", chainPath);
            }
            else
            {
                g_realD3D9 = LoadLibraryA(chainPath);
                Log("LoadLibraryA(%s) [chain] -> %p", chainPath, g_realD3D9);
                if (g_realD3D9 != nullptr)
                    return g_realD3D9;
            }
        }

        char path[MAX_PATH] = {};
        GetSystemDirectoryA(path, MAX_PATH);
        strncat_s(path, "\\d3d9.dll", _TRUNCATE);

        g_realD3D9 = LoadLibraryA(path);
        Log("LoadLibraryA(%s) -> %p", path, g_realD3D9);
        return g_realD3D9;
    }

    FARPROC GetRealProc(const char* name)
    {
        auto module = LoadRealD3D9();
        if (module == nullptr)
            return nullptr;

        auto proc = GetProcAddress(module, name);
        if (proc == nullptr)
            Log("GetProcAddress(%s) failed: %lu", name, GetLastError());
        return proc;
    }

    struct FrameStats
    {
        unsigned frame = 0;
        unsigned beginScene = 0;
        unsigned endScene = 0;
        unsigned clears = 0;
        unsigned drawPrimitive = 0;
        unsigned drawIndexedPrimitive = 0;
        unsigned drawPrimitiveUP = 0;
        unsigned drawIndexedPrimitiveUP = 0;
        unsigned uiCandidateDraws = 0;
        unsigned worldCandidateDraws = 0;
        unsigned detailDraws = 0;
        unsigned waterCandidateDraws = 0;
        unsigned waterReflectDraws = 0;
        unsigned dynamicLightDraws = 0;
        DWORD zEnable = TRUE;
        DWORD zWriteEnable = TRUE;
        DWORD alphaBlend = FALSE;
        DWORD lighting = TRUE;
        DWORD fvf = 0;
        bool inScene = false;
    };

    FrameStats g_stats;
    void** g_deviceVtable = nullptr;
    bool g_deviceHooked = false;
    bool g_hasVertexShader = false;
    bool g_hasPixelShader = false;
    bool g_sampleDone = false;
    bool g_sampleNextFrame = false;
    bool g_sampleActive = false;
    unsigned g_sampleDraws = 0;
    unsigned g_drawOrdinal = 0;
    bool g_stage0TextureBound = false;
    bool g_textureDetailLogged = false;
    unsigned g_waterCandidateLogs = 0;
    bool g_waterReflectionLogged = false;
    IDirect3DTexture9* g_reflectionTexture = nullptr;
    IDirect3DTexture9* g_surfaceDetailTexture = nullptr;
    IDirect3DDevice9* g_lastDevice = nullptr;
    D3DMATRIX g_lastProjection {};
    bool g_hasLastProjection = false;
    bool g_dynamicLightingLogged = false;
    bool g_metalSheenLogged = false;
    bool g_fogLogged = false;
    bool g_farPlaneLogged = false;
    bool g_shadowPlaneLogged = false;
    bool g_surfaceDetailLogged = false;

    using ResetFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    using PresentFn = HRESULT (WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using CreateAdditionalSwapChainFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**);
    using SwapChainPresentFn = HRESULT (WINAPI*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
    using BeginSceneFn = HRESULT (WINAPI*)(IDirect3DDevice9*);
    using EndSceneFn = HRESULT (WINAPI*)(IDirect3DDevice9*);
    using ClearFn = HRESULT (WINAPI*)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
    using SetRenderStateFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
    using SetTextureFn = HRESULT (WINAPI*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
    using DrawPrimitiveFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
    using DrawIndexedPrimitiveFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
    using DrawPrimitiveUPFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
    using DrawIndexedPrimitiveUPFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
    using SetFVFFn = HRESULT (WINAPI*)(IDirect3DDevice9*, DWORD);
    using SetVertexShaderFn = HRESULT (WINAPI*)(IDirect3DDevice9*, IDirect3DVertexShader9*);
    using SetPixelShaderFn = HRESULT (WINAPI*)(IDirect3DDevice9*, IDirect3DPixelShader9*);

    ResetFn g_origReset = nullptr;
    PresentFn g_origPresent = nullptr;
    CreateAdditionalSwapChainFn g_origCreateAdditionalSwapChain = nullptr;
    SwapChainPresentFn g_origSwapChainPresent = nullptr;
    void** g_swapChainVtable = nullptr;
    BeginSceneFn g_origBeginScene = nullptr;
    EndSceneFn g_origEndScene = nullptr;
    ClearFn g_origClear = nullptr;
    SetRenderStateFn g_origSetRenderState = nullptr;
    using SetTransformFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
    SetTextureFn g_origSetTexture = nullptr;
    DrawPrimitiveFn g_origDrawPrimitive = nullptr;
    DrawIndexedPrimitiveFn g_origDrawIndexedPrimitive = nullptr;
    DrawPrimitiveUPFn g_origDrawPrimitiveUP = nullptr;
    DrawIndexedPrimitiveUPFn g_origDrawIndexedPrimitiveUP = nullptr;
    SetFVFFn g_origSetFVF = nullptr;
    SetVertexShaderFn g_origSetVertexShader = nullptr;
    SetPixelShaderFn g_origSetPixelShader = nullptr;

    enum ConfigControlId
    {
        IDC_TEXTURE_DETAIL = 2001,
        IDC_WATER_REFLECTION,
        IDC_DYNAMIC_LIGHTING,
        IDC_METAL_SHEEN,
        IDC_SURFACE_DETAIL,
        IDC_VOLUMETRIC_FOG,
        IDC_EXTEND_DISTANCE,
        IDC_WATER_STRENGTH,
        IDC_MIP_BIAS,
        IDC_SURFACE_STRENGTH,
        IDC_METAL_SPECULAR,
        IDC_METAL_POWER,
        IDC_FAR_PLANE,
        IDC_FOG_START,
        IDC_FOG_END,
        IDC_FOG_DENSITY,
        IDC_HUD_ENABLED,
        IDC_HUD_CRAWLER,
        IDC_HUD_IRONMAN,
        IDC_HUD_X,
        IDC_HUD_Y,
        IDC_SAVE_RELOAD,
        IDC_RELOAD,
        IDC_CLOSE
    };

    HWND g_configWindow = nullptr;
    bool g_configWindowClassRegistered = false;
    bool g_f9WasDown = false;
    HWND g_gameWindow = nullptr;
    HWND g_hudWindow = nullptr;
    bool g_hudWindowClassRegistered = false;
    bool g_f10WasDown = false;

    HRESULT SetConfiguredTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix);

    void PumpConfigWindowMessages()
    {
        if (g_configWindow == nullptr && g_hudWindow == nullptr)
            return;

        MSG msg {};
        while (g_configWindow != nullptr && PeekMessageA(&msg, g_configWindow, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        while (g_hudWindow != nullptr && PeekMessageA(&msg, g_hudWindow, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    void ReleaseGeneratedTextures()
    {
        if (g_reflectionTexture != nullptr)
        {
            g_reflectionTexture->Release();
            g_reflectionTexture = nullptr;
        }

        if (g_surfaceDetailTexture != nullptr)
        {
            g_surfaceDetailTexture->Release();
            g_surfaceDetailTexture = nullptr;
        }
    }

    void ReloadConfig()
    {
        ReleaseGeneratedTextures();
        g_config = Config {};
        g_configLoaded = false;
        g_textureDetailLogged = false;
        g_waterReflectionLogged = false;
        g_dynamicLightingLogged = false;
        g_metalSheenLogged = false;
        g_fogLogged = false;
        g_farPlaneLogged = false;
        g_shadowPlaneLogged = false;
        g_surfaceDetailLogged = false;
        LoadConfig();
        Log("config reloaded: texture=%d water=%d lighting=%d surface=%d fog=%d distance=%d hud=%d/%s waterStrength=%.2f farPlane=%.1f path=%s",
            g_config.textureDetail ? 1 : 0,
            g_config.waterReflection ? 1 : 0,
            g_config.dynamicLighting ? 1 : 0,
            g_config.surfaceDetail ? 1 : 0,
            g_config.volumetricFog ? 1 : 0,
            g_config.extendRenderDistance ? 1 : 0,
            g_config.hudEnabled ? 1 : 0,
            g_config.hudMode,
            g_config.waterStrength,
            g_config.farPlane,
            g_configPath);

        if (g_lastDevice != nullptr && g_hasLastProjection)
            SetConfiguredTransform(g_lastDevice, D3DTS_PROJECTION, &g_lastProjection);
    }

    void WriteIniBool(const char* section, const char* key, bool value)
    {
        WritePrivateProfileStringA(section, key, value ? "1" : "0", g_configPath);
    }

    void WriteIniFloat(const char* section, const char* key, float value)
    {
        char text[64] = {};
        std::snprintf(text, sizeof(text), "%.3f", value);
        WritePrivateProfileStringA(section, key, text, g_configPath);
    }

    void WriteIniInt(const char* section, const char* key, int value)
    {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "%d", value);
        WritePrivateProfileStringA(section, key, text, g_configPath);
    }

    void WriteIniString(const char* section, const char* key, const char* value)
    {
        WritePrivateProfileStringA(section, key, value, g_configPath);
    }

    void SetEditFloat(HWND window, int id, float value)
    {
        char text[64] = {};
        std::snprintf(text, sizeof(text), "%.3f", value);
        SetDlgItemTextA(window, id, text);
    }

    float GetEditFloat(HWND window, int id, float fallback)
    {
        char text[64] = {};
        GetDlgItemTextA(window, id, text, sizeof(text));
        if (text[0] == '\0')
            return fallback;
        return static_cast<float>(std::atof(text));
    }

    HWND AddControl(HWND parent, const char* cls, const char* text, DWORD style, int id, int x, int y, int w, int h)
    {
        return CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    }

    void AddLabel(HWND parent, const char* text, int x, int y)
    {
        AddControl(parent, "STATIC", text, 0, 0, x, y, 130, 20);
    }

    void FillConfigWindow(HWND window)
    {
        LoadConfig();
        CheckDlgButton(window, IDC_TEXTURE_DETAIL, g_config.textureDetail ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_WATER_REFLECTION, g_config.waterReflection ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_DYNAMIC_LIGHTING, g_config.dynamicLighting ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_METAL_SHEEN, g_config.metalSheen ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_SURFACE_DETAIL, g_config.surfaceDetail ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_VOLUMETRIC_FOG, g_config.volumetricFog ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_EXTEND_DISTANCE, g_config.extendRenderDistance ? BST_CHECKED : BST_UNCHECKED);
        SetEditFloat(window, IDC_WATER_STRENGTH, g_config.waterStrength);
        SetEditFloat(window, IDC_MIP_BIAS, g_config.detailMipBias);
        SetEditFloat(window, IDC_SURFACE_STRENGTH, g_config.surfaceDetailStrength);
        SetEditFloat(window, IDC_METAL_SPECULAR, g_config.metalSpecular);
        SetEditFloat(window, IDC_METAL_POWER, g_config.metalPower);
        SetEditFloat(window, IDC_FAR_PLANE, g_config.farPlane);
        SetEditFloat(window, IDC_FOG_START, g_config.fogStartScale);
        SetEditFloat(window, IDC_FOG_END, g_config.fogEndScale);
        SetEditFloat(window, IDC_FOG_DENSITY, g_config.fogDensityScale);
        CheckDlgButton(window, IDC_HUD_ENABLED, g_config.hudEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_HUD_CRAWLER, _stricmp(g_config.hudMode, "ironman") == 0 ? BST_UNCHECKED : BST_CHECKED);
        CheckDlgButton(window, IDC_HUD_IRONMAN, _stricmp(g_config.hudMode, "ironman") == 0 ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(window, IDC_HUD_X, static_cast<UINT>(g_config.hudX), FALSE);
        SetDlgItemInt(window, IDC_HUD_Y, static_cast<UINT>(g_config.hudY), FALSE);
    }

    void SaveConfigWindow(HWND window)
    {
        WriteIniBool("Effects", "textureDetail", IsDlgButtonChecked(window, IDC_TEXTURE_DETAIL) == BST_CHECKED);
        WriteIniBool("Effects", "waterReflection", IsDlgButtonChecked(window, IDC_WATER_REFLECTION) == BST_CHECKED);
        WriteIniBool("Effects", "dynamicLighting", IsDlgButtonChecked(window, IDC_DYNAMIC_LIGHTING) == BST_CHECKED);
        WriteIniBool("Effects", "metalSheen", IsDlgButtonChecked(window, IDC_METAL_SHEEN) == BST_CHECKED);
        WriteIniBool("Effects", "surfaceDetail", IsDlgButtonChecked(window, IDC_SURFACE_DETAIL) == BST_CHECKED);
        WriteIniBool("Effects", "volumetricFog", IsDlgButtonChecked(window, IDC_VOLUMETRIC_FOG) == BST_CHECKED);
        WriteIniBool("Effects", "extendRenderDistance", IsDlgButtonChecked(window, IDC_EXTEND_DISTANCE) == BST_CHECKED);
        WriteIniFloat("Water", "strength", GetEditFloat(window, IDC_WATER_STRENGTH, g_config.waterStrength));
        WriteIniFloat("TextureDetail", "mipBias", GetEditFloat(window, IDC_MIP_BIAS, g_config.detailMipBias));
        WriteIniFloat("SurfaceDetail", "strength", GetEditFloat(window, IDC_SURFACE_STRENGTH, g_config.surfaceDetailStrength));
        WriteIniFloat("Metal", "specular", GetEditFloat(window, IDC_METAL_SPECULAR, g_config.metalSpecular));
        WriteIniFloat("Metal", "power", GetEditFloat(window, IDC_METAL_POWER, g_config.metalPower));
        WriteIniFloat("RenderDistance", "farPlane", GetEditFloat(window, IDC_FAR_PLANE, g_config.farPlane));
        WriteIniFloat("Atmosphere", "fogStartScale", GetEditFloat(window, IDC_FOG_START, g_config.fogStartScale));
        WriteIniFloat("Atmosphere", "fogEndScale", GetEditFloat(window, IDC_FOG_END, g_config.fogEndScale));
        WriteIniFloat("Atmosphere", "fogDensityScale", GetEditFloat(window, IDC_FOG_DENSITY, g_config.fogDensityScale));
        WriteIniBool("HUD", "enabled", IsDlgButtonChecked(window, IDC_HUD_ENABLED) == BST_CHECKED);
        WriteIniString("HUD", "mode", IsDlgButtonChecked(window, IDC_HUD_IRONMAN) == BST_CHECKED ? "ironman" : "crawler");
        BOOL hudXOk = FALSE;
        BOOL hudYOk = FALSE;
        const UINT hudX = GetDlgItemInt(window, IDC_HUD_X, &hudXOk, FALSE);
        const UINT hudY = GetDlgItemInt(window, IDC_HUD_Y, &hudYOk, FALSE);
        if (hudXOk)
            WriteIniInt("HUD", "x", hudX);
        if (hudYOk)
            WriteIniInt("HUD", "y", hudY);
        ReloadConfig();
        FillConfigWindow(window);
    }


    const char* GetHudTitle()
    {
        return _stricmp(g_config.hudMode, "ironman") == 0 ? "IRONMAN" : "CRAWLER";
    }

    const char* GetHudSubtitle()
    {
        return _stricmp(g_config.hudMode, "ironman") == 0 ? "SELF-FOUND RUN" : "DUNGEON CRAWL";
    }

    COLORREF GetHudAccent()
    {
        return _stricmp(g_config.hudMode, "ironman") == 0 ? RGB(235, 92, 64) : RGB(84, 184, 255);
    }

    bool HasText(const char* text)
    {
        return text != nullptr && text[0] != '\0';
    }

    bool IsCrawlerHud()
    {
        return _stricmp(g_config.hudMode, "ironman") != 0;
    }

    int GetHudDetailLineCount()
    {
        if (!g_config.hudShowDetails || !IsCrawlerHud())
            return 0;
        int lines = 0;
        if (HasText(g_config.hudPerks)) lines++;
        if (HasText(g_config.hudBonuses)) lines++;
        if (HasText(g_config.hudProgress)) lines++;
        if (HasText(g_config.hudPending)) lines++;
        return lines;
    }

    int GetHudWidth()
    {
        return GetHudDetailLineCount() > 0 ? 330 : 190;
    }

    int GetHudHeight()
    {
        return GetHudDetailLineCount() > 0 ? 90 + GetHudDetailLineCount() * 18 : 78;
    }

    void DrawHudDetailLine(HDC dc, int& y, const char* label, const char* value, COLORREF labelColor)
    {
        if (!HasText(value))
            return;

        SetTextColor(dc, labelColor);
        HFONT labelFont = CreateFontA(11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, labelFont));
        RECT labelRect { 17, y, 82, y + 18 };
        DrawTextA(dc, label, -1, &labelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SetTextColor(dc, RGB(225, 229, 234));
        SelectObject(dc, oldFont);
        DeleteObject(labelFont);
        HFONT valueFont = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        oldFont = reinterpret_cast<HFONT>(SelectObject(dc, valueFont));
        RECT valueRect { 86, y, GetHudWidth() - 10, y + 18 };
        DrawTextA(dc, value, -1, &valueRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
        DeleteObject(valueFont);
        y += 18;
    }
    LRESULT CALLBACK HudWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        UNREFERENCED_PARAMETER(wparam);
        UNREFERENCED_PARAMETER(lparam);
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps {};
            HDC dc = BeginPaint(window, &ps);
            RECT client {};
            GetClientRect(window, &client);

            HBRUSH bg = CreateSolidBrush(RGB(7, 9, 12));
            FillRect(dc, &client, bg);
            DeleteObject(bg);

            RECT strip { 0, 0, 6, client.bottom };
            HBRUSH accentBrush = CreateSolidBrush(GetHudAccent());
            FillRect(dc, &strip, accentBrush);
            DeleteObject(accentBrush);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetHudAccent());
            HFONT titleFont = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, titleFont));
            RECT titleRect { 16, 8, client.right - 10, 34 };
            DrawTextA(dc, GetHudTitle(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            SetTextColor(dc, RGB(225, 229, 234));
            HFONT subFont = CreateFontA(13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            SelectObject(dc, subFont);
            RECT subRect { 17, 34, client.right - 10, 56 };
            DrawTextA(dc, GetHudSubtitle(), -1, &subRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            int detailY = 56;
            if (GetHudDetailLineCount() > 0)
            {
                DrawHudDetailLine(dc, detailY, "Perks", g_config.hudPerks, GetHudAccent());
                DrawHudDetailLine(dc, detailY, "Bonuses", g_config.hudBonuses, RGB(120, 210, 132));
                DrawHudDetailLine(dc, detailY, "Progress", g_config.hudProgress, RGB(240, 196, 90));
                DrawHudDetailLine(dc, detailY, "Pending", g_config.hudPending, RGB(208, 154, 255));
            }

            SetTextColor(dc, RGB(154, 162, 172));
            HFONT noteFont = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            SelectObject(dc, noteFont);
            RECT noteRect { 17, GetHudHeight() - 22, client.right - 10, GetHudHeight() - 2 };
            DrawTextA(dc, "F10 toggles mode", -1, &noteRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            SelectObject(dc, oldFont);
            DeleteObject(titleFont);
            DeleteObject(subFont);
            DeleteObject(noteFont);
            EndPaint(window, &ps);
            return 0;
        }
        case WM_DESTROY:
            g_hudWindow = nullptr;
            return 0;
        default:
            break;
        }
        return DefWindowProcA(window, message, wparam, lparam);
    }

    void EnsureHudWindow()
    {
        if (!g_hudWindowClassRegistered)
        {
            WNDCLASSA cls {};
            cls.lpfnWndProc = HudWindowProc;
            cls.hInstance = g_instance;
            cls.lpszClassName = "DerethFXHudWindow";
            cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassA(&cls);
            g_hudWindowClassRegistered = true;
        }

        if (g_hudWindow == nullptr)
        {
            g_hudWindow = CreateWindowExA(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                "DerethFXHudWindow",
                "DerethFX HUD",
                WS_POPUP,
                0, 0, GetHudWidth(), GetHudHeight(),
                nullptr,
                nullptr,
                g_instance,
                nullptr);
        }

        if (g_hudWindow != nullptr)
            SetLayeredWindowAttributes(g_hudWindow, 0, g_config.hudOpacity, LWA_ALPHA);
    }

    void HideHudWindow()
    {
        if (g_hudWindow != nullptr && IsWindowVisible(g_hudWindow))
            ShowWindow(g_hudWindow, SW_HIDE);
    }

    void UpdateHudWindow()
    {
        LoadConfig();
        if (!g_config.hudEnabled)
        {
            HideHudWindow();
            return;
        }

        EnsureHudWindow();
        RefreshHudStatusConfig();
        if (g_hudWindow == nullptr)
            return;

        HWND target = g_gameWindow != nullptr ? g_gameWindow : GetForegroundWindow();
        RECT rect {};
        POINT origin {};
        if (target != nullptr && GetClientRect(target, &rect))
        {
            ClientToScreen(target, &origin);
        }
        else
        {
            origin.x = 0;
            origin.y = 0;
        }

        SetWindowPos(g_hudWindow, HWND_TOPMOST, origin.x + g_config.hudX, origin.y + g_config.hudY,
            GetHudWidth(), GetHudHeight(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(g_hudWindow, nullptr, FALSE);
    }

    void CycleHudMode()
    {
        LoadConfig();
        if (!g_config.hudEnabled)
        {
            WriteIniBool("HUD", "enabled", true);
            WriteIniString("HUD", "mode", "crawler");
        }
        else if (_stricmp(g_config.hudMode, "crawler") == 0)
        {
            WriteIniString("HUD", "mode", "ironman");
        }
        else
        {
            WriteIniBool("HUD", "enabled", false);
        }

        ReloadConfig();
        if (g_configWindow != nullptr)
            FillConfigWindow(g_configWindow);
        UpdateHudWindow();
    }
    LRESULT CALLBACK ConfigWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        UNREFERENCED_PARAMETER(lparam);
        switch (message)
        {
        case WM_CREATE:
            AddControl(window, "BUTTON", "Texture detail", BS_AUTOCHECKBOX, IDC_TEXTURE_DETAIL, 16, 16, 150, 22);
            AddControl(window, "BUTTON", "Water reflection", BS_AUTOCHECKBOX, IDC_WATER_REFLECTION, 16, 42, 150, 22);
            AddControl(window, "BUTTON", "Dynamic lighting", BS_AUTOCHECKBOX, IDC_DYNAMIC_LIGHTING, 16, 68, 150, 22);
            AddControl(window, "BUTTON", "Metal sheen", BS_AUTOCHECKBOX, IDC_METAL_SHEEN, 16, 94, 150, 22);
            AddControl(window, "BUTTON", "Surface detail", BS_AUTOCHECKBOX, IDC_SURFACE_DETAIL, 16, 120, 150, 22);
            AddControl(window, "BUTTON", "Fog override", BS_AUTOCHECKBOX, IDC_VOLUMETRIC_FOG, 16, 146, 150, 22);
            AddControl(window, "BUTTON", "Extend distance", BS_AUTOCHECKBOX, IDC_EXTEND_DISTANCE, 16, 172, 150, 22);
            AddLabel(window, "Water strength", 190, 18);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_WATER_STRENGTH, 320, 16, 80, 22);
            AddLabel(window, "Mip bias", 190, 46);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_MIP_BIAS, 320, 44, 80, 22);
            AddLabel(window, "Surface strength", 190, 74);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_SURFACE_STRENGTH, 320, 72, 80, 22);
            AddLabel(window, "Metal specular", 190, 102);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_METAL_SPECULAR, 320, 100, 80, 22);
            AddLabel(window, "Metal power", 190, 130);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_METAL_POWER, 320, 128, 80, 22);
            AddLabel(window, "Far plane", 190, 158);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_FAR_PLANE, 320, 156, 80, 22);
            AddLabel(window, "Fog start scale", 190, 186);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_FOG_START, 320, 184, 80, 22);
            AddLabel(window, "Fog end scale", 190, 214);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_FOG_END, 320, 212, 80, 22);
            AddLabel(window, "Fog density", 190, 242);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_FOG_DENSITY, 320, 240, 80, 22);
            AddControl(window, "BUTTON", "HUD overlay", BS_AUTOCHECKBOX, IDC_HUD_ENABLED, 16, 206, 150, 22);
            AddControl(window, "BUTTON", "Crawler", BS_AUTORADIOBUTTON, IDC_HUD_CRAWLER, 16, 232, 90, 22);
            AddControl(window, "BUTTON", "Ironman", BS_AUTORADIOBUTTON, IDC_HUD_IRONMAN, 104, 232, 90, 22);
            AddLabel(window, "HUD x", 190, 270);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_HUD_X, 320, 268, 80, 22);
            AddLabel(window, "HUD y", 190, 298);
            AddControl(window, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL, IDC_HUD_Y, 320, 296, 80, 22);
            AddControl(window, "BUTTON", "Save + Reload", BS_PUSHBUTTON, IDC_SAVE_RELOAD, 16, 334, 120, 28);
            AddControl(window, "BUTTON", "Reload", BS_PUSHBUTTON, IDC_RELOAD, 150, 334, 90, 28);
            AddControl(window, "BUTTON", "Close", BS_PUSHBUTTON, IDC_CLOSE, 310, 334, 90, 28);
            FillConfigWindow(window);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wparam))
            {
            case IDC_SAVE_RELOAD:
                SaveConfigWindow(window);
                return 0;
            case IDC_RELOAD:
                ReloadConfig();
                FillConfigWindow(window);
                return 0;
            case IDC_CLOSE:
                ShowWindow(window, SW_HIDE);
                return 0;
            default:
                break;
            }
            break;
        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_configWindow = nullptr;
            return 0;
        default:
            break;
        }
        return DefWindowProcA(window, message, wparam, lparam);
    }

    void ShowConfigWindow()
    {
        if (!g_configWindowClassRegistered)
        {
            WNDCLASSA cls {};
            cls.lpfnWndProc = ConfigWindowProc;
            cls.hInstance = g_instance;
            cls.lpszClassName = "DerethFXConfigWindow";
            cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
            cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            RegisterClassA(&cls);
            g_configWindowClassRegistered = true;
        }

        if (g_configWindow == nullptr)
        {
            g_configWindow = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "DerethFXConfigWindow", "DerethFX Config",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT, 430, 420, nullptr, nullptr, g_instance, nullptr);
        }

        FillConfigWindow(g_configWindow);
        ShowWindow(g_configWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_configWindow);
    }

    void ToggleConfigWindow()
    {
        if (g_configWindow != nullptr && IsWindowVisible(g_configWindow))
            ShowWindow(g_configWindow, SW_HIDE);
        else
            ShowConfigWindow();
    }

    void PollConfigHotkey()
    {
        PumpConfigWindowMessages();

        const bool f9Down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (f9Down && !g_f9WasDown)
        {
            Log("F9 config hotkey pressed");
            ToggleConfigWindow();
        }
        g_f9WasDown = f9Down;

        const bool f10Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        if (f10Down && !g_f10WasDown)
        {
            Log("F10 HUD hotkey pressed");
            CycleHudMode();
        }
        g_f10WasDown = f10Down;
    }

    bool LooksLikeUiDraw(UINT primitiveCount)
    {
        const bool fixedFunctionOverlay =
            g_stats.zEnable == FALSE
            && g_stats.alphaBlend == TRUE
            && g_stats.lighting == FALSE
            && !g_hasVertexShader
            && !g_hasPixelShader;

        const bool tinyQuadBatch = primitiveCount > 0 && primitiveCount <= 8;
        return fixedFunctionOverlay && tinyQuadBatch;
    }
    void CountDraw(bool up, UINT primitiveCount = 0, const char* method = "draw")
    {
        g_drawOrdinal++;
        if (g_sampleNextFrame && g_drawOrdinal == 1)
        {
            g_sampleNextFrame = false;
            g_sampleActive = true;
            g_sampleDraws = 0;
            DebugLog("sample-start frame=%u", g_stats.frame);
        }

        if (LooksLikeUiDraw(primitiveCount))
            g_stats.uiCandidateDraws++;
        else
            g_stats.worldCandidateDraws++;

        if (g_sampleActive && g_sampleDraws < 220)
        {
            DebugLog("sample frame=%u draw=%u method=%s prim=%u fvf=0x%08lx z=%lu zw=%lu alpha=%lu lighting=%lu vs=%d ps=%d up=%d uiGuess=%d",
                g_stats.frame,
                g_drawOrdinal,
                method,
                primitiveCount,
                g_stats.fvf,
                g_stats.zEnable,
                g_stats.zWriteEnable,
                g_stats.alphaBlend,
                g_stats.lighting,
                g_hasVertexShader ? 1 : 0,
                g_hasPixelShader ? 1 : 0,
                up ? 1 : 0,
                LooksLikeUiDraw(primitiveCount) ? 1 : 0);
            g_sampleDraws++;
        }
    }
    DWORD FloatAsDword(float value)
    {
        DWORD bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "float/DWORD size mismatch");
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }


    float DwordAsFloat(DWORD value)
    {
        float result = 0.0f;
        static_assert(sizeof(result) == sizeof(value), "float/DWORD size mismatch");
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    bool LooksLikePerspectiveProjection(const D3DMATRIX& matrix)
    {
        return std::fabs(matrix._34) > 0.90f
            && std::fabs(matrix._34) < 1.10f
            && std::fabs(matrix._44) < 0.001f
            && matrix._33 > 1.0f
            && matrix._43 < 0.0f;
    }

    bool ExtendFarPlane(D3DMATRIX& matrix)
    {
        if (!g_config.extendRenderDistance || !LooksLikePerspectiveProjection(matrix))
            return false;

        const float nearPlane = -matrix._43 / matrix._33;
        const float currentFarPlane = -matrix._43 / (matrix._33 - 1.0f);
        const float requestedFarPlane = g_config.farPlane;
        if (nearPlane <= 0.0f || currentFarPlane <= 0.0f || requestedFarPlane <= currentFarPlane)
            return false;

        matrix._33 = requestedFarPlane / (requestedFarPlane - nearPlane);
        matrix._43 = -nearPlane * requestedFarPlane / (requestedFarPlane - nearPlane);

        if (!g_farPlaneLogged)
        {
            g_farPlaneLogged = true;
            Log("render-distance enabled: farPlane %.1f -> %.1f near=%.3f", currentFarPlane, requestedFarPlane, nearPlane);
        }
        return true;
    }

    DWORD AdjustFogRenderState(D3DRENDERSTATETYPE state, DWORD value)
    {
        if (!g_config.volumetricFog)
            return value;

        switch (state)
        {
        case D3DRS_RANGEFOGENABLE:
            return TRUE;
        case D3DRS_FOGTABLEMODE:
            return D3DFOG_EXP2;
        case D3DRS_FOGVERTEXMODE:
            return D3DFOG_LINEAR;
        case D3DRS_FOGSTART:
            return FloatAsDword(DwordAsFloat(value) * g_config.fogStartScale);
        case D3DRS_FOGEND:
        {
            float fogEnd = DwordAsFloat(value) * g_config.fogEndScale;
            if (fogEnd < g_config.minFogEnd)
                fogEnd = g_config.minFogEnd;
            return FloatAsDword(fogEnd);
        }
        case D3DRS_FOGDENSITY:
            return FloatAsDword(DwordAsFloat(value) * g_config.fogDensityScale);
        default:
            return value;
        }
    }

    void TrackRenderState(D3DRENDERSTATETYPE state, DWORD value)
    {
        if (state == D3DRS_ZENABLE)
            g_stats.zEnable = value;
        else if (state == D3DRS_ZWRITEENABLE)
            g_stats.zWriteEnable = value;
        else if (state == D3DRS_ALPHABLENDENABLE)
            g_stats.alphaBlend = value;
        else if (state == D3DRS_LIGHTING)
            g_stats.lighting = value;
    }

    HRESULT SetConfiguredRenderState(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value)
    {
        TrackRenderState(state, value);
        const DWORD adjusted = AdjustFogRenderState(state, value);
        if (adjusted != value && !g_fogLogged)
        {
            g_fogLogged = true;
            Log("atmosphere enabled: fogStartScale=%.2f fogEndScale=%.2f fogDensityScale=%.2f minFogEnd=%.1f",
                g_config.fogStartScale,
                g_config.fogEndScale,
                g_config.fogDensityScale,
                g_config.minFogEnd);
        }
        return device->SetRenderState(state, adjusted);
    }

    HRESULT SetConfiguredTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix)
    {
        if (device == nullptr)
            return D3DERR_INVALIDCALL;

        g_lastDevice = device;
        if (matrix != nullptr && state == D3DTS_PROJECTION)
        {
            g_lastProjection = *matrix;
            g_hasLastProjection = true;
        }

        if (matrix == nullptr || state != D3DTS_PROJECTION || !g_config.extendRenderDistance)
            return device->SetTransform(state, matrix);

        D3DMATRIX adjusted = *matrix;
        if (ExtendFarPlane(adjusted))
            return device->SetTransform(state, &adjusted);
        return device->SetTransform(state, matrix);
    }

    bool LooksLikeSoftAlphaDraw(UINT primitiveCount, bool up)
    {
        return g_config.protectAlphaLights
            && !up
            && primitiveCount >= 1
            && g_stats.alphaBlend == TRUE
            && g_stats.zWriteEnable == FALSE
            && g_stats.zEnable != FALSE
            && !LooksLikeUiDraw(primitiveCount);
    }
    struct TextureDetailState
    {
        DWORD minFilter = D3DTEXF_POINT;
        DWORD magFilter = D3DTEXF_POINT;
        DWORD mipFilter = D3DTEXF_NONE;
        DWORD maxAniso = 1;
        DWORD mipBias = 0;
        bool active = false;
    };

    bool ShouldApplyTextureDetail(UINT primitiveCount, bool up)
    {
        return g_config.textureDetail
            && !up
            && primitiveCount >= g_config.detailMinPrimitiveCount
            && g_stage0TextureBound
            && !g_hasPixelShader
            && !LooksLikeUiDraw(primitiveCount)
            && !LooksLikeSoftAlphaDraw(primitiveCount, up)
            && g_stats.zEnable != FALSE;
    }

    TextureDetailState ApplyTextureDetail(IDirect3DDevice9* device, UINT primitiveCount, bool up)
    {
        TextureDetailState state {};
        if (device == nullptr || !ShouldApplyTextureDetail(primitiveCount, up))
            return state;

        if (FAILED(device->GetSamplerState(0, D3DSAMP_MINFILTER, &state.minFilter))
            || FAILED(device->GetSamplerState(0, D3DSAMP_MAGFILTER, &state.magFilter))
            || FAILED(device->GetSamplerState(0, D3DSAMP_MIPFILTER, &state.mipFilter))
            || FAILED(device->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &state.maxAniso))
            || FAILED(device->GetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, &state.mipBias)))
            return state;

        state.active = true;
        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, g_config.detailAnisotropy);
        device->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, FloatAsDword(g_config.detailMipBias));
        g_stats.detailDraws++;

        if (!g_textureDetailLogged)
        {
            g_textureDetailLogged = true;
            Log("texture-detail enabled: anisotropic=%lu mipBias=%.2f world-only", g_config.detailAnisotropy, g_config.detailMipBias);
        }

        return state;
    }

    void RestoreTextureDetail(IDirect3DDevice9* device, const TextureDetailState& state)
    {
        if (device == nullptr || !state.active)
            return;

        device->SetSamplerState(0, D3DSAMP_MINFILTER, state.minFilter);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, state.magFilter);
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, state.mipFilter);
        device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, state.maxAniso);
        device->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, state.mipBias);
    }
    bool TextureMatchesWater(IDirect3DDevice9* device)
    {
        if (device == nullptr)
            return false;

        IDirect3DBaseTexture9* baseTexture = nullptr;
        if (FAILED(device->GetTexture(0, &baseTexture)) || baseTexture == nullptr)
            return false;

        bool matches = false;
        IDirect3DTexture9* texture2d = nullptr;
        if (SUCCEEDED(baseTexture->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&texture2d))) && texture2d != nullptr)
        {
            D3DSURFACE_DESC desc {};
            if (SUCCEEDED(texture2d->GetLevelDesc(0, &desc)))
                matches = desc.Width == 32 && desc.Height == 32 && desc.Format == static_cast<D3DFORMAT>(0x16);
            texture2d->Release();
        }
        baseTexture->Release();
        return matches;
    }

    bool LooksLikeWaterDraw(IDirect3DDevice9* device, UINT primitiveCount, bool up)
    {
        UNREFERENCED_PARAMETER(device);
        return g_config.waterReflection
            && !up
            && primitiveCount >= g_config.waterMinPrimitiveCount
            && primitiveCount <= g_config.waterMaxPrimitiveCount
            && g_stage0TextureBound
            && !LooksLikeUiDraw(primitiveCount)
            && g_stats.zEnable != FALSE
            && g_stats.zWriteEnable == FALSE
            && g_stats.alphaBlend == TRUE
            && g_stats.lighting == TRUE;
    }

    IDirect3DTexture9* GetReflectionTexture(IDirect3DDevice9* device)
    {
        if (g_reflectionTexture != nullptr)
            return g_reflectionTexture;
        if (device == nullptr)
            return nullptr;

        constexpr UINT Size = 64;
        if (FAILED(device->CreateTexture(Size, Size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_reflectionTexture, nullptr)) || g_reflectionTexture == nullptr)
            return nullptr;

        D3DLOCKED_RECT lock {};
        if (SUCCEEDED(g_reflectionTexture->LockRect(0, &lock, nullptr, 0)))
        {
            for (UINT y = 0; y < Size; ++y)
            {
                auto* row = reinterpret_cast<DWORD*>(static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch);
                for (UINT x = 0; x < Size; ++x)
                {
                    const float fx = static_cast<float>(x) / static_cast<float>(Size - 1);
                    const float fy = static_cast<float>(y) / static_cast<float>(Size - 1);
                    const float streak = ((x + y) % 17) < 3 ? 1.0f : 0.0f;
                    const BYTE a = static_cast<BYTE>(72.0f * g_config.waterStrength);
                    const BYTE r = static_cast<BYTE>(42 + 74 * (1.0f - fy) + 38 * streak);
                    const BYTE g = static_cast<BYTE>(112 + 78 * (1.0f - fy) + 36 * streak);
                    const BYTE b = static_cast<BYTE>(154 + 82 * fx + 42 * streak);
                    row[x] = D3DCOLOR_ARGB(a, r, g, b);
                }
            }
            g_reflectionTexture->UnlockRect(0);
        }

        return g_reflectionTexture;
    }

    struct WaterReflectionState
    {
        IDirect3DBaseTexture9* stage1Texture = nullptr;
        DWORD colorOp = D3DTOP_DISABLE;
        DWORD colorArg1 = D3DTA_TEXTURE;
        DWORD colorArg2 = D3DTA_CURRENT;
        DWORD alphaOp = D3DTOP_DISABLE;
        DWORD texCoordIndex = 1;
        DWORD transformFlags = D3DTTFF_DISABLE;
        DWORD minFilter = D3DTEXF_POINT;
        DWORD magFilter = D3DTEXF_POINT;
        DWORD addressU = D3DTADDRESS_WRAP;
        DWORD addressV = D3DTADDRESS_WRAP;
        bool active = false;
    };

    WaterReflectionState ApplyWaterReflection(IDirect3DDevice9* device, UINT primitiveCount, bool up)
    {
        WaterReflectionState state {};
        if (!LooksLikeWaterDraw(device, primitiveCount, up))
            return state;

        auto* reflection = GetReflectionTexture(device);
        if (reflection == nullptr)
            return state;

        if (FAILED(device->GetTexture(1, &state.stage1Texture))
            || FAILED(device->GetTextureStageState(1, D3DTSS_COLOROP, &state.colorOp))
            || FAILED(device->GetTextureStageState(1, D3DTSS_COLORARG1, &state.colorArg1))
            || FAILED(device->GetTextureStageState(1, D3DTSS_COLORARG2, &state.colorArg2))
            || FAILED(device->GetTextureStageState(1, D3DTSS_ALPHAOP, &state.alphaOp))
            || FAILED(device->GetTextureStageState(1, D3DTSS_TEXCOORDINDEX, &state.texCoordIndex))
            || FAILED(device->GetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, &state.transformFlags))
            || FAILED(device->GetSamplerState(1, D3DSAMP_MINFILTER, &state.minFilter))
            || FAILED(device->GetSamplerState(1, D3DSAMP_MAGFILTER, &state.magFilter))
            || FAILED(device->GetSamplerState(1, D3DSAMP_ADDRESSU, &state.addressU))
            || FAILED(device->GetSamplerState(1, D3DSAMP_ADDRESSV, &state.addressV)))
        {
            if (state.stage1Texture != nullptr)
            {
                state.stage1Texture->Release();
                state.stage1Texture = nullptr;
            }
            return state;
        }

        state.active = true;
        device->SetTexture(1, reflection);
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADDSIGNED2X);
        device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        device->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        device->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        device->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        g_stats.waterReflectDraws++;

        if (!g_waterReflectionLogged)
        {
            g_waterReflectionLogged = true;
            Log("water-reflection enabled: tuned indexed water-like stage1 reflection overlay strength=%.2f", g_config.waterStrength);
        }

        return state;
    }

    void RestoreWaterReflection(IDirect3DDevice9* device, const WaterReflectionState& state)
    {
        if (device == nullptr || !state.active)
            return;

        device->SetTexture(1, state.stage1Texture);
        device->SetTextureStageState(1, D3DTSS_COLOROP, state.colorOp);
        device->SetTextureStageState(1, D3DTSS_COLORARG1, state.colorArg1);
        device->SetTextureStageState(1, D3DTSS_COLORARG2, state.colorArg2);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, state.alphaOp);
        device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, state.texCoordIndex);
        device->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, state.transformFlags);
        device->SetSamplerState(1, D3DSAMP_MINFILTER, state.minFilter);
        device->SetSamplerState(1, D3DSAMP_MAGFILTER, state.magFilter);
        device->SetSamplerState(1, D3DSAMP_ADDRESSU, state.addressU);
        device->SetSamplerState(1, D3DSAMP_ADDRESSV, state.addressV);

        if (state.stage1Texture != nullptr)
            state.stage1Texture->Release();
    }


    bool ShouldApplySurfaceDetail(UINT primitiveCount, bool up)
    {
        return g_config.surfaceDetail
            && g_config.surfaceDetailStrength > 0.0f
            && !up
            && primitiveCount >= g_config.detailMinPrimitiveCount
            && g_stage0TextureBound
            && !g_hasPixelShader
            && !LooksLikeUiDraw(primitiveCount)
            && !LooksLikeSoftAlphaDraw(primitiveCount, up)
            && !LooksLikeWaterDraw(nullptr, primitiveCount, up)
            && g_stats.alphaBlend == FALSE
            && g_stats.zEnable != FALSE;
    }

    struct SurfaceDetailState
    {
        IDirect3DBaseTexture9* stage0Texture = nullptr;
        IDirect3DBaseTexture9* stage1Texture = nullptr;
        DWORD colorOp = D3DTOP_DISABLE;
        DWORD colorArg1 = D3DTA_TEXTURE;
        DWORD colorArg2 = D3DTA_CURRENT;
        DWORD alphaOp = D3DTOP_DISABLE;
        DWORD texCoordIndex = 1;
        DWORD transformFlags = D3DTTFF_DISABLE;
        DWORD minFilter = D3DTEXF_POINT;
        DWORD magFilter = D3DTEXF_POINT;
        DWORD addressU = D3DTADDRESS_WRAP;
        DWORD addressV = D3DTADDRESS_WRAP;
        bool active = false;
    };

    SurfaceDetailState ApplySurfaceDetail(IDirect3DDevice9* device, UINT primitiveCount, bool up)
    {
        SurfaceDetailState state {};
        if (device == nullptr || !ShouldApplySurfaceDetail(primitiveCount, up))
            return state;

        if (FAILED(device->GetTexture(0, &state.stage0Texture)) || state.stage0Texture == nullptr)
            return state;

        if (FAILED(device->GetTexture(1, &state.stage1Texture))
            || FAILED(device->GetTextureStageState(1, D3DTSS_COLOROP, &state.colorOp))
            || FAILED(device->GetTextureStageState(1, D3DTSS_COLORARG1, &state.colorArg1))
            || FAILED(device->GetTextureStageState(1, D3DTSS_COLORARG2, &state.colorArg2))
            || FAILED(device->GetTextureStageState(1, D3DTSS_ALPHAOP, &state.alphaOp))
            || FAILED(device->GetTextureStageState(1, D3DTSS_TEXCOORDINDEX, &state.texCoordIndex))
            || FAILED(device->GetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, &state.transformFlags))
            || FAILED(device->GetSamplerState(1, D3DSAMP_MINFILTER, &state.minFilter))
            || FAILED(device->GetSamplerState(1, D3DSAMP_MAGFILTER, &state.magFilter))
            || FAILED(device->GetSamplerState(1, D3DSAMP_ADDRESSU, &state.addressU))
            || FAILED(device->GetSamplerState(1, D3DSAMP_ADDRESSV, &state.addressV)))
        {
            if (state.stage1Texture != nullptr)
                state.stage1Texture->Release();
            if (state.stage0Texture != nullptr)
                state.stage0Texture->Release();
            state.stage0Texture = nullptr;
            return state;
        }

        state.active = true;
        DWORD colorOp = D3DTOP_ADDSMOOTH;
        if (g_config.surfaceDetailStrength >= 0.75f)
            colorOp = D3DTOP_ADDSIGNED;
        else if (g_config.surfaceDetailStrength >= 0.50f)
            colorOp = D3DTOP_MODULATE2X;

        device->SetTexture(1, state.stage0Texture);
        device->SetTextureStageState(1, D3DTSS_COLOROP, colorOp);
        device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        device->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        device->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        device->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

        if (!g_surfaceDetailLogged)
        {
            g_surfaceDetailLogged = true;
            Log("surface-detail enabled: same-texture modulation strength=%.2f op=%s", g_config.surfaceDetailStrength, colorOp == D3DTOP_ADDSIGNED ? "addsigned" : (colorOp == D3DTOP_MODULATE2X ? "modulate2x" : "addsmooth"));
        }
        return state;
    }

    void RestoreSurfaceDetail(IDirect3DDevice9* device, const SurfaceDetailState& state)
    {
        if (device == nullptr || !state.active)
            return;
        device->SetTexture(1, state.stage1Texture);
        device->SetTextureStageState(1, D3DTSS_COLOROP, state.colorOp);
        device->SetTextureStageState(1, D3DTSS_COLORARG1, state.colorArg1);
        device->SetTextureStageState(1, D3DTSS_COLORARG2, state.colorArg2);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, state.alphaOp);
        device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, state.texCoordIndex);
        device->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, state.transformFlags);
        device->SetSamplerState(1, D3DSAMP_MINFILTER, state.minFilter);
        device->SetSamplerState(1, D3DSAMP_MAGFILTER, state.magFilter);
        device->SetSamplerState(1, D3DSAMP_ADDRESSU, state.addressU);
        device->SetSamplerState(1, D3DSAMP_ADDRESSV, state.addressV);
        if (state.stage1Texture != nullptr)
            state.stage1Texture->Release();
        if (state.stage0Texture != nullptr)
            state.stage0Texture->Release();
    }

    bool ShouldSuppressShadowPlane(UINT primitiveCount, bool up)
    {
        UNREFERENCED_PARAMETER(up);
        const bool smallAlphaWorldQuad = primitiveCount >= 1
            && primitiveCount <= g_config.shadowMaxPrimitiveCount
            && g_stage0TextureBound
            && g_stats.alphaBlend == TRUE
            && g_stats.zWriteEnable == FALSE
            && g_stats.zEnable != FALSE
            && !LooksLikeUiDraw(primitiveCount)
            && !LooksLikeWaterDraw(nullptr, primitiveCount, false);

        if (!g_config.suppressShadowPlanes || !smallAlphaWorldQuad)
            return false;

        if (!g_shadowPlaneLogged)
        {
            g_shadowPlaneLogged = true;
            Log("shadow-plane suppression enabled: skipped alpha world quads prims<=%u", g_config.shadowMaxPrimitiveCount);
        }
        return true;
    }
    struct SoftAlphaState
    {
        DWORD alphaTest = FALSE;
        DWORD alphaFunc = D3DCMP_ALWAYS;
        DWORD alphaRef = 0;
        DWORD srcBlend = D3DBLEND_SRCALPHA;
        DWORD destBlend = D3DBLEND_INVSRCALPHA;
        bool active = false;
    };

    SoftAlphaState ApplySoftAlphaFix(IDirect3DDevice9* device, UINT primitiveCount, bool up)
    {
        SoftAlphaState state {};
        if (device == nullptr || !g_config.softAlphaFix || !LooksLikeSoftAlphaDraw(primitiveCount, up) || LooksLikeWaterDraw(device, primitiveCount, up))
            return state;

        if (FAILED(device->GetRenderState(D3DRS_ALPHATESTENABLE, &state.alphaTest))
            || FAILED(device->GetRenderState(D3DRS_ALPHAFUNC, &state.alphaFunc))
            || FAILED(device->GetRenderState(D3DRS_ALPHAREF, &state.alphaRef))
            || FAILED(device->GetRenderState(D3DRS_SRCBLEND, &state.srcBlend))
            || FAILED(device->GetRenderState(D3DRS_DESTBLEND, &state.destBlend)))
            return state;

        state.active = true;
        device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        device->SetRenderState(D3DRS_ALPHAREF, g_config.softAlphaRef);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        return state;
    }

    void RestoreSoftAlphaFix(IDirect3DDevice9* device, const SoftAlphaState& state)
    {
        if (device == nullptr || !state.active)
            return;

        device->SetRenderState(D3DRS_ALPHATESTENABLE, state.alphaTest);
        device->SetRenderState(D3DRS_ALPHAFUNC, state.alphaFunc);
        device->SetRenderState(D3DRS_ALPHAREF, state.alphaRef);
        device->SetRenderState(D3DRS_SRCBLEND, state.srcBlend);
        device->SetRenderState(D3DRS_DESTBLEND, state.destBlend);
    }
    struct DynamicLightingState
    {
        DWORD ambient = 0;
        DWORD specularEnable = FALSE;
        DWORD normalizeNormals = FALSE;
        DWORD localViewer = FALSE;
        DWORD colorVertex = TRUE;
        D3DMATERIAL9 material {};
        D3DLIGHT9 light {};
        BOOL lightEnabled = FALSE;
        bool hadMaterial = false;
        bool hadLight = false;
        bool active = false;
    };

    bool ShouldApplyDynamicLighting(UINT primitiveCount, bool up)
    {
        return g_config.dynamicLighting
            && !up
            && primitiveCount >= g_config.lightMinPrimitiveCount
            && primitiveCount <= g_config.lightMaxPrimitiveCount
            && !g_hasPixelShader
            && !LooksLikeUiDraw(primitiveCount)
            && !LooksLikeWaterDraw(nullptr, primitiveCount, up)
            && !LooksLikeSoftAlphaDraw(primitiveCount, up)
            && g_stats.zEnable != FALSE
            && g_stats.lighting == TRUE;
    }

    DynamicLightingState ApplyDynamicLighting(IDirect3DDevice9* device, UINT primitiveCount, bool up)
    {
        DynamicLightingState state {};
        if (device == nullptr || !ShouldApplyDynamicLighting(primitiveCount, up))
            return state;

        if (FAILED(device->GetRenderState(D3DRS_AMBIENT, &state.ambient))
            || FAILED(device->GetRenderState(D3DRS_SPECULARENABLE, &state.specularEnable))
            || FAILED(device->GetRenderState(D3DRS_NORMALIZENORMALS, &state.normalizeNormals))
            || FAILED(device->GetRenderState(D3DRS_LOCALVIEWER, &state.localViewer))
            || FAILED(device->GetRenderState(D3DRS_COLORVERTEX, &state.colorVertex)))
            return state;

        state.hadMaterial = SUCCEEDED(device->GetMaterial(&state.material));
        state.hadLight = SUCCEEDED(device->GetLight(7, &state.light));
        device->GetLightEnable(7, &state.lightEnabled);

        state.active = true;
        device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(g_config.ambientR, g_config.ambientG, g_config.ambientB));
        device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
        device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        device->SetRenderState(D3DRS_LOCALVIEWER, TRUE);
        device->SetRenderState(D3DRS_COLORVERTEX, TRUE);

        if (state.hadMaterial)
        {
            D3DMATERIAL9 material = state.material;
            if (material.Specular.r < g_config.materialSpecular) material.Specular.r = g_config.materialSpecular;
            if (material.Specular.g < g_config.materialSpecular) material.Specular.g = g_config.materialSpecular;
            if (material.Specular.b < g_config.materialSpecularBlue) material.Specular.b = g_config.materialSpecularBlue;
            if (material.Power < g_config.materialPower) material.Power = g_config.materialPower;
            device->SetMaterial(&material);
        }

        const float seconds = static_cast<float>(GetTickCount() & 0x00ffffff) * 0.001f;
        float flicker = 1.0f + g_config.lightFlickerStrength * (0.65f * std::sin(seconds * 5.1f) + 0.35f * std::sin(seconds * 9.7f));
        if (flicker < 0.70f) flicker = 0.70f;
        if (flicker > 1.30f) flicker = 1.30f;
        const float sway = g_config.lightMotionStrength;

        D3DLIGHT9 light {};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse.r = 0.22f * g_config.diffuseStrength * flicker;
        light.Diffuse.g = 0.24f * g_config.diffuseStrength * flicker;
        light.Diffuse.b = 0.28f * g_config.diffuseStrength * flicker;
        light.Specular.r = 0.45f * g_config.specularStrength * flicker;
        light.Specular.g = 0.48f * g_config.specularStrength * flicker;
        light.Specular.b = 0.55f * g_config.specularStrength * flicker;
        light.Ambient.r = 0.03f;
        light.Ambient.g = 0.035f;
        light.Ambient.b = 0.045f;
        light.Direction.x = -0.35f + sway * 0.35f * std::sin(seconds * 0.73f);
        light.Direction.y = -0.65f + sway * 0.18f * std::sin(seconds * 0.49f);
        light.Direction.z = 0.45f + sway * 0.30f * std::sin(seconds * 0.61f);
        device->SetLight(7, &light);
        device->LightEnable(7, TRUE);
        g_stats.dynamicLightDraws++;

        if (!g_dynamicLightingLogged)
        {
            g_dynamicLightingLogged = true;
            Log("dynamic-lighting enabled: ambient/specular/headlight object-gated prims=%u-%u ambient=%u,%u,%u diffuse=%.2f specular=%.2f flicker=%.2f motion=%.2f alphaProtect=%d", g_config.lightMinPrimitiveCount, g_config.lightMaxPrimitiveCount, g_config.ambientR, g_config.ambientG, g_config.ambientB, g_config.diffuseStrength, g_config.specularStrength, g_config.lightFlickerStrength, g_config.lightMotionStrength, g_config.protectAlphaLights ? 1 : 0);
        }

        return state;
    }

    void RestoreDynamicLighting(IDirect3DDevice9* device, const DynamicLightingState& state)
    {
        if (device == nullptr || !state.active)
            return;

        device->SetRenderState(D3DRS_AMBIENT, state.ambient);
        device->SetRenderState(D3DRS_SPECULARENABLE, state.specularEnable);
        device->SetRenderState(D3DRS_NORMALIZENORMALS, state.normalizeNormals);
        device->SetRenderState(D3DRS_LOCALVIEWER, state.localViewer);
        device->SetRenderState(D3DRS_COLORVERTEX, state.colorVertex);

        if (state.hadMaterial)
            device->SetMaterial(&state.material);
        if (state.hadLight)
            device->SetLight(7, &state.light);
        device->LightEnable(7, state.lightEnabled);
    }

    struct MetalSheenState
    {
        DWORD specularEnable = FALSE;
        DWORD normalizeNormals = FALSE;
        DWORD localViewer = FALSE;
        D3DMATERIAL9 material {};
        bool hadMaterial = false;
        bool active = false;
    };

    bool ShouldApplyMetalSheen(UINT primitiveCount, bool up)
    {
        return g_config.metalSheen
            && !up
            && primitiveCount >= g_config.metalMinPrimitiveCount
            && primitiveCount <= g_config.metalMaxPrimitiveCount
            && g_stage0TextureBound
            && !g_hasPixelShader
            && !LooksLikeUiDraw(primitiveCount)
            && !LooksLikeWaterDraw(nullptr, primitiveCount, up)
            && !LooksLikeSoftAlphaDraw(primitiveCount, up)
            && g_stats.alphaBlend == FALSE
            && g_stats.zEnable != FALSE
            && g_stats.lighting == TRUE;
    }

    MetalSheenState ApplyMetalSheen(IDirect3DDevice9* device, UINT primitiveCount, bool up)
    {
        MetalSheenState state {};
        if (device == nullptr || !ShouldApplyMetalSheen(primitiveCount, up))
            return state;

        if (FAILED(device->GetRenderState(D3DRS_SPECULARENABLE, &state.specularEnable))
            || FAILED(device->GetRenderState(D3DRS_NORMALIZENORMALS, &state.normalizeNormals))
            || FAILED(device->GetRenderState(D3DRS_LOCALVIEWER, &state.localViewer)))
            return state;

        state.hadMaterial = SUCCEEDED(device->GetMaterial(&state.material));
        if (!state.hadMaterial)
            return state;

        const float dr = state.material.Diffuse.r;
        const float dg = state.material.Diffuse.g;
        const float db = state.material.Diffuse.b;
        float maxDiffuse = dr;
        if (dg > maxDiffuse) maxDiffuse = dg;
        if (db > maxDiffuse) maxDiffuse = db;
        float minDiffuse = dr;
        if (dg < minDiffuse) minDiffuse = dg;
        if (db < minDiffuse) minDiffuse = db;
        const float saturation = maxDiffuse - minDiffuse;

        // Avoid skin, cloth, bright paint, and saturated dyed armor. This pass is only
        // meant for dull neutral metal-like materials.
        if (saturation > 0.18f || maxDiffuse > 0.82f)
            return state;

        state.active = true;
        device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
        device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        device->SetRenderState(D3DRS_LOCALVIEWER, TRUE);

        if (state.hadMaterial)
        {
            D3DMATERIAL9 material = state.material;
            if (material.Specular.r < g_config.metalSpecular) material.Specular.r = g_config.metalSpecular;
            if (material.Specular.g < g_config.metalSpecular) material.Specular.g = g_config.metalSpecular;
            if (material.Specular.b < g_config.metalSpecularBlue) material.Specular.b = g_config.metalSpecularBlue;
            if (material.Power < g_config.metalPower) material.Power = g_config.metalPower;
            device->SetMaterial(&material);
        }

        if (!g_metalSheenLogged)
        {
            g_metalSheenLogged = true;
            Log("metal-sheen enabled: material-only prims=%u-%u specular=%.2f blue=%.2f power=%.1f", g_config.metalMinPrimitiveCount, g_config.metalMaxPrimitiveCount, g_config.metalSpecular, g_config.metalSpecularBlue, g_config.metalPower);
        }
        return state;
    }

    void RestoreMetalSheen(IDirect3DDevice9* device, const MetalSheenState& state)
    {
        if (device == nullptr || !state.active)
            return;

        device->SetRenderState(D3DRS_SPECULARENABLE, state.specularEnable);
        device->SetRenderState(D3DRS_NORMALIZENORMALS, state.normalizeNormals);
        device->SetRenderState(D3DRS_LOCALVIEWER, state.localViewer);
        if (state.hadMaterial)
            device->SetMaterial(&state.material);
    }
    bool LooksLikeWaterCandidate(UINT primitiveCount, bool up)
    {
        return !up
            && primitiveCount >= 2
            && g_stage0TextureBound
            && !LooksLikeUiDraw(primitiveCount)
            && g_stats.zEnable != FALSE
            && g_stats.alphaBlend == TRUE;
    }

    void LogWaterCandidate(IDirect3DDevice9* device, UINT primitiveCount, bool up, const char* method)
    {
        if (!g_config.waterDiagnostics || !LooksLikeWaterCandidate(primitiveCount, up))
            return;

        g_stats.waterCandidateDraws++;
        if (g_waterCandidateLogs >= 80 || device == nullptr)
            return;

        IDirect3DBaseTexture9* baseTexture = nullptr;
        HRESULT texHr = device->GetTexture(0, &baseTexture);
        UINT width = 0;
        UINT height = 0;
        UINT levels = 0;
        D3DFORMAT format = D3DFMT_UNKNOWN;

        if (SUCCEEDED(texHr) && baseTexture != nullptr)
        {
            levels = baseTexture->GetLevelCount();
            IDirect3DTexture9* texture2d = nullptr;
            if (SUCCEEDED(baseTexture->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&texture2d))) && texture2d != nullptr)
            {
                D3DSURFACE_DESC desc {};
                if (SUCCEEDED(texture2d->GetLevelDesc(0, &desc)))
                {
                    width = desc.Width;
                    height = desc.Height;
                    format = desc.Format;
                }
                texture2d->Release();
            }
            baseTexture->Release();
        }

        DebugLog("water-candidate frame=%u draw=%u method=%s prim=%u fvf=0x%08lx z=%lu zw=%lu alpha=%lu lighting=%lu tex=%ux%u fmt=0x%08x levels=%u",
            g_stats.frame,
            g_drawOrdinal,
            method,
            primitiveCount,
            g_stats.fvf,
            g_stats.zEnable,
            g_stats.zWriteEnable,
            g_stats.alphaBlend,
            g_stats.lighting,
            width,
            height,
            static_cast<unsigned>(format),
            levels);
        g_waterCandidateLogs++;
    }
    void ResetFrameStats()
    {
        g_stats.frame++;
        g_stats.beginScene = 0;
        g_stats.endScene = 0;
        g_stats.clears = 0;
        g_stats.drawPrimitive = 0;
        g_stats.drawIndexedPrimitive = 0;
        g_stats.drawPrimitiveUP = 0;
        g_stats.drawIndexedPrimitiveUP = 0;
        g_stats.uiCandidateDraws = 0;
        g_stats.worldCandidateDraws = 0;
        g_stats.detailDraws = 0;
        g_stats.waterCandidateDraws = 0;
        g_stats.waterReflectDraws = 0;
        g_stats.dynamicLightDraws = 0;
        g_drawOrdinal = 0;
    }

    void LogFrameSummary(const char* source)
    {
        auto totalDraws = g_stats.drawPrimitive + g_stats.drawIndexedPrimitive + g_stats.drawPrimitiveUP + g_stats.drawIndexedPrimitiveUP;
        if (g_config.drawSampling && !g_sampleDone && !g_sampleActive && !g_sampleNextFrame && totalDraws > 180)
            g_sampleNextFrame = true;

        if (g_sampleActive)
        {
            DebugLog("sample-end frame=%u sampled=%u totalDraws=%u", g_stats.frame, g_sampleDraws, g_drawOrdinal);
            g_sampleActive = false;
            g_sampleDone = true;
        }

        if (g_config.frameSummaries && (g_stats.frame < 30 || (g_stats.frame % 300) == 0))
        {
            Log("%s frame=%u begin=%u end=%u clear=%u dp=%u dip=%u dpup=%u dipup=%u worldLike=%u uiLike=%u detail=%u waterCand=%u waterFx=%u lightFx=%u fvf=0x%08lx z=%lu zw=%lu alpha=%lu lighting=%lu",
                source,
                g_stats.frame,
                g_stats.beginScene,
                g_stats.endScene,
                g_stats.clears,
                g_stats.drawPrimitive,
                g_stats.drawIndexedPrimitive,
                g_stats.drawPrimitiveUP,
                g_stats.drawIndexedPrimitiveUP,
                g_stats.worldCandidateDraws,
                g_stats.uiCandidateDraws,
                g_stats.detailDraws,
                g_stats.waterCandidateDraws,
                g_stats.waterReflectDraws,
                g_stats.dynamicLightDraws,
                g_stats.fvf,
                g_stats.zEnable,
                g_stats.zWriteEnable,
                g_stats.alphaBlend,
                g_stats.lighting);
        }
    }
    template <typename T>
    void PatchMethod(int index, T hook, T* original)
    {
        *original = reinterpret_cast<T>(g_deviceVtable[index]);

        DWORD oldProtect = 0;
        if (!VirtualProtect(&g_deviceVtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            Log("VirtualProtect failed for vtable[%d]: %lu", index, GetLastError());
            return;
        }

        g_deviceVtable[index] = reinterpret_cast<void*>(hook);

        DWORD unused = 0;
        VirtualProtect(&g_deviceVtable[index], sizeof(void*), oldProtect, &unused);
    }

    HRESULT WINAPI HookReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params)
    {
        Log("Reset(windowed=%d, %ux%u)", params ? params->Windowed : -1, params ? params->BackBufferWidth : 0, params ? params->BackBufferHeight : 0);
        return g_origReset(self, params);
    }

    HRESULT WINAPI HookSwapChainPresent(IDirect3DSwapChain9* self, const RECT* src, const RECT* dst, HWND hwnd, const RGNDATA* dirty, DWORD flags)
    {
        if (g_config.frameSummaries && (g_stats.frame < 30 || (g_stats.frame % 300) == 0))
            Log("swapchain-present frame=%u worldLike=%u uiLike=%u", g_stats.frame, g_stats.worldCandidateDraws, g_stats.uiCandidateDraws);
        return g_origSwapChainPresent(self, src, dst, hwnd, dirty, flags);
    }

    void HookSwapChain(IDirect3DSwapChain9* swapChain)
    {
        if (swapChain == nullptr || g_origSwapChainPresent != nullptr)
            return;

        g_swapChainVtable = *reinterpret_cast<void***>(swapChain);
        PatchMethod(3, HookSwapChainPresent, &g_origSwapChainPresent);
        Log("IDirect3DSwapChain9 hooks installed. vtable=%p present=%p original=%p", g_swapChainVtable, HookSwapChainPresent, g_origSwapChainPresent);
    }

    HRESULT WINAPI HookCreateAdditionalSwapChain(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params, IDirect3DSwapChain9** swapChain)
    {
        auto hr = g_origCreateAdditionalSwapChain(self, params, swapChain);
        Log("CreateAdditionalSwapChain -> 0x%08lx swap=%p", hr, swapChain ? *swapChain : nullptr);
        if (SUCCEEDED(hr) && swapChain != nullptr)
            HookSwapChain(*swapChain);
        return hr;
    }
    HRESULT WINAPI HookPresent(IDirect3DDevice9* self, const RECT* src, const RECT* dst, HWND hwnd, const RGNDATA* dirty)
    {
        PollConfigHotkey();
        UpdateHudWindow();
        if (g_config.frameSummaries && (g_stats.frame < 30 || (g_stats.frame % 300) == 0))
        {
            Log("frame=%u begin=%u end=%u clear=%u dp=%u dip=%u dpup=%u dipup=%u worldLike=%u uiLike=%u detail=%u waterCand=%u waterFx=%u lightFx=%u fvf=0x%08lx z=%lu zw=%lu alpha=%lu lighting=%lu",
                g_stats.frame,
                g_stats.beginScene,
                g_stats.endScene,
                g_stats.clears,
                g_stats.drawPrimitive,
                g_stats.drawIndexedPrimitive,
                g_stats.drawPrimitiveUP,
                g_stats.drawIndexedPrimitiveUP,
                g_stats.worldCandidateDraws,
                g_stats.uiCandidateDraws,
                g_stats.detailDraws,
                g_stats.waterCandidateDraws,
                g_stats.waterReflectDraws,
                g_stats.dynamicLightDraws,
                g_stats.fvf,
                g_stats.zEnable,
                g_stats.zWriteEnable,
                g_stats.alphaBlend,
                g_stats.lighting);
        }

        auto hr = g_origPresent(self, src, dst, hwnd, dirty);
        g_stats.frame++;
        g_stats.beginScene = 0;
        g_stats.endScene = 0;
        g_stats.clears = 0;
        g_stats.drawPrimitive = 0;
        g_stats.drawIndexedPrimitive = 0;
        g_stats.drawPrimitiveUP = 0;
        g_stats.drawIndexedPrimitiveUP = 0;
        g_stats.uiCandidateDraws = 0;
        g_stats.worldCandidateDraws = 0;
        g_stats.detailDraws = 0;
        g_stats.waterCandidateDraws = 0;
        g_stats.waterReflectDraws = 0;
        g_stats.dynamicLightDraws = 0;
        g_drawOrdinal = 0;
        return hr;
    }

    HRESULT WINAPI HookBeginScene(IDirect3DDevice9* self)
    {
        g_stats.beginScene++;
        g_stats.inScene = true;
        return g_origBeginScene(self);
    }

    HRESULT WINAPI HookEndScene(IDirect3DDevice9* self)
    {
        PollConfigHotkey();
        g_stats.endScene++;
        g_stats.inScene = false;
        return g_origEndScene(self);
    }

    HRESULT WINAPI HookClear(IDirect3DDevice9* self, DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
    {
        g_stats.clears++;
        if (g_config.frameSummaries && g_stats.frame < 20)
            DebugLog("clear frame=%u flags=0x%08lx color=0x%08lx z=%.3f", g_stats.frame, flags, color, z);
        return g_origClear(self, count, rects, flags, color, z, stencil);
    }

    HRESULT WINAPI HookSetRenderState(IDirect3DDevice9* self, D3DRENDERSTATETYPE state, DWORD value)
    {
        if (state == D3DRS_ZENABLE)
            g_stats.zEnable = value;
        else if (state == D3DRS_ZWRITEENABLE)
            g_stats.zWriteEnable = value;
        else if (state == D3DRS_ALPHABLENDENABLE)
            g_stats.alphaBlend = value;
        else if (state == D3DRS_LIGHTING)
            g_stats.lighting = value;

        return g_origSetRenderState(self, state, value);
    }

    HRESULT WINAPI HookSetTexture(IDirect3DDevice9* self, DWORD stage, IDirect3DBaseTexture9* texture)
    {
        if (stage == 0)
            g_stage0TextureBound = texture != nullptr;
        return g_origSetTexture(self, stage, texture);
    }

    HRESULT WINAPI HookDrawPrimitive(IDirect3DDevice9* self, D3DPRIMITIVETYPE type, UINT startVertex, UINT primitiveCount)
    {
        g_stats.drawPrimitive++;
        CountDraw(false);
        return g_origDrawPrimitive(self, type, startVertex, primitiveCount);
    }

    HRESULT WINAPI HookDrawIndexedPrimitive(IDirect3DDevice9* self, D3DPRIMITIVETYPE type, INT baseVertexIndex, UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount)
    {
        g_stats.drawIndexedPrimitive++;
        CountDraw(false);
        return g_origDrawIndexedPrimitive(self, type, baseVertexIndex, minVertexIndex, numVertices, startIndex, primitiveCount);
    }

    HRESULT WINAPI HookDrawPrimitiveUP(IDirect3DDevice9* self, D3DPRIMITIVETYPE type, UINT primitiveCount, const void* vertexData, UINT stride)
    {
        g_stats.drawPrimitiveUP++;
        CountDraw(true);
        return g_origDrawPrimitiveUP(self, type, primitiveCount, vertexData, stride);
    }

    HRESULT WINAPI HookDrawIndexedPrimitiveUP(IDirect3DDevice9* self, D3DPRIMITIVETYPE type, UINT minVertexIndex, UINT numVertices, UINT primitiveCount, const void* indexData, D3DFORMAT indexFormat, const void* vertexData, UINT stride)
    {
        g_stats.drawIndexedPrimitiveUP++;
        CountDraw(true);
        return g_origDrawIndexedPrimitiveUP(self, type, minVertexIndex, numVertices, primitiveCount, indexData, indexFormat, vertexData, stride);
    }

    HRESULT WINAPI HookSetFVF(IDirect3DDevice9* self, DWORD fvf)
    {
        g_stats.fvf = fvf;
        return g_origSetFVF(self, fvf);
    }

    HRESULT WINAPI HookSetVertexShader(IDirect3DDevice9* self, IDirect3DVertexShader9* shader)
    {
        if (shader != nullptr)
            g_stats.fvf = 0;
        return g_origSetVertexShader(self, shader);
    }

    HRESULT WINAPI HookSetPixelShader(IDirect3DDevice9* self, IDirect3DPixelShader9* shader)
    {
        return g_origSetPixelShader(self, shader);
    }

    void HookDevice(IDirect3DDevice9* device)
    {
        if (device == nullptr || g_deviceHooked)
            return;

        g_deviceVtable = *reinterpret_cast<void***>(device);
        Log("HookDevice vtable=%p reset=%p present=%p begin=%p end=%p clear=%p", g_deviceVtable, g_deviceVtable[16], g_deviceVtable[17], g_deviceVtable[41], g_deviceVtable[42], g_deviceVtable[43]);
        PatchMethod(13, HookCreateAdditionalSwapChain, &g_origCreateAdditionalSwapChain);
        PatchMethod(16, HookReset, &g_origReset);
        PatchMethod(17, HookPresent, &g_origPresent);
        PatchMethod(41, HookBeginScene, &g_origBeginScene);
        PatchMethod(42, HookEndScene, &g_origEndScene);
        PatchMethod(43, HookClear, &g_origClear);
        PatchMethod(57, HookSetRenderState, &g_origSetRenderState);
        PatchMethod(65, HookSetTexture, &g_origSetTexture);
        PatchMethod(81, HookDrawPrimitive, &g_origDrawPrimitive);
        PatchMethod(82, HookDrawIndexedPrimitive, &g_origDrawIndexedPrimitive);
        PatchMethod(83, HookDrawPrimitiveUP, &g_origDrawPrimitiveUP);
        PatchMethod(84, HookDrawIndexedPrimitiveUP, &g_origDrawIndexedPrimitiveUP);
        PatchMethod(89, HookSetFVF, &g_origSetFVF);
        PatchMethod(92, HookSetVertexShader, &g_origSetVertexShader);
        PatchMethod(107, HookSetPixelShader, &g_origSetPixelShader);
        g_deviceHooked = true;
        Log("IDirect3DDevice9 hooks installed. vtable=%p", g_deviceVtable);
    }
    class DeviceProxy final : public IDirect3DDevice9
    {
    public:
        explicit DeviceProxy(IDirect3DDevice9* real) : real_(real) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (ppvObj == nullptr)
                return E_POINTER;
            if (riid == IID_IUnknown || riid == IID_IDirect3DDevice9)
            {
                *ppvObj = static_cast<IDirect3DDevice9*>(this);
                AddRef();
                return S_OK;
            }
            return real_->QueryInterface(riid, ppvObj);
        }

        ULONG STDMETHODCALLTYPE AddRef() override { InterlockedIncrement(&refs_); return real_->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            auto realRefs = real_->Release();
            auto refs = InterlockedDecrement(&refs_);
            if (refs == 0)
                delete this;
            return realRefs;
        }

        HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return real_->TestCooperativeLevel(); }
        UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return real_->GetAvailableTextureMem(); }
        HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return real_->EvictManagedResources(); }
        HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** ppD3D9) override { return real_->GetDirect3D(ppD3D9); }
        HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps) override { return real_->GetDeviceCaps(pCaps); }
        HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override { return real_->GetDisplayMode(iSwapChain, pMode); }
        HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters) override { return real_->GetCreationParameters(pParameters); }
        HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override { return real_->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap); }
        void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override { real_->SetCursorPosition(X, Y, Flags); }
        BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override { return real_->ShowCursor(bShow); }
        HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override { Log("proxy CreateAdditionalSwapChain"); return real_->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain); }
        HRESULT STDMETHODCALLTYPE GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override { return real_->GetSwapChain(iSwapChain, pSwapChain); }
        UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return real_->GetNumberOfSwapChains(); }
        HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override { Log("proxy Reset(windowed=%d, %ux%u)", pPresentationParameters ? pPresentationParameters->Windowed : -1, pPresentationParameters ? pPresentationParameters->BackBufferWidth : 0, pPresentationParameters ? pPresentationParameters->BackBufferHeight : 0); return real_->Reset(pPresentationParameters); }
        HRESULT STDMETHODCALLTYPE Present(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) override
        {
            PollConfigHotkey();
            LogFrameSummary("proxy-present");
            auto hr = real_->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
            ResetFrameStats();
            return hr;
        }
        HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override { return real_->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer); }
        HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override { return real_->GetRasterStatus(iSwapChain, pRasterStatus); }
        HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL bEnableDialogs) override { return real_->SetDialogBoxMode(bEnableDialogs); }
        void STDMETHODCALLTYPE SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP* pRamp) override { real_->SetGammaRamp(iSwapChain, Flags, pRamp); }
        void STDMETHODCALLTYPE GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override { real_->GetGammaRamp(iSwapChain, pRamp); }
        HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override { return real_->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override { return real_->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override { return real_->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override { return real_->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override { return real_->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override { return real_->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override { return real_->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, const POINT* pDestPoint) override { return real_->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint); }
        HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override { return real_->UpdateTexture(pSourceTexture, pDestinationTexture); }
        HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override { return real_->GetRenderTargetData(pRenderTarget, pDestSurface); }
        HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override { return real_->GetFrontBufferData(iSwapChain, pDestSurface); }
        HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestSurface, const RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) override { return real_->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter); }
        HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* pSurface, const RECT* pRect, D3DCOLOR color) override { return real_->ColorFill(pSurface, pRect, color); }
        HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override { return real_->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle); }
        HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override { return real_->SetRenderTarget(RenderTargetIndex, pRenderTarget); }
        HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override { return real_->GetRenderTarget(RenderTargetIndex, ppRenderTarget); }
        HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override { return real_->SetDepthStencilSurface(pNewZStencil); }
        HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override { return real_->GetDepthStencilSurface(ppZStencilSurface); }
        HRESULT STDMETHODCALLTYPE BeginScene() override { g_stats.beginScene++; return real_->BeginScene(); }
        HRESULT STDMETHODCALLTYPE EndScene() override { PollConfigHotkey(); g_stats.endScene++; LogFrameSummary("proxy-endscene"); auto hr = real_->EndScene(); ResetFrameStats(); return hr; }
        HRESULT STDMETHODCALLTYPE Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override { g_stats.clears++; return real_->Clear(Count, pRects, Flags, Color, Z, Stencil); }
        HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override { return SetConfiguredTransform(real_, State, pMatrix); }
        HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override { return real_->GetTransform(State, pMatrix); }
        HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override { return real_->MultiplyTransform(State, pMatrix); }
        HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* pViewport) override { return real_->SetViewport(pViewport); }
        HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override { return real_->GetViewport(pViewport); }
        HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* pMaterial) override { return real_->SetMaterial(pMaterial); }
        HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* pMaterial) override { return real_->GetMaterial(pMaterial); }
        HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, const D3DLIGHT9* pLight) override { return real_->SetLight(Index, pLight); }
        HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT9* pLight) override { return real_->GetLight(Index, pLight); }
        HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) override { return real_->LightEnable(Index, Enable); }
        HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) override { return real_->GetLightEnable(Index, pEnable); }
        HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, const float* pPlane) override { return real_->SetClipPlane(Index, pPlane); }
        HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) override { return real_->GetClipPlane(Index, pPlane); }
        HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override { return SetConfiguredRenderState(real_, State, Value); }
        HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override { return real_->GetRenderState(State, pValue); }
        HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override { return real_->CreateStateBlock(Type, ppSB); }
        HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return real_->BeginStateBlock(); }
        HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** ppSB) override { return real_->EndStateBlock(ppSB); }
        HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9* pClipStatus) override { return real_->SetClipStatus(pClipStatus); }
        HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* pClipStatus) override { return real_->GetClipStatus(pClipStatus); }
        HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override { return real_->GetTexture(Stage, ppTexture); }
        HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override { if (Stage == 0) g_stage0TextureBound = pTexture != nullptr; return real_->SetTexture(Stage, pTexture); }
        HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override { return real_->GetTextureStageState(Stage, Type, pValue); }
        HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override { return real_->SetTextureStageState(Stage, Type, Value); }
        HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override { return real_->GetSamplerState(Sampler, Type, pValue); }
        HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override { return real_->SetSamplerState(Sampler, Type, Value); }
        HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) override { return real_->ValidateDevice(pNumPasses); }
        HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, const PALETTEENTRY* pEntries) override { return real_->SetPaletteEntries(PaletteNumber, pEntries); }
        HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) override { return real_->GetPaletteEntries(PaletteNumber, pEntries); }
        HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override { return real_->SetCurrentTexturePalette(PaletteNumber); }
        HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *PaletteNumber) override { return real_->GetCurrentTexturePalette(PaletteNumber); }
        HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* pRect) override { return real_->SetScissorRect(pRect); }
        HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* pRect) override { return real_->GetScissorRect(pRect); }
        HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL bSoftware) override { return real_->SetSoftwareVertexProcessing(bSoftware); }
        BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return real_->GetSoftwareVertexProcessing(); }
        HRESULT STDMETHODCALLTYPE SetNPatchMode(float nSegments) override { return real_->SetNPatchMode(nSegments); }
        float STDMETHODCALLTYPE GetNPatchMode() override { return real_->GetNPatchMode(); }
        HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override { g_stats.drawPrimitive++; CountDraw(false, PrimitiveCount, "dp"); if (ShouldSuppressShadowPlane(PrimitiveCount, false)) return D3D_OK; LogWaterCandidate(real_, PrimitiveCount, false, "dp"); auto alpha = ApplySoftAlphaFix(real_, PrimitiveCount, false); auto water = ApplyWaterReflection(real_, PrimitiveCount, false); auto light = ApplyDynamicLighting(real_, PrimitiveCount, false); auto metal = ApplyMetalSheen(real_, PrimitiveCount, false); auto detail = ApplyTextureDetail(real_, PrimitiveCount, false); auto surface = ApplySurfaceDetail(real_, PrimitiveCount, false); auto hr = real_->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount); RestoreSurfaceDetail(real_, surface); RestoreTextureDetail(real_, detail); RestoreMetalSheen(real_, metal); RestoreDynamicLighting(real_, light); RestoreWaterReflection(real_, water); RestoreSoftAlphaFix(real_, alpha); return hr; }
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override { g_stats.drawIndexedPrimitive++; CountDraw(false, primCount, "dip"); if (ShouldSuppressShadowPlane(primCount, false)) return D3D_OK; LogWaterCandidate(real_, primCount, false, "dip"); auto alpha = ApplySoftAlphaFix(real_, primCount, false); auto water = ApplyWaterReflection(real_, primCount, false); auto light = ApplyDynamicLighting(real_, primCount, false); auto metal = ApplyMetalSheen(real_, primCount, false); auto detail = ApplyTextureDetail(real_, primCount, false); auto surface = ApplySurfaceDetail(real_, primCount, false); auto hr = real_->DrawIndexedPrimitive(Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount); RestoreSurfaceDetail(real_, surface); RestoreTextureDetail(real_, detail); RestoreMetalSheen(real_, metal); RestoreDynamicLighting(real_, light); RestoreWaterReflection(real_, water); RestoreSoftAlphaFix(real_, alpha); return hr; }
        HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override { g_stats.drawPrimitiveUP++; CountDraw(true, PrimitiveCount, "dpup"); if (ShouldSuppressShadowPlane(PrimitiveCount, true)) return D3D_OK; return real_->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride); }
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override { g_stats.drawIndexedPrimitiveUP++; CountDraw(true, PrimitiveCount, "dipup"); if (ShouldSuppressShadowPlane(PrimitiveCount, true)) return D3D_OK; return real_->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride); }
        HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override { return real_->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags); }
        HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) override { return real_->CreateVertexDeclaration(pVertexElements, ppDecl); }
        HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override { g_stats.fvf = 0; return real_->SetVertexDeclaration(pDecl); }
        HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override { return real_->GetVertexDeclaration(ppDecl); }
        HRESULT STDMETHODCALLTYPE SetFVF(DWORD FVF) override { g_stats.fvf = FVF; return real_->SetFVF(FVF); }
        HRESULT STDMETHODCALLTYPE GetFVF(DWORD* pFVF) override { return real_->GetFVF(pFVF); }
        HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) override { return real_->CreateVertexShader(pFunction, ppShader); }
        HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader) override { g_hasVertexShader = pShader != nullptr; if (pShader != nullptr) g_stats.fvf = 0; return real_->SetVertexShader(pShader); }
        HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override { return real_->GetVertexShader(ppShader); }
        HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override { return real_->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount); }
        HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override { return real_->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount); }
        HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) override { return real_->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
        HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override { return real_->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
        HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) override { return real_->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount); }
        HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override { return real_->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount); }
        HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override { return real_->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride); }
        HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override { return real_->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride); }
        HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT StreamNumber, UINT Setting) override { return real_->SetStreamSourceFreq(StreamNumber, Setting); }
        HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) override { return real_->GetStreamSourceFreq(StreamNumber, pSetting); }
        HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData) override { return real_->SetIndices(pIndexData); }
        HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ppIndexData) override { return real_->GetIndices(ppIndexData); }
        HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) override { return real_->CreatePixelShader(pFunction, ppShader); }
        HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader) override { g_hasPixelShader = pShader != nullptr; return real_->SetPixelShader(pShader); }
        HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override { return real_->GetPixelShader(ppShader); }
        HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override { return real_->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount); }
        HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override { return real_->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount); }
        HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) override { return real_->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
        HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override { return real_->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
        HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) override { return real_->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount); }
        HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override { return real_->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount); }
        HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, const float* pNumSegs, const D3DRECTPATCH_INFO* pRectPatchInfo) override { return real_->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo); }
        HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, const float* pNumSegs, const D3DTRIPATCH_INFO* pTriPatchInfo) override { return real_->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo); }
        HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override { return real_->DeletePatch(Handle); }
        HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override { return real_->CreateQuery(Type, ppQuery); }

    private:
        IDirect3DDevice9* real_ = nullptr;
        volatile LONG refs_ = 1;
    };
    class Direct3D9Proxy final : public IDirect3D9
    {
    public:
        explicit Direct3D9Proxy(IDirect3D9* real) : real_(real) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (ppvObj == nullptr)
                return E_POINTER;

            if (riid == IID_IUnknown || riid == IID_IDirect3D9)
            {
                *ppvObj = static_cast<IDirect3D9*>(this);
                AddRef();
                return S_OK;
            }

            return real_->QueryInterface(riid, ppvObj);
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            InterlockedIncrement(&refs_);
            return real_->AddRef();
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            auto realRefs = real_->Release();
            auto refs = InterlockedDecrement(&refs_);
            if (refs == 0)
                delete this;
            return realRefs;
        }

        HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* pInitializeFunction) override
        {
            return real_->RegisterSoftwareDevice(pInitializeFunction);
        }

        UINT STDMETHODCALLTYPE GetAdapterCount() override
        {
            return real_->GetAdapterCount();
        }

        HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) override
        {
            return real_->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
        }

        UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override
        {
            return real_->GetAdapterModeCount(Adapter, Format);
        }

        HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) override
        {
            return real_->EnumAdapterModes(Adapter, Format, Mode, pMode);
        }

        HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) override
        {
            return real_->GetAdapterDisplayMode(Adapter, pMode);
        }

        HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) override
        {
            return real_->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
        }

        HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override
        {
            return real_->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
        }

        HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) override
        {
            return real_->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
        }

        HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override
        {
            return real_->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
        }

        HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) override
        {
            return real_->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
        }

        HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) override
        {
            return real_->GetDeviceCaps(Adapter, DeviceType, pCaps);
        }

        HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) override
        {
            return real_->GetAdapterMonitor(Adapter);
        }

        HRESULT STDMETHODCALLTYPE CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) override
        {
            g_gameWindow = hFocusWindow;
            Log("CreateDevice(adapter=%u, type=%u, hwnd=%p, flags=0x%08lx, windowed=%d, %ux%u)",
                Adapter,
                static_cast<unsigned>(DeviceType),
                hFocusWindow,
                BehaviorFlags,
                pPresentationParameters ? pPresentationParameters->Windowed : -1,
                pPresentationParameters ? pPresentationParameters->BackBufferWidth : 0,
                pPresentationParameters ? pPresentationParameters->BackBufferHeight : 0);

            auto hr = real_->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
            Log("CreateDevice -> 0x%08lx, device=%p", hr, ppReturnedDeviceInterface ? *ppReturnedDeviceInterface : nullptr);
            Log("CreateDevice hook dispatch: succeeded=%d pp=%p", SUCCEEDED(hr) ? 1 : 0, ppReturnedDeviceInterface);
            if (SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr)
            {
                auto realDevice = *ppReturnedDeviceInterface;
                *ppReturnedDeviceInterface = new DeviceProxy(realDevice);
                Log("CreateDevice returned DeviceProxy=%p real=%p", *ppReturnedDeviceInterface, realDevice);
            }
            return hr;
        }

    private:
        IDirect3D9* real_ = nullptr;
        volatile LONG refs_ = 1;
    };
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
        LoadConfig();
        Log("AC D3D9 proxy loaded. config=%s logging=%d", g_configPath, static_cast<int>(g_config.logging));
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        ReleaseGeneratedTextures();
        if (g_hudWindow != nullptr)
            DestroyWindow(g_hudWindow);
        Log("AC D3D9 proxy unloaded.");
    }
    return TRUE;
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion)
{
    using Fn = IDirect3D9* (WINAPI*)(UINT);
    auto fn = reinterpret_cast<Fn>(GetRealProc("Direct3DCreate9"));
    if (fn == nullptr)
        return nullptr;

    Log("Direct3DCreate9(%u)", sdkVersion);
    auto real = fn(sdkVersion);
    Log("Direct3DCreate9 real -> %p", real);
    return real ? new Direct3D9Proxy(real) : nullptr;
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** d3d9Ex)
{
    using Fn = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);
    auto fn = reinterpret_cast<Fn>(GetRealProc("Direct3DCreate9Ex"));
    if (fn == nullptr)
        return D3DERR_NOTAVAILABLE;

    Log("Direct3DCreate9Ex(%u)", sdkVersion);
    return fn(sdkVersion, d3d9Ex);
}

#define FORWARD_VOID_EXPORT(name, signature, args) \
    extern "C" void WINAPI name signature \
    { \
        using Fn = void (WINAPI*) signature; \
        auto fn = reinterpret_cast<Fn>(GetRealProc(#name)); \
        if (fn != nullptr) fn args; \
    }

#define FORWARD_DWORD_EXPORT(name, signature, args) \
    extern "C" DWORD WINAPI name signature \
    { \
        using Fn = DWORD (WINAPI*) signature; \
        auto fn = reinterpret_cast<Fn>(GetRealProc(#name)); \
        return fn != nullptr ? fn args : 0; \
    }

#define FORWARD_INT_EXPORT(name, signature, args) \
    extern "C" int WINAPI name signature \
    { \
        using Fn = int (WINAPI*) signature; \
        auto fn = reinterpret_cast<Fn>(GetRealProc(#name)); \
        return fn != nullptr ? fn args : -1; \
    }

extern "C" void* WINAPI Direct3DShaderValidatorCreate9()
{
    using Fn = void* (WINAPI*)();
    auto fn = reinterpret_cast<Fn>(GetRealProc("Direct3DShaderValidatorCreate9"));
    return fn != nullptr ? fn() : nullptr;
}

extern "C" void WINAPI PSGPError()
{
    auto fn = reinterpret_cast<void (WINAPI*)()>(GetRealProc("PSGPError"));
    if (fn != nullptr)
        fn();
}

extern "C" void WINAPI PSGPSampleTexture()
{
    auto fn = reinterpret_cast<void (WINAPI*)()>(GetRealProc("PSGPSampleTexture"));
    if (fn != nullptr)
        fn();
}

FORWARD_INT_EXPORT(D3DPERF_BeginEvent, (D3DCOLOR color, LPCWSTR name), (color, name))
FORWARD_INT_EXPORT(D3DPERF_EndEvent, (), ())
FORWARD_DWORD_EXPORT(D3DPERF_GetStatus, (), ())

extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame()
{
    using Fn = BOOL (WINAPI*)();
    auto fn = reinterpret_cast<Fn>(GetRealProc("D3DPERF_QueryRepeatFrame"));
    return fn != nullptr ? fn() : FALSE;
}

FORWARD_VOID_EXPORT(D3DPERF_SetMarker, (D3DCOLOR color, LPCWSTR name), (color, name))
FORWARD_VOID_EXPORT(D3DPERF_SetOptions, (DWORD options), (options))
FORWARD_VOID_EXPORT(D3DPERF_SetRegion, (D3DCOLOR color, LPCWSTR name), (color, name))
