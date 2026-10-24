#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace gravity::hle::avplayer {
struct MediaMetadata {
    std::uint32_t width{}, height{}, channels{}, sample_rate{};
    float frame_rate{};
    std::uint64_t duration_us{};
    bool video{}, audio{};
};
struct MediaSample {
    enum class Status { data, end, again, error } status{Status::error};
    std::vector<std::uint8_t> bytes;
    std::uint64_t timestamp_us{}, duration_us{};
    std::uint32_t width{}, height{};
};

// All COM objects, including their final Release, belong to a private MTA thread.
// At most one pending request per stream is issued by AvPlayer.
class MediaDecoder {
public:
    MediaDecoder();
    ~MediaDecoder();
    MediaDecoder(const MediaDecoder&) = delete;
    MediaDecoder& operator=(const MediaDecoder&) = delete;
    bool open(const std::string& guest_path, MediaMetadata& metadata);
    bool rewind();
    std::future<MediaSample> read(bool video);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
