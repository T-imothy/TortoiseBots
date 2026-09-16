#pragma once
#include "playerbot/PlayerbotAI.h"
#include "MovementActions.h"
#include "runtime/BotWorldActions.h"

namespace ai
{
	class ReviveFromCorpseAction : public MovementAction
    {
	public:
		ReviveFromCorpseAction(PlayerbotAI* ai) : MovementAction(ai, "revive from corpse") {}
        bool RequiresWorldOwner() const override { return true; }
        virtual bool Execute(Event& event) override;
    };

    class FindCorpseAction : public MovementAction
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        FindCorpseAction(PlayerbotAI* ai) : MovementAction(ai, "find corpse") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

	class SpiritHealerAction : public MovementAction
    {
	public:
	    SpiritHealerAction(PlayerbotAI* ai, std::string name = "spirit healer") : MovementAction(ai,name) {}
        bool RequiresWorldOwner() const override { return true; }
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };
}
