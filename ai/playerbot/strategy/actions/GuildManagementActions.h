#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"
#include "Guild/GuildMgr.h"

namespace ai
{
    class GuidManageAction : public ChatCommandAction
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        GuidManageAction(PlayerbotAI* ai, std::string name = "guild manage", uint16 opcode = CMSG_GUILD_INVITE) : ChatCommandAction(ai, name), opcode(opcode) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override { return false; }

    protected:
        virtual WorldPacket GetPacket(Player* player) { WorldPacket data(OpcodesList(opcode), 8); data << player->GetName(); return data; }
        virtual void SendPacket(WorldPacket data, Event event) {};
        virtual void SendPacket(WorldPacket data) { Event event = Event();  SendPacket(data, event); };
        virtual Player* GetPlayer(Event event);
        virtual bool PlayerIsValid(Player* member) { return !member->GetGuildId(); };
        // Native guild membership queries.
        Guild* MemberGuild(Player* member) const
        {
            if (!member || !member->GetGuildId())
                return nullptr;
            Guild* guild = sGuildMgr.GetGuildById(member->GetGuildId());
            return guild && guild->GetMemberSlot(member->getObjectGuid()) ? guild : nullptr;
        }
        bool HasGuildRight(Player* member, uint32 right) const
        {
            Guild* guild = MemberGuild(member);
            return guild && guild->HasRankRight(guild->GetMemberSlot(member->getObjectGuid())->RankId, right);
        }
        bool IsGuildLeader(Player* member) const
        {
            Guild* guild = MemberGuild(member);
            return guild && guild->GetLeaderGuid() == member->getObjectGuid();
        }
        virtual uint8 GetRankId(Player* member)
        {
            Guild* guild = MemberGuild(member);
            return guild ? guild->GetMemberSlot(member->getObjectGuid())->RankId : 255;
        }
        virtual bool GuildIsFull(uint32 guildId)
        {
            Guild* guild = sGuildMgr.GetGuildById(guildId);
            return !guild || guild->GetMemberSize() >= 1000;
        }

        uint16 opcode;
    };

    class GuildInviteAction : public GuidManageAction
    {
    public:
        GuildInviteAction(PlayerbotAI* ai, std::string name = "guild invite", uint16 opcode = CMSG_GUILD_INVITE) : GuidManageAction(ai, name, opcode) {}
        virtual bool isUseful() override { return HasGuildRight(bot, GR_RIGHT_INVITE) && !GuildIsFull(bot->GetGuildId()); }

    protected:
        virtual void SendPacket(WorldPacket data, Event event) override { bot->GetSession()->HandleGuildInviteOpcode(data); };
        virtual bool PlayerIsValid(Player* member) override { return !member->GetGuildId(); };
    };

    class GuildJoinAction : public GuidManageAction
    {
    public:
        GuildJoinAction(PlayerbotAI* ai, std::string name = "guild join", uint16 opcode = CMSG_GUILD_INVITE) : GuidManageAction(ai, name, opcode) {}
        virtual bool isUseful() override { return !bot->GetGuildId(); }

    protected:
        virtual WorldPacket GetPacket(Player* player) override { WorldPacket data(OpcodesList(opcode), 8); data << bot->GetName(); return data; }
        virtual void SendPacket(WorldPacket data, Event event) override { if (Player* inviter = GetPlayer(event)) if (inviter->GetSession()) inviter->GetSession()->HandleGuildInviteOpcode(data); };
        virtual bool PlayerIsValid(Player* member) override { return !bot->GetGuildId() && HasGuildRight(member, GR_RIGHT_INVITE) && !GuildIsFull(member->GetGuildId()); };
    };

    class GuildPromoteAction : public GuidManageAction
    {
    public:
        GuildPromoteAction(PlayerbotAI* ai, std::string name = "guild promote", uint16 opcode = CMSG_GUILD_PROMOTE) : GuidManageAction(ai, name, opcode) {}
        virtual bool isUseful() override { return HasGuildRight(bot, GR_RIGHT_PROMOTE); }

    protected:
        virtual void SendPacket(WorldPacket data, Event event) override { bot->GetSession()->HandleGuildPromoteOpcode(data); };
        virtual bool PlayerIsValid(Player* member) override { return MemberGuild(member) && member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member) - 1; };
    };

    class GuildDemoteAction : public GuidManageAction
    {
    public:
        GuildDemoteAction(PlayerbotAI* ai, std::string name = "guild demote", uint16 opcode = CMSG_GUILD_DEMOTE) : GuidManageAction(ai, name, opcode) {}
        virtual bool isUseful() override { return HasGuildRight(bot, GR_RIGHT_DEMOTE); }

    protected:
        virtual void SendPacket(WorldPacket data, Event event) override { bot->GetSession()->HandleGuildDemoteOpcode(data); };
        virtual bool PlayerIsValid(Player* member) override { return MemberGuild(member) && member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member); };
    };

    class GuildLeaderAction : public GuidManageAction
    {
    public:
        GuildLeaderAction(PlayerbotAI* ai, std::string name = "guild leader", uint16 opcode = CMSG_GUILD_LEADER) : GuidManageAction(ai, name, opcode) {}
        virtual bool isUseful() override { return IsGuildLeader(bot); }

    protected:
        virtual void SendPacket(WorldPacket data, Event event) override { bot->GetSession()->HandleGuildLeaderOpcode(data); };
        virtual bool PlayerIsValid(Player* member) override { return MemberGuild(member) && member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member) - 1; };
    };

    class GuildRemoveAction : public GuidManageAction
    {
    public:
        GuildRemoveAction(PlayerbotAI* ai, std::string name = "guild remove", uint16 opcode = CMSG_GUILD_REMOVE) : GuidManageAction(ai, name, opcode) {}
        virtual bool isUseful() override { return HasGuildRight(bot, GR_RIGHT_REMOVE); }

    protected:
        virtual void SendPacket(WorldPacket data, Event event) override { bot->GetSession()->HandleGuildRemoveOpcode(data); };
        virtual bool PlayerIsValid(Player* member) override { return MemberGuild(member) && member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member); };
    };

    class GuildManageNearbyAction : public ChatCommandAction
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        GuildManageNearbyAction(PlayerbotAI* ai) : ChatCommandAction(ai, "guild manage nearby") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class GuildLeaveAction : public ChatCommandAction
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        GuildLeaveAction(PlayerbotAI* ai) : ChatCommandAction(ai, "guild leave") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override { return bot->GetGuildId(); }
    };
}
