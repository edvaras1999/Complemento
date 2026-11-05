#pragma once

#include <SKSE.h>

// Comer e beber: classifica o item equipado pelas 5 listas do ESD (_ESDFoodsList, _ESDDrinkslList,
// _ESDSoupsList, _ESDRawFoodsList, _ESDNoFoodsNoDrinksList) e repoe fome/sede.
// Logica lida de BasicNeeds::ProcessEvent(TESEquipEvent) e MatchMealItem no ESDPlugin.dll original.
namespace Food
{
	void FindLists();					// descobre as listas do ESD (plugin light ou normal)
	bool HasLists();
	void OnEquip(UInt32 baseFormId);	// chamar na thread principal
}
