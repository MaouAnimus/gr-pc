#include "avplayer_fixture.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr, int line) {
    if (FAILED(hr)) { std::fprintf(stderr, "Fixture line %d: HRESULT 0x%08lx\n", line, static_cast<unsigned long>(hr)); std::exit(1); }
}
#define HR(x) check((x), __LINE__)
ComPtr<IMFMediaType> video_type(const GUID& subtype, std::uint32_t width, std::uint32_t height) {
    ComPtr<IMFMediaType> type; HR(MFCreateMediaType(&type));
    HR(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)); HR(type->SetGUID(MF_MT_SUBTYPE, subtype));
    HR(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height));
    HR(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 60, 1));
    HR(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    HR(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    return type;
}
void write(IMFSinkWriter* writer, DWORD stream, const void* bytes, DWORD size, LONGLONG time, LONGLONG duration) {
    ComPtr<IMFMediaBuffer> buffer; HR(MFCreateMemoryBuffer(size, &buffer));
    BYTE* data; HR(buffer->Lock(&data, nullptr, nullptr)); std::memcpy(data, bytes, size); HR(buffer->Unlock());
    HR(buffer->SetCurrentLength(size));
    ComPtr<IMFSample> sample; HR(MFCreateSample(&sample)); HR(sample->AddBuffer(buffer.Get()));
    HR(sample->SetSampleTime(time)); HR(sample->SetSampleDuration(duration)); HR(writer->WriteSample(stream, sample.Get()));
}
}
void create_avplayer_fixture(const std::filesystem::path& path, std::uint32_t width,
                            std::uint32_t height, std::uint32_t frames, bool audio) {
    HR(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); HR(MFStartup(MF_VERSION));
    {
        ComPtr<IMFSinkWriter> writer; HR(MFCreateSinkWriterFromURL(path.wstring().c_str(), nullptr, nullptr, &writer));
        auto output = video_type(MFVideoFormat_H264, width, height);
        HR(output->SetUINT32(MF_MT_AVG_BITRATE, width * height > 1000000 ? 12000000 : 500000));
        DWORD video_stream{}; HR(writer->AddStream(output.Get(), &video_stream));
        auto input = video_type(MFVideoFormat_NV12, width, height);
        HR(writer->SetInputMediaType(video_stream, input.Get(), nullptr));
        DWORD audio_stream{};
        if (audio) {
            ComPtr<IMFMediaType> encoded; HR(MFCreateMediaType(&encoded));
            HR(encoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)); HR(encoded->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
            HR(encoded->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2)); HR(encoded->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
            HR(encoded->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)); HR(encoded->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000));
            HR(writer->AddStream(encoded.Get(), &audio_stream));
            ComPtr<IMFMediaType> pcm; HR(MFCreateMediaType(&pcm));
            HR(pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)); HR(pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
            HR(pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2)); HR(pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
            HR(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)); HR(pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4));
            HR(pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000));
            HR(writer->SetInputMediaType(audio_stream, pcm.Get(), nullptr));
        }
        HR(writer->BeginWriting());
        std::vector<std::uint8_t> nv12(std::size_t(width) * height * 3 / 2);
        std::vector<std::int16_t> pcm(800 * 2);
        for (std::uint32_t i = 0; i < frames; ++i) {
            // Red over blue; swap every frame to expose stale texture caches,
            // channel swaps, vertical inversion and decoded black placeholders.
            for (std::uint32_t y = 0; y < height; ++y) {
                const bool red = (y < height / 2) != (i % 2 != 0);
                std::memset(nv12.data() + y * width, red ? 81 : 41, width);
            }
            for (std::uint32_t y = 0; y < height / 2; ++y) for (std::uint32_t x = 0; x < width; x += 2) {
                const bool red = (y < height / 4) != (i % 2 != 0);
                auto* uv = nv12.data() + width * height + y * width + x;
                uv[0] = red ? 90 : 240; uv[1] = red ? 240 : 110;
            }
            const LONGLONG time = LONGLONG(i) * 10000000 / 60;
            const LONGLONG duration = LONGLONG(i + 1) * 10000000 / 60 - time;
            write(writer.Get(), video_stream, nv12.data(), static_cast<DWORD>(nv12.size()), time, duration);
            if (audio) {
                for (std::size_t j = 0; j < pcm.size() / 2; ++j) {
                    const auto value = static_cast<std::int16_t>(10000 * std::sin((i * 800 + j) * 440.0 * 6.283185307179586 / 48000));
                    pcm[j*2] = pcm[j*2+1] = value;
                }
                write(writer.Get(), audio_stream, pcm.data(), static_cast<DWORD>(pcm.size() * 2), time, duration);
            }
        }
        HR(writer->Finalize());
    }
    HR(MFShutdown()); CoUninitialize();
}
