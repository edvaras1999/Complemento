#include <SKSE.h>
#include <SKSE/PluginAPI.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <SKSE/GameExtraData.h>
#include <Skyrim/SkyrimVM.h>
#include <Skyrim/Menus/MenuManager.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/AlchemyItem.h>
#include <Skyrim/Forms/PlayerCharacter.h>
#include <windows.h>

#include "Hotkeys.h"
#include "Needs.h"
#include "Food.h"
#include "Settings.h"
#include "Messages.h"

namespace
{
	typedef _NativeFunction<Actor, void, TESForm *, bool, bool> EquipItemNative;

	// codigo de tecla do jogo (DirectInput) -> tecla virtual do Windows
	UINT VirtualKey(int dx)
	{
		if (dx <= 0) return 0;
		UINT sc = (dx >= 0x80) ? (0xE000 | (UINT)(dx - 0x80)) : (UINT)dx;
		return MapVirtualKeyA(sc, MAPVK_VSC_TO_VK_EX);
	}

	bool GameHasFocus()
	{
		HWND w = GetForegroundWindow();
		if (!w) return false;
		DWORD pid = 0;
		GetWindowThreadProcessId(w, &pid);
		return pid == GetCurrentProcessId();
	}

	//-----------------------------------------------------------
	// Atalho de refeicoes: come ou bebe o que fizer mais falta
	//-----------------------------------------------------------
	void DoMeals()
	{
		MenuManager *mm = MenuManager::GetSingleton();
		if (!g_thePlayer || !g_skyrimVM || !Food::HasLists() || (mm && mm->numPauseGame != 0)) return;

		ESDSettings::Core s = ESDSettings::GetCore();
		if (!s.hungry && !s.thirsty) return;

		float hungryV = Needs::Get(Needs::kHungry), thirstyV = Needs::Get(Needs::kThirsty);
		bool wantDrink;
		if (s.thirsty && s.hungry) wantDrink = (thirstyV <= hungryV);
		else wantDrink = s.thirsty;

		const float kFull = 2880.0f;			// valor inicial / "cheio"
		float value = wantDrink ? thirstyV : hungryV;
		if (value >= kFull) return;				// nao precisa
		float deficit = kFull - value;

		float intFoods = ESDSettings::GetFloat("fIntensityFoods", 1.0f);
		float intDrinks = ESDSettings::GetFloat("fIntensityDrinks", 1.0f);

		ExtraContainerChanges *ex = g_thePlayer->extraData.GetExtraData<ExtraContainerChanges>();
		InventoryChanges *changes = ex ? ex->changes : nullptr;
		if (!changes || !changes->entryList) return;

		TESForm *best = nullptr;
		float bestAmount = 0.0f;
		bool bestEnough = false;
		for (InventoryEntryData *e : *changes->entryList)
		{
			if (!e) continue;
			TESForm *item = e->baseForm;
			if (!item || item->formType != FormType::Potion || e->GetCount() <= 0) continue;
			float w = (float)item->GetWeight();
			if (!(w > 0.0f) || !static_cast<AlchemyItem *>(item)->IsFood()) continue;

			UInt32 type = Food::TypeOf(item->formID);
			float amount = 0.0f;
			if (wantDrink && type == 2) amount = w * intDrinks * 1500.0f;
			else if (!wantDrink && (type == 1 || type == 4)) amount = w * intFoods * 1000.0f;
			else if (!wantDrink && type == 8) amount = w * intFoods * 1000.0f * 0.1f;
			if (amount <= 0.0f) continue;

			// prefere o que cobre a falta com menos desperdicio; senao o que repoe mais
			bool enough = amount >= deficit;
			bool better;
			if (!best) better = true;
			else if (enough && !bestEnough) better = true;
			else if (enough && bestEnough) better = amount < bestAmount;
			else if (!enough && !bestEnough) better = amount > bestAmount;
			else better = false;
			if (better) { best = item; bestAmount = amount; bestEnough = enough; }
		}
		if (!best)
		{
			Messages::Show("sNotEnoughItem");
			return;
		}

		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		EquipItemNative *fn = (EquipItemNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "EquipItem");
		if (!fn || !fn->Validate(state)) { _MESSAGE("ESD: nao encontrei Actor.EquipItem; o atalho de refeicoes fica desligado"); return; }
		(*fn)(state, 0, static_cast<Actor *>(g_thePlayer), best, false, true);		// o ESD trata do resto ao detetar o consumo
		_MESSAGE("ESD: atalho de refeicoes: %s %08X", wantDrink ? "bebeu" : "comeu", best->formID);
	}

	class MealsTask : public TaskDelegate
	{
	public:
		virtual void Run() override { DoMeals(); }
		virtual void Dispose() override {}
	};
	MealsTask g_mealsTask;

	DWORD WINAPI Watch(LPVOID)
	{
		bool mealsDown = false;
		int tick = 0;
		UINT mealsVk = 0;
		for (;;)
		{
			Sleep(30);
			if ((tick++ % 20) == 0)			// reler a tecla escolhida de vez em quando (o MCM pode mudar)
				mealsVk = VirtualKey((int)ESDSettings::GetFloat("iMealsHotkey", 0.0f));
			if (!mealsVk || !GameHasFocus()) { mealsDown = false; continue; }

			bool down = (GetAsyncKeyState((int)mealsVk) & 0x8000) != 0;
			if (down && !mealsDown)
			{
				const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
				const SKSETaskInterface *task = plugin ? plugin->GetInterface<SKSETaskInterface>() : nullptr;
				if (task) task->AddTask(&g_mealsTask);
			}
			mealsDown = down;
		}
		return 0;
	}
}

namespace Hotkeys
{
	void Start()
	{
		CreateThread(NULL, 0, &Watch, NULL, 0, NULL);
	}
}
