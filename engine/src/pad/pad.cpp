#include "hle/pad.h"
#include "hle/kernel.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <windows.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <algorithm>

namespace gravity::hle::pad {

namespace {

struct Quat {
    float x{0.0f}, y{0.0f}, z{0.0f}, w{1.0f};

    void normalize() noexcept {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len > 1e-6f) {
            float inv = 1.0f / len;
            x *= inv; y *= inv; z *= inv; w *= inv;
        } else {
            x = 0.0f; y = 0.0f; z = 0.0f; w = 1.0f;
        }
    }

    void integrate(float gx, float gy, float gz, float dt) noexcept {
        float hx = 0.5f * gx * dt;
        float hy = 0.5f * gy * dt;
        float hz = 0.5f * gz * dt;

        float nx = x + (hx * w + hy * z - hz * y);
        float ny = y + (-hx * z + hy * w + hz * x);
        float nz = z + (hx * y - hy * x + hz * w);
        float nw = w + (-hx * x - hy * y - hz * z);

        x = nx; y = ny; z = nz; w = nw;
        normalize();
    }

    void transform_vec(float vx, float vy, float vz, float& ox, float& oy, float& oz) const noexcept {
        // Inverse rotation (conjugate: -x, -y, -z, w)
        float rx = -x, ry = -y, rz = -z;
        float tx = 2.0f * (ry * vz - rz * vy + w * vx);
        float ty = 2.0f * (rz * vx - rx * vz + w * vy);
        float tz = 2.0f * (rx * vy - ry * vx + w * vz);

        ox = vx + (ry * tz - rz * ty);
        oy = vy + (rz * tx - rx * tz);
        oz = vz + (rx * ty - ry * tx);
    }
};

static std::chrono::high_resolution_clock::time_point g_lastTime;
static bool g_initialized = false;
static float g_emaDX = 0.0f;
static float g_emaDY = 0.0f;
static constexpr float g_sensitivity = 0.005f; // rad/pixel
static constexpr float g_deadzone = 0.5f;     // pixels
static POINT g_lastMousePos{};
static Quat g_orientation{};
static hle::Mutex g_padMutex;
static SDL_Gamepad* g_gamepad = nullptr;

static void openFirstAvailableGamepad() {
    if (g_gamepad) return;
    int count = 0;
    SDL_JoystickID* joysticks = SDL_GetGamepads(&count);
    if (joysticks && count > 0) {
        g_gamepad = SDL_OpenGamepad(joysticks[0]);
        if (g_gamepad) {
            if (SDL_GamepadHasSensor(g_gamepad, SDL_SENSOR_GYRO)) {
                SDL_SetGamepadSensorEnabled(g_gamepad, SDL_SENSOR_GYRO, true);
            }
            if (SDL_GamepadHasSensor(g_gamepad, SDL_SENSOR_ACCEL)) {
                SDL_SetGamepadSensorEnabled(g_gamepad, SDL_SENSOR_ACCEL, true);
            }
        }
        SDL_free(joysticks);
    }
}

static void updateMouseDelta(float& dx, float& dy, float dt) {
    POINT curPos{};
    if (!GetCursorPos(&curPos)) {
        dx = 0.0f;
        dy = 0.0f;
        return;
    }
    int rawDX = curPos.x - g_lastMousePos.x;
    int rawDY = curPos.y - g_lastMousePos.y;
    g_lastMousePos = curPos;

    if (std::abs(rawDX) < g_deadzone) rawDX = 0;
    if (std::abs(rawDY) < g_deadzone) rawDY = 0;

    // Exponential Moving Average
    constexpr float alpha = 0.25f;
    g_emaDX = alpha * static_cast<float>(rawDX) + (1.0f - alpha) * g_emaDX;
    g_emaDY = alpha * static_cast<float>(rawDY) + (1.0f - alpha) * g_emaDY;

    dx = g_emaDX;
    dy = g_emaDY;

    // Angular velocity in rad/s: pitch (X) from dy, yaw (Z) from dx
    float gx = dy * g_sensitivity;
    float gy = 0.0f;
    float gz = dx * g_sensitivity;

    if (dt > 0.0f && dt < 0.1f) {
        g_orientation.integrate(gx, gy, gz, dt);
    }
}

static void pollKeyboardAndGamepad(
    uint64_t& buttons, uint8_t& lx, uint8_t& ly, uint8_t& rx, uint8_t& ry,
    uint8_t& l2, uint8_t& r2, float& gyro_x, float& gyro_y, float& gyro_z,
    OrbisPadTouchData& touch, float dt) {

    buttons = 0;
    lx = 128;
    ly = 128;
    rx = 128;
    ry = 128;
    l2 = 0;
    r2 = 0;
    std::memset(&touch, 0, sizeof(touch));

    // 1. Keyboard & Mouse baseline
    bool w = (GetAsyncKeyState('W') & 0x8000) != 0;
    bool s = (GetAsyncKeyState('S') & 0x8000) != 0;
    bool a = (GetAsyncKeyState('A') & 0x8000) != 0;
    bool d = (GetAsyncKeyState('D') & 0x8000) != 0;

    if (w && !s) ly = 0;
    else if (s && !w) ly = 255;

    if (a && !d) lx = 0;
    else if (d && !a) lx = 255;

    if (GetAsyncKeyState(VK_SPACE) & 0x8000)   buttons |= SCE_PAD_BUTTON_CROSS;
    if (GetAsyncKeyState('F') & 0x8000)          buttons |= SCE_PAD_BUTTON_SQUARE;
    if (GetAsyncKeyState('E') & 0x8000)          buttons |= SCE_PAD_BUTTON_CIRCLE;
    if (GetAsyncKeyState('Q') & 0x8000)          buttons |= SCE_PAD_BUTTON_TRIANGLE;
    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) { buttons |= SCE_PAD_BUTTON_R2; r2 = 255; }
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000){ buttons |= SCE_PAD_BUTTON_L2; l2 = 255; }
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)  buttons |= SCE_PAD_BUTTON_R1;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)  buttons |= SCE_PAD_BUTTON_L1;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)   buttons |= SCE_PAD_BUTTON_START;
    if (GetAsyncKeyState(VK_TAB) & 0x8000)      buttons |= SCE_PAD_BUTTON_TOUCH;
    if (GetAsyncKeyState(VK_UP) & 0x8000)       buttons |= SCE_PAD_BUTTON_UP;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)     buttons |= SCE_PAD_BUTTON_DOWN;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)     buttons |= SCE_PAD_BUTTON_LEFT;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)    buttons |= SCE_PAD_BUTTON_RIGHT;

    // Touchpad simulation via mouse middle click or drag
    if ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) {
        POINT cur{};
        if (GetCursorPos(&cur)) {
            touch.touchNum = 1;
            touch.touch[0].id = 0;
            touch.touch[0].x = static_cast<uint16_t>(cur.x % 1920);
            touch.touch[0].y = static_cast<uint16_t>(cur.y % 943);
        }
    }

    // 2. Physical Gamepad query via SDL3
    openFirstAvailableGamepad();
    if (g_gamepad && SDL_GamepadConnected(g_gamepad)) {
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= SCE_PAD_BUTTON_CROSS;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_EAST))  buttons |= SCE_PAD_BUTTON_CIRCLE;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_WEST))  buttons |= SCE_PAD_BUTTON_SQUARE;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_NORTH)) buttons |= SCE_PAD_BUTTON_TRIANGLE;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    buttons |= SCE_PAD_BUTTON_UP;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  buttons |= SCE_PAD_BUTTON_DOWN;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  buttons |= SCE_PAD_BUTTON_LEFT;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) buttons |= SCE_PAD_BUTTON_RIGHT;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  buttons |= SCE_PAD_BUTTON_L1;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) buttons |= SCE_PAD_BUTTON_R1;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_START)) buttons |= SCE_PAD_BUTTON_START;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_BACK) ||
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD)) buttons |= SCE_PAD_BUTTON_TOUCH;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK))  buttons |= SCE_PAD_BUTTON_L3;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) buttons |= SCE_PAD_BUTTON_R3;

        // Analog sticks
        Sint16 ax_lx = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 ax_ly = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        if (std::abs(ax_lx) > 3200 || std::abs(ax_ly) > 3200) {
            lx = static_cast<uint8_t>(std::clamp((ax_lx + 32768) >> 8, 0, 255));
            ly = static_cast<uint8_t>(std::clamp((ax_ly + 32768) >> 8, 0, 255));
        }

        Sint16 ax_rx = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        Sint16 ax_ry = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        if (std::abs(ax_rx) > 3200 || std::abs(ax_ry) > 3200) {
            rx = static_cast<uint8_t>(std::clamp((ax_rx + 32768) >> 8, 0, 255));
            ry = static_cast<uint8_t>(std::clamp((ax_ry + 32768) >> 8, 0, 255));
        }

        Sint16 ax_l2 = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        if (ax_l2 > 0) {
            uint8_t val = static_cast<uint8_t>(ax_l2 >> 7);
            if (val > l2) l2 = val;
            if (l2 > 30) buttons |= SCE_PAD_BUTTON_L2;
        }

        Sint16 ax_r2 = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (ax_r2 > 0) {
            uint8_t val = static_cast<uint8_t>(ax_r2 >> 7);
            if (val > r2) r2 = val;
            if (r2 > 30) buttons |= SCE_PAD_BUTTON_R2;
        }

        // Hardware Gyro integration
        if (SDL_GamepadSensorEnabled(g_gamepad, SDL_SENSOR_GYRO)) {
            float g_data[3]{};
            if (SDL_GetGamepadSensorData(g_gamepad, SDL_SENSOR_GYRO, g_data, 3)) {
                gyro_x = g_data[0];
                gyro_y = g_data[1];
                gyro_z = g_data[2];
                if (dt > 0.0f && dt < 0.1f) {
                    g_orientation.integrate(gyro_x, gyro_y, gyro_z, dt);
                }
            }
        }

        // Hardware Touchpad
        if (SDL_GetNumGamepadTouchpads(g_gamepad) > 0) {
            bool down = false;
            float tx = 0.0f, ty = 0.0f, pressure = 0.0f;
            if (SDL_GetGamepadTouchpadFinger(g_gamepad, 0, 0, &down, &tx, &ty, &pressure) && down) {
                touch.touchNum = 1;
                touch.touch[0].id = 0;
                touch.touch[0].x = static_cast<uint16_t>(std::clamp(tx * 1920.0f, 0.0f, 1919.0f));
                touch.touch[0].y = static_cast<uint16_t>(std::clamp(ty * 943.0f, 0.0f, 942.0f));
            }
        }
    }
}

} // anonymous namespace

extern "C" GRAVITY_SYSV_ABI int32_t scePadInit() {
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    if (g_initialized) return 0;
    g_initialized = true;
    g_emaDX = 0.0f;
    g_emaDY = 0.0f;
    g_orientation = Quat{};
    g_lastTime = std::chrono::high_resolution_clock::now();
    GetCursorPos(&g_lastMousePos);
    (void)SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    openFirstAvailableGamepad();
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadEnd() {
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    if (g_gamepad) {
        SDL_CloseGamepad(g_gamepad);
        g_gamepad = nullptr;
    }
    g_initialized = false;
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadSetActionMode(int32_t port, int32_t mode) {
    (void)port; (void)mode;
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadGetState(int32_t port, ScePadState* state) {
    (void)port;
    if (!state) return -1;
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    if (!g_initialized) return -1;

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTime).count();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    g_lastTime = now;

    float dx = 0.0f, dy = 0.0f;
    updateMouseDelta(dx, dy, dt);

    std::memset(state, 0, sizeof(ScePadState));
    state->timestamp = timestamp;

    uint8_t dummy_l2 = 0, dummy_r2 = 0;
    float gx = dy * g_sensitivity, gy = 0.0f, gz = dx * g_sensitivity;
    OrbisPadTouchData touch{};
    pollKeyboardAndGamepad(state->buttons, state->lx, state->ly, state->rx, state->ry,
                           dummy_l2, dummy_r2, gx, gy, gz, touch, dt);

    state->touch.reportCount = touch.touchNum;
    if (touch.touchNum > 0) {
        state->touch.touch[0].id = touch.touch[0].id;
        state->touch.touch[0].x = touch.touch[0].x;
        state->touch.touch[0].y = touch.touch[0].y;
    }

    // Simulate gravity vector [0, -1, 0] transformed by current orientation
    float sim_gx = 0.0f, sim_gy = -1.0f, sim_gz = 0.0f;
    g_orientation.transform_vec(sim_gx, sim_gy, sim_gz, state->motion.accel[0], state->motion.accel[1], state->motion.accel[2]);

    state->motion.gyro[0] = gx;
    state->motion.gyro[1] = gy;
    state->motion.gyro[2] = gz;

    state->motion.quat[0] = g_orientation.x;
    state->motion.quat[1] = g_orientation.y;
    state->motion.quat[2] = g_orientation.z;
    state->motion.quat[3] = g_orientation.w;
    state->motion.timestamp = timestamp;

    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadGetState2(int32_t port, ScePadData* state) {
    (void)port;
    if (!state) return -1;
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    if (!g_initialized) return -1;

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTime).count();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    g_lastTime = now;

    float dx = 0.0f, dy = 0.0f;
    updateMouseDelta(dx, dy, dt);

    std::memset(state, 0, sizeof(ScePadData));
    state->timestamp = timestamp;

    float gx = dy * g_sensitivity, gy = 0.0f, gz = dx * g_sensitivity;
    OrbisPadTouchData touch{};
    pollKeyboardAndGamepad(state->buttons, state->lx, state->ly, state->rx, state->ry,
                           state->l2, state->r2, gx, gy, gz, touch, dt);

    state->touch.reportCount = touch.touchNum;
    if (touch.touchNum > 0) {
        state->touch.touch[0].id = touch.touch[0].id;
        state->touch.touch[0].x = touch.touch[0].x;
        state->touch.touch[0].y = touch.touch[0].y;
    }

    float sim_gx = 0.0f, sim_gy = -1.0f, sim_gz = 0.0f;
    g_orientation.transform_vec(sim_gx, sim_gy, sim_gz, state->motion.accel[0], state->motion.accel[1], state->motion.accel[2]);

    state->motion.gyro[0] = gx;
    state->motion.gyro[1] = gy;
    state->motion.gyro[2] = gz;

    state->motion.quat[0] = g_orientation.x;
    state->motion.quat[1] = g_orientation.y;
    state->motion.quat[2] = g_orientation.z;
    state->motion.quat[3] = g_orientation.w;
    state->motion.timestamp = timestamp;

    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadGetMotionData(int32_t port, ScePadMotionData* motion) {
    (void)port;
    if (!motion) return -1;
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    if (!g_initialized) return -1;

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTime).count();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    g_lastTime = now;

    float dx = 0.0f, dy = 0.0f;
    updateMouseDelta(dx, dy, dt);

    float sim_gx = 0.0f, sim_gy = -1.0f, sim_gz = 0.0f;
    g_orientation.transform_vec(sim_gx, sim_gy, sim_gz, motion->accel[0], motion->accel[1], motion->accel[2]);

    motion->gyro[0] = dy * g_sensitivity;
    motion->gyro[1] = 0.0f;
    motion->gyro[2] = dx * g_sensitivity;

    motion->quat[0] = g_orientation.x;
    motion->quat[1] = g_orientation.y;
    motion->quat[2] = g_orientation.z;
    motion->quat[3] = g_orientation.w;
    motion->timestamp = timestamp;

    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadSetRumble(int32_t port, int32_t weak, int32_t strong) {
    (void)port;
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    if (g_gamepad && SDL_GamepadConnected(g_gamepad)) {
        Uint16 low_freq = static_cast<Uint16>(std::clamp(weak * 257, 0, 65535));
        Uint16 high_freq = static_cast<Uint16>(std::clamp(strong * 257, 0, 65535));
        SDL_RumbleGamepad(g_gamepad, low_freq, high_freq, 100);
    }
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadGetBatteryInfo(int32_t port, uint8_t* level, bool* charging) {
    (void)port;
    if (level) *level = 100;
    if (charging) *charging = false;
    return 0;
}

void resetOrientation() noexcept {
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    g_orientation = Quat{};
    g_emaDX = 0.0f;
    g_emaDY = 0.0f;
}

int32_t getControllerInformation(int32_t handle, ScePadControllerInformation* info) {
    if (!info || handle <= 0) return -1;
    std::memset(info, 0, sizeof(ScePadControllerInformation));
    info->touchPadPixelDensity = 1.0f;
    info->touchPadResolutionX = 1920;
    info->touchPadResolutionY = 943;
    info->stickDeadzoneLeft = 10;
    info->stickDeadzoneRight = 10;
    info->connectionType = 0; // standard wireless DS4
    info->connected = 1;      // controller is connected
    info->deviceClass = 0;    // standard
    return 0;
}

int32_t readPadData(int32_t handle, OrbisPadData* data, int32_t count) {
    if (!data || count <= 0 || handle <= 0) return -1;
    std::lock_guard<hle::Mutex> lock(g_padMutex);
    if (!g_initialized) {
        g_initialized = true;
        g_lastTime = std::chrono::high_resolution_clock::now();
        GetCursorPos(&g_lastMousePos);
        (void)SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        openFirstAvailableGamepad();
    }

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTime).count();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    g_lastTime = now;

    float dx = 0.0f, dy = 0.0f;
    updateMouseDelta(dx, dy, dt);

    uint64_t buttons = 0;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128, l2 = 0, r2 = 0;
    float gx = dy * g_sensitivity, gy = 0.0f, gz = dx * g_sensitivity;
    OrbisPadTouchData touch{};
    pollKeyboardAndGamepad(buttons, lx, ly, rx, ry, l2, r2, gx, gy, gz, touch, dt);

    float accel[3]{0.0f, 0.0f, 0.0f};
    float sim_gx = 0.0f, sim_gy = -1.0f, sim_gz = 0.0f;
    g_orientation.transform_vec(sim_gx, sim_gy, sim_gz, accel[0], accel[1], accel[2]);

    float gyro[3]{gx, gy, gz};

    for (int32_t i = 0; i < count; ++i) {
        std::memset(&data[i], 0, sizeof(OrbisPadData));
        data[i].buttons = static_cast<uint32_t>(buttons);
        data[i].lx = lx;
        data[i].ly = ly;
        data[i].rx = rx;
        data[i].ry = ry;
        data[i].l2 = l2;
        data[i].r2 = r2;
        data[i].orientation[0] = g_orientation.x;
        data[i].orientation[1] = g_orientation.y;
        data[i].orientation[2] = g_orientation.z;
        data[i].orientation[3] = g_orientation.w;
        data[i].accel[0] = accel[0];
        data[i].accel[1] = accel[1];
        data[i].accel[2] = accel[2];
        data[i].angularVelocity[0] = gyro[0];
        data[i].angularVelocity[1] = gyro[1];
        data[i].angularVelocity[2] = gyro[2];
        data[i].touch = touch;
        data[i].connected = 1;
        data[i].connectedCount = 1;
        data[i].timestamp = timestamp;
    }
    return count;
}

} // namespace gravity::hle::pad