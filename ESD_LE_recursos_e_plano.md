# ESD-LE: recursos do mod original, estado do port e plano "sem scripts"

## 1. Recursos do ESD e estado no port
| Recurso | Estado |
|---|---|
| Fome, sede, sono e corpo; limites e passagem do tempo | feito |
| Comer e beber (comidas, bebidas, sopas, cruas) | feito (falta o "bónus" de alimentos e o caso especial do HearthFires) |
| Sono ao dormir na cama | feito |
| Buffs e debuffs (feitiços do .esp) e mensagens | feito (falta os feitiços de duração de 180 s) |
| HUD principal (fome, sede, sono) e opcional (corpo) | feito (faltam ícones por necessidade e deslocamentos finos) |
| Toalhas e lavar o corpo | feito (a animação de lavar vem do Action Animations) |
| Desgaste por atividade (golpes, magias, gritos, passos) | feito (passos estimados pela distância, não por evento de animação) |
| Funções nativas dos scripts (cama, barril de hidromel) | feito: IsTeammateFavor, GetHeadingAngle, ShowMessage, TakeMeals |
| Níveis visuais de sujidade (3 feitiços) | por fazer |
| Sons (estômago a roncar) e expressões do rosto | por fazer |
| Garrafas vazias ao beber ("CovertBottle") | por fazer |
| Detetar alimento novo e ensinar ao ESD (caixa de mensagem) | por fazer (precisa de caixa de mensagem) |
| Agulha de tricô (fazer toalhas com algodão da tundra) | por fazer |
| Alma de dragão (evento OnPlayerAbsorbDragonSoul) | por fazer |
| Atalhos de teclado (refeições, conveniência) e comandos de consola | por fazer |
| Condição do OAR (ESDBasicNeeds) | por fazer (a LE já tem OAR) |
| Action Animations (animações de comer, beber, lavar, etc.) | por fazer (outra DLL) |

## 2. Onde o Papyrus do ESD trabalha de facto
- Os 4 scripts de efeito (fome, sede, sono, corpo): só correm quando um buff ou debuff entra ou sai (OnEffectStart/OnEffectFinish), com 2 chamadas a GetESDSetting.
- Evento principal, detetar alimento novo, agulha de tricô, alma de dragão: parados até ao evento.
- Expressões (_ESDExpressionAlias): só se as expressões estiverem ativadas; um OnUpdate a cada fExpressionDuration.
- Barril de hidromel e cama portátil: só ao ativar.
- MCM (SkyUI, ~1600 linhas): carrega ao arrancar e só trabalha com o menu aberto.
- Antes desta versão faltavam 4 funções nativas, e cada chamada a uma função em falta gera um erro de Papyrus (e um bloco no log). Isto pode ter dado a sensação de peso. Já estão implementadas.

## 3. O que se pode passar para a DLL (e com que risco)
1. Efeitos (vida, vigor, magia, velocidade, resistências): passar para a DLL exige retirar os scripts dos efeitos no .esp
   e a DLL aplicar e desfazer as alterações aos valores do jogador, guardando no cosave o que aplicou. Se isto falhar, os valores ficam
   alterados para sempre; por isso só vale a pena com cuidado e testes. Ganho: elimina 4 scripts.
2. Expressões do rosto: passam para a DLL chamando diretamente as funções nativas do SKSE (MfgConsoleFunc). Ganho: elimina o script que corre a cada fExpressionDuration.
3. Ficam em Papyrus (inativos até ao uso): evento principal, detetar alimento, agulha de tricô, alma de dragão (usam caixas de mensagem), barril e cama, MCM.

## 4. Opções próprias do port (Settings.json, grupo "port")
bTowelRequiresWater, bMessageCentered, fMessageSeconds, fMessageOffsetY, bActivityDecay, fActivityScale.
