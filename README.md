# ESD-LE — Eating Sleeping Drinking para Skyrim LE (fase 1: núcleo)

Plugin SKSE (32 bits) que reimplementa o núcleo do `ESDPlugin.dll` do ESD NG 2.3.0, lido a partir do código desmontado.
Uso pessoal (o autor está incontactável).

## O que já faz
- Mantém fome, sede, sono e corpo (`Needs.cpp`), com as mesmas regras de limite do original.
- Passagem do tempo (a fórmula de `SetNegativeBasicNeeds`): lê `GameDaysPassed` e `TimeScale` e aplica o desgaste a cada 5 s de relógio real.
- Funções Papyrus: `ESDUtility.GetBasicNeeds / SetBasicNeeds / ModBasicNeeds` e `_ESDMCM.GetESDSetting / GetESDSettingString / SetESDSetting`.
- Definições em `Data\SKSE\Plugins\EatingSleepingDrinking\Settings.json`, com os valores por omissão do original (aceita o `;` no lugar de `:` do ficheiro de exemplo).
- Grava as necessidades no cosave (registo `ESDD`, versão 2, 8 floats).

## Comer e beber (novo)
- Ao equipar (consumir) comida ou bebida, o plugin classifica o item pelas 5 listas do ESD e repõe fome/sede:
  comida: (peso) x fIntensityFoods x 1000; comida crua: o mesmo x 0,1; bebida: peso x fIntensityDrinks x 1500;
  sopa: fome como comida + 25% de sede. Itens da lista "nem comida nem bebida" são ignorados.
- As listas são encontradas sem depender do índice de carga (procura o plugin do ESD entre os normais e os light).
  O `ESDPlugin.log` diz "listas de alimentos encontradas" ou "nao encontradas".
- Ainda sem: bónus de alimentos, mensagens, itens especiais (HearthFires), garrafas vazias.

## Alimento novo e agulha de tricô (novo)
- Se consumires um alimento que o ESD não conhece (poção "de comer" fora das 5 listas), a DLL envia o evento `OnDetectNewFood` ao script do ESD,
  que mostra a caixa "o que é isto?" (comida, sopa, bebida, crua, nada) e acrescenta o item à lista certa; depois repõe como se o tivesses acabado de consumir.
- Agulha de tricô (arma `0x834` do ESD): se tiveres algodão da tundra (`fTrundraCottonAmount`, 3 por omissão), envia `OnEquippedKnittingNeedle` e o script faz uma toalha;
  se não tiveres, mostra "Not enough Tundra Cotton...".
- Ainda sem: o evento da alma de dragão (liga-se ao Action Animations).

## Sujidade por níveis e sons (novo)
- Com "Dirty body" ativado no MCM, o corpo recebe um de 3 feitiços de sujidade: acima de `intervalo x 480`, `x 960` e `x 1440`
  (com o intervalo normal 3: 1440, 2880 e 4320). Abaixo do primeiro nível, os feitiços saem.
- Sons, no máximo um de cada tipo a cada 90 s: estômago a roncar (fome, se "Hungry growl" estiver ativo), bocejo (sono abaixo de 720),
  moscas (corpo acima de `intervalo x 1440`). Tocam no jogador pela função `Sound.Play` do jogo.

## Desgaste por atividade e funções Papyrus em falta (novo)
- Funções nativas novas (a cama portátil e o barril de hidromel chamavam-nas e, sem elas, o Papyrus abortava a ativação):
  `IsTeammateFavor` (devolve false), `GetHeadingAngle`, `ShowMessage`, `TakeMeals`.
- Atividade: golpes e arco soltado +0,25; magias +0,15; gritos +0,5; arco puxado/arma sacada/guardada = 0,1 (valores do original).
  Passos: o original conta eventos de animação; aqui estimam-se a cada segundo pela distância percorrida
  (sprint 0,1 por passo de 130 unidades, correr 0,05 por 90, andar 0,025 por 60, nadar 0,05 por 80). É uma aproximação.
- Estes pesos entram na fórmula do desgaste: fome/sede `activity x magnitude`, sono `activity x fIntensitySleepiness`, corpo `activity x fIntensityBody`.

## Mensagens no centro do ecrã, lavar e toalhas (novo)
- As mensagens aparecem no centro do ecrã (com sombra, até 4 linhas, desvanecem-se em ~4 s). Precisam do ficheiro
  `Data\Interface\EatingSleepingDrinking\ESDMessage.swf` (está em `Interface\...` neste projeto; copiar para o mod instalado).
  Se esse menu não abrir, usa-se a notificação normal do canto.
- Toalhas: `_ESDTowelPotion` só funciona a nadar (se não, devolve a toalha e diz "Won't be able to do it..."); `_ESDTowelAnyWherePotion` funciona em qualquer sítio.
  Lavar tira `1440 x fWashBodyInterval` ao corpo; se sobrar entre 0 e 720, fica a 0. A animação de lavar (Action Animations) ainda não existe.
- Widget opcional ("Secondary" no MCM) mostra o corpo: percentagem `valor / (intervalo x 1440) x 100` (1..100). Mesma lógica de posição e visibilidade do principal;
  agora há uma vigia que o volta a mostrar se ficar escondido por engano.
- A LE não tem aqui uma função para detetar água rasa. Por omissão a toalha de água funciona em qualquer sítio; com `"bTowelRequiresWater": 1` em Settings.json passa a exigir estar a nadar.
- O nome do item nas mensagens ("Ate a ...") vem do nome completo da poção.

## HUD (novo)
- Menu Scaleform próprio que carrega `Data\Interface\EatingSleepingDrinking\KNNWidgetMeter.swf` (o mesmo do original) e mostra fome, sede e sono.
- Liga-se no MCM (widget "Primary"). Posição, escala e transparência vêm de `fPrimaryPosX/PosY/Scale/Alpha`.
- A percentagem de cada barra segue o original: acima do limiar `(valor - limiar) / escala x 100` (lado positivo), abaixo `(valor - limiar) / denominador x 100`
  (lado negativo; o denominador usa `fDayToDie`). Esconde-se enquanto há menus que pausam o jogo.
- Ainda sem: widget opcional (corpo e embriaguez), ícones por necessidade, deslocamentos (`...OffsetX/Y`).
- O SWF original é versão 15 (SE); se a LE não o mostrar, o log diz "ERRO: nao consegui carregar ...swf".

## Buffs, debuffs e mensagens (novo)
- Limiares do original: fome e sede 1440; sono 720; corpo `fWashBodyInterval x 1440` (aviso de banho a partir de `(intervalo-1) x 1440 + 720`).
- Acima do limiar (fome/sede/sono): buff (bem alimentado, sem sede, descansado). Abaixo: debuff + mensagem uma vez
  ("I'm starving...", "I'm thirsty...", "I'm tired..."). No corpo: limpo = buff, sujo = debuff + "My body have got dirty...".
- Os feitiços são os do .esp do ESD (os scripts do ESD tratam dos efeitos); a DLL só os adiciona e remove.
- Comer/beber mostra "Ate a {}" / "Drank a {}".
- Ainda sem: feitiços de duração (180 s), níveis visuais de sujidade, sons/expressões, HUD.

## Dormir (novo)
- Ao fechar o menu Dormir/Esperar, o sono sobe `(2 x minutos) x fShortSleeper + minutos` (com fShortSleeper = 1: 3 por minuto dormido),
  com o mesmo limite de antes (acima de 4320 passa a 2160). O desgaste do tempo passado é aplicado logo a seguir.
  (Regra lida de BasicNeeds::ProcessEvent do TESSleepStartEvent e do MenuOpenCloseEvent no original.)

## O que ainda NÃO faz (próximas fases)
- Debuffs, buffs, sons e expressões (`TaskDebuff`, `SetResult*`).
- Eventos que alimentam os contadores de atividade (animação, golpes, magias) e o que cada evento faz às necessidades.
- Sono (recuperar ao dormir), garrafas e toalhas; evento `OnDetectNewFood` etc.
- Widget do HUD, atalhos, comandos de consola, `TakeMeals`, `ShowMessage`, `IsTeammateFavor`, `GetHeadingAngle`, `GetAddedFoodsFromList`.
- Condição do OAR (`ESDBasicNeeds`).

## Valores assumidos (não confirmados no original)
- Valores iniciais num save novo: fome 2880, sede 2880, sono 1680, corpo 0 (são os valores dos botões "Reset" do MCM original; que o original comece mesmo daqui num save novo não está confirmado).
- `bIsEnableMessage` começa desligado.
- Ao carregar um save, o relógio é ressincronizado (o tempo passado antes de carregar não é aplicado).

## Como compilar
Igual ao Better Looting: envia estes ficheiros para um repositório GitHub (raiz), mais o `.7z` do QuickLoot 1.1f (libSkyrim) e a pasta `.github/workflows/build.yml`.
Corre "Run workflow" e baixa o artefacto `dll` (`ESDPlugin.dll`).

## Instalação para teste
`ESDPlugin.dll` em `Data\SKSE\Plugins\`. Os `.esp` e os scripts do ESD têm de ser os da versão NG (ESL na LE: ver o que já funciona na tua lista).
Procura no `Documents\My Games\Skyrim\SKSE\ESDPlugin.log` as linhas que começam por "ESD:".
