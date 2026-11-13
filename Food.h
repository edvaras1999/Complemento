#pragma once

#include <SKSE.h>

// Comer e beber: classifica o item equipado pelas 5 listas do ESD (_ESDFoodsList, _ESDDrinkslList,
// _ESDSoupsList, _ESDRawFoodsList, _ESDNoFoodsNoDrinksList) e repoe fome/sede.
// Logica lida de BasicNeeds::ProcessEvent(TESEquipEvent) e MatchMealItem no ESDPlugin.dll original.
namespace Food
{
	void FindLists();					// descobre as listas do ESD (plugin light ou normal)
	bool HasLists();
	UInt32 TypeOf(UInt32 formId);		// tipo do alimento pelas listas do ESD (1 comida, 2 bebida, 4 sopa, 8 crua, 0x10 excluido, 0 desconhecido)
	UInt32 FormId(UInt32 localId);		// FormID real de uma forma do ESD (0 se o plugin nao foi encontrado)
	void OnEquip(UInt32 baseFormId);	// chamar na thread principal
}
