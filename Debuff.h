#pragma once

#include <SKSE.h>

// Buffs e debuffs do ESD: adiciona/remove os feitiços do .esp conforme os valores das necessidades.
// Limiares e feiticos lidos de DebuffBasicNeedsHandler::ApplyBasicNeedsDamage no ESDPlugin.dll original.
namespace Debuff
{
	void Refresh(UInt32 mask);		// recalcula o estado das necessidades em 'mask'; chamar na thread principal
	void QueueRefresh(UInt32 mask);	// o mesmo, mas posto na fila (para chamar a partir do Papyrus)
}
