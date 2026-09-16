#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/values/GuildValues.h"

namespace ai
{
    class GuildShareItemAction : public Action
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        GuildShareItemAction(PlayerbotAI* ai) : Action(ai, "guild share item") {}

        bool Execute(Event& event) override;
        bool isUseful() override;
    };
}