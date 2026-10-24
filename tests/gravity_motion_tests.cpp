#include "hle/physics/gravity_motion.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

using namespace gravity::hle::physics;

namespace {

bool near(double a, double b, double tolerance = 1.0e-7) { return std::abs(a - b) <= tolerance; }
bool near(Vec3 a, Vec3 b, double tolerance = 1.0e-7) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
double length(Vec3 v) { return std::hypot(v.x, v.y, v.z); }

std::array<Triangle, 2> floor_mesh(double height = 0) {
    return {{{{-100, -100, height}, {100, -100, height}, {100, 100, height}},
             {{-100, -100, height}, {100, 100, height}, {-100, 100, height}}}};
}
std::array<Triangle, 2> wall_mesh(double x = 0) {
    return {{{{x, -100, -100}, {x, 100, -100}, {x, 100, 100}},
             {{x, -100, -100}, {x, 100, 100}, {x, -100, 100}}}};
}

void free_fall() {
    const SphereState initial{{1, 2, 3}, {4, 5, 6}, 0.5};
    const Vec3 gravity{7, -8, 9};
    const auto result = step_sphere_60hz(initial, gravity, {});
    CHECK(result.status == MotionStatus::completed);
    CHECK(result.time_advanced == fixed_step_seconds);
    CHECK(result.contacts.empty());
    CHECK(near(result.state.velocity, initial.velocity + gravity * fixed_step_seconds));
    CHECK(near(result.state.position, initial.position + result.state.velocity * fixed_step_seconds));
    auto state = initial;
    auto repeated = initial;
    for (unsigned i = 0; i < 120; ++i) {
        state = step_sphere_60hz(state, gravity, {}).state;
        repeated = step_sphere_60hz(repeated, gravity, {}).state;
    }
    CHECK(near(state.position, initial.position + initial.velocity * 2 + gravity * (fixed_step_seconds * fixed_step_seconds * 120 * 121 / 2)));
    CHECK(state.position.x == repeated.position.x && state.position.y == repeated.position.y && state.position.z == repeated.position.z);
    CHECK(state.velocity.x == repeated.velocity.x && state.velocity.y == repeated.velocity.y && state.velocity.z == repeated.velocity.z);
}

void tunneling() {
    const auto floor = floor_mesh();
    // The uncorrected displacement crosses a zero-thickness floor by 23 m.
    auto result = step_sphere_60hz({{0, 0, 10}, {0, 0, -2000}, 0.5}, {0, 0, -9.81}, floor);
    CHECK(result.status == MotionStatus::completed && !result.contacts.empty());
    CHECK(result.state.position.z >= 0.5 && result.state.position.z < 0.50001);
    CHECK(near(result.state.velocity, {}));
    CHECK(result.time_advanced == fixed_step_seconds);
    CHECK(result.contacts.front().time_seconds > 0 && result.contacts.front().time_seconds < fixed_step_seconds);

    const auto wall = wall_mesh();
    result = step_sphere_60hz({{-10, 3, 4}, {3000, 0, 0}, 0.25}, {}, wall);
    CHECK(result.status == MotionStatus::completed && !result.contacts.empty());
    CHECK(result.state.position.x <= -0.25 && result.state.position.x > -0.25001);
    CHECK(near(result.state.velocity, {}));
    // Both directions collide regardless of the triangle winding.
    result = step_sphere_60hz({{10, 3, 4}, {-1.0e6, 0, 0}, 0.25}, {}, wall);
    CHECK(result.status == MotionStatus::completed && !result.contacts.empty());
    CHECK(result.state.position.x >= 0.25 && result.state.position.x < 0.25001);
}

void orientation() {
    const auto ceiling = floor_mesh(5);
    SphereState state{{0, 0, 3}, {}, 0.5};
    for (unsigned i = 0; i < 100; ++i) {
        const auto result = step_sphere_60hz(state, {0, 0, 30}, ceiling);
        CHECK(result.status == MotionStatus::completed);
        state = result.state;
        CHECK(state.position.z <= 4.5);
    }
    CHECK(near(state.position.z, 4.5, 2.0e-6));
    CHECK(near(state.velocity, {}));

    const std::array<Vec3, 8> orientations{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
                                           {0, 0, 1}, {0, 0, -1}, {1, 2, 3}, {-3, 1, -2}}};
    for (Vec3 axis : orientations) {
        const Vec3 normal = axis / length(axis);
        const Vec3 reference = std::abs(normal.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
        const Vec3 tangent = cross(normal, reference) / length(cross(normal, reference));
        const Vec3 bitangent = cross(normal, tangent);
        const std::array<Triangle, 1> rotated{{{tangent * -100 + bitangent * -100,
                                              tangent * 100 + bitangent * -100,
                                              bitangent * 100}}};
        const auto result = step_sphere_60hz({normal * 5, normal * -2000, 0.5}, normal * -9.81, rotated);
        CHECK(result.status == MotionStatus::completed && !result.contacts.empty());
        CHECK(dot(result.state.position, normal) >= 0.5);
        CHECK(near(dot(result.state.position, normal), 0.5, 2.0e-6));
        CHECK(near(result.contacts.front().normal, normal));
        CHECK(length(result.state.velocity) < 1.0e-6);
    }
}

void sliding() {
    auto floor = floor_mesh();
    auto wall = wall_mesh();
    std::vector<Triangle> world(floor.begin(), floor.end());
    world.insert(world.end(), wall.begin(), wall.end());
    const auto result = step_sphere_60hz({{-2, 0, 1}, {400, 30, -120}, 0.5}, {}, world);
    CHECK(result.status == MotionStatus::completed);
    CHECK(result.contacts.size() >= 2);
    CHECK(result.state.position.x <= -0.5 && result.state.position.x > -0.50001);
    CHECK(result.state.position.z >= 0.5 && result.state.position.z < 0.50001);
    CHECK(near(result.state.position.y, 0.5));
    CHECK(near(result.state.velocity, {0, 30, 0}));

    MotionSettings limited;
    limited.max_iterations = 1;
    const auto stopped = step_sphere_60hz({{-2, 0, 1}, {400, 30, -120}, 0.5}, {}, world, limited);
    CHECK(stopped.status == MotionStatus::iteration_limit);
    CHECK(stopped.contacts.size() == 1);
    CHECK(stopped.time_advanced > 0 && stopped.time_advanced < fixed_step_seconds);
    CHECK(stopped.state.position.x <= -0.5 && stopped.state.position.z >= 0.5);
    CHECK(near(stopped.state.velocity, {}));

    // A sphere touching a floor can move away and can slide tangentially.
    const auto away = step_sphere_60hz({{0, 0, 0.5}, {0, 0, 10}, 0.5}, {}, floor);
    CHECK(away.status == MotionStatus::completed && away.contacts.empty());
    CHECK(away.state.position.z > 0.5);
    const auto tangent = step_sphere_60hz({{0, 0, 0.5}, {20, 0, 0}, 0.5}, {}, floor);
    CHECK(tangent.status == MotionStatus::completed && tangent.contacts.empty());
    CHECK(near(tangent.state.position, {20 * fixed_step_seconds, 0, 0.5}));
}

void features() {
    const std::array<Triangle, 1> isolated{{{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}}};
    // Motion parallel to the face cannot be caught by a face-only sweep.
    const auto edge = step_sphere_60hz({{5, -2, 0.3}, {0, 240, 0}, 0.5}, {}, isolated);
    CHECK(edge.status == MotionStatus::completed && !edge.contacts.empty());
    CHECK(near(edge.contacts.front().point, {5, 0, 0}));
    CHECK(edge.contacts.front().normal.y < -0.79 && edge.contacts.front().normal.z > 0.59);
    CHECK(edge.state.position.z > 0.3);
    CHECK(dot(edge.state.velocity, edge.contacts.front().normal) >= -1.0e-10);

    const auto corner = step_sphere_60hz({{-2, -2, 0.3}, {240, 240, 0}, 0.5}, {}, isolated);
    CHECK(corner.status == MotionStatus::completed && !corner.contacts.empty());
    CHECK(near(corner.contacts.front().point, {}));
    CHECK(corner.contacts.front().normal.x < -0.56 && corner.contacts.front().normal.y < -0.56);
    CHECK(corner.contacts.front().normal.z > 0.59);
    CHECK(corner.state.position.z > 0.3);
    CHECK(dot(corner.state.velocity, corner.contacts.front().normal) >= -1.0e-10);
}

void invalid() {
    const auto floor = floor_mesh();
    const SphereState initial{{1, 2, 3}, {4, 5, 6}, 0.5};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double radius : {0.0, -1.0, nan, infinity}) {
        auto bad = initial;
        bad.radius = radius;
        const auto result = step_sphere_60hz(bad, {}, floor);
        CHECK(result.status == MotionStatus::invalid_input && result.time_advanced == 0);
        CHECK(near(result.state.position, initial.position));
    }
    auto bad = initial;
    bad.velocity.x = nan;
    CHECK(step_sphere_60hz(bad, {}, floor).status == MotionStatus::invalid_input);
    CHECK(step_sphere_60hz(initial, {0, infinity, 0}, floor).status == MotionStatus::invalid_input);
    auto bad_triangle = floor;
    bad_triangle[0].a.x = nan;
    CHECK(step_sphere_60hz(initial, {}, bad_triangle).status == MotionStatus::invalid_input);
    const std::array<Triangle, 1> degenerate{{{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}}};
    CHECK(step_sphere_60hz(initial, {}, degenerate).status == MotionStatus::invalid_input);
    MotionSettings settings;
    settings.max_iterations = 0;
    CHECK(step_sphere_60hz(initial, {}, floor, settings).status == MotionStatus::invalid_input);
    settings.max_iterations = 65;
    CHECK(step_sphere_60hz(initial, {}, floor, settings).status == MotionStatus::invalid_input);
    settings.max_iterations = 12;
    settings.surface_clearance = -1;
    CHECK(step_sphere_60hz(initial, {}, floor, settings).status == MotionStatus::invalid_input);
    const SphereState overlap{{0, 0, 0.25}, {0, 0, -2000}, 0.5};
    const auto result = step_sphere_60hz(overlap, {0, 0, -30}, floor);
    CHECK(result.status == MotionStatus::initial_overlap && result.time_advanced == 0 && result.contacts.empty());
    CHECK(near(result.state.position, overlap.position) && near(result.state.velocity, overlap.velocity));
    bad = initial;
    bad.velocity.x = std::numeric_limits<double>::max();
    CHECK(step_sphere_60hz(bad, {}, floor).status == MotionStatus::invalid_input);
}

} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    if (suite == "free_fall") free_fall();
    else if (suite == "tunneling") tunneling();
    else if (suite == "orientation") orientation();
    else if (suite == "sliding") sliding();
    else if (suite == "features") features();
    else if (suite == "invalid") invalid();
    else CHECK(false);
    std::printf("gravity motion %s passed\n", argv[1]);
}
