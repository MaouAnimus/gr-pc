#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include <cstdint>
#include <chrono>

namespace gravity::hle::pad {

    // Button bitmask (example, not exhaustive)
    constexpr uint64_t SCE_PAD_BUTTON_SELECT   = 0x0000000000000001ULL;
    constexpr uint64_t SCE_PAD_BUTTON_L3       = 0x0000000000000002ULL;
    constexpr uint64_t SCE_PAD_BUTTON_R3       = 0x0000000000000004ULL;
    constexpr uint64_t SCE_PAD_BUTTON_START    = 0x0000000000000008ULL;
    constexpr uint64_t SCE_PAD_BUTTON_UP       = 0x0000000000000010ULL;
    constexpr uint64_t SCE_PAD_BUTTON_RIGHT    = 0x0000000000000020ULL;
    constexpr uint64_t SCE_PAD_BUTTON_DOWN     = 0x0000000000000040ULL;
    constexpr uint64_t SCE_PAD_BUTTON_LEFT     = 0x0000000000000080ULL;
    constexpr uint64_t SCE_PAD_BUTTON_L2       = 0x0000000000000100ULL;
    constexpr uint64_t SCE_PAD_BUTTON_R2       = 0x0000000000000200ULL;
    constexpr uint64_t SCE_PAD_BUTTON_L1       = 0x0000000000000400ULL;
    constexpr uint64_t SCE_PAD_BUTTON_R1       = 0x0000000000000800ULL;
    constexpr uint64_t SCE_PAD_BUTTON_TRIANGLE = 0x0000000000001000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_CIRCLE   = 0x0000000000002000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_CROSS    = 0x0000000000004000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_SQUARE   = 0x0000000000008000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_PS       = 0x0000000000010000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_TOUCH    = 0x0000000000200000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_PADLEFT  = 0x0000000000400000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_PADRIGHT  = 0x0000000000800000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_PADUP    = 0x0000000001000000ULL;
    constexpr uint64_t SCE_PAD_BUTTON_PADDOWN  = 0x0000000002000000ULL;
    // ... (we can keep only a few for simplicity)

    // Analog stick range: 0-255, center ~128
    // Touch point
    struct ScePadTouchPoint {
        uint8_t id;          // touch identifier (0-1)
        uint16_t x;          // X coordinate (0-1919?)
        uint16_t y;          // Y coordinate (0-943?)
        uint16_t reserve;
    };

    // Touch data (up to 2 points)
    struct ScePadTouchData {
        uint8_t reportCount; // number of active touches (0-2)
        uint8_t effectIndex; // effect index (unused)
        uint8_t reserve[2];
        ScePadTouchPoint touch[2];
    };

    // Motion data (accelerometer, gyroscope, orientation quaternion, timestamp)
    struct ScePadMotionData {
        float accel[3];    // m/s^2
        float gyro[3];     // rad/s
        float quat[4];     // quaternion (w, x, y, z) or (x, y, z, w) – we'll use (x,y,z,w)
        uint64_t timestamp; // microseconds
    };

    // Old pad state (used by scePadGetState)
    struct ScePadState {
        uint64_t buttons;
        uint8_t  lx;   // left stick X
        uint8_t  ly;   // left stick Y
        uint8_t  rx;   // right stick X
        uint8_t  ry;   // right stick Y
        uint8_t  reserved[4];
        ScePadTouchData touch;
        ScePadMotionData motion;
        uint64_t timestamp;
    };

    // New pad state (used by scePadGetState2 or scePadGetState)
    struct ScePadData {
        uint64_t buttons;
        uint8_t  lx;
        uint8_t  ly;
        uint8_t  rx;
        uint8_t  ry;
        uint8_t  reserved[2];
        uint8_t  l2; // analog trigger L2 (0-255)
        uint8_t  r2; // analog trigger R2
        uint16_t reserved2;
        ScePadTouchData touch;
        ScePadMotionData motion;
        uint64_t timestamp;
    };

    // Authentic 120-byte Orbis DualShock 4 structure
    struct OrbisPadTouchPoint {
        uint16_t x;
        uint16_t y;
        uint8_t  id;
        uint8_t  reserve[3];
    };

    struct OrbisPadTouchData {
        uint8_t  touchNum;
        uint8_t  reserve[3];
        uint32_t reserve1;
        OrbisPadTouchPoint touch[2];
    };

    struct OrbisPadData {
        uint32_t buttons;
        uint8_t  lx;
        uint8_t  ly;
        uint8_t  rx;
        uint8_t  ry;
        uint8_t  l2;
        uint8_t  r2;
        uint8_t  padding[2];
        float    orientation[4]; // x, y, z, w
        float    accel[3];       // x, y, z
        float    angularVelocity[3]; // x, y, z
        OrbisPadTouchData touch;
        uint8_t  connected;
        uint8_t  padding2[3];
        uint32_t reserve[2];
        uint64_t timestamp;
        uint8_t  extensionData[16];
        uint8_t  connectedCount;
        uint8_t  reserve2[7];
    };
    static_assert(sizeof(OrbisPadData) == 120, "OrbisPadData must be 120 bytes");

    struct ScePadControllerInformation {
        float    touchPadPixelDensity;
        uint16_t touchPadResolutionX;
        uint16_t touchPadResolutionY;
        uint8_t  stickDeadzoneLeft;
        uint8_t  stickDeadzoneRight;
        uint8_t  connectionType;
        uint8_t  connected;
        uint8_t  deviceClass;
        uint8_t  reserve[19];
    };
    static_assert(sizeof(ScePadControllerInformation) == 32, "ScePadControllerInformation must be 32 bytes");

    // Live input functions
    int32_t readPadData(int32_t handle, OrbisPadData* data, int32_t count);
    int32_t getControllerInformation(int32_t handle, ScePadControllerInformation* info);
    void resetOrientation() noexcept;

    // HLE functions (SysV ABI)
    extern "C" GRAVITY_SYSV_ABI int32_t scePadInit();
    extern "C" GRAVITY_SYSV_ABI int32_t scePadEnd();
    extern "C" GRAVITY_SYSV_ABI int32_t scePadSetActionMode(int32_t port, int32_t mode);
    extern "C" GRAVITY_SYSV_ABI int32_t scePadGetState(int32_t port, ScePadState* state);
    extern "C" GRAVITY_SYSV_ABI int32_t scePadGetState2(int32_t port, ScePadData* state);
    extern "C" GRAVITY_SYSV_ABI int32_t scePadGetMotionData(int32_t port, ScePadMotionData* motion);
    extern "C" GRAVITY_SYSV_ABI int32_t scePadSetRumble(int32_t port, int32_t weak, int32_t strong);
    extern "C" GRAVITY_SYSV_ABI int32_t scePadGetBatteryInfo(int32_t port, uint8_t* level, bool* charging);

} // namespace gravity::hle::pad