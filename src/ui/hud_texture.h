#pragma once
#include <vector>
#include <cstdint>
#include <entt/entt.hpp>
#include "game/game_events.h"

typedef unsigned int GLuint;

// Renders HUD stats into a CPU RGBA pixel buffer that is uploaded to a GL
// texture each frame.  The texture is composited over the 3-D viewport by
// the caller using ImGui's draw list (alpha-blended, always in front).
class HudTexture {
public:
    bool   init(int w, int h);
    void   resize(int w, int h);
    // Call once per frame after game state is updated.
    void   update(entt::registry& reg, entt::entity player, float dt);
    GLuint texture() const { return tex_; }
    void   destroy();

    void onLowFuel(const LowFuelEvent&);
    void onFuelDepleted(const FuelDepletedEvent&);

private:
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void fillRect(int x, int y, int w, int h,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void drawChar(int x, int y, char c, int scale,
                  uint8_t r, uint8_t g, uint8_t b);
    void drawText(int x, int y, const char* s, int scale,
                  uint8_t r, uint8_t g, uint8_t b);

    int                  w_  = 0;
    int                  h_  = 0;
    std::vector<uint8_t> pixels_;   // RGBA, row-major, top-to-bottom
    GLuint               tex_ = 0;

    bool  fuelDepleted_ = false;
    bool  lowFuel_      = false;
    float warningTimer_ = 0.0f;
};
