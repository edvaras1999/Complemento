#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <SKSE/PapyrusEvents.h>
#include <Skyrim/SkyrimVM.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/PlayerCharacter.h>
#include <windows.h>
#include <cstdio>

#include "Expression.h"
#include "Settings.h"

namespace
{
	typedef _NativeFunction<Actor, void, SInt32, SInt32>	SetOverrideNative;
	typedef _NativeFunction<Actor, void>					ClearOverrideNative;

	bool	g_active = false;
	int		g_priority = 0;
	DWORD	g_endMs = 0;
	bool	g_failed = false;

	// MfgConsoleFunc.SetPhoneme/SetModifier(Actor, int, int)
	class Mfg3Args : public SKSEScript::FunctionArguments<Actor *, SInt32, SInt32>
	{
	public:
		Mfg3Args(VMState *state, Actor *a, SInt32 id, SInt32 value) : SKSEScript::FunctionArguments<Actor *, SInt32, SInt32>(a, id, value)
		{
			m_state = state;
		}
	};

	// MfgConsoleFunc.ResetPhonemeModifier(Actor)
	class Mfg1Args : public SKSEScript::FunctionArguments<Actor *>
	{
	public:
		Mfg1Args(VMState *state, Actor *a) : SKSEScript::FunctionArguments<Actor *>(a)
		{
			m_state = state;
		}
	};

	void CallMfg3(VMState *state, const char *fn, Actor *a, int id, int value)
	{
		Mfg3Args args(state, a, id, value);
		BSScript::BSScriptVariable result;
		state->CallStaticFunction(BSFixedString("MfgConsoleFunc"), BSFixedString(fn), &args, result);
	}

	void Reset(VMState *state, Actor *player)
	{
		Mfg1Args args(state, player);
		BSScript::BSScriptVariable result;
		state->CallStaticFunction(BSFixedString("MfgConsoleFunc"), BSFixedString("ResetPhonemeModifier"), &args, result);
		ClearOverrideNative *clr = (ClearOverrideNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "ClearExpressionOverride");
		if (clr && clr->Validate(state))
			(*clr)(state, 0, player);
	}
}

namespace Expression
{
	void Trigger(int priority)
	{
		if (g_failed || !g_skyrimVM || !g_thePlayer) return;
		if (ESDSettings::GetFloat("bIsEnableExpression", 0.0f) == 0.0f) return;
		if (g_active && priority < g_priority) return;

		VMState *state = g_skyrimVM->GetState();
		if (!state) return;
		Actor *player = static_cast<Actor *>(g_thePlayer);

		SetOverrideNative *set = (SetOverrideNative *)SKSEScript::GetFunction(state, Actor::kTypeID, "SetExpressionOverride");
		if (!set || !set->Validate(state))
		{
			_MESSAGE("ESD: nao encontrei Actor.SetExpressionOverride; as expressoes ficam desligadas");
			g_failed = true;
			return;
		}

		(*set)(state, 0, player, (SInt32)ESDSettings::GetFloat("expression_id", 7.0f), (SInt32)ESDSettings::GetFloat("expression_value", 50.0f));
		char key[24];
		for (int i = 0; i < 16; i++)
		{
			sprintf(key, "phoneme_%02d", i);
			int v = (int)ESDSettings::GetFloat(key, 0.0f);
			if (v > 0) CallMfg3(state, "SetPhoneme", player, i, v);
		}
		for (int i = 0; i < 17; i++)
		{
			sprintf(key, "modifier_%02d", i);
			int v = (int)ESDSettings::GetFloat(key, 0.0f);
			if (v > 0) CallMfg3(state, "SetModifier", player, i, v);
		}

		float secs = ESDSettings::GetFloat("fExpressionDuration", 20.0f);
		if (secs < 1.0f) secs = 1.0f;
		g_endMs = GetTickCount() + (DWORD)(secs * 1000.0f);
		g_priority = priority;
		g_active = true;
	}

	void Clear()
	{
		if (!g_active) return;
		g_active = false;
		g_priority = 0;
		if (!g_skyrimVM || !g_thePlayer) return;
		VMState *state = g_skyrimVM->GetState();
		if (state)
			Reset(state, static_cast<Actor *>(g_thePlayer));
	}

	void Tick()
	{
		if (g_active && (int)(GetTickCount() - g_endMs) >= 0)
			Clear();
	}
}
