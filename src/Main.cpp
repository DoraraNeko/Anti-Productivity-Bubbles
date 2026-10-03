#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "BubbleVS.h"
#include "BubblePS.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "windowsapp.lib")

using Microsoft::WRL::ComPtr;
namespace Capture = winrt::Windows::Graphics::Capture;
namespace Direct3D11 = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace
{
    constexpr wchar_t kAppVersion[] = L"1.0.0";
    constexpr wchar_t kOverlayClassName[] = L"APBOverlayWindow";
    constexpr wchar_t kControlClassName[] = L"APBControlWindow";
    constexpr wchar_t kWindowTitle[] = L"APB";
    constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\APB.SingleInstance";
    constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValueName[] = L"APB";

    constexpr UINT_PTR kFrameTimerId = 1;
    constexpr UINT_PTR kRebuildTimerId = 2;
    constexpr UINT kTrayMessage = WM_APP + 1;
    constexpr UINT kTrayIconId = 1;
    constexpr WORD kIconResourceId = 1;
    constexpr UINT kMenuPause = 1001;
    constexpr UINT kMenuReload = 1002;
    constexpr UINT kMenuOpenSettings = 1003;
    constexpr UINT kMenuAutostart = 1004;
    constexpr UINT kMenuExit = 1005;

    constexpr UINT kMaxBubblesPerMonitor = 64;
    constexpr UINT kMaxMonitorSections = 32;
    constexpr UINT kFrameIntervalMilliseconds = 16;
    constexpr ULONGLONG kHousekeepingIntervalMilliseconds = 1000;
    constexpr int kMaxRebuildRetries = 3;

    struct BubbleConstants
    {
        float x;
        float y;
        float radius;
        float phase;
    };

    struct FrameConstants
    {
        float width;
        float height;
        float time;
        float refraction;
        BubbleConstants bubbles[kMaxBubblesPerMonitor];
    };

    struct Bubble
    {
        float x;
        float y;
        float radius;
        float speed;
        float phase;
        float sway;
        float swayRate;
    };

    enum class BatteryMode
    {
        Normal,
        Reduce,
        Pause
    };

    struct MonitorSetting
    {
        UINT number = 0;
        bool enabled = true;
        int count = -1;
    };

    struct Settings
    {
        UINT bubbleCount = 15;
        float diameterMin = 200.0f;
        float diameterMax = 300.0f;
        float speedMin = 30.0f;
        float speedMax = 60.0f;
        float swayMin = 24.0f;
        float swayMax = 48.0f;
        float swayRateMin = 0.35f;
        float swayRateMax = 0.7f;
        float refraction = 0.55f;
        UINT exitKey = VK_ESCAPE;
        std::vector<UINT> exitModifiers;
        BatteryMode onBattery = BatteryMode::Normal;
        float batteryFps = 30.0f;
        std::vector<MonitorSetting> monitors;
    };

    struct MonitorOverlay
    {
        HMONITOR monitor = nullptr;
        UINT displayNumber = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        UINT bubbleCount = 0;
        HWND window = nullptr;
        std::vector<Bubble> bubbles;

        ComPtr<ID3D11Texture2D> renderTexture;
        ComPtr<ID3D11RenderTargetView> renderTarget;
        ComPtr<ID3D11Texture2D> stagingTexture;
        ComPtr<ID3D11ShaderResourceView> desktopView;

        HDC bitmapDC = nullptr;
        HBITMAP bitmap = nullptr;
        HBITMAP previousBitmap = nullptr;
        void* bitmapPixels = nullptr;

        Capture::Direct3D11CaptureFramePool framePool{ nullptr };
        Capture::GraphicsCaptureSession captureSession{ nullptr };
    };

    HINSTANCE gInstance = nullptr;
    HWND gControlWindow = nullptr;
    HHOOK gKeyboardHook = nullptr;
    HANDLE gSingleInstanceMutex = nullptr;
    UINT gTaskbarCreatedMessage = 0;
    bool gFatalErrorReported = false;
    bool gOverlaysReady = false;
    bool gUserPaused = false;
    bool gBatteryPaused = false;
    bool gOnBattery = false;
    int gRebuildRetries = 0;
    ULONGLONG gStartTime = 0;
    ULONGLONG gLastRender = 0;
    ULONGLONG gLastHousekeeping = 0;
    FILETIME gSettingsWriteTime{};
    std::wstring gSettingsPath;
    std::wstring gLogPath;
    std::vector<std::wstring> gWarnings;
    Settings gSettings;
    std::vector<std::unique_ptr<MonitorOverlay>> gOverlays;
    std::mt19937 gRandom{ std::random_device{}() };

    ComPtr<ID3D11Device> gDevice;
    ComPtr<ID3D11DeviceContext> gContext;
    ComPtr<ID3D11VertexShader> gVertexShader;
    ComPtr<ID3D11PixelShader> gPixelShader;
    ComPtr<ID3D11Buffer> gFrameBuffer;
    ComPtr<ID3D11SamplerState> gSampler;
    ComPtr<ID3D11BlendState> gBlendState;
    ComPtr<ID3D11RasterizerState> gRasterizerState;

    const wchar_t* Tr(const wchar_t* japanese, const wchar_t* english)
    {
        return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? japanese : english;
    }

    std::wstring GetExecutablePath()
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length == MAX_PATH)
        {
            winrt::throw_last_error();
        }
        return std::wstring(path, length);
    }

    std::wstring GetExecutableDirectory()
    {
        std::wstring directory = GetExecutablePath();
        const size_t separator = directory.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return L".";
        }
        directory.resize(separator);
        return directory;
    }

    void InitializeLog()
    {
        wchar_t base[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
        if (length == 0 || length >= ARRAYSIZE(base))
        {
            return;
        }
        const std::wstring directory = std::wstring(base) + L"\\APB";
        CreateDirectoryW(directory.c_str(), nullptr);
        gLogPath = directory + L"\\APB.log";
        HANDLE file = CreateFileW(gLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(file);
        }
    }

    void Log(const std::wstring& message)
    {
        if (gLogPath.empty())
        {
            return;
        }
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t stamp[32]{};
        swprintf_s(stamp, L"[%02u:%02u:%02u] ", time.wHour, time.wMinute, time.wSecond);
        const std::wstring line = stamp + message + L"\r\n";
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
        {
            return;
        }
        std::string utf8(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), bytes, nullptr, nullptr);
        HANDLE file = CreateFileW(gLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            CloseHandle(file);
        }
    }

    void AddWarning(const std::wstring& message)
    {
        gWarnings.push_back(message);
        Log(L"Warning: " + message);
    }

    std::wstring Trim(const std::wstring& text)
    {
        const size_t first = text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
        {
            return L"";
        }
        const size_t last = text.find_last_not_of(L" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    std::wstring ToUpper(std::wstring text)
    {
        for (wchar_t& c : text)
        {
            c = static_cast<wchar_t>(std::towupper(c));
        }
        return text;
    }

    std::wstring ReadString(const std::wstring& section, const wchar_t* key)
    {
        wchar_t buffer[256]{};
        GetPrivateProfileStringW(section.c_str(), key, L"", buffer, ARRAYSIZE(buffer), gSettingsPath.c_str());
        return Trim(buffer);
    }

    float ReadFloat(const std::wstring& section, const wchar_t* key, float fallback)
    {
        const std::wstring text = ReadString(section, key);
        if (text.empty())
        {
            return fallback;
        }
        wchar_t* end = nullptr;
        const float value = std::wcstof(text.c_str(), &end);
        if (end == text.c_str() || *end != L'\0' || !std::isfinite(value))
        {
            AddWarning(L"[" + section + L"] " + key + L"=" + text + L" is not a valid number; using the default.");
            return fallback;
        }
        return value;
    }

    bool ReadBool(const std::wstring& section, const wchar_t* key, bool fallback)
    {
        const std::wstring text = ToUpper(ReadString(section, key));
        if (text.empty())
        {
            return fallback;
        }
        if (text == L"1" || text == L"TRUE" || text == L"YES" || text == L"ON")
        {
            return true;
        }
        if (text == L"0" || text == L"FALSE" || text == L"NO" || text == L"OFF")
        {
            return false;
        }
        AddWarning(L"[" + section + L"] " + key + L"=" + text + L" is not 0/1; using the default.");
        return fallback;
    }

    void ReadRange(const wchar_t* section, const wchar_t* minKey, const wchar_t* maxKey,
        float lowest, float& minimum, float& maximum)
    {
        minimum = std::max(lowest, ReadFloat(section, minKey, minimum));
        maximum = std::max(lowest, ReadFloat(section, maxKey, maximum));
        if (maximum < minimum)
        {
            std::swap(minimum, maximum);
        }
    }

    // Returns 0 when the name is not recognized.
    UINT ParseKeyName(const std::wstring& text)
    {
        std::wstring name;
        for (wchar_t c : text)
        {
            if (c != L' ' && c != L'\t' && c != L'\r')
            {
                name += static_cast<wchar_t>(std::towupper(c));
            }
        }
        if (name.empty())
        {
            return 0;
        }
        if (name.size() == 1 && ((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'0' && name[0] <= L'9')))
        {
            return name[0];
        }
        if (name.size() >= 2 && name[0] == L'F' && name.find_first_not_of(L"0123456789", 1) == std::wstring::npos)
        {
            const int number = _wtoi(name.c_str() + 1);
            if (number >= 1 && number <= 24)
            {
                return VK_F1 + number - 1;
            }
            return 0;
        }
        if (name.size() > 2 && name[0] == L'0' && name[1] == L'X')
        {
            wchar_t* end = nullptr;
            const unsigned long code = std::wcstoul(name.c_str() + 2, &end, 16);
            return (*end == L'\0' && code > 0 && code < 256) ? static_cast<UINT>(code) : 0;
        }
        static const struct { const wchar_t* name; UINT code; } keys[] = {
            { L"ESC", VK_ESCAPE }, { L"ESCAPE", VK_ESCAPE }, { L"SPACE", VK_SPACE },
            { L"ENTER", VK_RETURN }, { L"RETURN", VK_RETURN }, { L"TAB", VK_TAB },
            { L"BACKSPACE", VK_BACK }, { L"DELETE", VK_DELETE }, { L"DEL", VK_DELETE },
            { L"INSERT", VK_INSERT }, { L"HOME", VK_HOME }, { L"END", VK_END },
            { L"PAGEUP", VK_PRIOR }, { L"PAGEDOWN", VK_NEXT }, { L"PAUSE", VK_PAUSE },
            { L"PRINTSCREEN", VK_SNAPSHOT }, { L"SCROLLLOCK", VK_SCROLL },
            { L"SHIFT", VK_SHIFT }, { L"LSHIFT", VK_LSHIFT }, { L"RSHIFT", VK_RSHIFT },
            { L"CTRL", VK_CONTROL }, { L"CONTROL", VK_CONTROL }, { L"LCTRL", VK_LCONTROL }, { L"RCTRL", VK_RCONTROL },
            { L"ALT", VK_MENU }, { L"LALT", VK_LMENU }, { L"RALT", VK_RMENU },
            { L"WIN", VK_LWIN }, { L"LWIN", VK_LWIN }, { L"RWIN", VK_RWIN },
            { L"LEFT", VK_LEFT }, { L"RIGHT", VK_RIGHT }, { L"UP", VK_UP }, { L"DOWN", VK_DOWN },
        };
        for (const auto& entry : keys)
        {
            if (name == entry.name)
            {
                return entry.code;
            }
        }
        return 0;
    }

    void ParseExitKey(const std::wstring& combo, Settings& settings)
    {
        std::vector<UINT> codes;
        size_t start = 0;
        while (true)
        {
            const size_t plus = combo.find(L'+', start);
            const UINT code = ParseKeyName(combo.substr(start, plus == std::wstring::npos ? plus : plus - start));
            if (code == 0)
            {
                AddWarning(L"[Input] ExitKey=" + combo + L" is not recognized; using Esc.");
                return;
            }
            codes.push_back(code);
            if (plus == std::wstring::npos)
            {
                break;
            }
            start = plus + 1;
        }
        settings.exitKey = codes.back();
        codes.pop_back();
        settings.exitModifiers = codes;
    }

    FILETIME GetSettingsWriteTime()
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(gSettingsPath.c_str(), GetFileExInfoStandard, &data))
        {
            return {};
        }
        return data.ftLastWriteTime;
    }

    void LoadSettings()
    {
        gWarnings.clear();
        gSettingsWriteTime = GetSettingsWriteTime();
        if (GetFileAttributesW(gSettingsPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            Log(L"settings.ini not found; using defaults.");
            gSettings = Settings{};
            return;
        }

        Settings s;
        const int count = static_cast<int>(std::lround(ReadFloat(L"Bubbles", L"Count", static_cast<float>(s.bubbleCount))));
        s.bubbleCount = static_cast<UINT>(std::clamp(count, 1, static_cast<int>(kMaxBubblesPerMonitor)));
        ReadRange(L"Bubbles", L"DiameterMin", L"DiameterMax", 10.0f, s.diameterMin, s.diameterMax);
        ReadRange(L"Bubbles", L"SpeedMin", L"SpeedMax", 0.0f, s.speedMin, s.speedMax);
        ReadRange(L"Sway", L"AmplitudeMin", L"AmplitudeMax", 0.0f, s.swayMin, s.swayMax);
        ReadRange(L"Sway", L"RateMin", L"RateMax", 0.0f, s.swayRateMin, s.swayRateMax);
        s.refraction = std::clamp(ReadFloat(L"Refraction", L"Strength", s.refraction), 0.0f, 3.0f);

        const std::wstring exitKey = ReadString(L"Input", L"ExitKey");
        if (!exitKey.empty())
        {
            ParseExitKey(exitKey, s);
        }

        const std::wstring battery = ToUpper(ReadString(L"Power", L"OnBattery"));
        if (battery == L"REDUCE")
        {
            s.onBattery = BatteryMode::Reduce;
        }
        else if (battery == L"PAUSE")
        {
            s.onBattery = BatteryMode::Pause;
        }
        else if (!battery.empty() && battery != L"NORMAL")
        {
            AddWarning(L"[Power] OnBattery=" + battery + L" is not Normal/Reduce/Pause; using Normal.");
        }
        s.batteryFps = std::clamp(ReadFloat(L"Power", L"BatteryFps", s.batteryFps), 5.0f, 60.0f);

        for (UINT number = 1; number <= kMaxMonitorSections; ++number)
        {
            const std::wstring section = L"Monitor" + std::to_wstring(number);
            if (ReadString(section, L"Enabled").empty() && ReadString(section, L"Count").empty())
            {
                continue;
            }
            MonitorSetting monitor;
            monitor.number = number;
            monitor.enabled = ReadBool(section, L"Enabled", true);
            const float monitorCount = ReadFloat(section, L"Count", -1.0f);
            if (monitorCount >= 0.0f)
            {
                monitor.count = std::clamp(static_cast<int>(std::lround(monitorCount)), 1, static_cast<int>(kMaxBubblesPerMonitor));
            }
            s.monitors.push_back(monitor);
        }
        gSettings = s;
    }

    bool IsKeyDown(UINT virtualKey)
    {
        return (GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0;
    }

    bool IsExitCombination(UINT virtualKey)
    {
        // Esc + Left Shift + Left Ctrl always exits, even if ExitKey is changed.
        if (virtualKey == VK_ESCAPE && IsKeyDown(VK_LSHIFT) && IsKeyDown(VK_LCONTROL))
        {
            return true;
        }
        if (virtualKey != gSettings.exitKey)
        {
            return false;
        }
        for (UINT modifier : gSettings.exitModifiers)
        {
            if (!IsKeyDown(modifier))
            {
                return false;
            }
        }
        return true;
    }

    bool IsPaused()
    {
        return gUserPaused || gBatteryPaused;
    }

    UINT ParseDisplayNumber(const wchar_t* deviceName)
    {
        const wchar_t* marker = std::wcsstr(deviceName, L"DISPLAY");
        return marker ? static_cast<UINT>(_wtoi(marker + 7)) : 0;
    }

    BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
    {
        reinterpret_cast<std::vector<HMONITOR>*>(parameter)->push_back(monitor);
        return TRUE;
    }

    const MonitorSetting* FindMonitorSetting(UINT displayNumber)
    {
        for (const auto& monitor : gSettings.monitors)
        {
            if (monitor.number == displayNumber)
            {
                return &monitor;
            }
        }
        return nullptr;
    }

    std::vector<std::unique_ptr<MonitorOverlay>> CreateMonitorOverlays()
    {
        std::vector<HMONITOR> monitors;
        if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors)))
        {
            winrt::throw_last_error();
        }
        if (monitors.empty())
        {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED));
        }

        auto build = [&](bool honorDisabled)
        {
            std::vector<std::unique_ptr<MonitorOverlay>> overlays;
            for (HMONITOR monitor : monitors)
            {
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (!GetMonitorInfoW(monitor, &info))
                {
                    winrt::throw_last_error();
                }

                auto overlay = std::make_unique<MonitorOverlay>();
                overlay->monitor = monitor;
                overlay->displayNumber = ParseDisplayNumber(info.szDevice);
                overlay->x = info.rcMonitor.left;
                overlay->y = info.rcMonitor.top;
                overlay->width = info.rcMonitor.right - info.rcMonitor.left;
                overlay->height = info.rcMonitor.bottom - info.rcMonitor.top;
                overlay->bubbleCount = gSettings.bubbleCount;

                const MonitorSetting* setting = FindMonitorSetting(overlay->displayNumber);
                if (setting)
                {
                    if (honorDisabled && !setting->enabled)
                    {
                        continue;
                    }
                    if (setting->count > 0)
                    {
                        overlay->bubbleCount = static_cast<UINT>(setting->count);
                    }
                }
                overlays.push_back(std::move(overlay));
            }
            return overlays;
        };

        auto overlays = build(true);
        if (overlays.empty())
        {
            AddWarning(L"All monitors are disabled in settings.ini; showing bubbles on all monitors.");
            overlays = build(false);
        }
        return overlays;
    }

    void CreateSharedD3DResources()
    {
        D3D_FEATURE_LEVEL featureLevel{};
        const D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        winrt::check_hresult(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            requestedLevels,
            ARRAYSIZE(requestedLevels),
            D3D11_SDK_VERSION,
            &gDevice,
            &featureLevel,
            &gContext));

        winrt::check_hresult(gDevice->CreateVertexShader(g_BubbleVS, sizeof(g_BubbleVS), nullptr, &gVertexShader));
        winrt::check_hresult(gDevice->CreatePixelShader(g_BubblePS, sizeof(g_BubblePS), nullptr, &gPixelShader));

        D3D11_BUFFER_DESC constantBufferDescription{};
        constantBufferDescription.ByteWidth = sizeof(FrameConstants);
        constantBufferDescription.Usage = D3D11_USAGE_DYNAMIC;
        constantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantBufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        winrt::check_hresult(gDevice->CreateBuffer(&constantBufferDescription, nullptr, &gFrameBuffer));

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        winrt::check_hresult(gDevice->CreateSamplerState(&samplerDescription, &gSampler));

        D3D11_BLEND_DESC blendDescription{};
        blendDescription.RenderTarget[0].BlendEnable = TRUE;
        blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        winrt::check_hresult(gDevice->CreateBlendState(&blendDescription, &gBlendState));

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;
        winrt::check_hresult(gDevice->CreateRasterizerState(&rasterizerDescription, &gRasterizerState));
    }

    void CreateOverlayTargets(MonitorOverlay& overlay)
    {
        D3D11_TEXTURE2D_DESC renderDescription{};
        renderDescription.Width = static_cast<UINT>(overlay.width);
        renderDescription.Height = static_cast<UINT>(overlay.height);
        renderDescription.MipLevels = 1;
        renderDescription.ArraySize = 1;
        renderDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        renderDescription.SampleDesc.Count = 1;
        renderDescription.Usage = D3D11_USAGE_DEFAULT;
        renderDescription.BindFlags = D3D11_BIND_RENDER_TARGET;
        winrt::check_hresult(gDevice->CreateTexture2D(&renderDescription, nullptr, &overlay.renderTexture));
        winrt::check_hresult(gDevice->CreateRenderTargetView(overlay.renderTexture.Get(), nullptr, &overlay.renderTarget));

        D3D11_TEXTURE2D_DESC stagingDescription = renderDescription;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        winrt::check_hresult(gDevice->CreateTexture2D(&stagingDescription, nullptr, &overlay.stagingTexture));

        overlay.bitmapDC = CreateCompatibleDC(nullptr);
        if (!overlay.bitmapDC)
        {
            winrt::throw_last_error();
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = overlay.width;
        bitmapInfo.bmiHeader.biHeight = -overlay.height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        overlay.bitmap = CreateDIBSection(
            overlay.bitmapDC, &bitmapInfo, DIB_RGB_COLORS, &overlay.bitmapPixels, nullptr, 0);
        if (!overlay.bitmap || !overlay.bitmapPixels)
        {
            winrt::throw_last_error();
        }
        overlay.previousBitmap = static_cast<HBITMAP>(SelectObject(overlay.bitmapDC, overlay.bitmap));
    }

    void ReleaseOverlay(MonitorOverlay& overlay)
    {
        try
        {
            if (overlay.captureSession)
            {
                overlay.captureSession.Close();
            }
            if (overlay.framePool)
            {
                overlay.framePool.Close();
            }
        }
        catch (...)
        {
        }
        overlay.captureSession = nullptr;
        overlay.framePool = nullptr;
        overlay.desktopView.Reset();

        if (overlay.window)
        {
            DestroyWindow(overlay.window);
            overlay.window = nullptr;
        }
        if (overlay.bitmapDC)
        {
            if (overlay.previousBitmap)
            {
                SelectObject(overlay.bitmapDC, overlay.previousBitmap);
            }
            DeleteDC(overlay.bitmapDC);
            overlay.bitmapDC = nullptr;
        }
        if (overlay.bitmap)
        {
            DeleteObject(overlay.bitmap);
            overlay.bitmap = nullptr;
        }
    }

    void StartMonitorCapture(MonitorOverlay& overlay)
    {
        Capture::GraphicsCaptureItem item{ nullptr };
        auto interop = winrt::get_activation_factory<Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForMonitor(
            overlay.monitor,
            winrt::guid_of<Capture::GraphicsCaptureItem>(),
            winrt::put_abi(item)));

        ComPtr<IDXGIDevice> dxgiDevice;
        winrt::check_hresult(gDevice.As(&dxgiDevice));
        winrt::com_ptr<IInspectable> inspectableDevice;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectableDevice.put()));
        auto direct3DDevice = inspectableDevice.as<Direct3D11::IDirect3DDevice>();

        const auto size = item.Size();
        if (size.Width != overlay.width || size.Height != overlay.height)
        {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }

        overlay.framePool = Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            direct3DDevice,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size);
        overlay.captureSession = overlay.framePool.CreateCaptureSession(item);
        overlay.captureSession.IsCursorCaptureEnabled(false);
        if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired"))
        {
            overlay.captureSession.IsBorderRequired(false);
        }
        overlay.captureSession.StartCapture();
    }

    void InitializeBubbles(MonitorOverlay& overlay)
    {
        const UINT bubbleCount = overlay.bubbleCount;
        std::uniform_real_distribution<float> radiusDistribution(gSettings.diameterMin * 0.5f, gSettings.diameterMax * 0.5f);
        std::uniform_real_distribution<float> speedDistribution(gSettings.speedMin, gSettings.speedMax);
        std::uniform_real_distribution<float> phaseDistribution(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> swayDistribution(gSettings.swayMin, gSettings.swayMax);
        std::uniform_real_distribution<float> swayRateDistribution(gSettings.swayRateMin, gSettings.swayRateMax);
        std::uniform_real_distribution<float> cellJitter(-0.18f, 0.18f);

        const float aspect = static_cast<float>(overlay.width) / static_cast<float>(overlay.height);
        const UINT columns = std::max(1u, static_cast<UINT>(std::lround(std::sqrt(bubbleCount * aspect))));
        const UINT rows = (bubbleCount + columns - 1) / columns;
        std::vector<UINT> cellOrder(columns * rows);
        for (UINT index = 0; index < cellOrder.size(); ++index)
        {
            cellOrder[index] = index;
        }
        std::shuffle(cellOrder.begin(), cellOrder.end(), gRandom);

        const float cellWidth = static_cast<float>(overlay.width) / columns;
        const float cellHeight = static_cast<float>(overlay.height) / rows;

        overlay.bubbles.clear();
        overlay.bubbles.reserve(bubbleCount);
        for (UINT index = 0; index < bubbleCount; ++index)
        {
            Bubble bubble{};
            const UINT cell = cellOrder[index];
            const UINT column = cell % columns;
            const UINT row = cell / columns;
            bubble.radius = radiusDistribution(gRandom);
            const float maxX = std::max(bubble.radius, static_cast<float>(overlay.width) - bubble.radius);
            bubble.x = std::clamp(
                (static_cast<float>(column) + 0.5f + cellJitter(gRandom)) * cellWidth,
                bubble.radius,
                maxX);
            // 全員を画面下の外側に待機させ、時間差で下から上ってくるようにする
            bubble.y = static_cast<float>(overlay.height) + bubble.radius +
                (static_cast<float>(row) + 0.5f + cellJitter(gRandom)) * cellHeight;
            bubble.speed = speedDistribution(gRandom);
            bubble.phase = phaseDistribution(gRandom);
            bubble.sway = swayDistribution(gRandom);
            bubble.swayRate = swayRateDistribution(gRandom);
            overlay.bubbles.push_back(bubble);
        }
    }

    bool UpdateDesktopTexture(MonitorOverlay& overlay)
    {
        auto frame = overlay.framePool.TryGetNextFrame();
        if (!frame)
        {
            return static_cast<bool>(overlay.desktopView);
        }

        auto inspectableSurface = frame.Surface().as<::IInspectable>();
        ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        winrt::check_hresult(inspectableSurface->QueryInterface(IID_PPV_ARGS(&access)));

        ComPtr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
        winrt::check_hresult(gDevice->CreateShaderResourceView(texture.Get(), nullptr, &overlay.desktopView));
        return true;
    }

    void ReportFatalError(const wchar_t* message)
    {
        Log(std::wstring(L"Fatal: ") + message);
        if (!gFatalErrorReported)
        {
            gFatalErrorReported = true;
            MessageBoxW(nullptr, message, L"APB Error", MB_OK | MB_ICONERROR);
        }
        if (gControlWindow)
        {
            PostMessageW(gControlWindow, WM_CLOSE, 0, 0);
        }
    }

    void RenderOverlay(MonitorOverlay& overlay, float elapsedSeconds, float deltaSeconds)
    {
        if (!overlay.window || !overlay.framePool || !UpdateDesktopTexture(overlay))
        {
            return;
        }

        const float width = static_cast<float>(overlay.width);
        const float height = static_cast<float>(overlay.height);
        FrameConstants constants{};
        constants.width = width;
        constants.height = height;
        constants.time = elapsedSeconds;
        constants.refraction = gSettings.refraction;
        const UINT bubbleCount = static_cast<UINT>(overlay.bubbles.size());
        for (UINT index = 0; index < bubbleCount; ++index)
        {
            Bubble& bubble = overlay.bubbles[index];
            bubble.y -= bubble.speed * deltaSeconds;
            if (bubble.y + bubble.radius < 0.0f)
            {
                std::uniform_real_distribution<float> xDistribution(bubble.radius, std::max(bubble.radius, width - bubble.radius));
                std::uniform_real_distribution<float> entryOffsetDistribution(0.0f, height * 0.3f);
                std::uniform_real_distribution<float> speedDistribution(gSettings.speedMin, gSettings.speedMax);
                bubble.y = height + bubble.radius + entryOffsetDistribution(gRandom);
                bubble.x = xDistribution(gRandom);
                bubble.speed = speedDistribution(gRandom);
                bubble.phase += 1.37f;
            }

            const float swayOffset = std::sin(elapsedSeconds * bubble.swayRate + bubble.phase) * bubble.sway;
            constants.bubbles[index] = {
                std::clamp(bubble.x + swayOffset, 0.0f, width),
                bubble.y,
                bubble.radius,
                bubble.phase
            };
        }

        D3D11_MAPPED_SUBRESOURCE mappedConstants{};
        winrt::check_hresult(gContext->Map(gFrameBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedConstants));
        std::memcpy(mappedConstants.pData, &constants, sizeof(constants));
        gContext->Unmap(gFrameBuffer.Get(), 0);

        const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, width, height, 0.0f, 1.0f };
        gContext->RSSetViewports(1, &viewport);
        gContext->RSSetState(gRasterizerState.Get());
        gContext->VSSetShader(gVertexShader.Get(), nullptr, 0);
        gContext->PSSetShader(gPixelShader.Get(), nullptr, 0);
        gContext->VSSetConstantBuffers(0, 1, gFrameBuffer.GetAddressOf());
        gContext->PSSetConstantBuffers(0, 1, gFrameBuffer.GetAddressOf());
        gContext->PSSetShaderResources(0, 1, overlay.desktopView.GetAddressOf());
        gContext->PSSetSamplers(0, 1, gSampler.GetAddressOf());
        gContext->IASetInputLayout(nullptr);
        gContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        gContext->ClearRenderTargetView(overlay.renderTarget.Get(), clearColor);
        gContext->OMSetRenderTargets(1, overlay.renderTarget.GetAddressOf(), nullptr);
        const float blendFactor[4] = {};
        gContext->OMSetBlendState(gBlendState.Get(), blendFactor, 0xffffffff);
        gContext->DrawInstanced(6, bubbleCount, 0, 0);
        gContext->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
        ID3D11ShaderResourceView* nullView = nullptr;
        gContext->PSSetShaderResources(0, 1, &nullView);

        gContext->CopyResource(overlay.stagingTexture.Get(), overlay.renderTexture.Get());
        D3D11_MAPPED_SUBRESOURCE pixels{};
        winrt::check_hresult(gContext->Map(overlay.stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &pixels));
        const size_t rowBytes = static_cast<size_t>(overlay.width) * 4;
        auto* destination = static_cast<unsigned char*>(overlay.bitmapPixels);
        const auto* source = static_cast<const unsigned char*>(pixels.pData);
        for (int row = 0; row < overlay.height; ++row)
        {
            std::memcpy(
                destination + static_cast<size_t>(row) * rowBytes,
                source + static_cast<size_t>(row) * pixels.RowPitch,
                rowBytes);
        }
        gContext->Unmap(overlay.stagingTexture.Get(), 0);

        HDC screenDC = GetDC(nullptr);
        if (!screenDC)
        {
            winrt::throw_last_error();
        }
        POINT destinationPoint{ overlay.x, overlay.y };
        POINT sourcePoint{ 0, 0 };
        SIZE size{ overlay.width, overlay.height };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        const BOOL presented = UpdateLayeredWindow(
            overlay.window, screenDC, &destinationPoint, &size, overlay.bitmapDC, &sourcePoint, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screenDC);
        if (!presented)
        {
            winrt::throw_last_error();
        }
    }

    void RenderAllOverlays(float elapsedSeconds, float deltaSeconds)
    {
        try
        {
            for (auto& overlay : gOverlays)
            {
                RenderOverlay(*overlay, elapsedSeconds, deltaSeconds);
            }
        }
        catch (const winrt::hresult_error& error)
        {
            wchar_t message[256]{};
            swprintf_s(message, L"Rendering failed (0x%08X).", static_cast<unsigned int>(error.code().value));
            ReportFatalError(message);
        }
    }

    void ApplyPauseState()
    {
        const int command = IsPaused() ? SW_HIDE : SW_SHOWNOACTIVATE;
        for (auto& overlay : gOverlays)
        {
            if (overlay->window)
            {
                ShowWindow(overlay->window, command);
            }
        }
        gLastRender = GetTickCount64();
    }

    LRESULT CALLBACK OverlayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void CreateOverlayWindow(MonitorOverlay& overlay)
    {
        overlay.window = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            kOverlayClassName,
            kWindowTitle,
            WS_POPUP,
            overlay.x,
            overlay.y,
            overlay.width,
            overlay.height,
            nullptr,
            nullptr,
            gInstance,
            nullptr);
        if (!overlay.window)
        {
            winrt::throw_last_error();
        }
        if (!SetWindowDisplayAffinity(overlay.window, WDA_EXCLUDEFROMCAPTURE))
        {
            winrt::throw_last_error();
        }
    }

    void SetupOverlays()
    {
        gOverlays = CreateMonitorOverlays();
        for (auto& overlay : gOverlays)
        {
            CreateOverlayTargets(*overlay);
            CreateOverlayWindow(*overlay);
            InitializeBubbles(*overlay);
            StartMonitorCapture(*overlay);
            Log(L"Monitor DISPLAY" + std::to_wstring(overlay->displayNumber) + L": " +
                std::to_wstring(overlay->width) + L"x" + std::to_wstring(overlay->height) + L" at (" +
                std::to_wstring(overlay->x) + L"," + std::to_wstring(overlay->y) + L"), " +
                std::to_wstring(overlay->bubbleCount) + L" bubbles");
        }
        gOverlaysReady = true;
        ApplyPauseState();
    }

    void RebuildOverlays()
    {
        gOverlaysReady = false;
        for (auto& overlay : gOverlays)
        {
            ReleaseOverlay(*overlay);
        }
        gOverlays.clear();
        SetupOverlays();
    }

    void ScheduleRebuild(UINT delayMilliseconds)
    {
        if (gControlWindow)
        {
            SetTimer(gControlWindow, kRebuildTimerId, delayMilliseconds, nullptr);
        }
    }

    bool IsAutostartEnabled()
    {
        return RegGetValueW(HKEY_CURRENT_USER, kRunKeyPath, kRunValueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }

    void SetAutostart(bool enabled)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            return;
        }
        if (enabled)
        {
            const std::wstring command = L"\"" + GetExecutablePath() + L"\"";
            RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            RegDeleteValueW(key, kRunValueName);
        }
        RegCloseKey(key);
    }

    void AddTrayIcon()
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = gControlWindow;
        data.uID = kTrayIconId;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = static_cast<HICON>(LoadImageW(
            gInstance, MAKEINTRESOURCEW(kIconResourceId), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        wcscpy_s(data.szTip, L"APB");
        Shell_NotifyIconW(NIM_ADD, &data);
    }

    void RemoveTrayIcon()
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = gControlWindow;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }

    void ShowTrayBalloon(const std::wstring& text)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = gControlWindow;
        data.uID = kTrayIconId;
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = NIIF_WARNING;
        wcscpy_s(data.szInfoTitle, L"APB");
        wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void OpenSettingsFile()
    {
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", gSettingsPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32)
        {
            ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + gSettingsPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        }
    }

    std::wstring WarningsText()
    {
        std::wstring text;
        for (const auto& warning : gWarnings)
        {
            text += L"\n- " + warning;
        }
        return text;
    }

    void ReloadSettings()
    {
        LoadSettings();
        Log(L"Settings reloaded.");
        if (!gWarnings.empty())
        {
            ShowTrayBalloon(std::wstring(Tr(L"設定の一部が無効なため既定値を使用しました。", L"Some settings were invalid; defaults were used.")) + WarningsText());
        }
        ScheduleRebuild(50);
    }

    void UpdateBatteryState()
    {
        SYSTEM_POWER_STATUS status{};
        gOnBattery = GetSystemPowerStatus(&status) && status.ACLineStatus == 0;
        const bool shouldPause = gOnBattery && gSettings.onBattery == BatteryMode::Pause;
        if (shouldPause != gBatteryPaused)
        {
            gBatteryPaused = shouldPause;
            ApplyPauseState();
        }
    }

    void Housekeeping()
    {
        UpdateBatteryState();
        const FILETIME writeTime = GetSettingsWriteTime();
        if (CompareFileTime(&writeTime, &gSettingsWriteTime) != 0)
        {
            ReloadSettings();
        }
    }

    void Tick()
    {
        const ULONGLONG now = GetTickCount64();
        if (now - gLastHousekeeping >= kHousekeepingIntervalMilliseconds)
        {
            gLastHousekeeping = now;
            Housekeeping();
        }
        if (!gOverlaysReady || IsPaused())
        {
            return;
        }
        if (gOnBattery && gSettings.onBattery == BatteryMode::Reduce)
        {
            const ULONGLONG interval = static_cast<ULONGLONG>(1000.0f / gSettings.batteryFps);
            if (now - gLastRender < interval)
            {
                return;
            }
        }
        const float deltaSeconds = std::min(static_cast<float>(now - gLastRender) / 1000.0f, 0.1f);
        gLastRender = now;
        RenderAllOverlays(static_cast<float>(now - gStartTime) / 1000.0f, deltaSeconds);
    }

    void ShowTrayMenu()
    {
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }
        const std::wstring title = std::wstring(L"APB v") + kAppVersion;
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, title.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuPause, gUserPaused ? Tr(L"再開", L"Resume") : Tr(L"一時停止", L"Pause"));
        AppendMenuW(menu, MF_STRING, kMenuReload, Tr(L"設定を再読み込み", L"Reload settings"));
        AppendMenuW(menu, MF_STRING, kMenuOpenSettings, Tr(L"設定ファイルを開く", L"Open settings file"));
        AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart,
            Tr(L"Windows 起動時に実行", L"Run at Windows startup"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, Tr(L"終了", L"Exit"));

        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(gControlWindow);
        const UINT command = static_cast<UINT>(TrackPopupMenu(
            menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, 0, gControlWindow, nullptr));
        PostMessageW(gControlWindow, WM_NULL, 0, 0);
        DestroyMenu(menu);

        switch (command)
        {
        case kMenuPause:
            gUserPaused = !gUserPaused;
            ApplyPauseState();
            break;
        case kMenuReload:
            ReloadSettings();
            break;
        case kMenuOpenSettings:
            OpenSettingsFile();
            break;
        case kMenuAutostart:
            SetAutostart(!IsAutostartEnabled());
            break;
        case kMenuExit:
            PostMessageW(gControlWindow, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }
    }

    LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
        {
            const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            if (gControlWindow && IsExitCombination(key->vkCode))
            {
                PostMessageW(gControlWindow, WM_CLOSE, 0, 0);
            }
        }
        return CallNextHookEx(gKeyboardHook, code, wParam, lParam);
    }

    LRESULT CALLBACK ControlWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (gTaskbarCreatedMessage != 0 && message == gTaskbarCreatedMessage)
        {
            AddTrayIcon();
            return 0;
        }

        switch (message)
        {
        case WM_TIMER:
            if (wParam == kFrameTimerId)
            {
                Tick();
                return 0;
            }
            if (wParam == kRebuildTimerId)
            {
                KillTimer(window, kRebuildTimerId);
                try
                {
                    RebuildOverlays();
                    gRebuildRetries = 0;
                }
                catch (const winrt::hresult_error& error)
                {
                    Log(L"Rebuild failed: " + std::wstring(error.message().c_str()));
                    if (++gRebuildRetries <= kMaxRebuildRetries)
                    {
                        ScheduleRebuild(1500);
                    }
                    else
                    {
                        ReportFatalError(Tr(
                            L"ディスプレイ構成の変更後に再初期化できませんでした。APB を再起動してください。",
                            L"Could not reinitialize after the display change. Please restart APB."));
                    }
                }
                return 0;
            }
            break;
        case WM_DISPLAYCHANGE:
            gOverlaysReady = false;
            Log(L"Display configuration changed.");
            ScheduleRebuild(1000);
            return 0;
        case kTrayMessage:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP)
            {
                ShowTrayMenu();
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            KillTimer(window, kFrameTimerId);
            KillTimer(window, kRebuildTimerId);
            if (gKeyboardHook)
            {
                UnhookWindowsHookEx(gKeyboardHook);
                gKeyboardHook = nullptr;
            }
            RemoveTrayIcon();
            gOverlaysReady = false;
            for (auto& overlay : gOverlays)
            {
                ReleaseOverlay(*overlay);
            }
            Log(L"Exit.");
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void RegisterWindowClasses()
    {
        WNDCLASSEXW overlayClass{};
        overlayClass.cbSize = sizeof(overlayClass);
        overlayClass.lpfnWndProc = OverlayWindowProc;
        overlayClass.hInstance = gInstance;
        overlayClass.lpszClassName = kOverlayClassName;
        overlayClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&overlayClass))
        {
            winrt::throw_last_error();
        }

        WNDCLASSEXW controlClass{};
        controlClass.cbSize = sizeof(controlClass);
        controlClass.lpfnWndProc = ControlWindowProc;
        controlClass.hInstance = gInstance;
        controlClass.lpszClassName = kControlClassName;
        if (!RegisterClassExW(&controlClass))
        {
            winrt::throw_last_error();
        }
    }

    void ShowStartupError(const std::wstring& message)
    {
        Log(L"Startup error: " + message);
        MessageBoxW(nullptr, message.c_str(), L"APB Error", MB_OK | MB_ICONERROR);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    gInstance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    gSingleInstanceMutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
    if (gSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr,
            Tr(L"APB はすでに実行中です。", L"APB is already running."),
            L"APB", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    InitializeLog();
    Log(std::wstring(L"APB v") + kAppVersion + L" starting.");

    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        if (!Capture::GraphicsCaptureSession::IsSupported())
        {
            winrt::throw_hresult(E_NOTIMPL);
        }

        gSettingsPath = GetExecutableDirectory() + L"\\settings.ini";
        LoadSettings();
        if (!gWarnings.empty())
        {
            MessageBoxW(nullptr,
                (std::wstring(Tr(L"settings.ini の一部が無効なため、既定値を使用します。", L"Some settings in settings.ini are invalid; defaults will be used.")) + L"\n" + WarningsText()).c_str(),
                L"APB", MB_OK | MB_ICONWARNING);
        }

        CreateSharedD3DResources();
        RegisterWindowClasses();

        gControlWindow = CreateWindowExW(
            WS_EX_TOOLWINDOW, kControlClassName, kWindowTitle, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
        if (!gControlWindow)
        {
            winrt::throw_last_error();
        }

        gStartTime = GetTickCount64();
        gLastRender = gStartTime;
        gLastHousekeeping = gStartTime;
        UpdateBatteryState();
        SetupOverlays();

        gKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, instance, 0);
        if (!gKeyboardHook)
        {
            winrt::throw_last_error();
        }
        gTaskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
        AddTrayIcon();
        if (!SetTimer(gControlWindow, kFrameTimerId, kFrameIntervalMilliseconds, nullptr))
        {
            winrt::throw_last_error();
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }
    catch (const winrt::hresult_error& error)
    {
        wchar_t code[32]{};
        swprintf_s(code, L"(0x%08X): ", static_cast<unsigned int>(error.code().value));
        ShowStartupError(std::wstring(Tr(L"初期化に失敗しました ", L"Initialization failed ")) + code + error.message().c_str());
        return 1;
    }
    catch (...)
    {
        ShowStartupError(Tr(L"初期化中に予期しないエラーが発生しました。", L"An unexpected initialization error occurred."));
        return 1;
    }
}
