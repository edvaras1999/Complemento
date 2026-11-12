#pragma once

#include <SKSE.h>

// Action Animations (AA) integrado: animacoes de comer, beber, lavar, fazer toalhas, etc.
// Logica lida de ActionAnimations.dll (CanPlayAnim, ProcessEvent do TESEquipEvent, SendESDAnimation).
// Le as listas XML do AA em Data\SKSE\Plugins\ActionAnimations\ e envia os eventos de animacao ao jogador.
namespace Animations
{
	void Init();									// le as listas XML e os mapas de itens (apos o carregamento dos dados)
	bool OnConsume(TESForm *item, bool isDrink);	// o jogador consumiu comida/bebida: toca a animacao certa
	bool PlayType(int type);
	bool PlayTypeNamed(int type, const char *name);	// como PlayType, mas com o nome da animacao pedido pelo script (ex.: PickupBedroll)						// animacoes pedidas pelo ESD: 1 agua de fonte, 2 encher garrafa, 4 toalhas,
													// 8 lavar o corpo, 20 cama portatil, 40 alma de dragao, 80 barril
}
