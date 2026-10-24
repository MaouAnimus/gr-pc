#pragma once
#include <filesystem>
#include <cstdint>

// Generated locally using Windows' H.264/AAC encoders; no game assets/downloads.
void create_avplayer_fixture(const std::filesystem::path& path, std::uint32_t width = 64,
                            std::uint32_t height = 48, std::uint32_t frames = 12, bool audio = true);
