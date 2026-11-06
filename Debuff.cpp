#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <SKSE/PluginAPI.h>
#include <Skyrim/SkyrimVM.h>
#include <Skyrim/Forms/TESForm.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/SpellItem.h>
#include <Skyrim/Forms/PlayerCharacter.h>

#include "Debuff.h"
#include "Needs.h"
#include "Food.h"
#include "Settings.h"
#include "Messages.h"
#include "Widget.h"

namespace
{
	// IDs locais dos feiticos no EatingSleepingDrinking.esp
	const UInt32 kHungryAbility		= 0x828;	// debuff de fome
	const UInt32 kStuffedSpell		= 0x82C;	// buff "bem alimentado"
	const UInt32 kThirstyAbility	= 0x827;	// debuff de sede
	const UInt32 kQuenchedSpell		= 0x822;	// buff "sem sede"
	const UInt32 kSleepyAbility		= 0x82A;	// debuff de sono
	const UInt32 kNotWearySpell		= 0x82E;	// buff "descansado"
	const UInt32 kBodyAbility		= 0x829;	// debuff de sujidade
	const UInt32 kCleanBodySpell	= 0x81F;	// buff "corpo limpo"

	// estados ja anunciados (evita repetir a mensagem a cada ciclo)
	enum { kMsgHungry = 1, kMsgThirsty = 2, kMsgTired = 4, kMsgDirty = 8, kMsgBathSoon = 0x10 };
	UInt32	g_msgFlags = 0;

	bool g_nativeFailed = false;

	typedef _NativeFunction<Actor, bool, SpellItem *, bool>	AddSpellNative;
	typedef _NativeFunction<Actor, bool, SpellItem *>		RemoveSpellNative;

	SpellItem *GetSpell(UInt32 localId)
	{
		UInt32 id = Food::FormId(localId);
		if (!id) return NULL;
		TESForm *f = LookupFormByID(id);
		if (f && f->formType == FormType::Spell)
			return static_cast<SpellItem *>(f);
		return NULL;
	}

	void AddSpellTo(Actor *actor, UInt32 localId)
	{
		if (g_nativeFailed || !g_skyrimVM) return;
		SpellItem *spell = GetSpell(localId);
		if (!spell || actor->HasSpell(spell)) return;
		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		AddSpellNative *fn = (AddSpellNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "AddSpell");
		if (!fn || !fn->Validate(state)) { _MESSAGE("ESD: nao encontrei Actor.AddSpell; buffs/debuffs desligados"); g_nativeFailed = true; return; }
		(*fn)(state, 0, actor, spell, false);
	}

	void RemoveSpellFrom(Actor *actor, UInt32 localId)
	{
		if (g_nativeFailed || !g_skyrimVM) return;
		SpellItem *spell = GetSpell(localId);
		if (!spell || !actor->HasSpell(spell)) return;
		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		RemoveSpellNative *fn = (RemoveSpellNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "RemoveSpell");
		if (!fn || !fn->Validate(state)) { _MESSAGE("ESD: nao encontrei Actor.RemoveSpell; buffs/debuffs desligados"); g_nativeFailed = true; return; }
		(*fn)(state, 0, actor, spell);
	}

	bool HasFlag(UInt32 f)		{ return (g_msgFlags & f) != 0; }
	void AddFlag(UInt32 f)		{ g_msgFlags |= f; }
	void ClearFlag(UInt32 f)	{ g_msgFlags &= ~f; }

	// Estado "bom" (>= limiar) -> buff; estado "mau" (< limiar) -> debuff + mensagem uma vez.
	void ApplyState(Actor *actor, float value, float threshold, UInt32 buffSpell, UInt32 debuffSpell,
					UInt32 msgFlag, const char *msgKey)
	{
		if (value >= threshold)
		{
			RemoveSpellFrom(actor, debuffSpell);
			AddSpellTo(actor, buffSpell);
			ClearFlag(msgFlag);
		}
		else
		{
			RemoveSpellFrom(actor, buffSpell);
			AddSpellTo(actor, debuffSpell);
			if (!HasFlag(msgFlag))
			{
				Messages::Show(msgKey);
				AddFlag(msgFlag);
			}
		}
	}

	class RefreshTask : public TaskDelegate
	{
		UInt32 m_mask;
	public:
		RefreshTask(UInt32 mask) : m_mask(mask) {}
		virtual void Run() override { Debuff::Refresh(m_mask); }
		virtual void Dispose() override { delete this; }
	};
}

namespace Debuff
{
	void QueueRefresh(UInt32 mask)
	{
		const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
		const SKSETaskInterface *task = plugin ? plugin->GetInterface<SKSETaskInterface>() : NULL;
		if (task)
			task->AddTask(new RefreshTask(mask));
	}

	void Refresh(UInt32 mask)
	{
		Widget::QueueUpdate();		// o HUD acompanha qualquer mudanca nas necessidades
		if (!Food::HasLists() || !g_thePlayer) return;		// sem o plugin do ESD nao ha feiticos
		Actor *actor = static_cast<Actor *>(g_thePlayer);

		ESDSettings::Core s = ESDSettings::GetCore();

		// fome: limiar 1440
		if (s.hungry && (mask & Needs::kHungry) && ESDSettings::GetFloat("bIsEnableHungryBuff", 0.0f) != 0.0f)
			ApplyState(actor, Needs::Get(Needs::kHungry), 1440.0f, kStuffedSpell, kHungryAbility, kMsgHungry, "sGetHungry");

		// sede: limiar 1440
		if (s.thirsty && (mask & Needs::kThirsty) && ESDSettings::GetFloat("bIsEnableThirstyBuff", 0.0f) != 0.0f)
			ApplyState(actor, Needs::Get(Needs::kThirsty), 1440.0f, kQuenchedSpell, kThirstyAbility, kMsgThirsty, "sGetThirsy");

		// sono: limiar 720 (valor baixo = cansado)
		if (s.sleepiness && (mask & Needs::kSleepy) && ESDSettings::GetFloat("bIsEnableSleepinessBuff", 0.0f) != 0.0f)
			ApplyState(actor, Needs::Get(Needs::kSleepy), 720.0f, kNotWearySpell, kSleepyAbility, kMsgTired, "sGetTired");

		// corpo: o valor SOBE com o tempo. limite = intervalo * 1440 (sujo); aviso de banho a partir de (intervalo-1)*1440+720
		if (s.bodyHealth && (mask & Needs::kBody) && ESDSettings::GetFloat("bIsEnableBodyHealthBuff", 0.0f) != 0.0f)
		{
			float interval = s.washBodyInterval;
			float limit = interval * 1440.0f;
			float soft = 720.0f;
			if (interval - 1.0f > 0.0f)
				soft = (interval - 1.0f) * 1440.0f + 720.0f;
			float v = Needs::Get(Needs::kBody);

			if (v >= limit)
			{
				RemoveSpellFrom(actor, kCleanBodySpell);
				AddSpellTo(actor, kBodyAbility);
				if (!HasFlag(kMsgDirty)) { Messages::Show("sGetDirtyBody"); AddFlag(kMsgDirty); }
			}
			else
			{
				if (v >= soft)
				{
					if (!HasFlag(kMsgBathSoon)) { Messages::Show("sBathingSoon"); AddFlag(kMsgBathSoon); }
				}
				else
					ClearFlag(kMsgBathSoon);
				RemoveSpellFrom(actor, kBodyAbility);
				AddSpellTo(actor, kCleanBodySpell);
				ClearFlag(kMsgDirty);
			}
		}
	}
}
