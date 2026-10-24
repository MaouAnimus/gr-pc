#include "hle/audio.h"
#include <iostream>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <combaseapi.h>
#include <vector>
#include <cstring>
#include <mutex>
#include <algorithm>
#include "hle/kernel.h"

namespace gravity::hle::audio {

static hle::Mutex g_audioMutex;

// Helper to convert HRESULT to hle::Status
static hle::Status hrToStatus(HRESULT hr) {
    if (SUCCEEDED(hr))
        return {};
    return {hle::Error::system_error, static_cast<uint32_t>(hr)};
}

class AudioRenderer {
public:
    AudioRenderer() : initialized_(false),
                      pDevice(nullptr), pAudioClient(nullptr), pRenderClient(nullptr),
                      bufferFrameCount_(0), frameSizeBytes_(0), sampleRate_(0) {}
    ~AudioRenderer() {
        shutdown();
    }

    hle::Status initialize(uint32_t sampleRate, uint32_t channels,
                           uint32_t bitsPerSample, uint32_t mode) {
        if (sampleRate < 8000 || sampleRate > 192000 || channels < 1 || channels > 2 ||
            (bitsPerSample != 16 && bitsPerSample != 32) || mode != 0)
            return {hle::Error::invalid_argument};
        std::lock_guard<hle::Mutex> lock(g_audioMutex);
        if (initialized_)
            return {hle::Error::invalid_state};

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
            return hrToStatus(hr);

        // Get default audio endpoint
        IMMDeviceEnumerator* pEnumerator = nullptr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&pEnumerator));
        if (FAILED(hr)) {
            CoUninitialize();
            return hrToStatus(hr);
        }
        IMMDevice* dev = nullptr;
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        pEnumerator->Release();
        if (FAILED(hr)) {
            CoUninitialize();
            return hrToStatus(hr);
        }
        this->pDevice = dev;

        // Create audio client
        IAudioClient* client = nullptr;
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                           nullptr, reinterpret_cast<void**>(&client));
        dev->Release();
        this->pDevice = nullptr;
        if (FAILED(hr)) {
            CoUninitialize();
            return hrToStatus(hr);
        }
        this->pAudioClient = client;

        // Honor the guest PCM format; WASAPI performs conversion to the endpoint.
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(channels);
        format.nSamplesPerSec = sampleRate;
        format.wBitsPerSample = static_cast<WORD>(bitsPerSample);
        format.nBlockAlign = static_cast<WORD>(channels * bitsPerSample / 8);
        format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;
        sampleRate_ = sampleRate;
        frameSizeBytes_ = format.nBlockAlign;
        owner_thread_ = GetCurrentThreadId();
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            0, 0, &format, nullptr);
        if (FAILED(hr)) {
            client->Release();
            this->pAudioClient = nullptr;
            CoUninitialize();
            return hrToStatus(hr);
        }

        // Get the actual buffer size in frames
        hr = client->GetBufferSize(&bufferFrameCount_);
        if (FAILED(hr)) {
            client->Release();
            this->pAudioClient = nullptr;
            CoUninitialize();
            return hrToStatus(hr);
        }

        // Get the render client
        IAudioRenderClient* rClient = nullptr;
        hr = client->GetService(__uuidof(IAudioRenderClient),
                                reinterpret_cast<void**>(&rClient));
        if (FAILED(hr)) {
            client->Release();
            this->pAudioClient = nullptr;
            CoUninitialize();
            return hrToStatus(hr);
        }
        this->pRenderClient = rClient;

        // Start the audio stream
        hr = client->Start();
        if (FAILED(hr)) {
            rClient->Release();
            this->pRenderClient = nullptr;
            client->Release();
            this->pAudioClient = nullptr;
            CoUninitialize();
            return hrToStatus(hr);
        }

        initialized_ = true;
        return {};
    }

    hle::Status shutdown() {
        std::lock_guard<hle::Mutex> lock(g_audioMutex);
        if (!initialized_) return {};
        if (GetCurrentThreadId() != owner_thread_) return {hle::Error::wrong_thread};
        if (pAudioClient) {
            pAudioClient->Stop();
        }
        if (pRenderClient) {
            pRenderClient->Release();
            pRenderClient = nullptr;
        }
        if (pAudioClient) {
            pAudioClient->Release();
            pAudioClient = nullptr;
        }
        if (pDevice) {
            pDevice->Release();
            pDevice = nullptr;
        }
        CoUninitialize();
        initialized_ = false;
        return {};
    }

    hle::Status output(const void* data, uint32_t sizeBytes) {
        std::lock_guard<hle::Mutex> lock(g_audioMutex);
        if (!initialized_)
            return {hle::Error::invalid_state};
        if (!data && sizeBytes) return {hle::Error::invalid_argument};
        if (sizeBytes == 0)
            return {};
        if (frameSizeBytes_ == 0) return {hle::Error::invalid_state};

        UINT32 paddingFrames = 0;
        HRESULT hr = pAudioClient->GetCurrentPadding(&paddingFrames);
        if (FAILED(hr))
            return hrToStatus(hr);

        if (sizeBytes % frameSizeBytes_) return {hle::Error::invalid_argument};
        if (paddingFrames >= bufferFrameCount_)
            return {hle::Error::busy};

        UINT32 framesAvailable = bufferFrameCount_ - paddingFrames;
        if (sizeBytes > framesAvailable * frameSizeBytes_) return {hle::Error::busy};
        UINT32 bytesToCopy = sizeBytes;
        if (bytesToCopy == 0)
            return {};

        UINT32 framesToWrite = bytesToCopy / frameSizeBytes_;
        if (framesToWrite == 0)
            return {};

        BYTE* pData = nullptr;
        hr = pRenderClient->GetBuffer(framesToWrite, &pData);
        if (FAILED(hr))
            return hrToStatus(hr);

        std::memcpy(pData, data, framesToWrite * frameSizeBytes_);
        hr = pRenderClient->ReleaseBuffer(framesToWrite, 0);
        if (FAILED(hr))
            return hrToStatus(hr);
        return {};
    }

    hle::Status getDelay(uint64_t* delayOut) {
        std::lock_guard<hle::Mutex> lock(g_audioMutex);
        if (!initialized_)
            return {hle::Error::invalid_state};
        if (GetCurrentThreadId() != owner_thread_) return {hle::Error::wrong_thread};
        if (!delayOut)
            return {hle::Error::invalid_argument};
        UINT32 padding = 0;
        HRESULT hr = pAudioClient->GetCurrentPadding(&padding);
        if (FAILED(hr))
            return hrToStatus(hr);
        if (sampleRate_ == 0)
            return {hle::Error::invalid_state};
        *delayOut = (static_cast<uint64_t>(padding) * 1000000ULL) / sampleRate_;
        return {};
    }

    hle::Status getWriteableSize(uint32_t* sizeOut) {
        std::lock_guard<hle::Mutex> lock(g_audioMutex);
        if (!initialized_)
            return {hle::Error::invalid_state};
        if (GetCurrentThreadId() != owner_thread_) return {hle::Error::wrong_thread};
        if (!sizeOut)
            return {hle::Error::invalid_argument};
        UINT32 paddingFrames = 0;
        HRESULT hr = pAudioClient->GetCurrentPadding(&paddingFrames);
        if (FAILED(hr))
            return hrToStatus(hr);
        UINT32 availableFrames = (bufferFrameCount_ > paddingFrames) ? (bufferFrameCount_ - paddingFrames) : 0;
        *sizeOut = availableFrames * frameSizeBytes_;
        return {};
    }

    // Unimplemented NGS2 calls must not report success.
    hle::Status sceNgs2Initialize() { return {hle::Error::unsupported}; }
    hle::Status sceNgs2Shutdown() { return {hle::Error::unsupported}; }
    hle::Status sceNgs2SetParameter(uint32_t param, uint32_t value) {
        (void)param; (void)value;
        return {hle::Error::unsupported};
    }
    hle::Status sceNgs2Start() { return {hle::Error::unsupported}; }
    hle::Status sceNgs2Stop() { return {hle::Error::unsupported}; }

    bool is_initialized() const noexcept { return initialized_; }

private:
    DWORD owner_thread_{};
    bool initialized_;
    IMMDevice* pDevice;
    IAudioClient* pAudioClient;
    IAudioRenderClient* pRenderClient;
    UINT32 bufferFrameCount_;
    UINT32 frameSizeBytes_;
    UINT32 sampleRate_;
};

static AudioRenderer* g_audioRenderer = nullptr;
static std::mutex g_audioRenderer_mutex;

AudioRenderer& get_audio_renderer() noexcept {
    std::lock_guard<std::mutex> lock(g_audioRenderer_mutex);
    if (!g_audioRenderer) {
        g_audioRenderer = new AudioRenderer();
    }
    return *g_audioRenderer;
}

void output_master_audio(const void* pcm_data, std::uint32_t size_bytes) noexcept {
    if (!pcm_data || size_bytes == 0) return;
    AudioRenderer& ar = get_audio_renderer();
    if (!ar.is_initialized()) {
        static bool s_init_attempted = false;
        if (!s_init_attempted) {
            s_init_attempted = true;
            (void)ar.initialize(48000, 2, 16, 0);
        }
    }
    if (ar.is_initialized()) {
        (void)ar.output(pcm_data, size_bytes);
    }
}

bool is_audio_renderer_initialized() noexcept {
    return get_audio_renderer().is_initialized();
}

// HLE function implementations
extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceAudioOutOpen(uint32_t sampleRate, uint32_t channels,
                    uint32_t bitsPerSample, uint32_t mode, uint64_t* outHandle) {
    if (!outHandle) return {hle::Error::invalid_argument};
    *outHandle = 0;
    AudioRenderer& ar = get_audio_renderer();
    auto status = ar.initialize(sampleRate, channels, bitsPerSample, mode);
    if (status && outHandle) {
        *outHandle = reinterpret_cast<uint64_t>(&ar);
    }
    return status;
}

extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceAudioOutRelease(uint64_t handle) {
    AudioRenderer* ar = reinterpret_cast<AudioRenderer*>(handle);
    if (!ar || ar != &get_audio_renderer())
        return {hle::Error::invalid_state};
    return ar->shutdown();
}

extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceAudioOutOutput(uint64_t handle, const void* buffer, uint32_t size) {
    AudioRenderer* ar = reinterpret_cast<AudioRenderer*>(handle);
    if (!ar || ar != &get_audio_renderer())
        return {hle::Error::invalid_state};
    return ar->output(buffer, size);
}

extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceAudioOutGetDelay(uint64_t handle, uint64_t* delay) {
    AudioRenderer* ar = reinterpret_cast<AudioRenderer*>(handle);
    if (!ar || ar != &get_audio_renderer())
        return {hle::Error::invalid_state};
    return ar->getDelay(delay);
}

extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceAudioOutGetWriteableSize(uint64_t handle, uint32_t* size) {
    AudioRenderer* ar = reinterpret_cast<AudioRenderer*>(handle);
    if (!ar || ar != &get_audio_renderer())
        return {hle::Error::invalid_state};
    return ar->getWriteableSize(size);
}

// Ngs2 stubs
extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceNgs2Initialize() {
        return get_audio_renderer().sceNgs2Initialize();
    }
extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceNgs2Shutdown() {
        return get_audio_renderer().sceNgs2Shutdown();
    }
extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceNgs2SetParameter(uint32_t param, uint32_t value) {
        return get_audio_renderer().sceNgs2SetParameter(param, value);
    }
extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceNgs2Start() {
        return get_audio_renderer().sceNgs2Start();
    }
extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
    sceNgs2Stop() {
        return get_audio_renderer().sceNgs2Stop();
    }

} // namespace gravity::hle::audio
