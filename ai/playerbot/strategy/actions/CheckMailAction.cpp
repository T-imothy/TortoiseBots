#include "playerbot/playerbot.h"
#include "CheckMailAction.h"
#include "MailAction.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"
#include "runtime/BotWorldActions.h"
#include "playerbot/PlayerbotAIConfig.h"
using namespace ai;

bool CheckMailAction::Execute(Event& event)
{
    if (auto deferred = TortoiseBots::BotWorldActions::Instance().Defer(bot, getName(), event))
        return *deferred;
    if (!isUseful()) return false;
    ObjectGuid const mailbox = MailProcessor::FindMailbox(ai);
    if (!mailbox) return false;

    MasterPlayer* master = bot->GetSession()->GetMasterPlayer();
    // Native return deletes the Mail object and can append a new self-addressed
    // message. Snapshot IDs only, then resolve before each native operation.
    std::vector<uint32> ids;
    for (auto i = master->GetMailBegin(); i != master->GetMailEnd(); ++i)
        if (*i) ids.push_back((*i)->messageID);
    bool returned = false;
    unsigned attempts = 0;
    for (uint32 id : ids)
    {
        Mail* mail = master->GetMail(id);
        if (!mail || mail->state == MAIL_STATE_DELETED || mail->deliver_time > time(nullptr) ||
            mail->stationery == MAIL_STATIONERY_AUCTION || mail->messageType != MAIL_NORMAL ||
            mail->items.empty() || mail->subject.find("Item(s) you asked for") != std::string::npos)
            continue;
        Player* owner = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, mail->sender));
        // Preserve the existing online-human-sender policy. Native return drops
        // mail to hardcore senders, so automatic processing must retain it.
        if (!owner || owner->IsHardcore() || sPlayerbotAIConfig.IsInRandomAccountList(
            sObjectMgr.GetPlayerAccountIdByGUID(owner->GetObjectGuid())))
            continue;
        WorldPacket packet(CMSG_MAIL_RETURN_TO_SENDER);
        packet << mailbox << id;
        bot->GetSession()->HandleMailReturnToSender(packet);
        returned = !master->GetMail(id) || returned;
        if (++attempts == 8) break;
    }
    return returned;
}

bool CheckMailAction::isUseful()
{
    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    return master && !ai->GetMaster() && master->GetMailSize() && bot->IsAlive() &&
        bot->IsInWorld() && !bot->IsBeingTeleported() && !bot->IsInCombat() && !bot->InBattleGround();
}
// End native automatic mail return.
