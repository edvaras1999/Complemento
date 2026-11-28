#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <Skyrim/Forms/TESForm.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/PlayerCharacter.h>
#include <SKSE/PapyrusInvoke.h>
#include <Skyrim/SkyrimVM.h>
#include <Skyrim/FileIO/TESFile.h>
#include <Skyrim/FileIO/TESDataHandler.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Animations.h"
#include "Food.h"
#include "Settings.h"

namespace
{
	const char *kRoot = "Data\\SKSE\\Plugins\\ActionAnimations\\";

	struct Anim
	{
		std::string	name;		// nome da animacao ("random" = qualquer uma da lista)
		std::string	event;		// evento do grafo de animacao
		float		duration;	// segundos
	};

	std::map<std::string, std::vector<Anim> >	g_lists;		// categoria -> animacoes
	std::map<UInt32, std::string>				g_itemAnim;		// FormID -> nome da animacao
	DWORD										g_busyUntil = 0;
	bool										g_ready = false;

	std::string Lower(const std::string &s)
	{
		std::string r(s);
		for (size_t i = 0; i < r.size(); i++) if (r[i] >= 'A' && r[i] <= 'Z') r[i] = (char)(r[i] + 32);
		return r;
	}

	std::string ReadFile(const std::string &path)
	{
		std::string data;
		FILE *f = fopen(path.c_str(), "rb");
		if (!f) return data;
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
		fclose(f);
		return data;
	}

	// valor de um atributo XML: name="valor"
	std::string Attr(const std::string &tag, const char *name)
	{
		std::string key = std::string(name) + "=\"";
		size_t p = tag.find(key);
		if (p == std::string::npos) return std::string();
		p += key.size();
		size_t e = tag.find('"', p);
		return (e == std::string::npos) ? std::string() : tag.substr(p, e - p);
	}

	std::vector<std::string> XmlFiles(const std::string &dir)
	{
		std::vector<std::string> out;
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "*.xml").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return out;
		do { out.push_back(dir + fd.cFileName); } while (FindNextFileA(h, &fd));
		FindClose(h);
		return out;
	}

	// <animation duration="7.0" event="X">Nome</animation>
	void LoadAnimDir(const char *category, const std::string &subdir)
	{
		std::vector<std::string> files = XmlFiles(std::string(kRoot) + subdir);
		for (size_t i = 0; i < files.size(); i++)
		{
			std::string text = ReadFile(files[i]);
			size_t pos = 0;
			while ((pos = text.find("<animation ", pos)) != std::string::npos)
			{
				size_t gt = text.find('>', pos);
				size_t end = text.find("</animation>", gt);
				if (gt == std::string::npos || end == std::string::npos) break;
				std::string tag = text.substr(pos, gt - pos);
				Anim a;
				a.name = text.substr(gt + 1, end - gt - 1);
				a.event = Attr(tag, "event");
				a.duration = (float)atof(Attr(tag, "duration").c_str());
				if (!a.event.empty()) g_lists[category].push_back(a);
				pos = end + 12;
			}
		}
	}

	UInt32 ModIndex(const std::string &modName)
	{
		TESDataHandler *dh = TESDataHandler::GetSingleton();
		if (!dh) return 0xFF;
		const TESFile *f = dh->LookupModByName(modName.c_str());
		return f ? (UInt32)f->modIndex : 0xFF;
	}

	// <form name=".." animationName=".." modName="Skyrim.esm">0x0010D666</form>
	void LoadForms()
	{
		std::vector<std::string> files = XmlFiles(std::string(kRoot) + "AlchemyItems\\");
		for (size_t i = 0; i < files.size(); i++)
		{
			std::string text = ReadFile(files[i]);
			size_t pos = 0;
			while ((pos = text.find("<form ", pos)) != std::string::npos)
			{
				size_t gt = text.find('>', pos);
				size_t end = text.find("</form>", gt);
				if (gt == std::string::npos || end == std::string::npos) break;
				std::string tag = text.substr(pos, gt - pos);
				std::string anim = Attr(tag, "animationName");
				std::string mod = Attr(tag, "modName");
				UInt32 local = (UInt32)strtoul(text.substr(gt + 1, end - gt - 1).c_str(), NULL, 16) & 0x00FFFFFF;
				pos = end + 7;

				UInt32 id = 0;
				if (!_stricmp(mod.c_str(), "EatingSleepingDrinking.esp"))
					id = Food::FormId(local & 0xFFF);
				else
				{
					UInt32 idx = ModIndex(mod);
					if (idx < 0xFE) id = (idx << 24) | local;
				}
				if (id && !anim.empty()) g_itemAnim[id] = anim;
			}
		}
	}

	// Condicoes do AA (CanPlayAnim): sem nadar, sem voar, sem estar caido/a sangrar, sem arma na mao
	bool CanPlay()
	{
		if (!g_thePlayer) return false;
		UInt32 f4 = g_thePlayer->flags04, f8 = g_thePlayer->flags08;
		if (f4 & 0x400) return false;								// a nadar
		UInt32 fly = (f4 >> 0x12) & 7;
		if (fly != 0 && fly != 5) return false;					// a voar
		UInt32 life = (f4 >> 0x15) & 0xF;
		if (life == 7 || life == 8) return false;					// caido / a sangrar
		UInt32 weapon = (f8 >> 5) & 7;
		if (weapon >= 3 && weapon <= 5) return false;				// arma sacada ou a mudar
		return true;
	}

	const Anim *Pick(const char *category, const std::string &name)
	{
		std::map<std::string, std::vector<Anim> >::iterator it = g_lists.find(category);
		if (it == g_lists.end() || it->second.empty()) return NULL;
		const std::vector<Anim> &v = it->second;
		std::string want = Lower(name);
		std::vector<const Anim *> hits;
		for (size_t i = 0; i < v.size(); i++)
		{
			std::string n = Lower(v[i].name);
			if (n == want || (want == "random" && n == "random")) hits.push_back(&v[i]);
		}
		if (hits.empty() && want == "random")
			for (size_t i = 0; i < v.size(); i++) hits.push_back(&v[i]);
		if (hits.empty()) return NULL;
		return hits[rand() % hits.size()];
	}


	//-----------------------------------------------------------
	// Armadura ao lavar o corpo (fBehaviorBathingArmor: 0 nada, 1 so tirar, 2 tirar e repor)
	//-----------------------------------------------------------
	typedef _NativeFunction<Actor, TESForm *, SInt32>					GetWornFormNative;
	typedef _NativeFunction<Actor, void, TESForm *, bool, bool>			EquipNative;

	std::vector<UInt32>	g_removed;			// armaduras retiradas, a repor
	DWORD				g_reequipAt = 0;

	void UnequipSlots(float animSeconds)
	{
		int mode = (int)ESDSettings::GetFloat("fBehaviorBathingArmor", 0.0f);
		if (mode < 1 || g_reequipAt != 0 || !g_skyrimVM || !g_thePlayer) return;
		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		GetWornFormNative *worn = (GetWornFormNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "GetWornForm");
		EquipNative *unequip = (EquipNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "UnequipItem");
		if (!worn || !worn->Validate(state) || !unequip || !unequip->Validate(state))
		{
			_MESSAGE("ESD/AA: nao encontrei GetWornForm/UnequipItem; a armadura nao sera retirada");
			return;
		}
		Actor *player = static_cast<Actor *>(g_thePlayer);
		g_removed.clear();
		for (int slot = 30; slot <= 59; slot++)
		{
			if (slot == 50 || slot == 51) continue;
			char key[16];
			sprintf(key, "SlotMask%d", slot);
			if (ESDSettings::GetFloat(key, 0.0f) == 0.0f) continue;
			TESForm *f = (*worn)(state, 0, player, (SInt32)(1 << (slot - 30)));
			if (!f) continue;
			bool seen = false;
			for (size_t i = 0; i < g_removed.size(); i++) if (g_removed[i] == f->formID) seen = true;
			if (seen) continue;
			(*unequip)(state, 0, player, f, false, true);
			g_removed.push_back(f->formID);
		}
		_MESSAGE("ESD/AA: %u pecas de armadura retiradas para lavar (modo %d)", (unsigned)g_removed.size(), mode);
		if (mode >= 2 && !g_removed.empty())
			g_reequipAt = GetTickCount() + (DWORD)(animSeconds * 1000.0f) + 500;
		else
			g_removed.clear();
	}

	void ReequipAll()
	{
		g_reequipAt = 0;
		if (g_removed.empty() || !g_skyrimVM || !g_thePlayer) { g_removed.clear(); return; }
		VMState *state = g_skyrimVM->GetState();
		EquipNative *equip = state ? (EquipNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "EquipItem") : NULL;
		if (equip && equip->Validate(state))
		{
			for (size_t i = 0; i < g_removed.size(); i++)
			{
				TESForm *f = LookupFormByID(g_removed[i]);
				if (f) (*equip)(state, 0, static_cast<Actor *>(g_thePlayer), f, false, true);
			}
			_MESSAGE("ESD/AA: %u pecas de armadura repostas", (unsigned)g_removed.size());
		}
		g_removed.clear();
	}

	bool Play(const Anim *a)
	{
		if (!a || !g_thePlayer) return false;
		DWORD now = GetTickCount();
		if (now < g_busyUntil) return false;				// ja ha uma animacao a tocar
		BSFixedString ev(a->event.c_str());
		bool ok = g_thePlayer->SendAnimationEvent(ev);
		if (ok) g_busyUntil = now + (DWORD)(a->duration * 1000.0f);
		_MESSAGE("ESD/AA: animacao '%s' (%s): %s", a->event.c_str(), a->name.c_str(), ok ? "enviada" : "recusada pelo jogo");
		return ok;
	}
}

namespace Animations
{
	void Init()
	{
		if (ESDSettings::GetFloat("bPortAnimations", 1.0f) == 0.0f) { _MESSAGE("ESD/AA: animacoes desligadas (bPortAnimations=0)"); return; }
		g_lists.clear();
		g_itemAnim.clear();
		LoadAnimDir("FoodsStanding",  "MealAnimations\\Foods_Standing\\");
		LoadAnimDir("FoodsSitting",   "MealAnimations\\Foods_Sitting\\");
		LoadAnimDir("FoodsMoving",    "MealAnimations\\Foods_Moving\\");
		LoadAnimDir("DrinksStanding", "MealAnimations\\Drinks_Standing\\");
		LoadAnimDir("DrinksSitting",  "MealAnimations\\Drinks_Sitting\\");
		LoadAnimDir("DrinksMoving",   "MealAnimations\\Drinks_Moving\\");
		LoadAnimDir("Books",          "ReadBooksNotes\\");
		LoadAnimDir("WaterSource",    "DrinkWaterFromWaterSource\\");
		LoadAnimDir("FillBottle",     "FillWaterBottle\\");
		LoadAnimDir("Bedroll",        "BedrollCampingAnimations\\");
		LoadAnimDir("Towels",         "MakeTowels\\");
		LoadAnimDir("Soul",           "AbsorbDragonSoul\\");
		LoadAnimDir("Wash",           "WashBodyAnimations\\");
		LoadForms();
		size_t n = 0;
		for (std::map<std::string, std::vector<Anim> >::iterator it = g_lists.begin(); it != g_lists.end(); ++it) n += it->second.size();
		g_ready = true;
		_MESSAGE("ESD/AA: %u animacoes em %u listas, %u itens mapeados", (unsigned)n, (unsigned)g_lists.size(), (unsigned)g_itemAnim.size());
	}

	bool OnConsume(TESForm *item, bool isDrink)
	{
		if (!g_ready || !item || !g_thePlayer) return false;
		std::map<UInt32, std::string>::iterator it = g_itemAnim.find(item->formID);
		if (it == g_itemAnim.end()) return false;
		if (!CanPlay()) return false;

		// postura: sentado, a andar (Speed > 0) ou parado
		const char *posture = "Standing";
		if (g_thePlayer->GetSitState() == 3) posture = "Sitting";
		else
		{
			float speed = 0.0f;
			if (g_thePlayer->GetAnimationVariableFloat(BSFixedString("Speed"), speed) && speed > 0.1f)
				posture = "Moving";
		}

		std::string category = std::string(isDrink ? "Drinks" : "Foods") + posture;
		const char *key = isDrink ? (posture[0] == 'S' && posture[1] == 'i' ? "DrinkSit" : (posture[0] == 'M' ? "DrinkMove" : "DrinkStand"))
								  : (posture[0] == 'S' && posture[1] == 'i' ? "EatFoodSit" : (posture[0] == 'M' ? "EatFoodMove" : "EatFoodStand"));
		if (ESDSettings::GetFloat(key, 1.0f) == 0.0f) return false;
		return Play(Pick(category.c_str(), it->second));
	}

	bool PlayType(int type)
	{
		return PlayTypeNamed(type, NULL);
	}

	bool PlayTypeNamed(int type, const char *requested)
	{
		if (!g_ready || !CanPlay()) return false;
		const char *category = NULL, *key = NULL, *name = "random";
		switch (type)
		{
		case 1:  category = "WaterSource"; key = "bDrinkWaterFromWaterSource"; break;
		case 2:  category = "FillBottle";  key = "bFillWaterBottle"; break;
		case 4:  category = "Towels";      key = "bMakeTowels"; name = "MakeTowel"; break;
		case 8:  category = "Wash";        key = "bWashBody"; break;
		case 20: category = "Bedroll";     key = "bBedrollCamping"; name = "MakeBedroll"; break;
		case 40: category = "Soul";        key = "bAbsorbDragonSoul"; name = "AbsorbDragonSoul"; break;
		case 80: category = "WaterSource"; key = "bDrinkWaterFromBarrel"; break;
		default: return false;
		}
		if (ESDSettings::GetFloat(key, 1.0f) == 0.0f) return false;
		if (requested && *requested) name = requested;
		const Anim *a = Pick(category, name);
		if (a && type == 8 && GetTickCount() >= g_busyUntil)
			UnequipSlots(a->duration);			// tira a armadura escolhida antes de comecar a animacao
		return Play(a);
	}

	void Tick()
	{
		if (g_reequipAt != 0 && (int)(GetTickCount() - g_reequipAt) >= 0)
			ReequipAll();
	}

	bool CanPlayNow()
	{
		return CanPlay();
	}

	bool SendEvent(const char *eventName)
	{
		if (!g_thePlayer || !eventName) return false;
		BSFixedString ev(eventName);
		return g_thePlayer->SendAnimationEvent(ev);
	}
}
