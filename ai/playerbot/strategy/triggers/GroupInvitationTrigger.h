#pragma once
#include "playerbot/strategy/Trigger.h"
#include "Player.h"
#include "Group.h"

namespace ai
{
// The native pending invitation is authoritative. A strategy graph rebuild
// may discard a packet-triggered action before it executes; the native invite
// must remain discoverable until accepted, declined or canceled by the core.
class GroupInvitationTrigger : public Trigger
{
public:
    explicit GroupInvitationTrigger(PlayerbotAI* ai) : Trigger(ai, "group invite") {}
    bool IsActive() override
    {
        Group* invite = bot->GetGroupInvite();
        // Native leaders also hold a pending group while awaiting their first
        // member. They have no invitation to accept themselves.
        return invite && invite->GetLeaderGuid() != bot->GetObjectGuid();
    }
};
}
