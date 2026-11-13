#pragma once

#include <SKSE.h>
#include <SKSE/PluginAPI.h>

// Efeitos dos 4 buffs do ESD aplicados pela DLL (substituem os scripts _ESDHungryEffectScript, _ESDThirsyEffectScript,
// _ESDSleepinessEffectScript e _ESDBodyEffectScript, que o .esp sem scripts ja nao tem).
// A DLL guarda no save o que aplicou, para desfazer exatamente o mesmo.
namespace Effects
{
	enum Kind
	{
		kStuffed	= 0,	// bem alimentado: vida e vigor
		kQuenched	= 1,	// sem sede: magia e recuperacao de gritos
		kNotWeary	= 2,	// descansado: velocidade de armas e de movimento
		kClean		= 3		// corpo limpo: resistencia a doencas e venenos
	};

	void Sync(Kind kind, bool shouldBeActive);	// aplica ou desfaz o efeito para ficar no estado pedido (so age se mudar)

	extern const UInt32 kRecordType;				// 'ESDE'
	void Save(const SKSESerializationInterface *intfc);
	void LoadRecord(const SKSESerializationInterface *intfc, UInt32 version, UInt32 length);
	void Revert();									// novo jogo / carregar: esquece o que estava aplicado (o save traz os valores dele)
}
