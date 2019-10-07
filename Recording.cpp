#include "Recording.hpp"
#include "resource.h"

Recording::Recording()
    : m_pwfx(NULL)
    , m_hShutdownEvent(NULL)
    , m_hWakeUp(NULL)
    , m_hThread(NULL)
    , m_hFile(NULL)
{
    m_hShutdownEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    m_hWakeUp = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    m_nFrames = 0;
    ::InitializeCriticalSection(&m_lock);
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
    CloseFile();

    ::DeleteCriticalSection(&m_lock);
}

void Recording::SetDevice(CComPtr<IMMDevice> pDevice)
{
    m_pDevice = pDevice;
}

BOOL Recording::OpenFile()
{
    CloseFile();

    WCHAR szFileName[] = L"sound.wav";
    MMIOINFO mi = {0};

    ::EnterCriticalSection(&m_lock);
    m_hFile = mmioOpen(szFileName, &mi, MMIO_WRITE | MMIO_CREATE);
    if (m_pwfx)
    {
        WriteHeader(m_pwfx);
    }
    ::LeaveCriticalSection(&m_lock);

    return m_hFile != NULL;
}

BOOL Recording::WriteHeader(const WAVEFORMATEX *pwfx)
{
    MMRESULT result;

    m_ckRIFF.ckid = MAKEFOURCC('R', 'I', 'F', 'F');
    m_ckRIFF.fccType = MAKEFOURCC('W', 'A', 'V', 'E');

    result = mmioCreateChunk(m_hFile, &m_ckRIFF, MMIO_CREATERIFF);
    assert(result == MMSYSERR_NOERROR);

    MMCKINFO chunk;
    chunk.ckid = MAKEFOURCC('f', 'm', 't', ' ');
    result = mmioCreateChunk(m_hFile, &chunk, 0);
    assert(result == MMSYSERR_NOERROR);

    LONG lBytesInWfx = sizeof(WAVEFORMATEX) + pwfx->cbSize;
    LONG lBytesWritten = mmioWrite(m_hFile,
        reinterpret_cast<PCHAR>(const_cast<LPWAVEFORMATEX>(pwfx)),
        lBytesInWfx);

    if (lBytesWritten != lBytesInWfx) {
        return FALSE;
    }

    result = mmioAscend(m_hFile, &chunk, 0);
    assert(result == MMSYSERR_NOERROR);

    chunk.ckid = MAKEFOURCC('f', 'a', 'c', 't');
    result = mmioCreateChunk(m_hFile, &chunk, 0);
    assert(result == MMSYSERR_NOERROR);

    DWORD frames = 0;
    lBytesWritten = mmioWrite(m_hFile, reinterpret_cast<PCHAR>(&frames), sizeof(frames));
    assert(lBytesWritten == sizeof(frames));

    result = mmioAscend(m_hFile, &chunk, 0);
    assert(result == MMSYSERR_NOERROR);

    m_ckData.ckid = MAKEFOURCC('d', 'a', 't', 'a');
    result = mmioCreateChunk(m_hFile, &m_ckData, 0);
    assert(result == MMSYSERR_NOERROR);

    return TRUE;
}

void Recording::FinishFile()
{
    MMRESULT result;

    ::EnterCriticalSection(&m_lock);
    {
        result = mmioAscend(m_hFile, &m_ckData, 0);
        assert(result == MMSYSERR_NOERROR);

        result = mmioAscend(m_hFile, &m_ckRIFF, 0);
        assert(result == MMSYSERR_NOERROR);
    }
    ::LeaveCriticalSection(&m_lock);

    CloseFile();
}

void Recording::FixupFile()
{
    CloseFile();

    WCHAR szFileName[] = L"sound.wav";
    MMIOINFO mi = {0};

    ::EnterCriticalSection(&m_lock);
    m_hFile = mmioOpen(szFileName, &mi, MMIO_WRITE | MMIO_READWRITE);
    assert(m_hFile);

    MMRESULT result;

    MMCKINFO ckRIFF = {0};
    ckRIFF.ckid = MAKEFOURCC('W', 'A', 'V', 'E');
    result = mmioDescend(m_hFile, &ckRIFF, NULL, MMIO_FINDRIFF);
    assert(result == MMSYSERR_NOERROR);

    MMCKINFO ckFact = {0};
    ckFact.ckid = MAKEFOURCC('f', 'a', 'c', 't');
    result = mmioDescend(m_hFile, &ckFact, &ckRIFF, MMIO_FINDCHUNK);
    assert(result == MMSYSERR_NOERROR);

    LONG lBytesWritten = mmioWrite(m_hFile,
        reinterpret_cast<PCHAR>(&m_nFrames),
        sizeof(m_nFrames));

    result = mmioAscend(m_hFile, &ckFact, 0);
    assert(result == MMSYSERR_NOERROR);

    ::LeaveCriticalSection(&m_lock);

    CloseFile();
}

void Recording::CloseFile()
{
    ::EnterCriticalSection(&m_lock);
    m_pwfx = NULL;
    if (m_hFile)
    {
        mmioClose(m_hFile, 0);
        m_hFile = NULL;
    }
    ::LeaveCriticalSection(&m_lock);

    ZeroMemory(&m_ckRIFF, sizeof(m_ckRIFF));
    ZeroMemory(&m_ckData, sizeof(m_ckData));
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

    hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
    assert(SUCCEEDED(hr));

    REFERENCE_TIME DevicePeriod;
    hr = m_pAudioClient->GetDevicePeriod(&DevicePeriod, NULL);
    assert(SUCCEEDED(hr));

    WAVEFORMATEX *pwfx;
    hr = m_pAudioClient->GetMixFormat(&pwfx);
    assert(SUCCEEDED(hr));

    ::EnterCriticalSection(&m_lock);
    if (m_hFile && !m_pwfx)
    {
        hr = WriteHeader(pwfx);
        assert(SUCCEEDED(hr));
    }
    m_pwfx = pwfx;
    ::LeaveCriticalSection(&m_lock);

    UINT32 nBlockAlign = m_pwfx->nBlockAlign;
    WORD wBitsPerSample = m_pwfx->wBitsPerSample;
    m_nFrames = 0;

    DWORD StreamFlags =
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_NOPERSIST |
        AUDCLNT_STREAMFLAGS_LOOPBACK;

    hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    StreamFlags,
                                    0, 0, m_pwfx, 0);
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
                                        0, 0, m_pwfx, 0);
    }
    assert(SUCCEEDED(hr));

    hr = m_pAudioClient->SetEventHandle(m_hWakeUp);
    assert(SUCCEEDED(hr));

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
            if (m_hFile)
            {
                LONG cbWritten = mmioWrite(m_hFile, reinterpret_cast<PCHAR>(pbData), cbToWrite);
            }
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

    FinishFile();
    FixupFile();

    return 0;
}
