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

struct WAVE_FORMAT_INFO
{
    DWORD flags;
    DWORD samples;
    WORD bits;
    WORD channels;
};

bool get_wave_formats(std::vector<WAVE_FORMAT_INFO>& formats);

bool save_pcm_wave_file(LPTSTR lpszFileName, LPWAVEFORMATEX lpwf,
                        LPCVOID lpWaveData, DWORD dwDataSize);

class Recording
{
public:
    WAVEFORMATEX m_wfx;
    LONG m_nValue;
    LONG m_nMax;
    void SetInfo(WORD nChannels, DWORD nSamplesPerSec, WORD wBitsPerSample);

    Recording();
    ~Recording();

    void SetDevice(CComPtr<IMMDevice> pDevice);

    BOOL StartHearing();
    BOOL StopHearing();

    BOOL SetRecording(BOOL bRecording);

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
    std::vector<BYTE> m_wave_data;
    BOOL m_bRecording;

    static DWORD WINAPI ThreadFunction(LPVOID pContext);
    void ScanBuffer(const BYTE *pb, DWORD cb, DWORD dwFlags);
};

#endif  // ndef RECORDING_HPP_
