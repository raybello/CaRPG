#pragma once
#include <box3d/box3d.h>
#include "glm_math.h"

// glm <-> box3d conversions. b3Pos is an alias for b3Vec3 in the single-
// precision build (the default here — see box3d/types.h), so one pair of
// vec3 helpers covers both positions and directions.
inline b3Vec3 toB3(const glm::vec3& v) { return b3Vec3{ v.x, v.y, v.z }; }
inline glm::vec3 fromB3(const b3Vec3& v) { return glm::vec3(v.x, v.y, v.z); }

inline b3Quat toB3(const glm::quat& q) { return b3Quat{ b3Vec3{ q.x, q.y, q.z }, q.w }; }
inline glm::quat fromB3(const b3Quat& q) { return glm::quat(q.s, q.v.x, q.v.y, q.v.z); }

// Shape-filter category bit for the player chassis hull. The per-wheel
// suspension raycast (fired from a point raised above the wheel mount by
// suspensionTravel, which can sit inside/behind the chassis hull) excludes
// this bit so it can't hit the car's own body; every other shape keeps
// box3d's all-bits default filter and collides with the chassis normally.
inline constexpr uint64_t kCategoryChassis = 1ull;
