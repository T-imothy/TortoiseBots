#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/RandomBotFacade.h"
#include "runtime/BotWorldActions.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "playerbot/strategy/Action.h"

namespace ai
{
    class RandomBotUpdateAction : public Action
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        RandomBotUpdateAction(PlayerbotAI* ai) : Action(ai, "random bot update")
        {}

        virtual bool Execute(Event& event) override
        {
            if (auto deferred = TortoiseBots::BotWorldActions::Instance().Defer(bot, getName(), event))
                return *deferred;
            if (!sRandomBotFacade.IsRandomBot(bot))
                return false;

            if (bot->GetGroup())
            {
                Player* leader = ai->GetGroupMaster();
                PlayerbotAI* leaderAI = PlayerbotAIStorage::Instance().GetAI(leader);
                // A human group leader has no module AI. Resolve once and
                // preserve that group's activity instead of background recovery.
                if (leader && (!leaderAI || leaderAI->IsRealPlayer()))
                    return true;
            }

            if (ai->HasPlayerNearby())
                return true;

            return sRandomBotFacade.ProcessBot(bot);
        }

        virtual bool isUseful() override
        {
            return AI_VALUE(bool, "random bot update");
        }
    };

}