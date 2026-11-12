#pragma once

#include <SKSE.h>

// Eventos que a DLL original envia ao script _ESDEventScritp (na quest _ESDMCM, 0x84E do .esp):
//   OnDetectNewFood(ObjectReference potionRef, ObjectReference playerRefOrNullRef)
//   OnEquippedKnittingNeedle(ObjectReference playerRef, int gettingTowelCount, int removeTundraCottonCount)
// Os scripts tratam das caixas de mensagem; a DLL decide quando os enviar.
namespace Events
{
	void DetectNewFood(TESForm *potion);		// alimento que nao esta em nenhuma lista: pergunta ao jogador o que e
	void KnittingNeedle();						// agulha de trico equipada: faz uma toalha com algodao da tundra
	void OnItemDropped(UInt32 itemFormId);		// o jogador largou um item no chao: cama portatil ou barril passam a objeto ativavel
	void ConvertDropped(int pairIndex);			// (interno) corre depois de um pequeno atraso
}
