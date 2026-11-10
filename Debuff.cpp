#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <SKSE/PluginAPI.h>
#include <Skyrim/SkyrimVM.h>
#include <Skyrim/Forms/TESForm.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/SpellItem.h>
#include <Skyrim/Forms/PlayerCharacter.h>
#include <Skyrim/Forms/TESSound.h>
#include <windows.h>

#include "Debuff.h"
#include "Needs.h"
#include "Food.h"
#include "Settings.h"
#include "Messages.h"
#include "Widget.h"
#include "Expression.h"

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

	// niveis de sujidade (cada um e um feitico do .esp); limiares como multiplos do intervalo de lavagem x 1440
	const UInt32 kDirtyLevel1 = 0x82D;		// > intervalo x  480
	const UInt32 kDirtyLevel2 = 0x825;		// > intervalo x  960
	const UInt32 kDirtyLevel3 = 0x82B;		// > intervalo x 1440

	// sons (SOUN do .esp)
	const UInt32 kSoundGrowl	= 0x806;	// estomago a roncar (fome)
	const UInt32 kSoundBuzzing	= 0x807;	// moscas (sujo)
	const UInt32 kSoundYawn		= 0x808;	// bocejo (sono)
	const DWORD  kSoundCooldownMs = 90000;	// o original espera 90 s entre sons do mesmo tipo

	DWORD g_lastGrowl = 0, g_lastBuzz = 0, g_lastYawn = 0;

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
				Expression::Trigger((msgFlag == kMsgTired) ? 3 : 2);		// cansado > fome/sede
			}
		}
	}


	typedef _NativeFunction<TESSound, int, TESObjectREFR *> SoundPlayNative;
	bool g_soundFailed = false;

	// Sound.Play(fonte): toca o som no jogador (no maximo uma vez por 90 s para cada tipo)
	void PlaySoundOnPlayer(UInt32 localId, DWORD &lastPlayed)
	{
		if (g_soundFailed || !g_skyrimVM || !g_thePlayer) return;
		DWORD now = GetTickCount();
		if (lastPlayed != 0 && now - lastPlayed < kSoundCooldownMs) return;

		UInt32 id = Food::FormId(localId);
		TESForm *f = id ? LookupFormByID(id) : NULL;
		if (!f || f->formType != FormType::Sound) return;

		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		SoundPlayNative *fn = (SoundPlayNative *)SKSEScript::GetFunction(state, TESSound::kTypeID, "Play");
		if (!fn || !fn->Validate(state))
		{
			_MESSAGE("ESD: nao encontrei Sound.Play; os sons ficam desligados");
			g_soundFailed = true;
			return;
		}
		lastPlayed = now;
		(*fn)(state, 0, static_cast<TESSound *>(f), (TESObjectREFR *)g_thePlayer);
	}

	// Sujidade por niveis: escolhe o feitico do nivel atual e remove os outros dois.
	void ApplyDirtyLevels(Actor *actor, float bodyValue, float interval)
	{
		UInt32 want = 0;
		if (bodyValue > interval * 1440.0f)		want = kDirtyLevel3;
		else if (bodyValue > interval * 960.0f)	want = kDirtyLevel2;
		else if (bodyValue > interval * 480.0f)	want = kDirtyLevel1;

		const UInt32 all[3] = { kDirtyLevel1, kDirtyLevel2, kDirtyLevel3 };
		for (int i = 0; i < 3; i++)
			if (all[i] != want)
				RemoveSpellFrom(actor, all[i]);
		if (want)
			AddSpellTo(actor, want);
	}

	void RemoveDirtyLevels(Actor *actor)
	{
		RemoveSpellFrom(actor, kDirtyLevel1);
		RemoveSpellFrom(actor, kDirtyLevel2);
		RemoveSpellFrom(actor, kDirtyLevel3);
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

		// som de fome (estomago a roncar), a cada 90 s enquanto tiver fome
		if (s.hungry && (mask & Needs::kHungry) && ESDSettings::GetFloat("bIsEnableHungryGrowl", 0.0f) != 0.0f
			&& Needs::Get(Needs::kHungry) < 1440.0f)
			PlaySoundOnPlayer(kSoundGrowl, g_lastGrowl);

		// sede: limiar 1440
		if (s.thirsty && (mask & Needs::kThirsty) && ESDSettings::GetFloat("bIsEnableThirstyBuff", 0.0f) != 0.0f)
			ApplyState(actor, Needs::Get(Needs::kThirsty), 1440.0f, kQuenchedSpell, kThirstyAbility, kMsgThirsty, "sGetThirsy");

		// sono: limiar 720 (valor baixo = cansado)
		if (s.sleepiness && (mask & Needs::kSleepy) && ESDSettings::GetFloat("bIsEnableSleepinessBuff", 0.0f) != 0.0f)
			ApplyState(actor, Needs::Get(Needs::kSleepy), 720.0f, kNotWearySpell, kSleepyAbility, kMsgTired, "sGetTired");

		// bocejo quando esta cansado (sono abaixo de 720)
		if (s.sleepiness && (mask & Needs::kSleepy) && Needs::Get(Needs::kSleepy) < 720.0f)
			PlaySoundOnPlayer(kSoundYawn, g_lastYawn);

		// sujidade por niveis (so se ativada no MCM); se estiver desativada, retira os feiticos
		if ((mask & Needs::kBody))
		{
			if (s.bodyHealth && ESDSettings::GetFloat("bIsEnableDirtyBody", 0.0f) != 0.0f)
				ApplyDirtyLevels(actor, Needs::Get(Needs::kBody), s.washBodyInterval);
			else
				RemoveDirtyLevels(actor);
			// moscas quando esta muito sujo
			if (s.bodyHealth && Needs::Get(Needs::kBody) > s.washBodyInterval * 1440.0f)
				PlaySoundOnPlayer(kSoundBuzzing, g_lastBuzz);
		}

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
				if (!HasFlag(kMsgDirty)) { Messages::Show("sGetDirtyBody"); AddFlag(kMsgDirty); Expression::Trigger(1); }
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
