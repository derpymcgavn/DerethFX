#include <windows.h>
#include <d3d9.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
    HMODULE g_realD3D9 = nullptr;
    char g_logPath[MAX_PATH] = {};

    void InitLogPath()
    {
        if (g_logPath[0] != '\0')
            return;

        GetModuleFileNameA(nullptr, g_logPath, MAX_PATH);
        char* slash = strrchr(g_logPath, '\\');
        if (slash != nullptr)
            *(slash + 1) = '\0';
        else
            g_logPath[0] = '\0';

        strncat_s(g_logPath, "ac_d3d9_proxy.log", _TRUNCATE);
    }

    void Log(const char* fmt, ...)
    {
        InitLogPath();

        FILE* file = nullptr;
        if (fopen_s(&file, g_logPath, "a") != 0 || file == nullptr)
            return;

        SYSTEMTIME st {};
        GetLocalTime(&st);
        std::fprintf(file, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        va_list args;
        va_start(args, fmt);
        std::vfprintf(file, fmt, args);
        va_end(args);

        std::fprintf(file, "\n");
        std::fclose(file);
    }

    HMODULE LoadRealD3D9()
    {
        if (g_realD3D9 != nullptr)
            return g_realD3D9;

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
    bool g_dynamicLightingLogged = false;

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
    SetTextureFn g_origSetTexture = nullptr;
    DrawPrimitiveFn g_origDrawPrimitive = nullptr;
    DrawIndexedPrimitiveFn g_origDrawIndexedPrimitive = nullptr;
    DrawPrimitiveUPFn g_origDrawPrimitiveUP = nullptr;
    DrawIndexedPrimitiveUPFn g_origDrawIndexedPrimitiveUP = nullptr;
    SetFVFFn g_origSetFVF = nullptr;
    SetVertexShaderFn g_origSetVertexShader = nullptr;
    SetPixelShaderFn g_origSetPixelShader = nullptr;

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
            Log("sample-start frame=%u", g_stats.frame);
        }

        if (LooksLikeUiDraw(primitiveCount))
            g_stats.uiCandidateDraws++;
        else
            g_stats.worldCandidateDraws++;

        if (g_sampleActive && g_sampleDraws < 220)
        {
            Log("sample frame=%u draw=%u method=%s prim=%u fvf=0x%08lx z=%lu zw=%lu alpha=%lu lighting=%lu vs=%d ps=%d up=%d uiGuess=%d",
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
        return !up
            && primitiveCount >= 4
            && g_stage0TextureBound
            && !g_hasPixelShader
            && !LooksLikeUiDraw(primitiveCount)
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
        device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 8);
        device->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, FloatAsDword(-0.65f));
        g_stats.detailDraws++;

        if (!g_textureDetailLogged)
        {
            g_textureDetailLogged = true;
            Log("texture-detail enabled: anisotropic=8 mipBias=-0.65 world-only");
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
        return !up
            && primitiveCount >= 80
            && primitiveCount <= 220
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
                    const BYTE a = 72;
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
            Log("water-reflection enabled: tuned indexed water-like stage1 reflection overlay");
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
        return !up
            && primitiveCount >= 4
            && !g_hasPixelShader
            && !LooksLikeUiDraw(primitiveCount)
            && !LooksLikeWaterDraw(nullptr, primitiveCount, up)
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
        device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(42, 42, 50));
        device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
        device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        device->SetRenderState(D3DRS_LOCALVIEWER, TRUE);
        device->SetRenderState(D3DRS_COLORVERTEX, TRUE);

        if (state.hadMaterial)
        {
            D3DMATERIAL9 material = state.material;
            if (material.Specular.r < 0.35f) material.Specular.r = 0.35f;
            if (material.Specular.g < 0.35f) material.Specular.g = 0.35f;
            if (material.Specular.b < 0.40f) material.Specular.b = 0.40f;
            if (material.Power < 18.0f) material.Power = 18.0f;
            device->SetMaterial(&material);
        }

        D3DLIGHT9 light {};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse.r = 0.22f;
        light.Diffuse.g = 0.24f;
        light.Diffuse.b = 0.28f;
        light.Specular.r = 0.45f;
        light.Specular.g = 0.48f;
        light.Specular.b = 0.55f;
        light.Ambient.r = 0.03f;
        light.Ambient.g = 0.035f;
        light.Ambient.b = 0.045f;
        light.Direction.x = -0.35f;
        light.Direction.y = -0.65f;
        light.Direction.z = 0.45f;
        device->SetLight(7, &light);
        device->LightEnable(7, TRUE);
        g_stats.dynamicLightDraws++;

        if (!g_dynamicLightingLogged)
        {
            g_dynamicLightingLogged = true;
            Log("dynamic-lighting enabled: ambient/specular/headlight world-only");
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
        if (!LooksLikeWaterCandidate(primitiveCount, up))
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

        Log("water-candidate frame=%u draw=%u method=%s prim=%u fvf=0x%08lx z=%lu zw=%lu alpha=%lu lighting=%lu tex=%ux%u fmt=0x%08x levels=%u",
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
        if (!g_sampleDone && !g_sampleActive && !g_sampleNextFrame && totalDraws > 180)
            g_sampleNextFrame = true;

        if (g_sampleActive)
        {
            Log("sample-end frame=%u sampled=%u totalDraws=%u", g_stats.frame, g_sampleDraws, g_drawOrdinal);
            g_sampleActive = false;
            g_sampleDone = true;
        }

        if (g_stats.frame < 30 || (g_stats.frame % 300) == 0)
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
        if (g_stats.frame < 30 || (g_stats.frame % 300) == 0)
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
        if (g_stats.frame < 30 || (g_stats.frame % 300) == 0)
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
        g_stats.endScene++;
        g_stats.inScene = false;
        return g_origEndScene(self);
    }

    HRESULT WINAPI HookClear(IDirect3DDevice9* self, DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
    {
        g_stats.clears++;
        if (g_stats.frame < 20)
            Log("clear frame=%u flags=0x%08lx color=0x%08lx z=%.3f", g_stats.frame, flags, color, z);
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
        HRESULT STDMETHODCALLTYPE EndScene() override { g_stats.endScene++; LogFrameSummary("proxy-endscene"); auto hr = real_->EndScene(); ResetFrameStats(); return hr; }
        HRESULT STDMETHODCALLTYPE Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override { g_stats.clears++; return real_->Clear(Count, pRects, Flags, Color, Z, Stencil); }
        HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override { return real_->SetTransform(State, pMatrix); }
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
        HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override { if (State == D3DRS_ZENABLE) g_stats.zEnable = Value; else if (State == D3DRS_ZWRITEENABLE) g_stats.zWriteEnable = Value; else if (State == D3DRS_ALPHABLENDENABLE) g_stats.alphaBlend = Value; else if (State == D3DRS_LIGHTING) g_stats.lighting = Value; return real_->SetRenderState(State, Value); }
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
        HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override { g_stats.drawPrimitive++; CountDraw(false, PrimitiveCount, "dp"); LogWaterCandidate(real_, PrimitiveCount, false, "dp"); auto water = ApplyWaterReflection(real_, PrimitiveCount, false); auto light = ApplyDynamicLighting(real_, PrimitiveCount, false); auto detail = ApplyTextureDetail(real_, PrimitiveCount, false); auto hr = real_->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount); RestoreTextureDetail(real_, detail); RestoreDynamicLighting(real_, light); RestoreWaterReflection(real_, water); return hr; }
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override { g_stats.drawIndexedPrimitive++; CountDraw(false, primCount, "dip"); LogWaterCandidate(real_, primCount, false, "dip"); auto water = ApplyWaterReflection(real_, primCount, false); auto light = ApplyDynamicLighting(real_, primCount, false); auto detail = ApplyTextureDetail(real_, primCount, false); auto hr = real_->DrawIndexedPrimitive(Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount); RestoreTextureDetail(real_, detail); RestoreDynamicLighting(real_, light); RestoreWaterReflection(real_, water); return hr; }
        HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override { g_stats.drawPrimitiveUP++; CountDraw(true, PrimitiveCount, "dpup"); return real_->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride); }
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override { g_stats.drawIndexedPrimitiveUP++; CountDraw(true, PrimitiveCount, "dipup"); return real_->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride); }
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
        DisableThreadLibraryCalls(instance);
        Log("AC D3D9 proxy loaded.");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_reflectionTexture != nullptr)
        {
            g_reflectionTexture->Release();
            g_reflectionTexture = nullptr;
        }
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