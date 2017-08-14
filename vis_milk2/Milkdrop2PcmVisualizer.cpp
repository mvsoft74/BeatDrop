#ifdef DEBUG
#define _CRTDBG_MAP_ALLOC
#endif

#include <stdlib.h>
#include <malloc.h>
#include <crtdbg.h>

#include <windows.h>
#include <process.h>
#include <d3d9.h>
#include <math.h>
#include <dwmapi.h>

#include "plugin.h"
#include "resource.h"

#include <mutex>
#include <atomic>

#include <core/sdk/constants.h>
#include <core/sdk/IPcmVisualizer.h>
#include <core/sdk/IPlaybackRemote.h>

#define DLL_EXPORT __declspec(dllexport)
#define COMPILE_AS_DLL
#define SAMPLE_SIZE 576
#define DEFAULT_WIDTH 1280;
#define DEFAULT_HEIGHT 720;

CPlugin g_plugin;
HINSTANCE api_orig_hinstance = nullptr;
_locale_t g_use_C_locale;
char keyMappings[8];

static IDirect3D9* pD3D9 = nullptr;
static IDirect3DDevice9* pD3DDevice = nullptr;
static D3DPRESENT_PARAMETERS d3dPp;

static LONG lastWindowStyle = 0;
static LONG lastWindowStyleEx = 0;

static bool fullscreen = false;
static RECT lastRect = { 0 };

static HMODULE module = nullptr;
static std::atomic<HANDLE> thread = nullptr;
static unsigned threadId = 0;
static std::mutex pcmMutex;
static unsigned char pcmLeftIn[SAMPLE_SIZE];
static unsigned char pcmRightIn[SAMPLE_SIZE];
static unsigned char pcmLeftOut[SAMPLE_SIZE];
static unsigned char pcmRightOut[SAMPLE_SIZE];

static musik::core::sdk::IPlaybackService* playback = nullptr;

static HICON icon = nullptr;

void InitD3d(HWND hwnd, int width, int height) {
    pD3D9 = Direct3DCreate9(D3D_SDK_VERSION);

    D3DDISPLAYMODE mode;
    pD3D9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);

    UINT adapterId = g_plugin.m_adapterId;

    if (adapterId > pD3D9->GetAdapterCount()) {
        adapterId = D3DADAPTER_DEFAULT;
    }

    memset(&d3dPp, 0, sizeof(d3dPp));

    d3dPp.BackBufferCount = 1;
    d3dPp.BackBufferFormat = mode.Format;
    d3dPp.BackBufferWidth = width;
    d3dPp.BackBufferHeight = height;
    d3dPp.SwapEffect = D3DSWAPEFFECT_COPY;
    d3dPp.Flags = 0;
    d3dPp.EnableAutoDepthStencil = TRUE;
    d3dPp.AutoDepthStencilFormat = D3DFMT_D24X8;
    d3dPp.Windowed = TRUE;
    d3dPp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    d3dPp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dPp.hDeviceWindow = (HWND) hwnd;

    pD3D9->CreateDevice(
        adapterId,
        D3DDEVTYPE_HAL,
        (HWND) hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &d3dPp,
        &pD3DDevice);
}

void DeinitD3d() {
    if (pD3DDevice) {
        pD3DDevice->Release();
        pD3DDevice = nullptr;
    }

    if (pD3D9) {
        pD3D9->Release();
        pD3D9 = nullptr;
    }
}

void ToggleFullScreen(HWND hwnd) {
    if (!fullscreen) {
        ShowCursor(FALSE);

        lastWindowStyle = GetWindowLong(hwnd, GWL_STYLE);
        lastWindowStyleEx = GetWindowLongW(hwnd, GWL_EXSTYLE);
        lastWindowStyleEx &= ~WS_EX_TOPMOST;
        GetWindowRect(hwnd, &lastRect);

        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

        MONITORINFO info;
        info.cbSize = sizeof(MONITORINFO);

        GetMonitorInfoW(monitor, &info);

        int width = info.rcMonitor.right - info.rcMonitor.left;
        int height = info.rcMonitor.bottom - info.rcMonitor.top;

        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
        SetWindowPos(hwnd, HWND_TOPMOST, info.rcMonitor.left, info.rcMonitor.top, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);

        d3dPp.BackBufferWidth = width;
        d3dPp.BackBufferHeight = height;

        pD3DDevice->Reset(&d3dPp);
        fullscreen = true;
    }
    else {
        ShowCursor(TRUE);

        int width = lastRect.right - lastRect.left;
        int height = lastRect.bottom - lastRect.top;

        d3dPp.BackBufferWidth = width;
        d3dPp.BackBufferHeight = height;

        pD3DDevice->Reset(&d3dPp);
        fullscreen = false;

        SetWindowLongW(hwnd, GWL_STYLE, lastWindowStyle);
        SetWindowLongW(hwnd, GWL_EXSTYLE, lastWindowStyleEx);
        SetWindowPos(hwnd, HWND_NOTOPMOST, lastRect.left, lastRect.top, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch(uMsg) {
        case WM_CLOSE: {
            DestroyWindow( hWnd );
            UnregisterClassW(L"Direct3DWindowClass", NULL);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        }

        case WM_KEYDOWN: {
            if (playback && wParam >= VK_F1 && wParam <= VK_F8) {
                switch (wParam) {
                case VK_F1:
                    playback->PauseOrResume();
                    break;
                case VK_F2:
                    playback->Stop();
                    break;
                case VK_F3:
                    playback->Previous();
                    break;
                case VK_F4:
                    playback->Next();
                    break;
                case VK_F5:
                    playback->SetVolume(playback->GetVolume() - 0.05);
                    break;
                case VK_F6:
                    playback->SetVolume(playback->GetVolume() + 0.05);
                    break;
                case VK_F7:
                    playback->ToggleShuffle();
                    break;
                case VK_F8:
                    playback->ToggleMute();
                    break;
                }
            }
            g_plugin.PluginShellWindowProc(hWnd, uMsg, wParam, lParam);
        }
        break;

        case WM_SYSKEYDOWN: {
            if (wParam == VK_F4) {
                PostQuitMessage(0);
            }
            else if (wParam == VK_RETURN) {
                ToggleFullScreen(hWnd);
            }
            else {
                g_plugin.PluginShellWindowProc(hWnd, uMsg, wParam, lParam);
            }
            break;
        }

        default:
            return g_plugin.PluginShellWindowProc(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void RenderFrame() {
    {
        std::unique_lock<std::mutex> lock(pcmMutex);
        memcpy(pcmLeftOut, pcmLeftIn, SAMPLE_SIZE);
        memcpy(pcmRightOut, pcmRightIn, SAMPLE_SIZE);
        memset(pcmLeftIn, 0, SAMPLE_SIZE);
        memset(pcmRightIn, 0, SAMPLE_SIZE);
    }

    g_plugin.PluginRender(
        (unsigned char*) pcmLeftOut,
        (unsigned char*) pcmRightOut);
}

unsigned __stdcall CreateWindowAndRun(void* data) {
    HINSTANCE instance = (HINSTANCE) data;

#ifdef DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetBreakAlloc(60);
#endif

    // Register the windows class
    WNDCLASSW wndClass;
    wndClass.style = CS_DBLCLKS;
    wndClass.lpfnWndProc = StaticWndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = instance;
    wndClass.hIcon = NULL;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = L"Direct3DWindowClass";

    if (!RegisterClassW(&wndClass)) {
        DWORD dwError = GetLastError();
        if (dwError != ERROR_CLASS_ALREADY_EXISTS) {
            return 0;
        }
    }

    int windowWidth = DEFAULT_WIDTH;
    int windowHeight = DEFAULT_HEIGHT;

    RECT rc;
    SetRect(&rc, 0, 0, windowWidth, windowHeight);
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);

    // Create the render window
    HWND hwnd = CreateWindowW(
        L"Direct3DWindowClass",
        L"milkdrop2-musikcube",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        (rc.right - rc.left),
        (rc.bottom - rc.top),
        0,
        NULL,
        instance,
        0);

    if (!hwnd) {
        DWORD dwError = GetLastError();
        return 0;
    }

    if (!icon) {
        icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PLUGIN_ICON));
    }

    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM) icon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) icon);

    ShowWindow(hwnd, SW_SHOW);

    int lastWidth = windowWidth;
    int lastHeight = windowHeight;

    g_plugin.PluginPreInitialize(0, 0);
    InitD3d(hwnd, windowWidth, windowHeight);

    g_plugin.PluginInitialize(
        pD3DDevice,
        &d3dPp,
        hwnd,
        windowWidth,
        windowHeight);

    MSG msg;
    msg.message = WM_NULL;

    PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);
    while (WM_QUIT != msg.message) {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE) != 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            RenderFrame();
        }
    }

    g_plugin.MyWriteConfig();
    g_plugin.PluginQuit();

    DeinitD3d();

    thread = nullptr;
    threadId = 0;

    return 1;
}

void StartRenderThread(HINSTANCE instance) {
    thread = (HANDLE) _beginthreadex(
        nullptr,
        0,
        &CreateWindowAndRun,
        (void *) instance,
        0,
        &threadId);
}

#ifdef COMPILE_AS_DLL
    BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
        module = hModule;
        api_orig_hinstance = hModule;
        return true;
    }
#else
    int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow) {
        api_orig_hinstance = hInstance;
        HANDLE thread = StartRenderThread(hInstance);
        WaitForSingleObject(thread, INFINITE);
        return 0;
    }
#endif

static std::string title;

class VisaulizerPlugin : public musik::core::sdk::IPlugin {
    public:
        virtual void Destroy() { }
        virtual const char* Name() { return "Milkdrop2 IPcmVisualizer, IPlaybackRemote"; }
        virtual const char* Version() { return "0.5.0"; }
        virtual const char* Author() { return "clangen"; }
        virtual const char* Guid() { return "5533c371-ed2b-40cf-aabb-f897661aeec1"; }
        virtual bool Configurable() { return false; }
        virtual void Configure() { }
        virtual void Reload() { }
        virtual int SdkVersion() { return musik::core::sdk::SdkVersion; }
};

class Visualizer :
    public musik::core::sdk::IPcmVisualizer ,
    public musik::core::sdk::IPlaybackRemote {
        public:
            virtual const char* Name() {
                return "Milkdrop2";
            }

            virtual void Destroy() {
                this->Hide();
            }

            virtual void SetPlaybackService(musik::core::sdk::IPlaybackService* playback) {
                g_plugin.playbackService = playback;
                ::playback = playback;
            }

            virtual void OnTrackChanged(musik::core::sdk::ITrack* track) {
                if (track) {
                    char buffer[1024];
                    track->GetValue("title", buffer, 1024);
                    g_plugin.emulatedWinampSongTitle = std::string(buffer);
                }
                else {
                    g_plugin.emulatedWinampSongTitle = "";
                }
            }

            virtual void OnPlaybackStateChanged(musik::core::sdk::PlaybackState state) {

            }

            virtual void OnVolumeChanged(double volume) {

            }

            virtual void OnModeChanged(musik::core::sdk::RepeatMode repeatMode, bool shuffled) {

            }

            virtual void OnPlayQueueChanged() {

            }

            virtual void OnPlaybackTimeChanged(double time) {

            }

            virtual void Write(musik::core::sdk::IBuffer* buffer) {
                if (Visible()) {
                    float* b = buffer->BufferPointer();

                    std::unique_lock<std::mutex> lock(pcmMutex);

                    int n = 0;
                    for (int i = 0; i < buffer->Samples(); i++, n++) {
                        int x = i * 2;
                        pcmLeftIn[n % SAMPLE_SIZE] = (unsigned char)(b[i + 0] * 255.0f);
                        pcmRightIn[n % SAMPLE_SIZE] = (unsigned char)(b[i + 1] * 255.0f);
                    }
                }
            }

            virtual void Show() {
                if (!Visible()) {
                    StartRenderThread(module);
                }
            }

            virtual void Hide() {
                if (Visible()) {
                    PostThreadMessage(threadId, WM_QUIT, 0, 0);
                    WaitForSingleObject(thread, INFINITE);
                }
            }

            virtual bool Visible() {
                return thread.load() != nullptr;
            }
};

static VisaulizerPlugin visualizerPlugin;
static Visualizer visualizer;

extern "C" DLL_EXPORT musik::core::sdk::IPlugin* GetPlugin() {
    return &visualizerPlugin;
}

extern "C" DLL_EXPORT musik::core::sdk::IPcmVisualizer* GetPcmVisualizer() {
    return &visualizer;
}

extern "C" DLL_EXPORT musik::core::sdk::IPlaybackRemote* GetPlaybackRemote() {
    return &visualizer;
}

#ifdef DEBUG
struct _DEBUG_STATE {
    _DEBUG_STATE() {
    }

    ~_DEBUG_STATE() {
        _CrtDumpMemoryLeaks();
    }
};

_DEBUG_STATE ds;
#endif