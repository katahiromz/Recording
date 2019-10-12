#include "../Recording.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <strsafe.h>
#include "resource.h"

void ErrorBoxDx(HWND hwnd, LPCTSTR pszText)
{
    MessageBox(hwnd, pszText, NULL, MB_ICONERROR);
}

LPTSTR LoadStringDx(INT nID)
{
    static UINT s_index = 0;
    const UINT cchBuffMax = 1024;
    static TCHAR s_sz[2][cchBuffMax];

    TCHAR *pszBuff = s_sz[s_index];
    s_index = (s_index + 1) % ARRAYSIZE(s_sz);
    pszBuff[0] = 0;
    if (!::LoadString(NULL, nID, pszBuff, cchBuffMax))
        assert(0);
    return pszBuff;
}

BOOL get_device_name(CComPtr<IMMDevice> pMMDevice, std::wstring& name)
{
    name.clear();

    CComPtr<IPropertyStore> pPropertyStore;
    HRESULT hr = pMMDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
    if (FAILED(hr))
        return FALSE;

    PROPVARIANT pv;
    PropVariantInit(&pv);
    hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &pv);
    if (FAILED(hr))
        return FALSE;

    if (VT_LPWSTR != pv.vt)
    {
        name = L"(unknown)";
        PropVariantClear(&pv);
        return FALSE;
    }

    name = pv.pwszVal;
    PropVariantClear(&pv);
    return TRUE;
}

typedef std::vector<CComPtr<IMMDevice> > devices_t;

BOOL get_devices(devices_t& devices)
{
    devices.clear();

    // get an IMMDeviceEnumerator
    HRESULT hr;
    CComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&pMMDeviceEnumerator);
    if (FAILED(hr))
    {
        return FALSE;
    }

    // get an IMMDeviceCollection
    CComPtr<IMMDeviceCollection> pMMDeviceCollection;
    hr = pMMDeviceEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE,
                                                 &pMMDeviceCollection);
    if (FAILED(hr))
    {
        return FALSE;
    }

    // get the number of devices
    UINT nCount = 0;
    hr = pMMDeviceCollection->GetCount(&nCount);
    if (FAILED(hr) || nCount == 0)
    {
        return FALSE;
    }

    // enumerate devices
    for (UINT i = 0; i < nCount; ++i)
    {
        // get a device
        CComPtr<IMMDevice> pMMDevice;
        hr = pMMDeviceCollection->Item(i, &pMMDevice);
        if (FAILED(hr))
        {
            return FALSE;
        }

        devices.push_back(pMMDevice);
    }

    return TRUE;
}

class MMainWnd
{
    HINSTANCE m_hInst;
    HWND m_hwnd;
    BOOL m_bInit;
    devices_t m_devices;
    Recording m_rec;
    std::vector<WAVE_FORMAT_INFO> m_formats;

public:
    MMainWnd(HINSTANCE hInst)
        : m_hInst(hInst)
        , m_hwnd(NULL)
        , m_bInit(FALSE)
    {
        ::InitCommonControls();

        get_wave_formats(m_formats);
    }

    BOOL OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
    {
        if (!get_devices(m_devices))
        {
            ErrorBoxDx(hwnd, TEXT("DoGetDevices"));
            EndDialog(hwnd, IDCLOSE);
            return FALSE;
        }

        HWND hCmb1 = GetDlgItem(hwnd, cmb1);
        std::wstring name;
        for (auto& dev : m_devices)
        {
            get_device_name(dev, name);
            ComboBox_AddString(hCmb1, name.c_str());
        }
        ComboBox_SetCurSel(hCmb1, 0);

        HWND hCmb2 = GetDlgItem(hwnd, cmb2);
        for (auto& format : m_formats)
        {
            TCHAR szText[64];
            StringCbPrintf(szText, sizeof(szText),
                LoadStringDx(IDS_FORMAT),
                format.samples, format.bits, format.channels);
            ComboBox_AddString(hCmb2, szText);
        }
        ComboBox_SetCurSel(hCmb2, 0);

        EnableWindow(GetDlgItem(hwnd, psh1), TRUE);
        EnableWindow(GetDlgItem(hwnd, psh2), FALSE);
        EnableWindow(GetDlgItem(hwnd, cmb1), TRUE);
        EnableWindow(GetDlgItem(hwnd, cmb2), TRUE);

        m_bInit = TRUE;
        UpdateDevice(hwnd);

        m_rec.StartHearing();
        SetTimer(hwnd, 999, 300, NULL);

        return TRUE;
    }

    void UpdateDevice(HWND hwnd)
    {
        if (!m_bInit)
            return;

        HWND hCmb1 = GetDlgItem(hwnd, cmb1);
        INT iDev = ComboBox_GetCurSel(hCmb1);
        if (iDev == CB_ERR || size_t(iDev) >= m_devices.size())
            return;

        HWND hCmb2 = GetDlgItem(hwnd, cmb2);
        INT iFormat = ComboBox_GetCurSel(hCmb2);
        if (iFormat == CB_ERR || size_t(iFormat) >= m_formats.size())
            return;

        auto& format = m_formats[iFormat];
        m_rec.SetInfo(format.channels, format.samples, format.bits);
        m_rec.SetDevice(m_devices[iDev]);
    }

    void OnCmb1(HWND hwnd)
    {
        KillTimer(hwnd, 999);
        m_rec.StopHearing();

        UpdateDevice(hwnd);

        m_rec.StartHearing();
        SetTimer(hwnd, 999, 300, NULL);
    }

    void OnCmb2(HWND hwnd)
    {
        KillTimer(hwnd, 999);
        m_rec.StopHearing();

        UpdateDevice(hwnd);

        m_rec.StartHearing();
        SetTimer(hwnd, 999, 300, NULL);
    }

    void OnPsh1(HWND hwnd)
    {
        m_rec.SetRecording(TRUE);

        EnableWindow(GetDlgItem(hwnd, psh1), FALSE);
        EnableWindow(GetDlgItem(hwnd, psh2), TRUE);
        EnableWindow(GetDlgItem(hwnd, cmb1), FALSE);
        EnableWindow(GetDlgItem(hwnd, cmb2), FALSE);
    }

    void OnPsh2(HWND hwnd)
    {
        KillTimer(hwnd, 999);
        m_rec.StopHearing();

        EnableWindow(GetDlgItem(hwnd, psh1), TRUE);
        EnableWindow(GetDlgItem(hwnd, psh2), FALSE);
        EnableWindow(GetDlgItem(hwnd, cmb1), TRUE);
        EnableWindow(GetDlgItem(hwnd, cmb2), TRUE);

        SendDlgItemMessage(hwnd, scr1, PBM_SETPOS, 0, 0);

        m_rec.StartHearing();
        SetTimer(hwnd, 999, 300, NULL);
    }

    void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
    {
        switch (id)
        {
        case IDOK:
        case IDCANCEL:
            KillTimer(hwnd, 999);
            m_rec.StopHearing();

            SendDlgItemMessage(hwnd, scr1, PBM_SETPOS, 0, 0);
            EndDialog(hwnd, id);
            break;
        case psh1:
            OnPsh1(hwnd);
            break;
        case psh2:
            OnPsh2(hwnd);
            break;
        case cmb1:
            if (codeNotify == CBN_SELCHANGE)
            {
                OnCmb1(hwnd);
            }
            break;
        case cmb2:
            if (codeNotify == CBN_SELCHANGE)
            {
                OnCmb2(hwnd);
            }
            break;
        }
    }

    void OnTimer(HWND hwnd, UINT id)
    {
        LONG nValue = m_rec.m_nValue;
        LONG nMax = m_rec.m_nMax;
        SendDlgItemMessage(hwnd, scr1, PBM_SETRANGE32, 0, nMax);
        SendDlgItemMessage(hwnd, scr1, PBM_SETPOS, nValue, 0);
    }

    virtual INT_PTR CALLBACK
    DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
            HANDLE_MSG(hwnd, WM_INITDIALOG, OnInitDialog);
            HANDLE_MSG(hwnd, WM_COMMAND, OnCommand);
            HANDLE_MSG(hwnd, WM_TIMER, OnTimer);
        }
        return 0;
    }

    static MMainWnd *GetUser(HWND hwnd)
    {
        return reinterpret_cast<MMainWnd *>(GetWindowLongPtr(hwnd, DWLP_USER));
    }

    static INT_PTR CALLBACK
    DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        MMainWnd *pMainWnd;
        if (uMsg == WM_INITDIALOG)
        {
            assert(lParam);
            pMainWnd = reinterpret_cast<MMainWnd *>(lParam);
            pMainWnd->m_hwnd = hwnd;
            SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)pMainWnd);
        }
        else
        {
            pMainWnd = MMainWnd::GetUser(hwnd);
        }
        if (pMainWnd)
        {
            return pMainWnd->DialogProcDx(hwnd, uMsg, wParam, lParam);
        }
        return 0;
    }

    INT_PTR DialogBoxDx(HWND hwnd, LPCTSTR pszTemplate)
    {
        return ::DialogBoxParam(m_hInst, pszTemplate, hwnd,
                                MMainWnd::DialogProc, (LPARAM)this);
    }
};


INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        ErrorBoxDx(NULL, TEXT("CoInitializeEx"));
        return EXIT_FAILURE;
    }

    {
        MMainWnd mainWnd(hInstance);
        mainWnd.DialogBoxDx(NULL, MAKEINTRESOURCE(100));
    }

    CoUninitialize();
    return 0;
}
