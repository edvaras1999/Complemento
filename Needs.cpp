#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PluginAPI.h>
#include <Skyrim/Forms/TESForm.h>
#include <Skyrim/Forms/TESGlobal.h>
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cfloat>

#include "Needs.h"
#include "Settings.h"

namespace
{
	const UInt32 kRecordType = 0x45534444;		// 'ESDD' (mesmo valor do original)
	const UInt32 kRecordVersion = 2;

	CRITICAL_SECTION	g_lock;				// valores
	CRITICAL_SECTION	g_lockAct;			// atividade
	bool				g_init = false;

	float	g_val[4];						// fome, sede, sono, corpo  (original: +0x130..+0x13C)
	float	g_ref[4];						// valor de referencia      (original: +0x140..+0x14C)
	float	g_act[7];						// contadores de atividade  (original: +0x150..+0x168)
	float	g_lastMinutes = 0.0f;			// ultimo instante processado (minutos de jogo)
	bool	g_ready = false;

	bool IsFinite(float f)
	{
		return _finite(f) != 0;
	}

	int Index(UInt32 type)
	{
		switch (type)
		{
		case Needs::kHungry:	return 0;
		case Needs::kThirsty:	return 1;
		case Needs::kSleepy:	return 2;
		case Needs::kBody:		return 3;
		}
		return -1;
	}

	// Regras de limite de cada necessidade (BasicNeeds::Mod no original):
	//  fome  > 12960 -> 5760      sede > 17280 -> 5760
	//  sono  >  4320 -> 2160      corpo <   0  -> 0
	float Clamp(int i, float v)
	{
		switch (i)
		{
		case 0: if (v > 12960.0f) v = 5760.0f; break;
		case 1: if (v > 17280.0f) v = 5760.0f; break;
		case 2: if (v > 4320.0f)  v = 2160.0f; break;
		case 3: if (v < 0.0f)     v = 0.0f;    break;
		}
		return v;
	}

	// valor de seguranca quando o resultado nao e um numero valido
	float Fallback(int i)
	{
		switch (i)
		{
		case 0: case 1: return 2880.0f;
		case 2:			return 1680.0f;
		}
		return 0.0f;
	}

	float GlobalValue(UInt32 formId, bool &ok)
	{
		ok = false;
		TESForm *form = LookupFormByID(formId);
		if (!form || form->formType != FormType::Global)
			return 0.0f;
		ok = true;
		return static_cast<TESGlobal *>(form)->value;
	}

	// minutos de jogo = GameDaysPassed * 24 * 60   (global 0x39 do Skyrim.esm)
	bool GameMinutes(float &minutes, float &timeScale)
	{
		bool okDays, okScale;
		float days = GlobalValue(0x39, okDays);
		float scale = GlobalValue(0x3A, okScale);		// TimeScale
		if (!okDays || !okScale || !(days > 0.0f) || !(scale > 0.0f))
			return false;
		minutes = days * 24.0f * 60.0f;
		timeScale = scale;
		return true;
	}

	void ResetValues()
	{
		// ASSUMIDO (nao confirmado no original): comeca "cheio". Valores de reposicao usados pelo Mod.
		g_val[0] = 5760.0f; g_val[1] = 5760.0f; g_val[2] = 2160.0f; g_val[3] = 0.0f;
		for (int i = 0; i < 4; i++) g_ref[i] = g_val[i];
		for (int i = 0; i < 7; i++) g_act[i] = 0.0f;
	}
}

namespace Needs
{
	void Init()
	{
		if (g_init) return;
		InitializeCriticalSection(&g_lock);
		InitializeCriticalSection(&g_lockAct);
		g_init = true;
		ResetValues();
	}

	bool TypeFromName(const char *name, UInt32 &type)
	{
		if (!name) return false;
		if (!_stricmp(name, "hungry"))		{ type = kHungry;	return true; }
		if (!_stricmp(name, "thirsty"))		{ type = kThirsty;	return true; }
		if (!_stricmp(name, "sleepiness"))	{ type = kSleepy;	return true; }
		if (!_stricmp(name, "bodyhealth"))	{ type = kBody;		return true; }
		return false;
	}

	float Get(UInt32 type)
	{
		int i = Index(type);
		if (i < 0) return 0.0f;
		EnterCriticalSection(&g_lock);
		float r = g_val[i];
		LeaveCriticalSection(&g_lock);
		return r;
	}

	void Set(UInt32 type, float value, bool saveReference)
	{
		int i = Index(type);
		if (i < 0 || !IsFinite(value)) return;
		EnterCriticalSection(&g_lock);
		if (i == 3 && value < 0.0f) value = 0.0f;
		g_val[i] = value;
		if (saveReference) g_ref[i] = value;
		LeaveCriticalSection(&g_lock);
	}

	float Mod(UInt32 type, float delta)
	{
		int i = Index(type);
		if (i < 0) return 0.0f;
		EnterCriticalSection(&g_lock);
		if (IsFinite(delta))
		{
			float v = Clamp(i, g_val[i] + delta);
			if (!IsFinite(v)) v = Fallback(i);
			g_val[i] = v;
		}
		float r = g_val[i];
		LeaveCriticalSection(&g_lock);
		return r;
	}

	void ModMask(UInt32 mask, ValueData &data, bool saveReference)
	{
		EnterCriticalSection(&g_lock);
		for (int i = 0; i < 4; i++)
		{
			if (!(mask & (1u << i))) continue;
			data.v[4 + i] = g_ref[i];							// referencia anterior
			float v = Clamp(i, g_val[i] + data.v[i]);
			g_val[i] = v;
			data.v[i] = v;										// valor novo
			if (saveReference) g_ref[i] = v;
		}
		LeaveCriticalSection(&g_lock);
	}

	void AddActivity(int slot, float amount)
	{
		if (slot < 0 || slot > 6) return;
		EnterCriticalSection(&g_lockAct);
		g_act[slot] += amount;
		LeaveCriticalSection(&g_lockAct);
	}

	void SetReady(bool ready)
	{
		g_ready = ready;
	}

	void ResyncClock()
	{
		float minutes, scale;
		if (GameMinutes(minutes, scale))
			g_lastMinutes = minutes;
	}

	// BasicNeeds::SetNegativeBasicNeeds + GetResultNegativeData do original.
	void Update()
	{
		if (!g_init || !g_ready) return;

		float minutes, timeScale;
		if (!GameMinutes(minutes, timeScale)) return;

		if (minutes < g_lastMinutes)					// carregou um save mais antigo
		{
			g_lastMinutes = minutes;
			return;
		}
		if (!(minutes > g_lastMinutes)) return;

		float factor = timeScale / 15.0f;
		if (!(factor > 0.0f)) factor = 1.0f;
		float elapsed = minutes - g_lastMinutes;

		// soma da atividade acumulada (e zera os contadores)
		float activity = 0.0f;
		EnterCriticalSection(&g_lockAct);
		for (int i = 0; i < 7; i++)
		{
			if (g_act[i] > 0.0f) activity += g_act[i] * factor;
			g_act[i] = 0.0f;
		}
		LeaveCriticalSection(&g_lockAct);

		ESDSettings::Core s = ESDSettings::GetCore();

		ValueData data;
		memset(&data, 0, sizeof(data));
		UInt32 mask = 0;
		if (s.hungry)		{ data.v[0] = -(activity * s.magnitudeHungry   + elapsed * s.foodsTimePerDays);  mask |= kHungry; }
		if (s.thirsty)		{ data.v[1] = -(activity * s.magnitudeThirsty  + elapsed * s.drinksTimePerDays); mask |= kThirsty; }
		if (s.sleepiness)	{ data.v[2] = -(activity * s.intensitySleepiness + elapsed);					 mask |= kSleepy; }
		if (s.bodyHealth)	{ data.v[3] =  (activity * s.intensityBody     + elapsed);						 mask |= kBody; }

		if (mask) ModMask(mask, data, true);

		g_lastMinutes = minutes;
		// FALTA (fase seguinte): TaskDebuff (efeitos) e atualizacao do widget.
	}

	void Save(const SKSESerializationInterface *intfc)
	{
		if (!g_init) return;
		float out[8];
		EnterCriticalSection(&g_lock);
		for (int i = 0; i < 4; i++) { out[i] = g_val[i]; out[4 + i] = g_ref[i]; }
		LeaveCriticalSection(&g_lock);
		if (!intfc->OpenRecord(kRecordType, kRecordVersion) || !intfc->WriteRecordData(out, sizeof(out)))
			_MESSAGE("ESD: falhou ao gravar as necessidades no cosave");
	}

	void Load(const SKSESerializationInterface *intfc)
	{
		if (!g_init) return;
		UInt32 type, version, length;
		while (intfc->GetNextRecordInfo(&type, &version, &length))
		{
			if (type != kRecordType || version != kRecordVersion) continue;
			float in[8];
			memset(in, 0, sizeof(in));
			UInt32 want = (length < sizeof(in)) ? length : (UInt32)sizeof(in);
			if (intfc->ReadRecordData(in, want) != want)
			{
				_MESSAGE("ESD: registo de necessidades incompleto");
				continue;
			}
			EnterCriticalSection(&g_lock);
			for (int i = 0; i < 4; i++)
			{
				if (IsFinite(in[i])) g_val[i] = in[i];
				g_ref[i] = IsFinite(in[4 + i]) ? in[4 + i] : g_val[i];
			}
			LeaveCriticalSection(&g_lock);
			_MESSAGE("ESD: necessidades carregadas do cosave");
		}
	}

	void Revert()
	{
		if (!g_init) return;
		EnterCriticalSection(&g_lock);
		ResetValues();
		LeaveCriticalSection(&g_lock);
		g_ready = false;
	}
}
