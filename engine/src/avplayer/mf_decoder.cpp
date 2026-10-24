#include "mf_decoder.h"
#include "hle/fs.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

#ifdef __MINGW32__
// Missing from the bundled MinGW mfidl.h; exported by mfplat on Windows 7+.
extern "C" HRESULT WINAPI MFCreateMFByteStreamOnStream(IStream*, IMFByteStream**);
#endif

namespace gravity::hle::avplayer {
namespace {
using Microsoft::WRL::ComPtr;

// A seekable, read-only stream keeps /app0 and PSARC access inside the VFS.
// Media Foundation's adapter supplies IMFByteStream asynchronous reads.
class VfsStream final : public IStream {
public:
    explicit VfsStream(int fd) : fd_(fd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ISequentialStream && iid != IID_IStream) return E_NOINTERFACE;
        *out = static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_; if (!count) delete this; return count;
    }
    HRESULT STDMETHODCALLTYPE Read(void* data, ULONG bytes, ULONG* read) override {
        std::lock_guard lock(mutex_);
        if (read) *read = 0;
        if (!data && bytes) return STG_E_INVALIDPOINTER;
        if (!bytes) return S_OK;
        const auto count = fs::vfs_read(fd_, data, (std::min)(bytes, ULONG(INT32_MAX)));
        if (count < 0) return STG_E_READFAULT;
        if (read) *read = static_cast<ULONG>(count);
        return static_cast<ULONG>(count) == bytes ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) override {
        std::lock_guard lock(mutex_);
        if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
        const auto offset = fs::vfs_lseek(fd_, move.QuadPart, static_cast<int>(origin));
        if (offset < 0) return STG_E_SEEKERROR;
        if (position) position->QuadPart = static_cast<ULONGLONG>(offset);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return E_POINTER;
        std::lock_guard lock(mutex_);
        const auto size = fs::vfs_get_size(fd_);
        if (size < 0) return STG_E_READFAULT;
        *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = static_cast<ULONGLONG>(size);
        stat->grfMode = STGM_READ; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
private:
    ~VfsStream() { fs::vfs_close(fd_); }
    std::atomic<ULONG> references_{1};
    std::mutex mutex_;
    int fd_;
};

struct DecoderState {
    ComPtr<IMFSourceReader> reader;
    MediaMetadata metadata;
    LONG stride{};

    bool open(const std::string& path) {
        reader.Reset(); metadata = {}; stride = 0;
        const auto fd = fs::vfs_open(path.c_str());
        if (fd < 0) return false;
        ComPtr<IStream> stream; stream.Attach(new VfsStream(fd));
        ComPtr<IMFByteStream> bytes;
        HRESULT hr = MFCreateMFByteStreamOnStream(stream.Get(), &bytes);
        ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attributes, 2);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromByteStream(bytes.Get(), attributes.Get(), &reader);
        if (FAILED(hr)) return fail(hr);
        if (FAILED(hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE))) return fail(hr);
        ComPtr<IMFMediaType> native;
        metadata.video = SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native));
        native.Reset();
        metadata.audio = SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &native));
        if (!metadata.video && !metadata.audio) return fail(MF_E_INVALIDMEDIATYPE);
        if (metadata.video) {
            ComPtr<IMFMediaType> type;
            hr = MFCreateMediaType(&type);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get());
            if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
            if (FAILED(hr) || !video_format()) return fail(hr);
        }
        if (metadata.audio) {
            ComPtr<IMFMediaType> type;
            hr = MFCreateMediaType(&type);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.Get());
            if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
            type.Reset();
            if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &type);
            if (SUCCEEDED(hr)) hr = type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &metadata.channels);
            if (SUCCEEDED(hr)) hr = type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &metadata.sample_rate);
            UINT32 bits{}; GUID subtype{};
            if (SUCCEEDED(hr)) hr = type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
            if (SUCCEEDED(hr)) hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (FAILED(hr) || !metadata.channels || !metadata.sample_rate || bits != 16 || subtype != MFAudioFormat_PCM)
                return fail(FAILED(hr) ? hr : MF_E_INVALIDMEDIATYPE);
        }
        PROPVARIANT duration{};
        if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) &&
            duration.vt == VT_UI8) metadata.duration_us = duration.uhVal.QuadPart / 10;
        PropVariantClear(&duration);
        return true;
    }
    bool fail(HRESULT hr) {
        std::fprintf(stderr, "[AvPlayer/MF] media operation failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        reader.Reset(); return false;
    }
    bool video_format() {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type))) return false;
        GUID subtype{};
        if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_RGB32 ||
            FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &metadata.width, &metadata.height)) ||
            !metadata.width || !metadata.height || metadata.width > 8192 || metadata.height > 8192) return false;
        UINT32 numerator{}, denominator{}, value{};
        if (SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) && denominator)
            metadata.frame_rate = static_cast<float>(numerator) / denominator;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &value))) stride = static_cast<LONG>(value);
        else if (FAILED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, metadata.width, &stride))) return false;
        return stride && std::uint64_t(stride < 0 ? -std::int64_t(stride) : stride) >= std::uint64_t(metadata.width) * 4;
    }
    MediaSample read(bool video) {
        MediaSample result;
        if (!reader) return result;
        DWORD flags{};
        LONGLONG timestamp{};
        ComPtr<IMFSample> sample;
        const auto stream = video ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM;
        const auto hr = reader->ReadSample(stream, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) return result;
        if (video && (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && !video_format()) return result;
        if (!video && (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
            ComPtr<IMFMediaType> type;
            UINT32 channels{}, rate{}, bits{}; GUID subtype{};
            if (FAILED(reader->GetCurrentMediaType(stream, &type)) ||
                FAILED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
                FAILED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) ||
                FAILED(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) ||
                FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
                channels != metadata.channels || rate != metadata.sample_rate || bits != 16 || subtype != MFAudioFormat_PCM) return result;
        }
        if (!sample) {
            result.status = flags & MF_SOURCE_READERF_ENDOFSTREAM ? MediaSample::Status::end : MediaSample::Status::again;
            return result;
        }
        result.timestamp_us = static_cast<std::uint64_t>((std::max)(0LL, timestamp)) / 10;
        LONGLONG duration{};
        if (SUCCEEDED(sample->GetSampleDuration(&duration)) && duration > 0) result.duration_us = duration / 10;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return result;
        if (!video) {
            BYTE* data{}; DWORD size{};
            if (FAILED(buffer->Lock(&data, nullptr, &size))) return result;
            result.bytes.assign(data, data + size);
            buffer->Unlock();
            if (!result.duration_us) result.duration_us = std::uint64_t(size) * 1000000 / (metadata.channels * metadata.sample_rate * 2);
        } else {
            const auto row_bytes = static_cast<std::size_t>(metadata.width) * 4;
            result.bytes.resize(row_bytes * metadata.height);
            result.width = metadata.width; result.height = metadata.height;
            BYTE* data{}; LONG pitch = stride;
            ComPtr<IMF2DBuffer> two_d;
            const bool locked_2d = SUCCEEDED(buffer.As(&two_d)) && SUCCEEDED(two_d->Lock2D(&data, &pitch));
            if (!locked_2d) {
                DWORD size{};
                if (FAILED(buffer->Lock(&data, nullptr, &size))) return result;
                const auto abs_pitch = static_cast<std::size_t>(pitch < 0 ? -std::int64_t(pitch) : pitch);
                if (size < abs_pitch * (metadata.height - 1) + row_bytes) { buffer->Unlock(); return result; }
                if (pitch < 0) data += abs_pitch * (metadata.height - 1);
            }
            for (std::uint32_t y = 0; y < metadata.height; ++y) {
                const auto* source = data + std::ptrdiff_t(y) * pitch;
                auto* destination = result.bytes.data() + y * row_bytes;
                // Media Foundation RGB32 is BGRX; the runtime contract is opaque RGBA.
                for (std::uint32_t x = 0; x < metadata.width; ++x) {
                    destination[x*4] = source[x*4+2]; destination[x*4+1] = source[x*4+1];
                    destination[x*4+2] = source[x*4]; destination[x*4+3] = 255;
                }
            }
            if (locked_2d) two_d->Unlock2D(); else buffer->Unlock();
            if (!result.duration_us && metadata.frame_rate > 0) result.duration_us = static_cast<std::uint64_t>(1000000 / metadata.frame_rate);
        }
        result.status = MediaSample::Status::data;
        return result;
    }
};
}

struct MediaDecoder::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void(DecoderState&)>> commands;
    bool stopping{};
    std::thread worker;
    Impl() : worker([this] {
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const auto mf = SUCCEEDED(com) ? MFStartup(MF_VERSION) : com;
        {
            DecoderState state;
            for (;;) {
                std::function<void(DecoderState&)> command;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return stopping || !commands.empty(); });
                    if (commands.empty() && stopping) break;
                    command = std::move(commands.front()); commands.pop_front();
                }
                command(state);
            }
        }
        if (SUCCEEDED(mf)) MFShutdown();
        if (SUCCEEDED(com)) CoUninitialize();
    }) {}
    ~Impl() {
        { std::lock_guard lock(mutex); stopping = true; }
        wake.notify_one(); worker.join();
    }
    template<class F> auto submit(F function) {
        using Result = std::invoke_result_t<F, DecoderState&>;
        auto task = std::make_shared<std::packaged_task<Result(DecoderState&)>>(std::move(function));
        auto future = task->get_future();
        { std::lock_guard lock(mutex); commands.emplace_back([task](DecoderState& state) { (*task)(state); }); }
        wake.notify_one(); return future;
    }
};
MediaDecoder::MediaDecoder() : impl_(std::make_unique<Impl>()) {}
MediaDecoder::~MediaDecoder() = default;
bool MediaDecoder::open(const std::string& path, MediaMetadata& metadata) {
    return impl_->submit([&](DecoderState& state) { const bool ok = state.open(path); metadata = state.metadata; return ok; }).get();
}
bool MediaDecoder::rewind() {
    return impl_->submit([](DecoderState& state) {
        if (!state.reader) return false;
        PROPVARIANT position{}; position.vt = VT_I8; position.hVal.QuadPart = 0;
        return SUCCEEDED(state.reader->SetCurrentPosition(GUID_NULL, position));
    }).get();
}
std::future<MediaSample> MediaDecoder::read(bool video) {
    return impl_->submit([video](DecoderState& state) { return state.read(video); });
}
}
