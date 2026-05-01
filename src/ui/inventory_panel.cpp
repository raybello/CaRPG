#include "inventory_panel.h"
#include "ecs/components.h"
#include "game/item_catalog.h"
#include "systems/item_system.h"
#include <imgui.h>

void InventoryPanel::onItemPickedUp(const ItemPickedUpEvent&) { dirty_ = true; }
void InventoryPanel::onGearEquipped(const GearEquippedEvent&) { dirty_ = true; }

void InventoryPanel::draw(entt::registry& reg, entt::entity player,
                           ItemSystem& itemSys, entt::dispatcher& disp) {
    if (!visible) return;
    if (player == entt::null || !reg.valid(player)) return;

    auto* inv = reg.try_get<Inventory>(player);
    if (!inv) return;

    ImGui::SetNextWindowSize(ImVec2(332.0f, 393.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(400.0f, 10.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Inventory  [i]", &visible)) {
        ImGui::End();
        return;
    }

    // Equipped gear summary
    ImGui::SeparatorText("Equipped Gear");
    auto showEquipped = [&](const char* label, entt::entity e) {
        ImGui::Text("%-12s ", label);
        ImGui::SameLine();
        if (e != entt::null && reg.valid(e)) {
            const auto* ic = reg.try_get<ItemComponent>(e);
            const ItemDef* def = ic ? getItemDef(ic->itemId) : nullptr;
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                               def ? def->name.c_str() : "Unknown");
        } else {
            ImGui::TextDisabled("(none)");
        }
    };
    showEquipped("Wheels:",     inv->equippedWheels);
    showEquipped("Suspension:", inv->equippedSuspension);
    showEquipped("Engine:",     inv->equippedEngine);

    ImGui::SeparatorText("Backpack");

    // Item grid
    ImGui::Columns(4, "##inv", false);
    for (int i = 0; i < Inventory::kSlots; ++i) {
        const auto& slot = inv->slots[i];
        bool empty = slot.id == ItemId::Count || slot.qty == 0;
        const ItemDef* def = empty ? nullptr : getItemDef(slot.id);

        char label[64];
        if (def)
            snprintf(label, sizeof(label), "%s\nx%u", def->name.c_str(), slot.qty);
        else
            snprintf(label, sizeof(label), "---");

        ImGui::PushID(i);
        bool pressed = ImGui::Button(label, ImVec2(72.0f, 48.0f));
        if (!empty && def) {
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s", def->name.c_str(), def->description.c_str());
            if (pressed)
                itemSys.useItem(reg, disp, player, i);
        }
        ImGui::PopID();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);

    ImGui::End();
    dirty_ = false;
}
