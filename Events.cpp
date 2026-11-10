#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <SKSE/PapyrusEvents.h>
#include <Skyrim/SkyrimVM.h>
#include <Skyrim/Forms/TESForm.h>
#include <Skyrim/Forms/TESQuest.h>
#include <Skyrim/Forms/TESObjectREFR.h>
#include <Skyrim/Forms/PlayerCharacter.h>
#include <windows.h>

#include "Events.h"
#include "Food.h"
#include "Settings.h"
#include "Messages.h"
#include "Animations.h"

namespace
{
	const UInt32 kEventQuest = 0x84E;			// quest _ESDMCM, onde esta o script _ESDEventScritp
	const UInt32 kTundraCotton = 0x0003F7F8;	// Skyrim.esm: Tundra Cotton

	typedef _NativeFunction<TESObjectREFR, TESObjectREFR *, TESForm *, SInt32, bool, bool>	PlaceAtMeNative;
	typedef _NativeFunction<TESObjectREFR, SInt32, TESForm *>								GetItemCountNative;

	DWORD g_lastDetect = 0;
	bool  g_nativeFailed = false;

	TESQuest *EventQuest()
	{
		UInt32 id = Food::FormId(kEventQuest);
		TESForm *f = id ? LookupFormByID(id) : NULL;
		if (f && f->formType == FormType::Quest)
			return static_cast<TESQuest *>(f);
		return NULL;
	}
}

namespace Events
{
	void DetectNewFood(TESForm *potion)
	{
		if (g_nativeFailed || !potion || !g_thePlayer || !g_skyrimVM) return;

		DWORD now = GetTickCount();
		if (g_lastDetect != 0 && now - g_lastDetect < 3000) return;		// uma pergunta de cada vez
		TESQuest *quest = EventQuest();
		if (!quest) return;

		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		PlaceAtMeNative *place = (PlaceAtMeNative *)SKSEScript::GetFunction(state, TESObjectREFR::kTypeID, "PlaceAtMe");
		if (!place || !place->Validate(state))
		{
			_MESSAGE("ESD: nao encontrei ObjectReference.PlaceAtMe; a deteccao de alimentos novos fica desligada");
			g_nativeFailed = true;
			return;
		}

		// o script precisa de uma referencia (desativada) ao alimento; apaga-a depois de usar
		TESObjectREFR *dummy = (*place)(state, 0, g_thePlayer, potion, 1, false, true);
		if (!dummy) return;

		g_lastDetect = now;
		SKSEScript::QueueEvent(state, quest, BSFixedString("OnDetectNewFood"), dummy, (TESObjectREFR *)g_thePlayer);
		_MESSAGE("ESD: alimento novo detetado (%08X): a perguntar ao jogador", potion->formID);
	}

	void KnittingNeedle()
	{
		if (g_nativeFailed || !g_thePlayer || !g_skyrimVM) return;
		TESQuest *quest = EventQuest();
		if (!quest) return;

		TESForm *cotton = LookupFormByID(kTundraCotton);
		if (!cotton) return;

		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		GetItemCountNative *count = (GetItemCountNative *)SKSEScript::GetFunction(state, TESObjectREFR::kTypeID, "GetItemCount");
		if (!count || !count->Validate(state))
		{
			_MESSAGE("ESD: nao encontrei ObjectReference.GetItemCount; a agulha de trico fica desligada");
			g_nativeFailed = true;
			return;
		}

		int needed = (int)ESDSettings::GetFloat("fTrundraCottonAmount", 3.0f);
		if (needed < 1) needed = 1;
		int have = (*count)(state, 0, g_thePlayer, cotton);
		if (have < needed)
		{
			Messages::ShowItem("sNotEnoughItem", "Tundra Cotton");
			return;
		}
		SKSEScript::QueueEvent(state, quest, BSFixedString("OnEquippedKnittingNeedle"), (TESObjectREFR *)g_thePlayer, (SInt32)1, (SInt32)needed);
		Animations::PlayType(4);
		_MESSAGE("ESD: agulha de trico: 1 toalha por %d de algodao da tundra", needed);
	}
}
