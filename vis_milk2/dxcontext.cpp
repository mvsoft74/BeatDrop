/*
LICENSE
-------
Copyright 2005-2013 Nullsoft, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

* Neither the name of Nullsoft nor the names of its contributors may be used to
endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "DXContext.h"
#include "utility.h"
#include "shell_defines.h"
#include <strsafe.h>

DXContext::DXContext(LPDIRECT3DDEVICE9 device, wchar_t* szIniFile)
{
    m_szWindowCaption[0] = 0;
    m_hwnd = NULL;
    m_lpDevice = device;
    m_hmod_d3d9 = NULL;
    m_hmod_d3dx9 = NULL;
    m_zFormat = D3DFMT_UNKNOWN;
    for (int i = 0; i<MAX_DXC_ADAPTERS; i++)
        m_orig_windowed_mode_format[i] = D3DFMT_UNKNOWN;
    m_ordinal_adapter = D3DADAPTER_DEFAULT;
    m_winamp_minimized = 0;
    m_truly_exiting = 0;
    m_bpp = 0;
    m_frame_delay = 0;
    StringCbCopyW(m_szIniFile, sizeof(m_szIniFile), szIniFile);
    m_szDriver[0] = 0;
    m_szDesc[0] = 0;

    // clear the error register

    m_lastErr = S_OK;

    // clear the active flag

    m_ready = FALSE;
}

DXContext::~DXContext()
{
    Internal_CleanUp();
}

void DXContext::Internal_CleanUp()
{
    // clear active flag
    m_ready = FALSE;
    /*

    // release 3D interfaces
    SafeRelease(m_lpDevice);
    SafeRelease(m_lpD3D);

    // destroy the window
    if (m_truly_exiting)
    {
    // somebody else will destroy the window for us!
    m_hwnd = NULL;
    if (m_hmod_d3d9)
    {
    FreeLibrary(m_hmod_d3d9);
    m_hmod_d3d9 = NULL;
    }

    if (m_hmod_d3dx9)
    {
    m_hmod_d3dx9 = NULL;
    }
    }*/
}

// {0000000A-000C-0010-FF7B-01014263450C}
const GUID avs_guid =
{ 10, 12, 16,{ 255, 123, 1, 1, 66, 99, 69, 12 } };

BOOL DXContext::Internal_Init(DXCONTEXT_PARAMS *pParams, BOOL bFirstInit)
{
    memcpy(&m_current_mode, pParams, sizeof(DXCONTEXT_PARAMS));
    //	memset(&myWindowState,0,sizeof(myWindowState));

    // various checks
    if (m_current_mode.screenmode != WINDOWED)
        m_current_mode.m_skin = 0;

    /*
    // 3. get the smallest single rectangle that encloses ALL the monitors on the desktop:
    SetRect(&m_all_monitors_rect, 0, 0, 0, 0);
    EnumDisplayMonitors(NULL, NULL, MyMonitorEnumProc, (LPARAM)&m_all_monitors_rect);
    */

    // 4. some DirectX- / DDraw-specific stuff.  Also determine hPluginMonitor.
    /*
    HMONITOR hPluginMonitor = NULL;
    {
    D3DADAPTER_IDENTIFIER9 temp;

    // find the ordinal # of the adapter whose GUID matches what the user picked from the config panel,
    // and whose DeviceName matches as well.
    // if no match found, use D3DADAPTER_DEFAULT.
    m_ordinal_adapter = D3DADAPTER_DEFAULT;
    int nAdapters = m_lpD3D->GetAdapterCount();
    {
    for (int i=0; i<nAdapters; i++)
    {
    if ((m_lpD3D->GetAdapterIdentifier(i, / *D3DENUM_NO_WHQL_LEVEL* / 0, &temp) == D3D_OK) &&
    (memcmp(&temp.DeviceIdentifier, &m_current_mode.adapter_guid, sizeof(GUID))==0) &&
    !strcmp(temp.DeviceName, m_current_mode.adapter_devicename)
    )
    {
    m_ordinal_adapter = i;
    break;
    }
    }
    }

    if (m_lpD3D->GetAdapterIdentifier(m_ordinal_adapter, / *D3DENUM_NO_WHQL_LEVEL* / 0, &temp) == D3D_OK)
    {
    StringCbCopy(m_szDriver, sizeof(m_szDriver), temp.Driver);
    StringCbCopy(m_szDesc, sizeof(m_szDesc), temp.Description);
    }

    int caps_ok = 0;
    int caps_tries = 0;
    int changed_fs_disp_mode;

    // try to get the device caps for the adapter selected from the config panel.
    // if GetDeviceCaps() fails, it's probably because the adapter has been
    // removed from the system (or disabled), so we try again with other adapter(s).
    do
    {
    changed_fs_disp_mode = 0;

    SetRect(&m_monitor_rect, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    // get bounding rect of the monitor attached to the adapter (to assist w/window positioning)
    // note: in vert/horz span setups (psuedo-multimon),
    //       this will be 2048x768 or 1024x1536 or something like that.
    hPluginMonitor = m_lpD3D->GetAdapterMonitor(m_ordinal_adapter);
    / *if (hPluginMonitor)
    {
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(hPluginMonitor, &mi))
    {
    memcpy(&m_monitor_rect, &mi.rcMonitor, sizeof(RECT));
    memcpy(&m_monitor_work_rect, &mi.rcWork, sizeof(RECT));
    }
    }* /

    if (bFirstInit)
    {
    for (int i=0; i<min(nAdapters, MAX_DXC_ADAPTERS); i++)
    {
    // if this is the first call to Init, get the display mode's original color format,
    // before we go changing it:
    D3DDISPLAYMODE d3ddm;
    if (FAILED(m_lpD3D->GetAdapterDisplayMode(i, &d3ddm)))
    {
    d3ddm.Format = D3DFMT_UNKNOWN;
    }
    m_orig_windowed_mode_format[i] = d3ddm.Format;
    }
    }

    // figure out pixel (color) format for back buffer: (m_current_mode.display_mode.Format)
    if (m_current_mode.screenmode!=FULLSCREEN && m_ordinal_adapter < MAX_DXC_ADAPTERS)
    m_current_mode.display_mode.Format = m_orig_windowed_mode_format[m_ordinal_adapter];
    // else
    // for fullscreen, use what they gave us

    if (m_current_mode.display_mode.Format == D3DFMT_UNKNOWN ||
    !TestFormat(m_ordinal_adapter, m_current_mode.display_mode.Format))
    {
    // if they try to run the plugin without ever running the config panel
    // first (& pressing OK), then the fullscreen pixelformat hasn't been
    // chosen... so we try all the possilibities until one works:
    if (TestFormat(m_ordinal_adapter,D3DFMT_A8R8G8B8)) m_current_mode.display_mode.Format = D3DFMT_A8R8G8B8;
    else if (TestFormat(m_ordinal_adapter,D3DFMT_X8R8G8B8)) m_current_mode.display_mode.Format = D3DFMT_X8R8G8B8;
    else if (TestFormat(m_ordinal_adapter,D3DFMT_R8G8B8)) m_current_mode.display_mode.Format = D3DFMT_R8G8B8  ;
    else if (TestFormat(m_ordinal_adapter,D3DFMT_R5G6B5)) m_current_mode.display_mode.Format = D3DFMT_R5G6B5  ;
    else if (TestFormat(m_ordinal_adapter,D3DFMT_X1R5G5B5)) m_current_mode.display_mode.Format = D3DFMT_X1R5G5B5;
    else if (TestFormat(m_ordinal_adapter,D3DFMT_A1R5G5B5)) m_current_mode.display_mode.Format = D3DFMT_A1R5G5B5;
    else if (TestFormat(m_ordinal_adapter,D3DFMT_A4R4G4B4)) m_current_mode.display_mode.Format = D3DFMT_A4R4G4B4;
    else if (TestFormat(m_ordinal_adapter,D3DFMT_X4R4G4B4)) m_current_mode.display_mode.Format = D3DFMT_X4R4G4B4;
    }

    if (m_current_mode.display_mode.Format==D3DFMT_UNKNOWN)
    {
    wchar_t title[64];
    m_lastErr = DXC_ERR_FORMAT;
    MessageBoxW(m_hwnd, WASABI_API_LNGSTRINGW(IDS_DIRECTX_INIT_FAILED),
    WASABI_API_LNGSTRINGW_BUF(IDS_MILKDROP_ERROR, title, 64),
    MB_OK|MB_SETFOREGROUND|MB_TOPMOST);
    return FALSE;
    }

    if (m_current_mode.screenmode == FULLSCREEN)
    changed_fs_disp_mode = CheckAndCorrectFullscreenDispMode(m_ordinal_adapter, &m_current_mode.display_mode);

    // figure out pixel format of the z-buffer: (m_zFormat)
    m_zFormat = D3DFMT_UNKNOWN;
    / *
    if      (TestDepth(m_ordinal_adapter,D3DFMT_D32         )) m_zFormat=D3DFMT_D32;
    else if (TestDepth(m_ordinal_adapter,D3DFMT_D24S8       )) m_zFormat=D3DFMT_D24S8;
    else if (TestDepth(m_ordinal_adapter,D3DFMT_D24X4S4     )) m_zFormat=D3DFMT_D24X4S4;
    else if (TestDepth(m_ordinal_adapter,D3DFMT_D24X8       )) m_zFormat=D3DFMT_D24X8;
    else if (TestDepth(m_ordinal_adapter,D3DFMT_D16         )) m_zFormat=D3DFMT_D16;
    else if (TestDepth(m_ordinal_adapter,D3DFMT_D15S1       )) m_zFormat=D3DFMT_D15S1;
    else if (TestDepth(m_ordinal_adapter,D3DFMT_D16_LOCKABLE)) m_zFormat=D3DFMT_D16_LOCKABLE;
    * /

    // get device caps:
    memset(&m_caps, 0, sizeof(m_caps));
    if (FAILED(m_lpD3D->GetDeviceCaps(m_ordinal_adapter, D3DDEVTYPE_HAL, &m_caps)))
    {
    // that adapter was found in the system, but it might be disabled
    // (i.e. 'extend my Windows desktop onto this monitor') is unchecked)
    // so, try other adapters (try all sequentially).

    if (caps_tries < nAdapters)
    {
    // try again, this time using the default adapter:
    m_ordinal_adapter = caps_tries;
    caps_tries++;
    }
    else
    {
    wchar_t title[64];
    m_lastErr = DXC_ERR_CAPSFAIL;
    MessageBoxW(m_hwnd, WASABI_API_LNGSTRINGW(IDS_DXC_ERR_CAPSFAIL),
    WASABI_API_LNGSTRINGW_BUF(IDS_MILKDROP_ERROR, title, 64),
    MB_OK|MB_SETFOREGROUND|MB_TOPMOST);
    return FALSE;
    }
    }
    else
    {
    caps_ok = 1;
    }
    }
    while (!caps_ok);

    if (changed_fs_disp_mode)
    {
    wchar_t title[64];
    MessageBoxW(m_hwnd, WASABI_API_LNGSTRINGW(IDS_FS_DISPLAY_MODE_SELECTED_IS_INVALID),
    WASABI_API_LNGSTRINGW_BUF(IDS_MILKDROP_WARNING, title, 64),
    MB_OK|MB_SETFOREGROUND|MB_TOPMOST);
    }

    switch (m_current_mode.display_mode.Format)
    {
    case D3DFMT_R8G8B8  : m_bpp = 32; break;
    case D3DFMT_A8R8G8B8: m_bpp = 32; break;
    case D3DFMT_X8R8G8B8: m_bpp = 32; break;
    case D3DFMT_R5G6B5  : m_bpp = 16; break;
    case D3DFMT_X1R5G5B5: m_bpp = 16; break;
    case D3DFMT_A1R5G5B5: m_bpp = 16; break;
    case D3DFMT_A8R3G3B2: m_bpp = 16; break;
    case D3DFMT_A4R4G4B4: m_bpp = 16; break;
    case D3DFMT_X4R4G4B4: m_bpp = 16; break;
    case D3DFMT_R3G3B2  : m_bpp =  8; break; // misleading?  implies a palette...
    }
    }*/

    // 5. set m_monitor_rect and m_monitor_work_rect.
    /*if (hPluginMonitor)
    {
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(hPluginMonitor, &mi))
    {
    m_monitor_rect = mi.rcMonitor;
    m_monitor_rect_orig = mi.rcMonitor;
    m_monitor_work_rect = mi.rcWork;
    m_monitor_work_rect_orig = mi.rcWork;
    }
    }*/

    // 6. embedded window stuff [where the plugin window is integrated w/winamp]
    /*if (m_current_mode.m_skin)
    {
    // set up the window's position on screen
    // note that we'd prefer to set the CLIENT size we want, but we can't, so we'll just do
    // this here, and later, adjust the client rect size to what's left...
    int size = GetWindowedModeAutoSize(0);  // note: requires 'm_monitor_rect' has been set!
    myWindowState.r.left   = GetPrivateProfileIntW(L"settings",L"avs_wx",64,m_szIniFile);
    myWindowState.r.top    = GetPrivateProfileIntW(L"settings",L"avs_wy",64,m_szIniFile);
    myWindowState.r.right  = myWindowState.r.left + GetPrivateProfileIntW(L"settings",L"avs_ww",size+24,m_szIniFile);
    myWindowState.r.bottom = myWindowState.r.top  + GetPrivateProfileIntW(L"settings",L"avs_wh",size+40,m_szIniFile);

    // only works on winamp 2.90+!
    int success = 0;
    if (GetWinampVersion(mod1.hwndParent) >= 0x2900)
    {
    SET_EMBED_GUID((&myWindowState), avs_guid);
    myWindowState.flags |= EMBED_FLAGS_NOTRANSPARENCY;
    HWND (*e)(embedWindowState *v);
    *(void**)&e = (void *)SendMessage(mod1.hwndParent,WM_WA_IPC,(LPARAM)0,IPC_GET_EMBEDIF);
    if (e)
    {
    m_current_mode.parent_window = e(&myWindowState);
    if (m_current_mode.parent_window)
    {
    SetWindowText(m_current_mode.parent_window, m_szWindowCaption);
    success = 1;
    }
    }
    }

    if (!success)
    m_current_mode.m_skin = 0;
    }

    // remember the client rect that was originally desired...
    RECT windowed_mode_desired_client_rect;
    windowed_mode_desired_client_rect.top    = GetPrivateProfileIntW(L"settings",L"nMainWndTop",-1,m_szIniFile);
    windowed_mode_desired_client_rect.left   = GetPrivateProfileIntW(L"settings",L"nMainWndLeft",-1,m_szIniFile);
    windowed_mode_desired_client_rect.right  = GetPrivateProfileIntW(L"settings",L"nMainWndRight",-1,m_szIniFile);
    windowed_mode_desired_client_rect.bottom = GetPrivateProfileIntW(L"settings",L"nMainWndBottom",-1,m_szIniFile);

    // ...and in case windowed mode init fails severely,
    // set it up to try next time for a simple 256x256 window.
    WriteSafeWindowPos();*/

    // 7. create the window, if not already created
    /*if (!m_hwnd)
    {
    m_hwnd = CreateWindowEx(
    MY_EXT_WINDOW_STYLE, // extended style
    MAKEINTATOM(m_classAtom), // class
    m_szWindowCaption, // caption
    MY_WINDOW_STYLE, // style
    0, // left
    0, // top
    256,  // temporary width
    256,  // temporary height
    m_current_mode.parent_window,  // parent window
    NULL, // menu
    m_hInstance, // instance
    (LPVOID)m_uWindowLong
    ); // parms

    if (!m_hwnd)
    {
    wchar_t title[64];
    m_lastErr = DXC_ERR_CREATEWIN;
    MessageBoxW(m_hwnd, WASABI_API_LNGSTRINGW(IDS_CREATEWINDOW_FAILED),
    WASABI_API_LNGSTRINGW_BUF(IDS_MILKDROP_ERROR, title, 64),
    MB_OK|MB_SETFOREGROUND|MB_TOPMOST);
    return FALSE;
    }

    SendMessage(m_hwnd_winamp, WM_WA_IPC, (WPARAM)m_hwnd, IPC_SETVISWND);

    if (m_current_mode.m_skin)
    {
    if (GetWinampVersion(mod1.hwndParent) < 0x5051)
    ShowWindow(m_current_mode.parent_window,SW_SHOWNA); // showing the parent wnd will make it size the child, too
    else
    SendMessage(m_current_mode.parent_window, WM_USER+102, 0, 0); // benski> major hack alert. winamp's embedwnd will call ShowWindow in response.  SendMessage moves us over to the main thread (we're currently sitting on the viz thread)
    }
    }
    */

    // get device caps:
    memset(&m_caps, 0, sizeof(m_caps));
    m_lpDevice->GetDeviceCaps(&m_caps);
    m_bpp = 32;

    // set initial viewport
    //	SetViewport();

    // return success
    m_ready = TRUE;
    // benski> a little hack to get the window size correct. it seems to work
    /*
    if (m_current_mode.screenmode==WINDOWED)
    PostMessage(m_hwnd, WM_USER+555, 0, 0);*/
    return TRUE;
}

BOOL DXContext::StartOrRestartDevice(DXCONTEXT_PARAMS *pParams)
{

    // call this to [re]initialize the DirectX environment with new parameters.
    // examples: startup; toggle windowed/fullscreen mode; change fullscreen resolution;
    //   and so on.
    // be sure to clean up all your DirectX stuff first (textures, vertex buffers,
    //   D3DX allocations, etc.) and reallocate it afterwards!

    // note: for windowed mode, 'pParams->disp_mode' (w/h/r/f) is ignored.

    if (!m_ready)
    {
        // first-time init: create a fresh new device
        return Internal_Init(pParams, TRUE);
    }
    else
    {
        // re-init: preserve the DX9 object (m_lpD3D),
        // but destroy and re-create the DX9 device (m_lpDevice).
        m_ready = FALSE;

        //SafeRelease(m_lpDevice);
        // but leave the D3D object!

        //		RestoreWinamp();
        return Internal_Init(pParams, FALSE);
    }
}

HWND DXContext::GetHwnd() { return nullptr; }
bool DXContext::OnUserResizeWindow(RECT* w, RECT* c) { return true; }
bool DXContext::TempIgnoreDestroyMessages() { return false; }
void DXContext::SaveWindow() { }