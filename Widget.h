#pragma once

// HUD do ESD: menu Scaleform com o KNNWidgetMeter.swf (o mesmo do original), que mostra fome, sede e sono.
// Funcoes do Flash e formulas das percentagens lidas de esd::WidgetHandler no ESDPlugin.dll original.
namespace Widget
{
	void Register();			// regista o menu no jogo (chamar apos o carregamento dos dados)
	void Open();				// abre o widget se estiver ativado nas definicoes
	void QueueConfigure();		// reaplica posicao, escala, transparencia e visibilidade (por exemplo apos mudar o MCM)
	void QueueUpdate();
	void Watchdog();			// reabre um widget ativado que o jogo tenha fechado (chamar a cada poucos segundos)
	void ToggleUserHidden();	// esconde/mostra os widgets a pedido do jogador (tecla mantida)			// atualiza os valores mostrados (fome, sede, sono)
}
