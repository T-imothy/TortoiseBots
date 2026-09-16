#pragma once

#include "ScriptObjects.h"

namespace TortoiseBots {

// Player lifecycle adapter. It observes generic player events and asks the
// module's BotManager to attach/detach AI only for its own Headless records.
class BotPlayerAdapter final : public PlayerScript
{
public:
    BotPlayerAdapter();

    void OnReleaseToClient(Player* player) override;
    bool IsManagedBot(Player* player) override;
    uint8 GetBotRoles(Player* player) override;
    bool IsAIControlled(Player const* player) override;
    bool HasAIFollowers(Player const* player) override;
    bool GetAllowedRoles(Player const* player, uint8& roles) override;
    void SetForcedRole(Player* player, uint8 role) override;
    bool IsMachineDriven(Player const* player) override;
    bool IsUpdateCritical(Player const* player) override;
    bool IsAIUpdateDue(Player* player, uint32 diff) override;
    void OnAIUpdate(Player* player, uint32 diff, bool minimal) override;
    void OnLogin(Player* player) override;
    void OnMapChanged(Player* player) override;
    void OnBeforeLogout(Player* player) override;
    void OnLogout(Player* player) override;
};

// Unit lifecycle adapter to capture lethal damage events (accurate killer attribution).
class BotUnitAdapter final : public UnitScript
{
public:
    BotUnitAdapter();

    void OnUnitDeath(Unit* unit, Unit* killer) override;
};

} // namespace TortoiseBots

