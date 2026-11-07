#pragma once

// Mensagens do ESD centradas no ecra (em vez da notificacao do canto). Usa o ESDMessage.swf:
// um campo de texto centrado, com sombra, que se apaga sozinho passados alguns segundos.
namespace Overlay
{
	void Register();					// regista o menu (apos o carregamento dos dados)
	void Open();						// abre o menu (ao carregar um save ou iniciar jogo)
	bool IsOpen();
	void Push(const char *utf8Text);	// junta uma linha ao ecra (pode ser chamada de qualquer thread)
}
