#include "hle/physics/gravity_motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gravity::hle::physics {
namespace {

constexpr double machine_epsilon = std::numeric_limits<double>::epsilon();
constexpr double max_component = 1.0e100;

double length(Vec3 v) { return std::hypot(v.x, v.y, v.z); }
double magnitude(Vec3 v) { return (std::max)({std::abs(v.x), std::abs(v.y), std::abs(v.z)}); }
bool valid(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && magnitude(v) <= max_component;
}
Vec3 add_scaled(Vec3 a, Vec3 b, double t) {
    return {std::fma(b.x, t, a.x), std::fma(b.y, t, a.y), std::fma(b.z, t, a.z)};
}

struct Edge {
    Vec3 start;
    Vec3 direction;
    double length;
};
struct PreparedTriangle {
    Triangle triangle;
    Vec3 normal;
    std::array<Edge, 3> edges;
};

bool prepare(const Triangle& triangle, PreparedTriangle& out) {
    if (!valid(triangle.a) || !valid(triangle.b) || !valid(triangle.c)) return false;
    const Vec3 ab = triangle.b - triangle.a;
    const Vec3 ac = triangle.c - triangle.a;
    const Vec3 bc = triangle.c - triangle.b;
    const double longest = (std::max)({length(ab), length(ac), length(bc)});
    const Vec3 area = cross(ab, ac);
    const double twice_area = length(area);
    if (!std::isfinite(twice_area) || longest == 0 ||
        twice_area <= 32 * machine_epsilon * longest * longest) return false;
    out.triangle = triangle;
    out.normal = area / twice_area;
    out.edges = {{{triangle.a, ab / length(ab), length(ab)},
                  {triangle.b, bc / length(bc), length(bc)},
                  {triangle.c, -ac / length(ac), length(ac)}}};
    return true;
}

bool on_face(Vec3 point, const PreparedTriangle& triangle, double tolerance) {
    for (const auto& edge : triangle.edges) {
        // Unit edge directions keep this signed side distance in world units.
        if (dot(cross(edge.direction, point - edge.start), triangle.normal) < -tolerance) return false;
    }
    return true;
}

Vec3 closest_point(Vec3 center, const PreparedTriangle& triangle, double tolerance) {
    const Vec3 projected = add_scaled(center, triangle.normal, -dot(center - triangle.triangle.a, triangle.normal));
    if (on_face(projected, triangle, tolerance)) return projected;
    Vec3 best{};
    double best_distance = std::numeric_limits<double>::infinity();
    for (const auto& edge : triangle.edges) {
        const double along = std::clamp(dot(center - edge.start, edge.direction), 0.0, edge.length);
        const Vec3 point = add_scaled(edge.start, edge.direction, along);
        const double distance = length(center - point);
        if (distance < best_distance) {
            best_distance = distance;
            best = point;
        }
    }
    return best;
}

struct Hit {
    double fraction{std::numeric_limits<double>::infinity()};
    Vec3 point{};
    Vec3 normal{};
    std::size_t triangle_index{};
};

void consider(Hit& best, double fraction, Vec3 point, Vec3 normal, Vec3 displacement,
              std::size_t triangle_index, double tolerance, double fraction_tolerance) {
    if (!std::isfinite(fraction) || fraction < -fraction_tolerance || fraction > 1.0 + fraction_tolerance) return;
    const double normal_length = length(normal);
    if (!std::isfinite(normal_length) || normal_length == 0) return;
    normal = normal / normal_length;
    // A tangential or separating path cannot enter the expanded surface.
    if (dot(displacement, normal) >= -tolerance) return;
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (fraction < best.fraction) best = {fraction, point, normal, triangle_index};
}

// Entry time for a moving point into a sphere about the origin. Computing the
// closest point on the line avoids subtracting two huge discriminant terms
// when a fast, nearly tangential path approaches a small feature.
bool sphere_entry(Vec3 offset, Vec3 displacement, double radius, double& fraction) {
    const double speed_squared = dot(displacement, displacement);
    if (speed_squared == 0) return false;
    const double nearest_time = -dot(offset, displacement) / speed_squared;
    const Vec3 nearest = add_scaled(offset, displacement, nearest_time);
    const double radial_squared = radius * radius - dot(nearest, nearest);
    if (radial_squared < 0) return false;
    fraction = nearest_time - std::sqrt(radial_squared / speed_squared);
    return std::isfinite(fraction);
}

Hit sweep(Vec3 center, Vec3 displacement, double radius,
          std::span<const PreparedTriangle> triangles, double tolerance) {
    Hit best;
    const double displacement_length = length(displacement);
    const double fraction_tolerance = tolerance / (std::max)(displacement_length, 1.0);
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        const auto& triangle = triangles[index];
        const Vec3 nearest = closest_point(center, triangle, tolerance);
        const Vec3 separation = center - nearest;
        if (length(separation) <= radius + tolerance) {
            consider(best, 0, nearest, separation, displacement, index, tolerance, fraction_tolerance);
        }

        // Both orientations are tested, so winding never decides which side
        // collides. A face candidate is accepted only over the finite face.
        for (double side : {-1.0, 1.0}) {
            const Vec3 normal = triangle.normal * side;
            const double along = dot(displacement, normal);
            if (along >= -tolerance) continue;
            const double distance = dot(center - triangle.triangle.a, normal);
            const double fraction = (radius - distance) / along;
            if (fraction < -fraction_tolerance || fraction > 1.0 + fraction_tolerance) continue;
            const Vec3 point = add_scaled(add_scaled(center, displacement, fraction), normal, -radius);
            if (on_face(point, triangle, tolerance)) {
                consider(best, fraction, point, normal, displacement, index, tolerance, fraction_tolerance);
            }
        }

        for (const auto& edge : triangle.edges) {
            const Vec3 offset = center - edge.start;
            const Vec3 perpendicular_offset = add_scaled(offset, edge.direction, -dot(offset, edge.direction));
            const Vec3 perpendicular_motion = add_scaled(displacement, edge.direction, -dot(displacement, edge.direction));
            double fraction{};
            // Finite-cylinder portion of the edge capsule.
            if (sphere_entry(perpendicular_offset, perpendicular_motion, radius, fraction) &&
                fraction >= -fraction_tolerance && fraction <= 1.0 + fraction_tolerance) {
                const Vec3 impact = add_scaled(center, displacement, fraction);
                const double along = dot(impact - edge.start, edge.direction);
                if (along >= 0 && along <= edge.length) {
                    const Vec3 point = add_scaled(edge.start, edge.direction, along);
                    consider(best, fraction, point, impact - point, displacement, index, tolerance, fraction_tolerance);
                }
            }
            // One sphere per vertex supplies the three rounded capsule ends.
            if (sphere_entry(offset, displacement, radius, fraction)) {
                const Vec3 impact = add_scaled(center, displacement, fraction);
                consider(best, fraction, edge.start, impact - edge.start, displacement, index, tolerance, fraction_tolerance);
            }
        }
    }
    return best;
}

Vec3 project_velocity(Vec3 velocity, std::span<const Vec3> normals, double tolerance) {
    // Projection onto the feasible velocity cone in 3D. Its closest point is
    // either unconstrained, on one plane, on an intersection of two planes,
    // or zero. Enumerating these cases avoids sequential-plane projections
    // reintroducing motion into a previously touched wall.
    Vec3 best{};
    double best_distance = dot(velocity, velocity);
    const auto consider_velocity = [&](Vec3 candidate) {
        for (Vec3 normal : normals) {
            if (dot(candidate, normal) < -tolerance) return;
        }
        const Vec3 difference = candidate - velocity;
        const double distance = dot(difference, difference);
        if (distance < best_distance) {
            best = candidate;
            best_distance = distance;
        }
    };
    consider_velocity(velocity);
    for (std::size_t i = 0; i < normals.size(); ++i) {
        consider_velocity(add_scaled(velocity, normals[i], -dot(velocity, normals[i])));
        for (std::size_t j = 0; j < i; ++j) {
            Vec3 direction = cross(normals[i], normals[j]);
            const double direction_length = length(direction);
            if (direction_length <= 64 * machine_epsilon) continue;
            direction = direction / direction_length;
            consider_velocity(direction * dot(velocity, direction));
        }
    }
    // Roundoff may leave a very small inward component. Bias only that
    // component outwards; stop conservatively if a degenerate cone resists it.
    for (unsigned pass = 0; pass < 8; ++pass) {
        bool corrected = false;
        for (Vec3 normal : normals) {
            const double inward = dot(best, normal);
            if (inward < 0) {
                best = add_scaled(best, normal, -inward + tolerance);
                corrected = true;
            }
        }
        if (!corrected) return best;
    }
    for (Vec3 normal : normals) {
        if (dot(best, normal) < 0) return {};
    }
    return best;
}

} // namespace

MotionResult step_sphere_60hz(const SphereState& initial, Vec3 gravity,
                              std::span<const Triangle> world, const MotionSettings& settings) {
    MotionResult result;
    result.state = initial;
    if (!valid(initial.position) || !valid(initial.velocity) || !valid(gravity) ||
        !std::isfinite(initial.radius) || initial.radius <= 0 || initial.radius > max_component ||
        !std::isfinite(settings.surface_clearance) || settings.surface_clearance < 0 ||
        settings.surface_clearance > max_component || settings.max_iterations == 0 || settings.max_iterations > 64) return result;

    const Vec3 velocity = add_scaled(initial.velocity, gravity, fixed_step_seconds);
    const Vec3 endpoint = add_scaled(initial.position, velocity, fixed_step_seconds);
    if (!valid(velocity) || !valid(endpoint)) return result;
    double scale = (std::max)({1.0, magnitude(initial.position), magnitude(endpoint), initial.radius});
    std::vector<PreparedTriangle> triangles;
    triangles.reserve(world.size());
    for (const auto& triangle : world) {
        PreparedTriangle prepared;
        if (!prepare(triangle, prepared)) return result;
        scale = (std::max)({scale, magnitude(triangle.a), magnitude(triangle.b), magnitude(triangle.c)});
        triangles.push_back(prepared);
    }
    const double tolerance = 32 * machine_epsilon * scale;
    if (initial.radius <= 4 * tolerance) return result;
    const double sweep_radius = initial.radius + (std::max)(settings.surface_clearance, 4 * tolerance);
    const double overlap_tolerance = (std::min)(initial.radius * 1.0e-6, tolerance);
    for (const auto& triangle : triangles) {
        if (length(initial.position - closest_point(initial.position, triangle, tolerance)) < initial.radius - overlap_tolerance) {
            result.status = MotionStatus::initial_overlap;
            return result;
        }
    }

    result.state.velocity = velocity;
    std::vector<Vec3> normals;
    normals.reserve(settings.max_iterations);
    result.contacts.reserve(settings.max_iterations);
    double remaining = fixed_step_seconds;
    std::size_t iteration = 0;
    for (;;) {
        const Vec3 displacement = result.state.velocity * remaining;
        if (length(displacement) <= tolerance) {
            result.time_advanced = fixed_step_seconds;
            result.status = MotionStatus::completed;
            return result;
        }
        if (iteration == settings.max_iterations) {
            result.state.velocity = {};
            result.status = MotionStatus::iteration_limit;
            return result;
        }
        const Hit hit = sweep(result.state.position, displacement, sweep_radius, triangles, tolerance);
        if (!std::isfinite(hit.fraction)) {
            result.state.position = add_scaled(result.state.position, result.state.velocity, remaining);
            result.time_advanced = fixed_step_seconds;
            result.status = MotionStatus::completed;
            return result;
        }
        result.state.position = add_scaled(result.state.position, displacement, hit.fraction);
        const double advanced = remaining * hit.fraction;
        result.time_advanced += advanced;
        remaining -= advanced;
        result.contacts.push_back({hit.point, hit.normal, hit.triangle_index, result.time_advanced});
        bool duplicate = false;
        for (Vec3 normal : normals) {
            if (dot(normal, hit.normal) > 1 - 64 * machine_epsilon) duplicate = true;
        }
        if (!duplicate) normals.push_back(hit.normal);
        const double velocity_tolerance = (std::max)(tolerance / fixed_step_seconds * 1.0e-3,
                                                     64 * machine_epsilon * length(result.state.velocity));
        result.state.velocity = project_velocity(result.state.velocity, normals, velocity_tolerance);
        ++iteration;
        if (remaining <= fixed_step_seconds * 16 * machine_epsilon) {
            result.time_advanced = fixed_step_seconds;
            result.status = MotionStatus::completed;
            return result;
        }
    }
}

} // namespace gravity::hle::physics
