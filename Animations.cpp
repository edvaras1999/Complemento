#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <Skyrim/Forms/TESForm.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/PlayerCharacter.h>
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
		return Play(Pick(category, name));
	}
}
