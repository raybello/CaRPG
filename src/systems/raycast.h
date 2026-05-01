#pragma once
#include "glm_math.h"
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------
// Ray / RaycastHit — lightweight raycast types
// ---------------------------------------------------------------------------
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;  // must be normalized before use
};

struct RaycastHit {
    bool      hit      = false;
    float     distance = 0.0f;
    glm::vec3 point    {0.0f};
    glm::vec3 normal   {0.0f, 1.0f, 0.0f};
};

// ---------------------------------------------------------------------------
// raycastPlane — infinite plane defined by { normal, d } where n·x = d
//
// Returns a hit if the ray intersects the plane from the front side within
// a positive distance. Returns hit.hit = false if parallel or behind origin.
// ---------------------------------------------------------------------------
inline RaycastHit raycastPlane(const Ray& ray, const glm::vec3& normal, float d) {
    RaycastHit hit;
    float denom = glm::dot(normal, ray.direction);
    if (std::fabs(denom) < 1e-6f) return hit;  // parallel
    float t = (d - glm::dot(normal, ray.origin)) / denom;
    if (t < 0.0f) return hit;                   // behind origin
    hit.hit      = true;
    hit.distance = t;
    hit.point    = ray.origin + ray.direction * t;
    hit.normal   = normal;
    return hit;
}

// ---------------------------------------------------------------------------
// raycastBox — oriented bounding box via slab method in OBB-local space
//
// center / halfExtents / rotation come from the entity's Transform + BoxCollider.
// Returns the entry hit (tMin). Returns hit.hit = false on miss or if the
// entry is behind the ray origin.
// ---------------------------------------------------------------------------
inline RaycastHit raycastBox(const Ray& ray,
                             const glm::vec3& center,
                             const glm::vec3& halfExtents,
                             const glm::quat& rotation)
{
    RaycastHit hit;

    // Transform ray into OBB local space (OBB is axis-aligned there).
    glm::mat3 R    = glm::mat3_cast(rotation);
    glm::mat3 Rinv = glm::transpose(R);  // orthonormal → inverse = transpose
    glm::vec3 localOrig = Rinv * (ray.origin - center);
    glm::vec3 localDir  = Rinv * ray.direction;

    float tMin = -std::numeric_limits<float>::infinity();
    float tMax =  std::numeric_limits<float>::infinity();
    int   normalAxis = -1;
    float normalSign = 1.0f;

    for (int i = 0; i < 3; ++i) {
        if (std::fabs(localDir[i]) < 1e-8f) {
            // Ray is parallel to this slab — miss if origin is outside.
            if (localOrig[i] < -halfExtents[i] || localOrig[i] > halfExtents[i])
                return hit;
        } else {
            float inv = 1.0f / localDir[i];
            float t0  = (-halfExtents[i] - localOrig[i]) * inv;
            float t1  = ( halfExtents[i] - localOrig[i]) * inv;
            float sign = -1.0f;
            if (t0 > t1) { std::swap(t0, t1); sign = 1.0f; }
            if (t0 > tMin) { tMin = t0; normalAxis = i; normalSign = sign; }
            tMax = std::min(tMax, t1);
            if (tMax < tMin) return hit;  // slabs don't overlap
        }
    }
    if (tMin < 0.0f) return hit;  // entry is behind origin

    hit.hit      = true;
    hit.distance = tMin;
    hit.point    = ray.origin + ray.direction * tMin;

    // Reconstruct the world-space entry normal from the entry slab axis.
    if (normalAxis >= 0) {
        glm::vec3 localNormal(0.0f);
        localNormal[normalAxis] = normalSign;
        hit.normal = glm::normalize(R * localNormal);
    }
    return hit;
}
