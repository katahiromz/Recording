#include "Recording.hpp"

int JustDoIt(INT iDev)
{
    CComPtr<IMMDevice> pDevice;
    CComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
    CComPtr<IMMDeviceCollection> pMMDeviceCollection;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, 
        __uuidof(IMMDeviceEnumerator),
        (void**)&pMMDeviceEnumerator);
    assert(SUCCEEDED(hr));

    pMMDeviceEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &pMMDeviceCollection);
    assert(pMMDeviceCollection);

    pMMDeviceCollection->Item(iDev, &pDevice);
    assert(pDevice);

    Recording rec;
    rec.SetDevice(pDevice);

    rec.OpenFile();

    rec.Start();
    puts("Press Enter key to stop recording");
    getchar();
    rec.Stop();

    puts("Finish.");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        puts("Usage: console <device-number>");
        return -1;
    }

    PlaySound(MAKEINTRESOURCE(1), GetModuleHandle(NULL),
              SND_ASYNC | SND_LOOP | SND_NODEFAULT |
              SND_RESOURCE);

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return -1;

    int iDev = atoi(argv[1]);
    int ret = JustDoIt(iDev);

    CoUninitialize();
    return ret;
}
