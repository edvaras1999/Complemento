#pragma once

#include <SKSE.h>
#include <SKSE/PluginAPI.h>

// Necessidades basicas (fome, sede, sono, corpo). A logica foi lida do ESDPlugin.dll original
// (BasicNeeds::Get/Set/Mod/SetNegativeBasicNeeds); ver ESD_LE_especificacao_fase1.md.
namespace Needs
{
	enum Type
	{
		kHungry		= 1,
		kThirsty	= 2,
		kSleepy		= 4,
		kBody		= 8,
		kAll		= 0xF
	};

	// 4 valores novos/variacoes (0..3) e 4 valores de referencia anteriores (4..7), como no original
	struct ValueData
	{
		float v[8];
	};

	void Init();
	bool TypeFromName(const char *name, UInt32 &type);		// "hungry", "thirsty", "sleepiness", "bodyhealth"

	float Get(UInt32 type);
	void Set(UInt32 type, float value, bool saveReference);
	float Mod(UInt32 type, float delta);					// um tipo; devolve o valor novo
	void ModMask(UInt32 mask, ValueData &data, bool saveReference);

	void AddActivity(int slot, float amount);				// slot 0..6 (contadores de atividade)
	void SetActivity(int slot, float value);				// atribui em vez de somar (como o original em alguns eventos)

	void SetReady(bool ready);								// jogo carregado: o relogio pode correr
	void ResyncClock();										// volta a ler o relogio do jogo (sem aplicar o intervalo perdido)
	void Update();											// passagem do tempo (corre na thread principal)

	void SleepStart(float durationDays);					// TESSleepStartEvent: guarda a duracao (em dias de jogo)
	void SleepMenuClosed();									// ao fechar o menu Dormir/Esperar: repoe o sono

	void Save(const SKSESerializationInterface *intfc);
	void LoadRecord(const SKSESerializationInterface *intfc, UInt32 type, UInt32 version, UInt32 length);
	void Revert();
}
