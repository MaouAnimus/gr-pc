#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace gravity::hle::physics {

inline constexpr double fixed_step_seconds = 1.0 / 60.0;

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
constexpr Vec3 operator*(Vec3 v, double s) { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator*(double s, Vec3 v) { return v * s; }
constexpr Vec3 operator/(Vec3 v, double s) { return {v.x / s, v.y / s, v.z / s}; }
constexpr double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// All triangle faces are two-sided. Degenerate triangles are rejected.
struct Triangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

struct SphereState {
    Vec3 position;
    Vec3 velocity;
    double radius{0.5};
};

struct MotionSettings {
    // Limits collision/slide iterations, not integration substeps.
    std::size_t max_iterations{12};
    // Additional radius used by CCD. A scale-dependent roundoff allowance is
    // also enforced. Tune this to the world's units; it never changes dt.
    double surface_clearance{1.0e-6};
};

enum class MotionStatus {
    completed,
    invalid_input,
    initial_overlap,
    iteration_limit,
};

struct Contact {
    Vec3 point;
    Vec3 normal; // Unit vector from the triangle towards the sphere.
    std::size_t triangle_index{};
    double time_seconds{}; // Time since the beginning of this fixed tick.
};

struct MotionResult {
    SphereState state;
    std::vector<Contact> contacts;
    double time_advanced{};
    MotionStatus status{MotionStatus::invalid_input};
};

// Performs exactly one semi-implicit Euler tick:
//   velocity += gravity * (1/60), then sweep/slide for that same 1/60 second.
// Gravity is a world-space vector; no world-up direction is assumed.
//
// Analytic swept-sphere CCD covers faces, finite edges, and vertices of static
// triangles, with frictionless, zero-restitution contact response. On exhaustion
// of the contact budget the sphere stops at the last safe position, velocity is
// zero, and time_advanced reports the partial tick. Invalid input and initial
// penetration return the original state without advancing time.
//
// This is a native sphere controller, not a Havok implementation or guest hook.
// It does not handle moving/deforming meshes, nonspherical bodies, penetration
// recovery, or angular motion. Finite components must have magnitude <= 1e100;
// scenes whose numerical resolution cannot represent the sphere are rejected.
// Results are deterministic for the same ordered triangles and floating-point
// build; bit-identical results across architectures are not promised.
[[nodiscard]] MotionResult step_sphere_60hz(
    const SphereState& initial,
    Vec3 gravity,
    std::span<const Triangle> world,
    const MotionSettings& settings = {});

} // namespace gravity::hle::physics
