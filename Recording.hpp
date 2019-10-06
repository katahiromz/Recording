#ifndef RECORDING_HPP_
#define RECORDING_HPP_

#include <windows.h>
#include <mmsystem.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include "CComPtr.hpp"
#include <cstdio>

class Recording
{
public:
    Recording();
    ~Recording();

    void SetDevice(CComPtr<IMMDevice> pDevice);

    BOOL OpenFile();
    BOOL Start();
    BOOL Stop();
    void CloseFile();

    DWORD ThreadProc();

protected:
    WAVEFORMATEX *m_pwfx;
    HANDLE m_hShutdownEvent;
    HANDLE m_hWakeUp;
    HANDLE m_hThread;
    HMMIO m_hFile;
    CComPtr<IMMDevice> m_pDevice;
    CComPtr<IAudioClient> m_pAudioClient;
    CComPtr<IAudioCaptureClient> m_pCaptureClient;
    MMCKINFO m_ckRIFF;
    MMCKINFO m_ckData;
    CRITICAL_SECTION m_lock;
    UINT32 m_nFrames;

    static DWORD WINAPI ThreadFunction(LPVOID pContext);
    BOOL WriteHeader(const WAVEFORMATEX *pwfx);
    void FixupFile();
    void FinishFile();
};

#endif  // ndef RECORDING_HPP_
