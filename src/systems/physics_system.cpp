#include "physics_system.h"
#include "ecs/components.h"
#include "physics/box3d_interop.h"

// TODO(box3d migration, next commit): rewrite the per-wheel suspension model
// on top of b3World_CastRayClosest + b3Body_ApplyForce, replacing the old
// raycast.h / RigidBodySystem-based implementation removed in this commit.
// For now this only mirrors the box3d body's velocity into Velocity (HUD/
// camera consumer) so the player car settles under gravity alone.
void PhysicsSystem::update(entt::registry& reg, float dt) {
    (void)dt;
    auto view = reg.view<PlayerTag, PhysicsBody, Velocity>();
    view.each([&](const PhysicsBody& pb, Velocity& vel) {
        vel.linear = fromB3(b3Body_GetLinearVelocity(pb.id));
    });
}
