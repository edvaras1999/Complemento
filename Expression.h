#pragma once

// Expressoes do rosto quando a necessidade fica ma (substitui o script _ESDExpressionAlias).
// Usa Actor.SetExpressionOverride e as funcoes MfgConsoleFunc do SKSE, tudo chamado pela DLL.
namespace Expression
{
	void Trigger(int priority);		// aplica a expressao das definicoes (se ativada); maior prioridade substitui menor
	void Tick();					// uma vez por segundo: repoe o rosto quando acaba o tempo
	void Clear();					// repoe o rosto ja (ao dormir, por exemplo)
}
