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
| Níveis visuais de sujidade (3 feitiços) | feito (ativar "Dirty body" no MCM) |
| Sons: estômago a roncar, bocejo, moscas | feito (a cada 90 s como o original; sem o som de sede nem a versão feminina do bocejo, ids desconhecidos) |
| Expressões do rosto | feito (na DLL, sem script; ativar "Expression" no MCM) |
| Garrafas vazias ao beber ("CovertBottle") | por fazer: o original depende de palavras-chave distribuídas por KID (ficheiro ESDDrinksDefinition_KID.ini); preciso de os FormIDs das bebidas do Skyrim.esm |
| Detetar alimento novo e ensinar ao ESD (caixa de mensagem) | por fazer (precisa de caixa de mensagem) |
| Agulha de tricô (fazer toalhas com algodão da tundra) | por fazer |
| Alma de dragão (evento OnPlayerAbsorbDragonSoul) | por fazer |
| Atalho de refeições | feito (tecla do MCM) |
| Atalho "conveniente" (beber em rios, recolher cama portátil) e comandos de consola | por fazer (a água não é detetável nesta LE) |
| Condição do OAR (ESDBasicNeeds) | por fazer: preciso do cabeçalho da API do OAR para LE. Alternativa já possível (OAR e DAR): usar nas condições os feitiços do ESD (ver secção 5) |
| Action Animations: animações ao comer e beber (parado, sentado, a andar), lavar o corpo, fazer toalhas | feito (integrado nesta DLL; precisa dos ficheiros do AA instalados) |
| Action Animations: ler livros e notas, armadura tirada e reposta, água de fonte, encher garrafa, cama portátil, alma de dragão, barril | por fazer (os tipos já existem no código; falta o ESD os pedir e a armadura) |

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

## 5. Animações por estado, com OAR ou DAR, sem código novo
Os estados das necessidades já existem no jogo como feitiços (adicionados e retirados pela DLL), por isso as condições de OAR e de DAR podem usá-los:
- Fome: `_ESDHungryAbilitySpell` (debuff); bem alimentado: `_ESDStuffedSpell`.
- Sede: `_ESDThirstyAbilitySpell`; sem sede: `_ESDQuenchedThirstSpell`.
- Sono: `_ESDSleepyAbilitySpell` (cansado); descansado: `_ESDNotWearySpell`.
- Corpo: `_ESDBodyAbilitySpell` (sujo); limpo: `_ESDCleanBodySpell`. Níveis de sujidade: `0x82D`, `0x825`, `0x82B`.
Condição tipo: "HasSpell / HasMagicEffect do jogador" com o feitiço do estado que queres.

## 6. Revisão do que falta no ESD (por ordem de valor)
1. Testar o que já está escrito mas nunca correu: cama e funções nativas, atividade (já confirmada pelo log), alimento novo, agulha de tricô, expressões, atalho de refeições, sujidade e sons, animações.
2. Efeitos dos buffs e debuffs na DLL (retirar os 4 scripts de efeitos): precisa de alterar o .esp e de guardar no save o que a DLL aplicou. Só com teste cuidadoso.
3. Garrafas vazias ao beber: já há os códigos das bebidas (ficheiros AlchemyItems do AA), falta ler no original a regra por tipo de garrafa.
4. Atalho "conveniente" (beber de um rio, recolher a cama portátil): a água não é detetável nesta LE; a cama portátil precisa do alvo da mira.
5. Alma de dragão (evento para o script do ESD e animação do AA).
6. Condição do OAR (ESDBasicNeeds): precisa do cabeçalho da API do OAR para LE; entretanto OAR e DAR podem usar os feitiços do ESD (secção 5).
7. Widget: ícones por necessidade, deslocamentos finos e o ícone de embriaguez.
8. Bónus de alimentos (IsFoodBonus) e o caso especial do HearthFires.
9. Comandos de consola.
