#pragma once

#include <SKSE.h>

// Desgaste por atividade: o original acumula pequenos pesos (passos, golpes, magias, gritos) que
// aceleram a fome, a sede, o sono e a sujidade (ver GetResultNegativeData em Needs.cpp).
namespace Activity
{
	void OnAction(UInt32 type);		// SKSEActionEvent do jogador: 0 golpe, 1/2 magia, 3/4 grito, 5 arco puxado, 6 arco solto, 8/10 sacar/guardar
	void Sample();					// uma vez por segundo, na thread principal: conta os passos pela distancia percorrida
	void Reset();					// ao carregar um save ou iniciar jogo
}
