#pragma once

#include <string>

// Definicoes do ESD (Settings.json). Os nomes e os valores por omissao vieram do ESDPlugin.dll original
// (funcoes GetSetting/SetSetting e construtor do ESDSettings). Os nomes nao distinguem maiusculas.
namespace ESDSettings
{
	void Load();									// valores por omissao + Data\SKSE\Plugins\EatingSleepingDrinking\Settings.json
	bool Save();									// grava o Settings.json
	bool Has(const char *name);
	float GetFloat(const char *name, float fallback);	// bool = 0/1
	bool SetFloat(const char *name, float value);		// false se o nome for desconhecido
	std::string GetString(const char *name, const char *fallback);

	// valores usados no calculo das necessidades
	struct Core
	{
		bool	hungry, thirsty, sleepiness, bodyHealth;
		float	magnitudeHungry, magnitudeThirsty;	// fMagnitudeHungry, fMagnitudeThisry
		float	foodsTimePerDays, drinksTimePerDays;
		float	intensitySleepiness, intensityBody;
		float	washBodyInterval;
	};
	Core GetCore();
}
