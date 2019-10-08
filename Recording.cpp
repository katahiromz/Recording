#define DEFINE_GUIDS
#include "Recording.hpp"
#include "win/resource.h"

static BOOL
save_pcm_wave_file(LPTSTR lpszFileName, LPWAVEFORMATEX lpwf,
                   LPCVOID lpWaveData, DWORD dwDataSize)
{
    HMMIO    hmmio;
    MMCKINFO mmckRiff;
    MMCKINFO mmckFmt;
    MMCKINFO mmckData;
    
    hmmio = mmioOpen(lpszFileName, NULL, MMIO_CREATE | MMIO_WRITE);
    if (hmmio == NULL)
        return FALSE;

    mmckRiff.fccType = mmioStringToFOURCC(TEXT("WAVE"), 0);
    mmioCreateChunk(hmmio, &mmckRiff, MMIO_CREATERIFF);

    mmckFmt.ckid = mmioStringToFOURCC(TEXT("fmt "), 0);
    mmioCreateChunk(hmmio, &mmckFmt, 0);
    mmioWrite(hmmio, (const char *)lpwf, sizeof(PCMWAVEFORMAT));
    mmioAscend(hmmio, &mmckFmt, 0);

    mmckData.ckid = mmioStringToFOURCC(TEXT("data"), 0);
    mmioCreateChunk(hmmio, &mmckData, 0);
    mmioWrite(hmmio, (const char *)lpWaveData, dwDataSize);
    mmioAscend(hmmio, &mmckData, 0);

    mmioAscend(hmmio, &mmckRiff, 0);
    mmioClose(hmmio, 0);

    return TRUE;
}

Recording::Recording()
    : m_hShutdownEvent(NULL)
    , m_hWakeUp(NULL)
    , m_hThread(NULL)
{
    m_hShutdownEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    m_hWakeUp = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    m_nFrames = 0;
    ::InitializeCriticalSection(&m_lock);

    ZeroMemory(&m_wfx, sizeof(m_wfx));
    m_wfx.wFormatTag = WAVE_FORMAT_PCM;
    m_wfx.cbSize = 0;
    SetInfo(1, 22050, 8);
}

void Recording::SetInfo(WORD nChannels, DWORD nSamplesPerSec, WORD wBitsPerSample)
{
    m_wfx.nChannels = nChannels;
    m_wfx.nSamplesPerSec = nSamplesPerSec;
    m_wfx.wBitsPerSample = wBitsPerSample;
    m_wfx.nBlockAlign = m_wfx.wBitsPerSample * m_wfx.nChannels / 8;
    m_wfx.nAvgBytesPerSec = m_wfx.nSamplesPerSec * m_wfx.nBlockAlign;
}

Recording::~Recording()
{
    if (m_hShutdownEvent)
    {
        ::CloseHandle(m_hShutdownEvent);
        m_hShutdownEvent = NULL;
    }
    if (m_hWakeUp)
    {
        ::CloseHandle(m_hWakeUp);
        m_hWakeUp = NULL;
    }
    if (m_hThread)
    {
        ::CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    ::DeleteCriticalSection(&m_lock);
}

void Recording::SetDevice(CComPtr<IMMDevice> pDevice)
{
    m_pDevice = pDevice;
}

BOOL Recording::Start()
{
    DWORD tid = 0;
    m_hThread = ::CreateThread(NULL, 0, Recording::ThreadFunction, this, 0, &tid);
    return m_hThread != NULL;
}

BOOL Recording::Stop()
{
    SetEvent(m_hShutdownEvent);
    m_pAudioClient->Stop();
    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    ::PlaySound(NULL, NULL, 0);

    return TRUE;
}

DWORD WINAPI Recording::ThreadFunction(LPVOID pContext)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return FALSE;

    Recording *pRecording = reinterpret_cast<Recording *>(pContext);
    DWORD ret = pRecording->ThreadProc();
    CoUninitialize();
    return ret;
}

DWORD Recording::ThreadProc()
{
    HRESULT hr;

    if (m_pAudioClient)
        m_pAudioClient.Detach();

    hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
    assert(SUCCEEDED(hr));

    REFERENCE_TIME DevicePeriod;
    hr = m_pAudioClient->GetDevicePeriod(&DevicePeriod, NULL);
    assert(SUCCEEDED(hr));

    m_wave_data.clear();

    UINT32 nBlockAlign = m_wfx.nBlockAlign;
    WORD wBitsPerSample = m_wfx.wBitsPerSample;
    m_nFrames = 0;

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
    #define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
    #define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif
    DWORD StreamFlags =
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_NOPERSIST |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_LOOPBACK;

    hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    StreamFlags,
                                    0, 0, &m_wfx, 0);
    if (SUCCEEDED(hr))
    {
        ::PlaySound(MAKEINTRESOURCE(IDR_SILENT_WAV), GetModuleHandle(NULL),
                    SND_ASYNC | SND_LOOP | SND_NODEFAULT |
                    SND_RESOURCE);
    }
    else if (hr == AUDCLNT_E_WRONG_ENDPOINT_TYPE)
    {
        StreamFlags &= ~AUDCLNT_STREAMFLAGS_LOOPBACK;
        hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        StreamFlags,
                                        0, 0, &m_wfx, 0);
    }
    assert(SUCCEEDED(hr));

    hr = m_pAudioClient->SetEventHandle(m_hWakeUp);
    assert(SUCCEEDED(hr));

    if (m_pCaptureClient)
        m_pCaptureClient.Detach();

    hr = m_pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_pCaptureClient);
    assert(SUCCEEDED(hr));

    DWORD nTaskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
    assert(hTask);

    hr = m_pAudioClient->Start();
    assert(SUCCEEDED(hr));

    HANDLE waitArray[2] = { m_hShutdownEvent, m_hWakeUp };

    bool bKeepRecording = true;
    bool bFirstPacket = true;
    BYTE *pbData;
    UINT32 uNumFrames;
    DWORD dwFlags;

    for (UINT32 nPasses = 0; bKeepRecording; nPasses++)
    {
        UINT32 nNextPacketSize;
        for (hr = m_pCaptureClient->GetNextPacketSize(&nNextPacketSize);
             SUCCEEDED(hr) && nNextPacketSize > 0;
             hr = m_pCaptureClient->GetNextPacketSize(&nNextPacketSize))
        {
            hr = m_pCaptureClient->GetBuffer(&pbData, &uNumFrames, &dwFlags, NULL, NULL);
            assert(SUCCEEDED(hr));

            LONG cbToWrite = uNumFrames * nBlockAlign;

            ::EnterCriticalSection(&m_lock);
            m_wave_data.insert(m_wave_data.end(), pbData, pbData + cbToWrite);
            ::LeaveCriticalSection(&m_lock);

            m_nFrames += uNumFrames;
            hr = m_pCaptureClient->ReleaseBuffer(uNumFrames);
            assert(SUCCEEDED(hr));

            bFirstPacket = false;
        }

        DWORD waitResult = ::WaitForMultipleObjects(2, waitArray, FALSE, INFINITE);
        switch (waitResult)
        {
        case WAIT_OBJECT_0:
            bKeepRecording = false;
            break;
        case WAIT_OBJECT_0 + 1:
            break;
        default:
            bKeepRecording = false;
            break;
        }
    }

    SaveToFile();

    return 0;
}

void Recording::SaveToFile()
{
    WCHAR szFileName[] = L"sound.wav";
    save_pcm_wave_file(szFileName, &m_wfx,
                       m_wave_data.data(), m_wave_data.size());
}
