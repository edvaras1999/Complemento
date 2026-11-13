#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <Skyrim/SkyrimVM.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/PlayerCharacter.h>
#include <cstring>
#include <cmath>

#include "Effects.h"
#include "Settings.h"

namespace
{
	typedef _NativeFunction<Actor, void, BSFixedString, float>	ModAVNative;
	typedef _NativeFunction<Actor, float, BSFixedString>		GetAVNative;

	const UInt32 kRecVersion = 1;

	struct Applied
	{
		bool	active;
		float	v[2];		// quanto foi somado a cada um dos dois valores do efeito
	};

	Applied	g_state[4];
	bool	g_failed = false;

	const char *AVName(int kind, int slot)
	{
		static const char *names[4][2] =
		{
			{ "Health", "Stamina" },
			{ "Magicka", "ShoutRecoveryMult" },
			{ "WeaponSpeedMult", "SpeedMult" },
			{ "DiseaseResist", "PoisonResist" }
		};
		return names[kind][slot];
	}

	bool ModAV(VMState *state, Actor *a, const char *name, float amount)
	{
		if (amount == 0.0f) return true;
		ModAVNative *fn = (ModAVNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "ModActorValue");
		if (!fn || !fn->Validate(state)) { _MESSAGE("ESD: nao encontrei Actor.ModActorValue; os efeitos dos buffs ficam desligados"); g_failed = true; return false; }
		(*fn)(state, 0, a, BSFixedString(name), amount);
		return true;
	}

	float GetAV(VMState *state, Actor *a, const char *name, bool base)
	{
		GetAVNative *fn = (GetAVNative *)SKSEScript::GetFunction(state, Actor::kTypeID, base ? "GetBaseActorValue" : "GetActorValue");
		if (!fn || !fn->Validate(state)) return 0.0f;
		return (*fn)(state, 0, a, BSFixedString(name));
	}

	// valores a somar, calculados como nos scripts originais
	void Compute(VMState *state, Actor *a, int kind, float out[2])
	{
		out[0] = out[1] = 0.0f;
		switch (kind)
		{
		case Effects::kStuffed:
		{
			float fh = ESDSettings::GetFloat("fHealth", 0.0f), fs = ESDSettings::GetFloat("fStamina", 0.0f);
			if (fh != 0.0f) out[0] = GetAV(state, a, "Health", true) / 2.0f * fh;
			if (fs != 0.0f) out[1] = GetAV(state, a, "Stamina", true) / 2.0f * fs;
			break;
		}
		case Effects::kQuenched:
		{
			float fm = ESDSettings::GetFloat("fMagicka", 0.0f), fr = ESDSettings::GetFloat("fShoutRecoveryMult", 0.0f);
			if (fm != 0.0f) out[0] = GetAV(state, a, "Magicka", true) / 2.0f * fm;
			if (fr != 0.0f) out[1] = -fr;
			break;
		}
		case Effects::kNotWeary:
		{
			float fw = ESDSettings::GetFloat("fWeaponSpeedMult", 0.0f), fv = ESDSettings::GetFloat("fSpeedMult", 0.0f);
			if (fw != 0.0f)
				out[0] = (GetAV(state, a, "WeaponSpeedMult", false) >= 1.0f) ? (fw - 1.0f) : fw;	// como o script original
			if (fv != 0.0f) out[1] = fv;
			break;
		}
		case Effects::kClean:
			out[0] = ESDSettings::GetFloat("fDiseaseResist", 0.0f);
			out[1] = ESDSettings::GetFloat("fPoisonResist", 0.0f);
			break;
		}
	}
}

namespace Effects
{
	const UInt32 kRecordType = 0x45534445;			// 'ESDE'

	void Sync(Kind kind, bool shouldBeActive)
	{
		if (g_failed || !g_skyrimVM || !g_thePlayer) return;
		Applied &st = g_state[kind];
		if (st.active == shouldBeActive) return;

		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		Actor *player = static_cast<Actor *>(g_thePlayer);

		if (shouldBeActive)
		{
			float v[2];
			Compute(state, player, kind, v);
			if (!ModAV(state, player, AVName(kind, 0), v[0])) return;
			if (!ModAV(state, player, AVName(kind, 1), v[1])) return;
			st.v[0] = v[0]; st.v[1] = v[1];
			st.active = true;
			_MESSAGE("ESD: efeito %d aplicado (%s %+.2f, %s %+.2f)", (int)kind, AVName(kind, 0), v[0], AVName(kind, 1), v[1]);
		}
		else
		{
			// desfaz exatamente o que foi somado
			if (!ModAV(state, player, AVName(kind, 0), -st.v[0])) return;
			if (!ModAV(state, player, AVName(kind, 1), -st.v[1])) return;
			_MESSAGE("ESD: efeito %d desfeito", (int)kind);
			st.active = false;
			st.v[0] = st.v[1] = 0.0f;
		}
	}

	void Save(const SKSESerializationInterface *intfc)
	{
		float out[12];
		for (int i = 0; i < 4; i++)
		{
			out[i * 3] = g_state[i].active ? 1.0f : 0.0f;
			out[i * 3 + 1] = g_state[i].v[0];
			out[i * 3 + 2] = g_state[i].v[1];
		}
		if (!intfc->OpenRecord(kRecordType, kRecVersion) || !intfc->WriteRecordData(out, sizeof(out)))
			_MESSAGE("ESD: falhou ao gravar os efeitos no cosave");
	}

	void LoadRecord(const SKSESerializationInterface *intfc, UInt32 version, UInt32 length)
	{
		if (version != kRecVersion) return;
		float in[12];
		memset(in, 0, sizeof(in));
		UInt32 want = (length < sizeof(in)) ? length : (UInt32)sizeof(in);
		if (intfc->ReadRecordData(in, want) != want) return;
		for (int i = 0; i < 4; i++)
		{
			g_state[i].active = in[i * 3] != 0.0f;
			g_state[i].v[0] = in[i * 3 + 1];
			g_state[i].v[1] = in[i * 3 + 2];
		}
		_MESSAGE("ESD: efeitos carregados do cosave");
	}

	void Revert()
	{
		memset(g_state, 0, sizeof(g_state));
	}
}
