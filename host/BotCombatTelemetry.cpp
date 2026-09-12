#include "ScriptObjects.h"
#include "SpellAuras.h"
#include "playerbot/PlayerbotAIConfig.h"

// These helpers now belong exclusively to the module. The host supplies
// generic observations and has no linker dependency on an AI implementation.
void BotActionLog_LogDamage(Unit*, Unit*, uint32, uint32, char const*);
void BotActionLog_LogAuraAttempt(Unit*, uint32, int32, uint64);
void BotActionLog_LogAuraApply(Unit*, uint32, int32, uint64);
void BotActionLog_LogAuraRemove(Unit*, uint32, uint64);
void BotActionLog_LogCastStart(WorldObject*, uint32, uint64, uint32);
void BotActionLog_LogCastResult(WorldObject*, uint32, uint8, char const*);

namespace TortoiseBots
{
namespace
{
bool LoggingEnabled()
{
    return sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.enableActionLog;
}

class BotUnitTelemetry final : public UnitScript
{
public:
    BotUnitTelemetry() : UnitScript("tortoisebots_unit_telemetry", {
        UNITHOOK_ON_DAMAGE_ATTEMPT, UNITHOOK_ON_AURA_HOLDER_ATTEMPT,
        UNITHOOK_ON_AURA_HOLDER_REMOVAL, UNITHOOK_ON_AURA_APPLY }) {}
    void OnDamageAttempt(Unit* attacker, Unit* victim, uint32 amount, uint32 spell, char const* type) override
    {
        if (LoggingEnabled()) BotActionLog_LogDamage(attacker, victim, amount, spell, type);
    }
    void OnAuraHolderAttempt(Unit* target, uint32 spell, int32 duration, uint64 caster) override
    {
        if (LoggingEnabled()) BotActionLog_LogAuraAttempt(target, spell, duration, caster);
    }
    void OnAuraHolderRemoval(Unit* target, uint32 spell, uint64 caster) override
    {
        if (LoggingEnabled()) BotActionLog_LogAuraRemove(target, spell, caster);
    }
    void OnAuraApply(Unit* target, Aura* aura) override
    {
        if (LoggingEnabled() && aura)
            BotActionLog_LogAuraApply(target, aura->GetId(), aura->GetHolder()->GetAuraMaxDuration(), aura->GetCasterGuid().GetRawValue());
    }
};

class BotSpellTelemetry final : public AllSpellScript
{
public:
    BotSpellTelemetry() : AllSpellScript("tortoisebots_spell_telemetry") {}
    void OnCastAttempt(WorldObject* caster, uint32 spell, uint64 target, uint32 time) override
    {
        if (LoggingEnabled()) BotActionLog_LogCastStart(caster, spell, target, time);
    }
    void OnCastFinished(WorldObject* caster, uint32 spell, bool ok) override
    {
        if (LoggingEnabled()) BotActionLog_LogCastResult(caster, spell, ok ? 0 : 1, "finish");
    }
};
}

void RegisterCombatTelemetry()
{
    new BotUnitTelemetry();
    new BotSpellTelemetry();
}
}
