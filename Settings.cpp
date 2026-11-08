#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Settings.h"

namespace
{
	const char *kFolder = "Data\\SKSE\\Plugins\\EatingSleepingDrinking\\";

	struct Entry
	{
		std::string	group;		// basicNeeds, widget, hotkey, buffs, expressions, messages
		std::string	sub;		// so para "expressions": expression, phoneme, modifier
		std::string	leaf;		// nome dentro do grupo (ex.: "id", "00" ou o proprio nome)
		std::string	name;		// nome usado pelo Papyrus (ex.: bIsEnableHungry, phoneme_00)
		bool		isString;
		float		num;
		std::string	str;
	};

	std::vector<Entry>				g_entries;
	std::map<std::string, size_t>	g_index;		// nome em minusculas -> posicao em g_entries
	CRITICAL_SECTION				g_cs;
	bool							g_csInit = false;

	std::string Lower(const std::string &s)
	{
		std::string r(s);
		for (size_t i = 0; i < r.size(); i++)
			if (r[i] >= 'A' && r[i] <= 'Z') r[i] = (char)(r[i] + 32);
		return r;
	}

	void Add(const char *group, const char *name, float value)
	{
		Entry e;
		e.group = group; e.name = name; e.leaf = name; e.isString = false; e.num = value;
		g_index[Lower(e.name)] = g_entries.size();
		g_entries.push_back(e);
	}

	void AddSub(const char *sub, const char *leaf, float value)
	{
		Entry e;
		e.group = "expressions"; e.sub = sub; e.leaf = leaf; e.isString = false; e.num = value;
		e.name = std::string(sub) + "_" + leaf;
		g_index[Lower(e.name)] = g_entries.size();
		g_entries.push_back(e);
	}

	void AddStr(const char *group, const char *name, const char *value)
	{
		Entry e;
		e.group = group; e.name = name; e.leaf = name; e.isString = true; e.num = 0; e.str = value;
		g_index[Lower(e.name)] = g_entries.size();
		g_entries.push_back(e);
	}

	// Valores por omissao: lidos do construtor do ESDSettings no ESDPlugin.dll.
	void LoadDefaults()
	{
		g_entries.clear();
		g_index.clear();

		Add("basicNeeds", "bIsEnableHungry", 0);
		Add("basicNeeds", "bIsEnableThirsty", 0);
		Add("basicNeeds", "bIsEnableSleepiness", 0);
		Add("basicNeeds", "bIsEnableBodyHealth", 0);
		Add("basicNeeds", "bIsEnableDirtyBody", 0);
		Add("basicNeeds", "bIsEnableHungryGrowl", 0);
		Add("basicNeeds", "bIsEnableCovertBottle", 0);
		Add("basicNeeds", "bIsEnableExpression", 0);
		Add("basicNeeds", "bIsEnableMessage", 0);			// valor por omissao nao confirmado
		Add("basicNeeds", "bIsEnableHungryBuff", 1);
		Add("basicNeeds", "bIsEnableThirstyBuff", 1);
		Add("basicNeeds", "bIsEnableSleepinessBuff", 1);
		Add("basicNeeds", "bIsEnableBodyHealthBuff", 1);
		Add("basicNeeds", "fIntensityFoods", 1.0f);
		Add("basicNeeds", "fIntensityDrinks", 1.0f);
		Add("basicNeeds", "fIntensitySleepiness", 1.0f);
		Add("basicNeeds", "fWashBodyInterval", 3.0f);
		Add("basicNeeds", "fMagnitudeHungry", 1.0f);
		Add("basicNeeds", "fMagnitudeThisry", 1.0f);
		Add("basicNeeds", "fShortSleeper", 1.0f);
		Add("basicNeeds", "fIntensityBody", 1.0f);
		Add("basicNeeds", "fFoodsTimePerDays", 3.0f);
		Add("basicNeeds", "fDrinksTimePerDays", 3.0f);
		Add("basicNeeds", "fDayToDie", 0.0f);
		Add("basicNeeds", "fCovertEmptyBottle", 0.0f);
		Add("basicNeeds", "fTrundraCottonAmount", 3.0f);
		Add("basicNeeds", "fExpressionDuration", 20.0f);

		Add("widget", "bIsEnableWidgetPrimary", 0);
		Add("widget", "bIsEnableWidgetSecondary", 0);
		Add("widget", "bIsEnableHungryIcon", 1);
		Add("widget", "bIsEnableThirstyIcon", 1);
		Add("widget", "bIsEnableSleepinessIcon", 1);
		Add("widget", "fPrimaryAlpha", 90.0f);
		Add("widget", "fPrimaryPosX", -650.0f);
		Add("widget", "fPrimaryPosY", 330.0f);
		Add("widget", "fPrimaryScale", 80.0f);
		Add("widget", "fSecondaryAlpha", 90.0f);
		Add("widget", "fSecondaryPosX", -650.0f);
		Add("widget", "fSecondaryPosY", 390.0f);
		Add("widget", "fSecondaryScale", 80.0f);
		Add("widget", "fHungryOffsetX", 0.0f);
		Add("widget", "fHungryOffsetY", 0.0f);
		Add("widget", "fThirstyOffsetX", 0.0f);
		Add("widget", "fThirstyOffsetY", 0.0f);
		Add("widget", "fSleepyOffsetX", 0.0f);
		Add("widget", "fSleepyOffsetY", 0.0f);

		Add("hotkey", "iMealsHotkey", 0);
		Add("hotkey", "iConvenientHotkey", 0);
		Add("hotkey", "bIsEnableAdvancedConvenientHotkey", 0);

		AddStr("messages", "sLanguageFile", "Messages_English.json");

		// Opcoes proprias deste port (nao existem no MCM do ESD): editar em Settings.json, grupo "port"
		Add("port", "bTowelRequiresWater", 0);		// 1 = a toalha de agua so funciona a nadar
		Add("port", "bMessageCentered", 1);			// 1 = mensagens no centro do ecra; 0 = notificacao normal do canto
		Add("port", "fMessageSeconds", 4.5f);		// tempo que cada mensagem fica no ecra
		Add("port", "fMessageOffsetY", 0.0f);		// desloca as mensagens na vertical (area de 1280x720; positivo = para baixo)
		Add("port", "bActivityDecay", 1);			// 0 = desliga o desgaste por atividade (passos, golpes, magias, gritos)
		Add("port", "fActivityScale", 1.0f);		// multiplicador da atividade (1 = como o original)

		Add("buffs", "fHealth", 2.0f);
		Add("buffs", "fStamina", 2.0f);
		Add("buffs", "fMagicka", 2.0f);
		Add("buffs", "fShoutRecoveryMult", 0.15f);
		Add("buffs", "fWeaponSpeedMult", 1.15f);
		Add("buffs", "fSpeedMult", 15.0f);
		Add("buffs", "fDiseaseResist", 5.0f);
		Add("buffs", "fPoisonResist", 5.0f);

		AddSub("expression", "id", 7);
		AddSub("expression", "value", 50);
		for (int i = 0; i < 16; i++)
		{
			char k[8]; sprintf(k, "%02d", i);
			AddSub("phoneme", k, (i == 9) ? 15.0f : 0.0f);
		}
		static const float mods[17] = { 25, 25, 60, 60, 60, 60, 60, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0 };
		for (int i = 0; i < 17; i++)
		{
			char k[8]; sprintf(k, "%02d", i);
			AddSub("modifier", k, mods[i]);
		}
	}

	//------------------------------------------------------------------
	// leitor de JSON tolerante (aceita ";" no lugar de ":" como no ficheiro de exemplo do mod)
	//------------------------------------------------------------------
	struct Parser
	{
		const char *p;
		const char *end;

		void Ws()
		{
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
		}

		bool ParseString(std::string &out)
		{
			out.clear();
			if (p >= end || *p != '"') return false;
			p++;
			while (p < end && *p != '"')
			{
				if (*p == '\\' && p + 1 < end) { p++; }
				out += *p++;
			}
			if (p >= end) return false;
			p++;
			return true;
		}

		void Store(const std::vector<std::string> &path, bool isStr, float num, const std::string &str)
		{
			if (path.empty()) return;
			std::string name;
			if (path.size() >= 3 && Lower(path[0]) == "expressions")
				name = path[1] + "_" + path[2];
			else
				name = path.back();
			std::map<std::string, size_t>::iterator it = g_index.find(Lower(name));
			if (it == g_index.end()) return;			// chave desconhecida: ignora
			Entry &e = g_entries[it->second];
			if (e.isString) { if (isStr) e.str = str; }
			else if (!isStr) e.num = num;
		}

		bool ParseValue(std::vector<std::string> &path, int depth)
		{
			Ws();
			if (p >= end || depth > 8) return false;
			if (*p == '{')
			{
				p++;
				for (;;)
				{
					Ws();
					if (p < end && *p == '}') { p++; return true; }
					std::string key;
					if (!ParseString(key)) return false;
					Ws();
					if (p < end && (*p == ':' || *p == ';')) p++; else return false;
					path.push_back(key);
					bool ok = ParseValue(path, depth + 1);
					path.pop_back();
					if (!ok) return false;
					Ws();
					if (p < end && *p == ',') { p++; continue; }
				}
			}
			if (*p == '"')
			{
				std::string s;
				if (!ParseString(s)) return false;
				Store(path, true, 0, s);
				return true;
			}
			if (!strncmp(p, "true", 4))  { p += 4; Store(path, false, 1.0f, ""); return true; }
			if (!strncmp(p, "false", 5)) { p += 5; Store(path, false, 0.0f, ""); return true; }
			if (!strncmp(p, "null", 4))  { p += 4; return true; }
			char *e = NULL;
			double d = strtod(p, &e);
			if (e == p) return false;
			p = e;
			Store(path, false, (float)d, "");
			return true;
		}
	};

	std::string ReadFile(const std::string &path)
	{
		std::string data;
		FILE *f = fopen(path.c_str(), "rb");
		if (!f) return data;
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
		fclose(f);
		// ignora o BOM UTF-8
		if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
			data.erase(0, 3);
		return data;
	}

	void FormatValue(const Entry &e, char *buf, size_t size)
	{
		if (e.isString) { _snprintf(buf, size, "\"%s\"", e.str.c_str()); return; }
		bool isBool = (e.name.size() > 1 && e.name[0] == 'b' && e.name[1] >= 'A' && e.name[1] <= 'Z');
		bool isInt = (e.name.size() > 1 && e.name[0] == 'i' && e.name[1] >= 'A' && e.name[1] <= 'Z') || !e.sub.empty();
		if (isBool) _snprintf(buf, size, "%s", (e.num != 0.0f) ? "true" : "false");
		else if (isInt) _snprintf(buf, size, "%d", (int)e.num);
		else _snprintf(buf, size, "%g", e.num);
		buf[size - 1] = 0;
	}
}

namespace ESDSettings
{
	void Load()
	{
		if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
		EnterCriticalSection(&g_cs);
		LoadDefaults();

		std::string path = std::string(kFolder) + "Settings.json";
		std::string text = ReadFile(path);
		if (text.empty())
		{
			_MESSAGE("ESD: %s nao encontrado, a usar os valores por omissao", path.c_str());
		}
		else
		{
			Parser ps; ps.p = text.c_str(); ps.end = text.c_str() + text.size();
			std::vector<std::string> pathKeys;
			if (!ps.ParseValue(pathKeys, 0))
				_MESSAGE("ESD: Settings.json invalido (lidos so os valores antes do erro)");
			else
				_MESSAGE("ESD: Settings.json carregado");
		}
		LeaveCriticalSection(&g_cs);
	}

	bool Save()
	{
		EnterCriticalSection(&g_cs);
		CreateDirectoryA("Data\\SKSE", NULL);
		CreateDirectoryA("Data\\SKSE\\Plugins", NULL);
		CreateDirectoryA("Data\\SKSE\\Plugins\\EatingSleepingDrinking", NULL);
		std::string path = std::string(kFolder) + "Settings.json";
		FILE *f = fopen(path.c_str(), "wb");
		if (!f) { LeaveCriticalSection(&g_cs); return false; }

		const char *groups[] = { "basicNeeds", "widget", "hotkey", "messages", "buffs", "port", "expressions" };
		const int kGroups = 7;
		fputs("{\n", f);
		for (int gi = 0; gi < kGroups; gi++)
		{
			fprintf(f, "\t\"%s\": {\n", groups[gi]);
			bool first = true;
			if (!strcmp(groups[gi], "expressions"))
			{
				const char *subs[] = { "expression", "phoneme", "modifier" };
				for (int si = 0; si < 3; si++)
				{
					if (!first) fputs(",\n", f);
					first = false;
					fprintf(f, "\t\t\"%s\": {\n", subs[si]);
					bool firstLeaf = true;
					for (size_t i = 0; i < g_entries.size(); i++)
					{
						const Entry &e = g_entries[i];
						if (e.group != groups[gi] || e.sub != subs[si]) continue;
						char v[64]; FormatValue(e, v, sizeof(v));
						fprintf(f, "%s\t\t\t\"%s\": %s", firstLeaf ? "" : ",\n", e.leaf.c_str(), v);
						firstLeaf = false;
					}
					fputs("\n\t\t}", f);
				}
			}
			else
			{
				for (size_t i = 0; i < g_entries.size(); i++)
				{
					const Entry &e = g_entries[i];
					if (e.group != groups[gi]) continue;
					char v[256]; FormatValue(e, v, sizeof(v));
					fprintf(f, "%s\t\t\"%s\": %s", first ? "" : ",\n", e.name.c_str(), v);
					first = false;
				}
			}
			fprintf(f, "\n\t}%s\n", (gi < kGroups - 1) ? "," : "");
		}
		fputs("}\n", f);
		fclose(f);
		LeaveCriticalSection(&g_cs);
		return true;
	}

	bool Has(const char *name)
	{
		if (!name) return false;
		EnterCriticalSection(&g_cs);
		bool r = g_index.find(Lower(name)) != g_index.end();
		LeaveCriticalSection(&g_cs);
		return r;
	}

	float GetFloat(const char *name, float fallback)
	{
		if (!name) return fallback;
		EnterCriticalSection(&g_cs);
		float r = fallback;
		std::map<std::string, size_t>::iterator it = g_index.find(Lower(name));
		if (it != g_index.end() && !g_entries[it->second].isString) r = g_entries[it->second].num;
		LeaveCriticalSection(&g_cs);
		return r;
	}

	bool SetFloat(const char *name, float value)
	{
		if (!name) return false;
		EnterCriticalSection(&g_cs);
		bool ok = false;
		std::map<std::string, size_t>::iterator it = g_index.find(Lower(name));
		if (it != g_index.end() && !g_entries[it->second].isString) { g_entries[it->second].num = value; ok = true; }
		LeaveCriticalSection(&g_cs);
		return ok;
	}

	std::string GetString(const char *name, const char *fallback)
	{
		std::string r(fallback ? fallback : "");
		if (!name) return r;
		EnterCriticalSection(&g_cs);
		std::map<std::string, size_t>::iterator it = g_index.find(Lower(name));
		if (it != g_index.end() && g_entries[it->second].isString) r = g_entries[it->second].str;
		LeaveCriticalSection(&g_cs);
		return r;
	}

	Core GetCore()
	{
		Core c;
		c.hungry = GetFloat("bIsEnableHungry", 0) != 0;
		c.thirsty = GetFloat("bIsEnableThirsty", 0) != 0;
		c.sleepiness = GetFloat("bIsEnableSleepiness", 0) != 0;
		c.bodyHealth = GetFloat("bIsEnableBodyHealth", 0) != 0;
		c.magnitudeHungry = GetFloat("fMagnitudeHungry", 1.0f);
		c.magnitudeThirsty = GetFloat("fMagnitudeThisry", 1.0f);
		c.foodsTimePerDays = GetFloat("fFoodsTimePerDays", 3.0f);
		c.drinksTimePerDays = GetFloat("fDrinksTimePerDays", 3.0f);
		c.intensitySleepiness = GetFloat("fIntensitySleepiness", 1.0f);
		c.intensityBody = GetFloat("fIntensityBody", 1.0f);
		c.washBodyInterval = GetFloat("fWashBodyInterval", 3.0f);
		return c;
	}
}
