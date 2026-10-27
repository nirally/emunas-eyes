// Stage 8F recon: records what the PC plays (WASAPI loopback of the default output) and prints the left/right
// loudness every 250 ms, to tell whether the game's sound turns when the head turns (no headset needed).
// Usage: audio_balance <seconds>
// Output per line: t (s), left RMS, right RMS, balance in dB (+ = louder on the right).
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? atof(argv[1]) : 10.0;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* ac = nullptr;
    IAudioCaptureClient* cap = nullptr;
    WAVEFORMATEX* wf = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) ||
        FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) ||
        FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac)) || FAILED(ac->GetMixFormat(&wf)) ||
        FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, wf, nullptr)) ||
        FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cap))) {
        fprintf(stderr, "can't open loopback capture\n");
        return 1;
    }
    // The shared mix format is 32-bit float on every current Windows; refuse anything else.
    const bool is_float = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                          (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->wBitsPerSample == 32);
    if (!is_float || wf->nChannels < 2) {
        fprintf(stderr, "mix format not float stereo (tag %u, %u bits, %u ch)\n", wf->wFormatTag, wf->wBitsPerSample, wf->nChannels);
        return 1;
    }
    printf("# %u Hz, %u channels\n", wf->nSamplesPerSec, wf->nChannels);
    ac->Start();
    const unsigned window = wf->nSamplesPerSec / 4;
    double sl = 0, sr = 0;
    unsigned n = 0, windows = 0;
    const ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < seconds * 1000) {
        Sleep(10);
        UINT32 packet = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet) {
            BYTE* data;
            UINT32 frames;
            DWORD flags;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            const float* f = reinterpret_cast<const float*>(data);
            for (UINT32 i = 0; i < frames; ++i) {
                const float l = flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0.f : f[i * wf->nChannels];
                const float r = flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0.f : f[i * wf->nChannels + 1];
                sl += l * l;
                sr += r * r;
                if (++n == window) {
                    const double L = std::sqrt(sl / n), R = std::sqrt(sr / n);
                    printf("%6.2f %.5f %.5f %+6.1f\n", ++windows * 0.25, L, R, L > 0 && R > 0 ? 20 * std::log10(R / L) : 0.0);
                    fflush(stdout);
                    sl = sr = 0;
                    n = 0;
                }
            }
            cap->ReleaseBuffer(frames);
        }
    }
    ac->Stop();
    return 0;
}
