#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusFunctions.h>

#include "Papyrus.h"
#include "Needs.h"
#include "Settings.h"
#include "Debuff.h"
#include "Widget.h"
#include "Food.h"
#include "Messages.h"
#include <Skyrim/Forms/TESObjectREFR.h>
#include <cmath>

//========================================================
// Funcoes nativas usadas pelos scripts do ESD (ESDUtility.psc e _ESDMCM.psc).
// Nomes e comportamento lidos do ESDPlugin.dll original.
//========================================================
namespace
{
	// GetBasicNeeds(string typeName): valor atual (hungry / thirsty / sleepiness / bodyhealth)
	float N_GetBasicNeeds(BSFixedString typeName)
	{
		UInt32 type;
		if (!Needs::TypeFromName(typeName.c_str(), type))
			return 0.0f;
		return Needs::Get(type);
	}

	// SetBasicNeeds(string typeName, float value): so age se a necessidade estiver ativada
	void N_SetBasicNeeds(BSFixedString typeName, float value)
	{
		UInt32 type;
		if (!Needs::TypeFromName(typeName.c_str(), type))
			return;
		ESDSettings::Core s = ESDSettings::GetCore();
		bool enabled = (type == Needs::kHungry && s.hungry) || (type == Needs::kThirsty && s.thirsty) ||
					   (type == Needs::kSleepy && s.sleepiness) || (type == Needs::kBody && s.bodyHealth);
		if (enabled)
		{
			Needs::Set(type, value, true);
			Debuff::QueueRefresh(type);
		}
	}

	// ModBasicNeeds(string typeName, float value): soma ao valor atual e devolve o resultado.
	// Para bodyhealth o valor e multiplicado por fWashBodyInterval (1440 = um ciclo, como diz o ESDUtility.psc).
	float N_ModBasicNeeds(BSFixedString typeName, float value)
	{
		UInt32 type;
		if (!Needs::TypeFromName(typeName.c_str(), type))
			return 0.0f;
		ESDSettings::Core s = ESDSettings::GetCore();
		bool enabled = (type == Needs::kHungry && s.hungry) || (type == Needs::kThirsty && s.thirsty) ||
					   (type == Needs::kSleepy && s.sleepiness) || (type == Needs::kBody && s.bodyHealth);
		if (!enabled)
			return Needs::Get(type);

		if (type == Needs::kBody)
			value *= s.washBodyInterval;

		Needs::ValueData data;
		for (int i = 0; i < 8; i++) data.v[i] = 0.0f;
		int idx = (type == Needs::kHungry) ? 0 : (type == Needs::kThirsty) ? 1 : (type == Needs::kSleepy) ? 2 : 3;
		data.v[idx] = value;
		Needs::ModMask(type, data, true);
		Debuff::QueueRefresh(type);
		return Needs::Get(type);
	}


	// IsTeammateFavor(): no original pergunta ao jogador se esta num "favor" de um companheiro.
	// Sem equivalente conhecido na LE: devolve false (nao esta), o que deixa os scripts seguirem.
	bool N_IsTeammateFavor()
	{
		return false;
	}

	// GetHeadingAngle(from, to): angulo (graus, -180..180) entre a direcao em que "from" olha e a direcao de "from" para "to"
	float N_GetHeadingAngle(TESObjectREFR *from, TESObjectREFR *to)
	{
		if (!from || !to) return -180.0f;
		const double kPi = 3.14159265358979323846;
		double ang = atan2((double)(to->pos.x - from->pos.x), (double)(to->pos.y - from->pos.y)) - (double)from->rot.z;
		while (ang > kPi) ang -= 2.0 * kPi;
		while (ang < -kPi) ang += 2.0 * kPi;
		return (float)(ang * 180.0 / kPi);
	}

	// ShowMessage(chave): mostra uma das mensagens do ESD (os scripts usam sCanNotDoIt e sBarrelGetEmpty)
	void N_ShowMessage(BSFixedString messageType)
	{
		if (messageType.c_str())
			Messages::Show(messageType.c_str());
	}

	class TakeMealsTask : public TaskDelegate
	{
		UInt32 m_id;
	public:
		TakeMealsTask(UInt32 id) : m_id(id) {}
		virtual void Run() override { Food::OnEquip(m_id); }
		virtual void Dispose() override { delete this; }
	};

	// TakeMeals(form): como se o jogador tivesse acabado de consumir o item (usado ao ensinar um alimento novo)
	void N_TakeMeals(TESForm *form)
	{
		if (!form) return;
		const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
		const SKSETaskInterface *task = plugin ? plugin->GetInterface<SKSETaskInterface>() : NULL;
		if (task)
			task->AddTask(new TakeMealsTask(form->formID));
	}


	// _ActionAnimationsMCM: Register(), GetSetting(nome), SetSetting(nome, valor)
	void N_AARegister() {}
	float N_AAGetSetting(BSFixedString name)
	{
		return ESDSettings::GetFloat(name.c_str(), 0.0f);
	}
	void N_AASetSetting(BSFixedString name, float value)
	{
		if (ESDSettings::SetFloat(name.c_str(), value))
			ESDSettings::Save();
	}

	float N_GetESDSetting(BSFixedString name)
	{
		return ESDSettings::GetFloat(name.c_str(), 0.0f);
	}

	BSFixedString N_GetESDSettingString(BSFixedString name)
	{
		std::string s = ESDSettings::GetString(name.c_str(), "");
		return BSFixedString(s.c_str());
	}

	void N_SetESDSetting(BSFixedString name, float value)
	{
		if (ESDSettings::SetFloat(name.c_str(), value))
		{
			ESDSettings::Save();
			Widget::QueueConfigure();
		}
		else
			_MESSAGE("ESD papyrus: definicao desconhecida: %s", name.c_str());
	}
}

bool Papyrus::RegisterFunctions(VMState *state)
{
	SKSEScript::RegisterFunction("ESDUtility", "GetBasicNeeds", &N_GetBasicNeeds, state);
	SKSEScript::RegisterFunction("ESDUtility", "SetBasicNeeds", &N_SetBasicNeeds, state);
	SKSEScript::RegisterFunction("ESDUtility", "ModBasicNeeds", &N_ModBasicNeeds, state);
	SKSEScript::RegisterFunction("ESDUtility", "IsTeammateFavor", &N_IsTeammateFavor, state);
	SKSEScript::RegisterFunction("ESDUtility", "GetHeadingAngle", &N_GetHeadingAngle, state);
	SKSEScript::RegisterFunction("ESDUtility", "ShowMessage", &N_ShowMessage, state);
	SKSEScript::RegisterFunction("ESDUtility", "TakeMeals", &N_TakeMeals, state);
	SKSEScript::RegisterFunction("_ActionAnimationsMCM", "Register", &N_AARegister, state);
	SKSEScript::RegisterFunction("_ActionAnimationsMCM", "GetSetting", &N_AAGetSetting, state);
	SKSEScript::RegisterFunction("_ActionAnimationsMCM", "SetSetting", &N_AASetSetting, state);
	SKSEScript::RegisterFunction("_ESDMCM", "GetESDSetting", &N_GetESDSetting, state);
	SKSEScript::RegisterFunction("_ESDMCM", "GetESDSettingString", &N_GetESDSettingString, state);
	SKSEScript::RegisterFunction("_ESDMCM", "SetESDSetting", &N_SetESDSetting, state);

	_MESSAGE("ESD: funcoes Papyrus registadas");
	return true;
}
