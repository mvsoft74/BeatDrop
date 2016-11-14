#ifdef DEBUG
#define _CRTDBG_MAP_ALLOC
#endif

#include <stdlib.h>
#include <malloc.h>
#include <crtdbg.h>

#include <windows.h>
#include <process.h>
#include <d3d9.h>
#include "plugin.h"
#include <math.h>

#include <mutex>
#include <atomic>

#include <core/sdk/IPcmVisualizer.h>

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

static HMODULE module = nullptr;
static std::atomic<HANDLE> thread = nullptr;
static unsigned threadId = 0;

static std::mutex pcmMutex;
static unsigned char pcmLeftIn[SAMPLE_SIZE];
static unsigned char pcmRightIn[SAMPLE_SIZE];
static unsigned char pcmLeftOut[SAMPLE_SIZE];
static unsigned char pcmRightOut[SAMPLE_SIZE];

void InitD3d(HWND hwnd, int width, int height) {
    pD3D9 = Direct3DCreate9(D3D_SDK_VERSION);

    D3DDISPLAYMODE mode;
    pD3D9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);

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
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        (HWND) hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &d3dPp,
        &pD3DDevice );
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

static bool resized = false;

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

        case WM_CHAR: {
            g_plugin.HandleRegularKey(wParam);
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
    }

    g_plugin.PluginRender(
        (unsigned char*) pcmLeftIn,
        (unsigned char*) pcmRightIn);
}

unsigned __stdcall CreateWindowAndRun(void* data) {
    HINSTANCE instance = (HINSTANCE) data;

#ifdef DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetBreakAlloc(60);
#endif

    // Find the window's initial size, but it might be changed later


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

class Visualizer : public musik::core::audio::IPcmVisualizer {
    public:
        virtual const char* Name() {
            return "milkdrop2";
        };

        virtual const char* Version() {
            return "0.1.2";
        };

        virtual const char* Author() {
            return "clangen";
        };

        virtual void Destroy() {
            this->Hide();
            delete this;
        }

        virtual void Write(musik::core::audio::IBuffer* buffer) {
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

extern "C" DLL_EXPORT musik::core::IPlugin* GetPlugin() {
    return new Visualizer();
}

extern "C" DLL_EXPORT musik::core::audio::IPcmVisualizer* GetPcmVisualizer() {
    return new Visualizer();
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