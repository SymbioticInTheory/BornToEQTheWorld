#include "Loopback.h"

#include <atomic>
#include <thread>
#include <vector>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <mmreg.h>

namespace {
template <typename T> void release(T*& p) {
    if (p != nullptr) { p->Release(); p = nullptr; }
}

// One capture session on the default output device; ends on an error or when
// the default device changes, and the thread opens a new one.
void captureOnce(bteq::spectrum::Tap& tap, std::atomic<bool>& quit) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    LPWSTR id = nullptr;
    auto cleanup = [&] {
        if (client != nullptr) client->Stop();
        release(capture);
        release(client);
        if (format != nullptr) { CoTaskMemFree(format); format = nullptr; }
        if (id != nullptr) { CoTaskMemFree(id); id = nullptr; }
        release(device);
        release(enumerator);
    };
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator))
        || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || FAILED(device->GetId(&id))
        || FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) || FAILED(client->GetMixFormat(&format))
        || FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000 /* 200 ms */, 0, format, nullptr))
        || FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture)) || FAILED(client->Start())) {
        cleanup();
        return;
    }
    // What the samples are: float (the usual mix format) or whole numbers.
    const int chans = format->nChannels, bits = format->wBitsPerSample;
    bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) isFloat = ((WAVEFORMATEXTENSIBLE*)format)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
    if (chans <= 0 || (isFloat && bits != 32) || (!isFloat && bits != 16 && bits != 24 && bits != 32)) { cleanup(); return; }
    tap.rate = (double)format->nSamplesPerSec;
    tap.generation.fetch_add(1);

    std::vector<std::vector<float>> data((size_t)chans);
    std::vector<const float*> ptrs((size_t)chans);
    DWORD lastCheck = GetTickCount();
    while (!quit.load()) {
        Sleep(10);
        UINT32 packet = 0;
        bool failed = false;
        while (!quit.load()) {
            if (FAILED(capture->GetNextPacketSize(&packet))) { failed = true; break; }
            if (packet == 0) break;
            BYTE* bytes = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr))) { failed = true; break; }
            for (auto& d : data) d.resize(frames);
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || bytes == nullptr;
            for (UINT32 i = 0; i < frames; ++i)
                for (int c = 0; c < chans; ++c) {
                    float v = 0;
                    if (!silent) {
                        const BYTE* s = bytes + ((size_t)i * (size_t)chans + (size_t)c) * (size_t)(bits / 8);
                        if (isFloat) v = *(const float*)s;
                        else if (bits == 16) v = (float)*(const int16_t*)s / 32768.0f;
                        else if (bits == 24) v = (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) / 8388608.0f;
                        else v = (float)((double)*(const int32_t*)s / 2147483648.0);
                    }
                    data[(size_t)c][i] = v;
                }
            capture->ReleaseBuffer(frames);
            for (int c = 0; c < chans; ++c) ptrs[(size_t)c] = data[(size_t)c].data();
            if (frames > 0) tap.write(ptrs.data(), chans, (int)frames);
        }
        if (failed) break; // the device went away or changed format: open it again
        if (GetTickCount() - lastCheck > 2000) { // a new default output (headphones plugged in, say)
            lastCheck = GetTickCount();
            IMMDevice* now = nullptr;
            LPWSTR nowId = nullptr;
            bool changed = false;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &now)) && SUCCEEDED(now->GetId(&nowId)))
                changed = wcscmp(nowId, id) != 0;
            if (nowId != nullptr) CoTaskMemFree(nowId);
            release(now);
            if (changed) break;
        }
    }
    cleanup();
}
} // namespace

struct SystemLoopback::Impl {
    std::thread thread;
    std::atomic<bool> quit { false };
    bteq::spectrum::Tap* tap = nullptr;
};

SystemLoopback::SystemLoopback() : impl(std::make_unique<Impl>()) {}
SystemLoopback::~SystemLoopback() { stop(); }

bool SystemLoopback::start(bteq::spectrum::Tap& tap) {
    if (running()) return true;
    impl->quit = false;
    impl->tap = &tap;
    tap.external = true;
    impl->thread = std::thread([this, &tap] {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (!impl->quit.load()) {
            captureOnce(tap, impl->quit);
            for (int i = 0; i < 50 && !impl->quit.load(); ++i) Sleep(20); // nothing to open: try again in a second
        }
        if (SUCCEEDED(co)) CoUninitialize();
    });
    return true;
}

void SystemLoopback::stop() {
    if (!impl->thread.joinable()) return;
    impl->quit = true;
    impl->thread.join();
    if (impl->tap != nullptr) {
        impl->tap->external = false;
        impl->tap->rate = 0;
        impl->tap->generation.fetch_add(1);
    }
    impl->tap = nullptr;
}

bool SystemLoopback::running() const { return impl->thread.joinable(); }

#else // not Windows: nothing to listen to

struct SystemLoopback::Impl {};
SystemLoopback::SystemLoopback() : impl(std::make_unique<Impl>()) {}
SystemLoopback::~SystemLoopback() = default;
bool SystemLoopback::start(bteq::spectrum::Tap&) { return false; }
void SystemLoopback::stop() {}
bool SystemLoopback::running() const { return false; }

#endif
