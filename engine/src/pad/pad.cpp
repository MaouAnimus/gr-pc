#include "hle/pad.h"
#include "hle/kernel.h"
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

static void pollKeyboard(uint64_t& buttons, uint8_t& lx, uint8_t& ly, uint8_t& rx, uint8_t& ry, uint8_t& l2, uint8_t& r2) {
    buttons = 0;
    lx = 128;
    ly = 128;
    rx = 128;
    ry = 128;
    l2 = 0;
    r2 = 0;

    // Left stick WASD
    bool w = (GetAsyncKeyState('W') & 0x8000) != 0;
    bool s = (GetAsyncKeyState('S') & 0x8000) != 0;
    bool a = (GetAsyncKeyState('A') & 0x8000) != 0;
    bool d = (GetAsyncKeyState('D') & 0x8000) != 0;

    if (w && !s) ly = 0;
    else if (s && !w) ly = 255;

    if (a && !d) lx = 0;
    else if (d && !a) lx = 255;

    // Buttons
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
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t scePadEnd() {
    std::lock_guard<hle::Mutex> lock(g_padMutex);
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
    pollKeyboard(state->buttons, state->lx, state->ly, state->rx, state->ry, dummy_l2, dummy_r2);

    state->touch.reportCount = 0;

    // Simulate gravity vector [0, -1, 0] transformed by current orientation
    float gx = 0.0f, gy = -1.0f, gz = 0.0f;
    g_orientation.transform_vec(gx, gy, gz, state->motion.accel[0], state->motion.accel[1], state->motion.accel[2]);

    state->motion.gyro[0] = dy * g_sensitivity;
    state->motion.gyro[1] = 0.0f;
    state->motion.gyro[2] = dx * g_sensitivity;

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

    pollKeyboard(state->buttons, state->lx, state->ly, state->rx, state->ry, state->l2, state->r2);

    state->touch.reportCount = 0;

    float gx = 0.0f, gy = -1.0f, gz = 0.0f;
    g_orientation.transform_vec(gx, gy, gz, state->motion.accel[0], state->motion.accel[1], state->motion.accel[2]);

    state->motion.gyro[0] = dy * g_sensitivity;
    state->motion.gyro[1] = 0.0f;
    state->motion.gyro[2] = dx * g_sensitivity;

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

    float gx = 0.0f, gy = -1.0f, gz = 0.0f;
    g_orientation.transform_vec(gx, gy, gz, motion->accel[0], motion->accel[1], motion->accel[2]);

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
    (void)port; (void)weak; (void)strong;
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
    }

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTime).count();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    g_lastTime = now;

    float dx = 0.0f, dy = 0.0f;
    updateMouseDelta(dx, dy, dt);

    uint64_t buttons = 0;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128, l2 = 0, r2 = 0;
    pollKeyboard(buttons, lx, ly, rx, ry, l2, r2);

    float accel[3]{0.0f, 0.0f, 0.0f};
    float gx = 0.0f, gy = -1.0f, gz = 0.0f;
    g_orientation.transform_vec(gx, gy, gz, accel[0], accel[1], accel[2]);

    float gyro[3]{dy * g_sensitivity, 0.0f, dx * g_sensitivity};

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
        data[i].connected = 1;
        data[i].connectedCount = 1;
        data[i].timestamp = timestamp;
    }
    return count;
}

} // namespace gravity::hle::pad