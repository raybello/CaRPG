#include "hud.h"
#include "ecs/components.h"
#include <imgui.h>
#include <cmath>

void HUD::onLowFuel(const LowFuelEvent&) {
    lowFuel_      = true;
    warningFlash_ = 3.0f;
}

void HUD::onFuelDepleted(const FuelDepletedEvent&) {
    fuelDepleted_ = true;
}

void HUD::draw(entt::registry& reg, entt::entity player, int vpW, int vpH) {
    if (player == entt::null || !reg.valid(player)) return;

    const Fuel*           fuel  = reg.try_get<Fuel>(player);
    const DerivedCarStats* stats = reg.try_get<DerivedCarStats>(player);
    const Velocity*        vel  = reg.try_get<Velocity>(player);

    // Top-right HUD window
    ImGui::SetNextWindowPos(ImVec2((float)vpW - 260.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250.0f, 120.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoInputs
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##hud", nullptr, flags);

    // Speed readout
    if (vel) {
        float speed = glm::length(vel->linear);
        ImGui::Text("Speed: %.1f m/s", speed);
    }

    // Top speed
    if (stats) {
        ImGui::Text("Top Speed: %.0f | Power: %.0f", stats->topSpeed, stats->power);
    }

    // Fuel bar
    if (fuel) {
        float ratio = fuel->ratio();
        ImVec4 barColor = fuelDepleted_ ? ImVec4(0.8f, 0.1f, 0.1f, 1.0f)
                        : (ratio < 0.25f) ? ImVec4(1.0f, 0.5f, 0.0f, 1.0f)
                        : ImVec4(0.2f, 0.8f, 0.3f, 1.0f);
        ImGui::Text("Fuel:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f));
        ImGui::PopStyleColor();

        if (fuelDepleted_) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "OUT OF FUEL");
        } else if (lowFuel_ && warningFlash_ > 0.0f) {
            // Simple flash by using time
            float blink = std::fmod(ImGui::GetTime(), 0.5f);
            if (blink < 0.25f)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "LOW FUEL!");
        }
    }

    ImGui::End();

    // OUT OF FUEL overlay
    if (fuelDepleted_) {
        ImGui::SetNextWindowPos(ImVec2((float)vpW * 0.5f - 120.0f, (float)vpH * 0.5f - 20.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(240.0f, 40.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##gameover", nullptr, flags);
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "  OUT OF FUEL - GAME OVER");
        ImGui::End();
    }
}
