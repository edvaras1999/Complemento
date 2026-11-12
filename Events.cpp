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
#include <Skyrim/Forms/TESObjectCELL.h>
#include <SKSE/PluginAPI.h>

namespace
{
	const UInt32 kEventQuest = 0x84E;			// quest _ESDMCM, onde esta o script _ESDEventScritp
	const UInt32 kTundraCotton = 0x0003F7F8;	// Skyrim.esm: Tundra Cotton

	typedef _NativeFunction<TESObjectREFR, TESObjectREFR *, TESForm *, SInt32, bool, bool>	PlaceAtMeNative;
	typedef _NativeFunction<TESObjectREFR, SInt32, TESForm *>								GetItemCountNative;

	DWORD g_lastDetect = 0;
	bool  g_nativeFailed = false;


	// itens do ESD que, ao serem largados no chao, passam a objetos ativaveis (o script decide o resto)
	struct DropPair { UInt32 miscLocal; UInt32 actiLocal; const char *name; };
	const DropPair kPairs[2] =
	{
		{ 0x831, 0x82F, "cama portatil" },		// _ESDBedrollMISC -> _ESDBedrollPotableACTI
		{ 0x869, 0x864, "barril de hidromel" }	// _ESDMeadBarrelMisc -> _ESDMeadBarrelMoveACTI
	};

	typedef _NativeFunction<TESObjectREFR, void, bool>	DisableNative;
	typedef _NativeFunction<TESObjectREFR, void>		DeleteNative;

	class ConvertTask : public TaskDelegate
	{
		int m_index;
	public:
		ConvertTask(int i) : m_index(i) {}
		virtual void Run() override { Events::ConvertDropped(m_index); }
		virtual void Dispose() override { delete this; }
	};

	DWORD WINAPI DelayThread(LPVOID param)
	{
		Sleep(500);			// o objeto no mundo so existe um instante depois do evento
		const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
		const SKSETaskInterface *task = plugin ? plugin->GetInterface<SKSETaskInterface>() : NULL;
		if (task)
			task->AddTask(new ConvertTask((int)(INT_PTR)param - 1));
		return 0;
	}

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

	void OnItemDropped(UInt32 itemFormId)
	{
		if (!itemFormId) return;
		for (int i = 0; i < 2; i++)
			if (itemFormId == Food::FormId(kPairs[i].miscLocal))
				CreateThread(NULL, 0, &DelayThread, (LPVOID)(INT_PTR)(i + 1), 0, NULL);
	}

	void ConvertDropped(int index)
	{
		if (index < 0 || index > 1 || !g_thePlayer || !g_skyrimVM) return;
		const DropPair &pr = kPairs[index];
		UInt32 miscId = Food::FormId(pr.miscLocal), actiId = Food::FormId(pr.actiLocal);
		TESForm *acti = actiId ? LookupFormByID(actiId) : NULL;
		TESObjectCELL *cell = g_thePlayer->GetParentCell();
		if (!miscId || !acti || !cell) return;

		// procura, na celula do jogador, o item largado mais proximo
		TESObjectREFR *best = NULL;
		float bestD2 = 800.0f * 800.0f;
		for (BSTArray<TESObjectREFR *>::const_iterator it = cell->objectList.begin(); it != cell->objectList.end(); ++it)
		{
			TESObjectREFR *r = *it;
			if (!r || !r->baseForm || r->baseForm->formID != miscId || r->IsDeleted() || r->IsDisabled()) continue;
			float dx = r->pos.x - g_thePlayer->pos.x, dy = r->pos.y - g_thePlayer->pos.y, dz = r->pos.z - g_thePlayer->pos.z;
			float d2 = dx * dx + dy * dy + dz * dz;
			if (d2 < bestD2) { bestD2 = d2; best = r; }
		}
		if (!best)
		{
			_MESSAGE("ESD: item largado (%s) nao encontrado na celula", pr.name);
			return;
		}

		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		PlaceAtMeNative *place = (PlaceAtMeNative *)SKSEScript::GetFunction(state, TESObjectREFR::kTypeID, "PlaceAtMe");
		DisableNative *disable = (DisableNative *)SKSEScript::GetFunction(state, TESObjectREFR::kTypeID, "Disable");
		DeleteNative *del = (DeleteNative *)SKSEScript::GetFunction(state, TESObjectREFR::kTypeID, "Delete");
		if (!place || !place->Validate(state) || !disable || !disable->Validate(state) || !del || !del->Validate(state))
		{
			_MESSAGE("ESD: faltam funcoes do jogo (PlaceAtMe/Disable/Delete); %s nao pode ser convertido", pr.name);
			return;
		}
		TESObjectREFR *placed = (*place)(state, 0, best, acti, 1, false, false);
		(*disable)(state, 0, best, false);
		(*del)(state, 0, best);
		_MESSAGE("ESD: %s largada: trocada pelo objeto ativavel (%p)", pr.name, placed);
	}

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
