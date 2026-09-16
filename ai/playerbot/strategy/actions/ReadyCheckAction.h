#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    class ReadyCheckAction : public Action
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        ReadyCheckAction(PlayerbotAI* ai, std::string name = "ready check") : Action(ai, name) {}
        virtual bool Execute(Event& event) override;

    protected:
        bool ReadyCheck(Player* requester);
    };

    class FinishReadyCheckAction : public ReadyCheckAction
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        FinishReadyCheckAction(PlayerbotAI* ai) : ReadyCheckAction(ai, "finish ready check") {}
        virtual bool Execute(Event& event) override;
    };
}
