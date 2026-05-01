#include "rigid_body_system.h"
#include "ecs/components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

// =============================================================================
// 3D rigid body physics — concepts adapted from research/physics3D
//
//   integration:
//     v ← v + (F/m + g)·dt              (linear)
//     ω ← I_world⁻¹ · (I_world·ω + τ·dt) (angular)
//     x ← x + v·dt
//     q ← normalize(q + 0.5·(0,ω)·q·dt)
//     apply linear/angular damping
//
//   collision detection:
//     box-vs-box  → separating axis theorem (3 face axes A, 3 face axes B,
//                   9 edge×edge cross products). Picks the axis of minimum
//                   overlap as the contact normal; the contact point is the
//                   incident-face vertex deepest along that normal.
//     box-vs-plane→ enumerate 8 corners, every penetrating corner is a
//                   contact (n = plane normal, depth = d − n·corner).
//
//   impulse-based response (per contact):
//     j  = -(1+e)·(vRel·n) / (1/mA + 1/mB + (rA×n)·IA⁻¹(rA×n) + (rB×n)·IB⁻¹(rB×n))
//     friction impulse along the tangent component of vRel,
//     clamped by μ·|j|.
//
//   positional correction:
//     split the overlap mass-weighted between the two bodies, with a small
//     "slop" to avoid jitter on resting contacts.
// =============================================================================

namespace {

// --- math helpers ----------------------------------------------------------
inline bool finiteV(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool finiteQ(const glm::quat& q) {
    return std::isfinite(q.w) && std::isfinite(q.x) &&
           std::isfinite(q.y) && std::isfinite(q.z);
}

// Inverse inertia tensor in world space: R · I⁻¹_local · Rᵀ.
inline glm::mat3 worldInvInertia(const glm::mat3& invIlocal, const glm::quat& q) {
    glm::mat3 R = glm::mat3_cast(q);
    return R * invIlocal * glm::transpose(R);
}

// Conservative world-space AABB of an OBB (for broad phase).
struct AABB { glm::vec3 lo, hi; };
inline AABB obbAabb(const glm::vec3& center, const glm::vec3& he, const glm::quat& q) {
    glm::mat3 R = glm::mat3_cast(q);
    glm::vec3 ext;
    for (int i = 0; i < 3; ++i) {
        ext[i] = std::fabs(R[0][i]) * he.x +
                 std::fabs(R[1][i]) * he.y +
                 std::fabs(R[2][i]) * he.z;
    }
    return { center - ext, center + ext };
}
inline bool aabbOverlap(const AABB& a, const AABB& b) {
    return (a.lo.x <= b.hi.x && a.hi.x >= b.lo.x) &&
           (a.lo.y <= b.hi.y && a.hi.y >= b.lo.y) &&
           (a.lo.z <= b.hi.z && a.hi.z >= b.lo.z);
}

// --- contact + body proxy --------------------------------------------------
struct Contact {
    entt::entity a = entt::null;
    entt::entity b = entt::null;          // entt::null → static plane
    glm::vec3    normal;                  // points A → B
    glm::vec3    point;                   // world-space
    float        depth = 0.0f;
};

// Cached per-body pointers — saves repeated try_get inside the inner loops.
struct BodyRef {
    entt::entity   ent;
    Transform*     tf;
    RigidBody*     rb;
    BoxCollider*   box;
    PlaneCollider* plane;
};

// --- Box vs Box (SAT) ------------------------------------------------------
//
// Returns true on overlap and fills `out` with normal (pointing A→B), the
// approximate contact point, and the penetration depth.
//
// Reference:
//   Christer Ericson, "Real-Time Collision Detection", §4.4.
//   Dirk Gregorius, GDC 2013, "The Separating Axis Test".
struct OBB {
    glm::vec3 c;        // center
    glm::vec3 he;       // half extents
    glm::vec3 axis[3];  // local +X, +Y, +Z in world space
};

inline OBB makeOBB(const glm::vec3& c, const glm::vec3& he, const glm::quat& q) {
    glm::mat3 R = glm::mat3_cast(q);
    return { c, he, { R[0], R[1], R[2] } };
}

bool satBoxBox(const OBB& A, const OBB& B, glm::vec3& outN, glm::vec3& outP, float& outDepth) {
    constexpr float kEps = 1e-6f;

    // Express B's basis in A's basis.
    float R[3][3];      // R[i][j] = A.axis[i] · B.axis[j]
    float AbsR[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j]    = glm::dot(A.axis[i], B.axis[j]);
            AbsR[i][j] = std::fabs(R[i][j]) + kEps;
        }
    }
    glm::vec3 t = B.c - A.c;
    glm::vec3 tA(glm::dot(t, A.axis[0]), glm::dot(t, A.axis[1]), glm::dot(t, A.axis[2]));

    float bestDepth = std::numeric_limits<float>::infinity();
    int   bestIdx   = -1;
    glm::vec3 bestAxis(0);

    auto test = [&](float ra, float rb, float dist, const glm::vec3& axis, int idx) -> bool {
        float overlap = ra + rb - std::fabs(dist);
        if (overlap < 0.0f) return false;
        if (overlap < bestDepth) {
            bestDepth = overlap;
            bestIdx   = idx;
            // Orient axis so it points A → B.
            bestAxis  = (dist < 0.0f) ? -axis : axis;
        }
        return true;
    };

    // --- 6 face-normal axes ------------------------------------------------
    for (int i = 0; i < 3; ++i) {
        float ra = A.he[i];
        float rb = B.he[0]*AbsR[i][0] + B.he[1]*AbsR[i][1] + B.he[2]*AbsR[i][2];
        if (!test(ra, rb, tA[i], A.axis[i], i)) return false;
    }
    for (int j = 0; j < 3; ++j) {
        float ra = A.he[0]*AbsR[0][j] + A.he[1]*AbsR[1][j] + A.he[2]*AbsR[2][j];
        float rb = B.he[j];
        float dist = tA[0]*R[0][j] + tA[1]*R[1][j] + tA[2]*R[2][j];
        if (!test(ra, rb, dist, B.axis[j], 3 + j)) return false;
    }

    // --- 9 edge-edge axes --------------------------------------------------
    // Use a small bias for nearly-parallel edge pairs (skip them — face axes
    // already detect those cases robustly).
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            glm::vec3 axis = glm::cross(A.axis[i], B.axis[j]);
            float len2 = glm::dot(axis, axis);
            if (len2 < 1e-8f) continue;
            axis /= std::sqrt(len2);

            int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
            int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            float ra = A.he[i1]*AbsR[i2][j] + A.he[i2]*AbsR[i1][j];
            float rb = B.he[j1]*AbsR[i][j2] + B.he[j2]*AbsR[i][j1];
            float dist = std::fabs(tA[i2]*R[i1][j] - tA[i1]*R[i2][j]);
            // Edge-edge result needs the *signed* distance for orientation.
            float signedDist = tA[i2]*R[i1][j] - tA[i1]*R[i2][j];
            if (!test(ra, rb, signedDist, axis, 6 + i*3 + j)) return false;
            (void)dist;
        }
    }

    if (bestIdx < 0 || !std::isfinite(bestDepth)) return false;

    // Contact point: deepest vertex of the incident OBB along the normal.
    // For face contacts (bestIdx 0..5) this picks the right corner; for
    // edge contacts (6..14) it's a reasonable approximation that keeps
    // impulse direction physically meaningful.
    auto deepestVertex = [](const OBB& X, const glm::vec3& n) {
        glm::vec3 v = X.c;
        for (int i = 0; i < 3; ++i) {
            float s = (glm::dot(n, X.axis[i]) >= 0.0f) ? -X.he[i] : X.he[i];
            v += X.axis[i] * s;
        }
        return v;
    };

    glm::vec3 vB = deepestVertex(B, bestAxis);
    glm::vec3 vA = deepestVertex(A, -bestAxis);
    outP     = 0.5f * (vA + vB);
    outN     = bestAxis;
    outDepth = bestDepth;
    return true;
}

// Box vs (infinite) plane: every corner that crosses the plane is a contact.
void detectBoxPlane(const OBB& A, const PlaneCollider& plane,
                    entt::entity entA, std::vector<Contact>& out)
{
    for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
    for (int sz = -1; sz <= 1; sz += 2) {
        glm::vec3 corner = A.c
                         + A.axis[0] * (sx * A.he.x)
                         + A.axis[1] * (sy * A.he.y)
                         + A.axis[2] * (sz * A.he.z);
        float dist = glm::dot(plane.normal, corner) - plane.d;
        if (dist < 0.0f) {
            Contact c;
            c.a      = entA;
            c.b      = entt::null;
            c.normal = -plane.normal;   // points A → "B" (B = plane below)
            c.point  = corner - plane.normal * dist;
            c.depth  = -dist;
            out.push_back(c);
        }
    }
}

// --- impulse resolution (mirrors research/PhysicsEngine::resolveImpulses) --
void resolveContact(const Contact& c, const BodyRef& A, const BodyRef* B,
                    float positionalBias, float slop)
{
    // Effective inverse mass and inverse inertia (world-space).
    float invMassA = A.rb->inverseMass;
    glm::mat3 invIA = (A.rb->fixed || A.rb->kinematic)
                       ? glm::mat3(0.0f)
                       : worldInvInertia(A.rb->invInertiaLocal, A.tf->rotation);
    float invMassB = B ? B->rb->inverseMass : 0.0f;
    glm::mat3 invIB = (!B || B->rb->fixed || B->rb->kinematic)
                       ? glm::mat3(0.0f)
                       : worldInvInertia(B->rb->invInertiaLocal, B->tf->rotation);

    glm::vec3 rA = c.point - A.tf->position;
    glm::vec3 rB = B ? (c.point - B->tf->position) : glm::vec3(0.0f);

    // Velocity of the contact point on each body (linear + ω×r).
    glm::vec3 vA = A.rb->linearVel + glm::cross(A.rb->angularVel, rA);
    glm::vec3 vB = B ? (B->rb->linearVel + glm::cross(B->rb->angularVel, rB))
                     : glm::vec3(0.0f);
    glm::vec3 vRel = vB - vA;
    float velAlongN = glm::dot(vRel, c.normal);

    // If they're already separating, only do positional correction (below).
    if (velAlongN < 0.0f) {
        float e = std::min(A.rb->restitution, B ? B->rb->restitution : 0.0f);

        // Normal impulse magnitude.
        glm::vec3 rAxN = glm::cross(rA, c.normal);
        glm::vec3 rBxN = glm::cross(rB, c.normal);
        float angA = glm::dot(c.normal, glm::cross(invIA * rAxN, rA));
        float angB = B ? glm::dot(c.normal, glm::cross(invIB * rBxN, rB)) : 0.0f;
        float invMassSum = invMassA + invMassB + angA + angB;
        if (invMassSum > 1e-8f) {
            float j = -(1.0f + e) * velAlongN / invMassSum;
            glm::vec3 impulse = c.normal * j;

            if (!A.rb->fixed && !A.rb->kinematic && finiteV(impulse)) {
                A.rb->linearVel  -= impulse * invMassA;
                A.rb->angularVel -= invIA * glm::cross(rA, impulse);
            }
            if (B && !B->rb->fixed && !B->rb->kinematic && finiteV(impulse)) {
                B->rb->linearVel  += impulse * invMassB;
                B->rb->angularVel += invIB * glm::cross(rB, impulse);
            }

            // Friction impulse along the tangent component of vRel.
            vA = A.rb->linearVel + glm::cross(A.rb->angularVel, rA);
            vB = B ? (B->rb->linearVel + glm::cross(B->rb->angularVel, rB))
                   : glm::vec3(0.0f);
            vRel = vB - vA;
            glm::vec3 tangent = vRel - c.normal * glm::dot(vRel, c.normal);
            float tLen = glm::length(tangent);
            if (tLen > 1e-4f) {
                tangent /= tLen;
                glm::vec3 rAxT = glm::cross(rA, tangent);
                glm::vec3 rBxT = glm::cross(rB, tangent);
                float angAT = glm::dot(tangent, glm::cross(invIA * rAxT, rA));
                float angBT = B ? glm::dot(tangent, glm::cross(invIB * rBxT, rB)) : 0.0f;
                float invSumT = invMassA + invMassB + angAT + angBT;
                if (invSumT > 1e-8f) {
                    float jt = -glm::dot(vRel, tangent) / invSumT;
                    float mu = 0.5f * (A.rb->friction + (B ? B->rb->friction : A.rb->friction));
                    jt = std::clamp(jt, -mu * std::fabs(j), mu * std::fabs(j));
                    glm::vec3 fImp = tangent * jt;
                    if (!A.rb->fixed && !A.rb->kinematic && finiteV(fImp)) {
                        A.rb->linearVel  -= fImp * invMassA;
                        A.rb->angularVel -= invIA * glm::cross(rA, fImp);
                    }
                    if (B && !B->rb->fixed && !B->rb->kinematic && finiteV(fImp)) {
                        B->rb->linearVel  += fImp * invMassB;
                        B->rb->angularVel += invIB * glm::cross(rB, fImp);
                    }
                }
            }
        }
    }

    // Positional correction — split overlap mass-weighted.
    float invMassSumPos = invMassA + invMassB;
    if (invMassSumPos > 1e-8f) {
        float corr = std::max(c.depth - slop, 0.0f) * positionalBias / invMassSumPos;
        glm::vec3 push = c.normal * corr;
        if (!A.rb->fixed && !A.rb->kinematic) A.tf->position -= push * invMassA;
        if (B && !B->rb->fixed && !B->rb->kinematic) B->tf->position += push * invMassB;
    }
}

// --- one fixed integration step --------------------------------------------
void stepOnce(entt::registry& reg, float dt, const glm::vec3& gravity,
              float positionalBias, float slop)
{
    // 1. Apply gravity, integrate linear/angular state for dynamic bodies.
    auto rbView = reg.view<Transform, RigidBody>();
    rbView.each([&](Transform& tf, RigidBody& rb) {
        if (rb.fixed || rb.kinematic) {
            // Kinematic: caller is responsible for setting linearVel/angularVel
            // (e.g. the car system). Don't integrate position/orientation —
            // the kinematic owner already did, or doesn't care.
            rb.forceAccum  = glm::vec3(0.0f);
            rb.torqueAccum = glm::vec3(0.0f);
            return;
        }

        // Forces → linear acceleration.
        glm::vec3 a = rb.forceAccum * rb.inverseMass;
        if (rb.useGravity) a += gravity;
        rb.linearVel += a * dt;

        // Damping.
        rb.linearVel  *= std::max(0.0f, 1.0f - rb.linearDamping  * dt);

        // Angular: ω updated via torque (skipping gyroscopic implicit solver).
        if (glm::dot(rb.torqueAccum, rb.torqueAccum) > 0.0f) {
            glm::mat3 invI = worldInvInertia(rb.invInertiaLocal, tf.rotation);
            rb.angularVel += invI * rb.torqueAccum * dt;
        }
        rb.angularVel *= std::max(0.0f, 1.0f - rb.angularDamping * dt);

        // Sanity.
        if (!finiteV(rb.linearVel))  rb.linearVel  = glm::vec3(0.0f);
        if (!finiteV(rb.angularVel)) rb.angularVel = glm::vec3(0.0f);

        // Integrate position.
        tf.position += rb.linearVel * dt;

        // Integrate orientation via q' = q + 0.5 * (0,ω) * q * dt.
        glm::quat omegaQ(0.0f, rb.angularVel.x, rb.angularVel.y, rb.angularVel.z);
        glm::quat dq = 0.5f * omegaQ * tf.rotation;
        glm::quat next = tf.rotation + dq * dt;
        if (finiteQ(next)) tf.rotation = glm::normalize(next);

        rb.forceAccum  = glm::vec3(0.0f);
        rb.torqueAccum = glm::vec3(0.0f);
    });

    // 2. Build a flat list of bodies for collision, with cached pointers.
    std::vector<BodyRef> bodies;
    bodies.reserve(32);
    for (auto e : rbView) {
        BodyRef br;
        br.ent   = e;
        br.tf    = &reg.get<Transform>(e);
        br.rb    = &reg.get<RigidBody>(e);
        br.box   = reg.try_get<BoxCollider>(e);
        br.plane = reg.try_get<PlaneCollider>(e);
        if (br.box || br.plane) bodies.push_back(br);
    }

    // 3. Detect collisions.
    std::vector<Contact> contacts;
    contacts.reserve(64);
    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            BodyRef& A = bodies[i];
            BodyRef& B = bodies[j];

            // Two static/kinematic bodies never collide with each other in
            // this engine — neither side can be moved by the impulse.
            bool aStatic = A.rb->fixed || A.rb->kinematic;
            bool bStatic = B.rb->fixed || B.rb->kinematic;
            if (aStatic && bStatic) continue;

            // box vs plane (always treat the plane side as B for the contact,
            // but our resolve helper takes the plane as "no body B").
            if (A.box && B.plane) {
                if (!A.box->skipGroundCollision) {
                    OBB obbA = makeOBB(A.tf->position, A.box->halfExtents, A.tf->rotation);
                    detectBoxPlane(obbA, *B.plane, A.ent, contacts);
                }
                continue;
            }
            if (B.box && A.plane) {
                if (!B.box->skipGroundCollision) {
                    OBB obbB = makeOBB(B.tf->position, B.box->halfExtents, B.tf->rotation);
                    detectBoxPlane(obbB, *A.plane, B.ent, contacts);
                }
                continue;
            }

            if (A.box && B.box) {
                OBB obbA = makeOBB(A.tf->position, A.box->halfExtents, A.tf->rotation);
                OBB obbB = makeOBB(B.tf->position, B.box->halfExtents, B.tf->rotation);

                AABB aA = obbAabb(obbA.c, obbA.he, A.tf->rotation);
                AABB aB = obbAabb(obbB.c, obbB.he, B.tf->rotation);
                if (!aabbOverlap(aA, aB)) continue;

                glm::vec3 n, p; float d;
                if (satBoxBox(obbA, obbB, n, p, d)) {
                    Contact c;
                    c.a      = A.ent;
                    c.b      = B.ent;
                    c.normal = n;
                    c.point  = p;
                    c.depth  = d;
                    contacts.push_back(c);
                }
            }
        }
    }

    // 4. Resolve contacts. For stability, run two passes — typical for
    //    sequential impulses.
    for (int pass = 0; pass < 2; ++pass) {
        for (const Contact& c : contacts) {
            // Re-fetch refs (positions may have shifted after correction).
            BodyRef A;
            A.ent = c.a;
            A.tf  = &reg.get<Transform>(c.a);
            A.rb  = &reg.get<RigidBody>(c.a);
            A.box = reg.try_get<BoxCollider>(c.a);
            A.plane = reg.try_get<PlaneCollider>(c.a);

            if (c.b == entt::null) {
                resolveContact(c, A, nullptr, positionalBias, slop);
            } else {
                BodyRef B;
                B.ent = c.b;
                B.tf  = &reg.get<Transform>(c.b);
                B.rb  = &reg.get<RigidBody>(c.b);
                B.box = reg.try_get<BoxCollider>(c.b);
                B.plane = reg.try_get<PlaneCollider>(c.b);
                resolveContact(c, A, &B, positionalBias, slop);
            }
        }
    }
}

}  // namespace

// =============================================================================
// Public entry points
// =============================================================================
glm::mat3 RigidBodySystem::boxInvInertia(float mass, const glm::vec3& he) {
    if (mass <= 0.0f) return glm::mat3(0.0f);
    float ixx = (mass / 3.0f) * (he.y * he.y + he.z * he.z);
    float iyy = (mass / 3.0f) * (he.x * he.x + he.z * he.z);
    float izz = (mass / 3.0f) * (he.x * he.x + he.y * he.y);
    glm::mat3 I(0.0f);
    I[0][0] = (ixx > 0.0f) ? 1.0f / ixx : 0.0f;
    I[1][1] = (iyy > 0.0f) ? 1.0f / iyy : 0.0f;
    I[2][2] = (izz > 0.0f) ? 1.0f / izz : 0.0f;
    return I;
}

void RigidBodySystem::update(entt::registry& reg, float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;

    // Clamp the per-frame budget so a hitch can't tunnel everything through
    // the ground. Also cap substeps so a long pause doesn't eat the CPU.
    dt = std::min(dt, 0.1f);
    int   nSteps = std::min(maxSubsteps, std::max(1, (int)std::ceil(dt / fixedStep)));
    float h      = dt / (float)nSteps;

    for (int i = 0; i < nSteps; ++i) {
        if (preStepCb) preStepCb(reg, h);
        stepOnce(reg, h, gravity, positionalBias, penetrationSlop);
    }
}
