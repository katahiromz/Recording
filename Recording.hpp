#ifndef RECORDING_HPP_
#define RECORDING_HPP_

#include <windows.h>
#include <mmsystem.h>
#ifdef DEFINE_GUIDS
#include <initguid.h>
#endif
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include "CComPtr.hpp"
#include <vector>
#include <cstdio>

class Recording
{
public:
    Recording();
    ~Recording();

    void SetDevice(CComPtr<IMMDevice> pDevice);

    BOOL Start();
    BOOL Stop();

    void SaveToFile();

    DWORD ThreadProc();

protected:
    HANDLE m_hShutdownEvent;
    HANDLE m_hWakeUp;
    HANDLE m_hThread;
    CComPtr<IMMDevice> m_pDevice;
    CComPtr<IAudioClient> m_pAudioClient;
    CComPtr<IAudioCaptureClient> m_pCaptureClient;
    MMCKINFO m_ckRIFF;
    MMCKINFO m_ckData;
    CRITICAL_SECTION m_lock;
    UINT32 m_nFrames;
    WAVEFORMATEX m_wfx;
    std::vector<BYTE> m_wave_data;

    static DWORD WINAPI ThreadFunction(LPVOID pContext);
};

#endif  // ndef RECORDING_HPP_
