#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <Skyrim/Forms/Actor.h>
#include <Skyrim/Forms/PlayerCharacter.h>
#include <cmath>

#include "Activity.h"
#include "Needs.h"

namespace
{
	// Indices dos contadores (os 7 contadores do original, +0x150..+0x168 do objeto BasicNeeds):
	//   1 = passos a correr (sprint)   2 = passos a correr / nadar / saltar   3 = passos a andar
	//   4 = golpes e armas             5 = magias                              6 = gritos
	enum { kSprint = 1, kRun = 2, kWalk = 3, kWeapon = 4, kSpell = 5, kShout = 6 };

	bool	g_have = false;
	float	g_x = 0.0f, g_y = 0.0f;
}

namespace Activity
{
	void OnAction(UInt32 type)
	{
		switch (type)
		{
		case 0:		// golpe
		case 6:		// arco solto
			Needs::AddActivity(kWeapon, 0.25f);
			break;
		case 1:		// magia (inicio)
		case 2:		// magia (disparo)
			Needs::AddActivity(kSpell, 0.15f);
			break;
		case 3:		// grito (inicio)
		case 4:		// grito (disparo)
			Needs::AddActivity(kShout, 0.5f);
			break;
		case 5:		// arco puxado
		case 8:		// arma sacada
		case 10:	// arma guardada
			Needs::SetActivity(kWeapon, 0.1f);		// o original faz "= 0.1", nao soma
			break;
		}
	}

	// O original conta os eventos de passo da animacao (FootLeft/FootRight): sprint 0.1, correr 0.05, andar 0.025
	// por passo; nadar e saltar 0.05. A LE nao oferece esse evento aqui, por isso estimamos os passos pela
	// distancia horizontal percorrida em cada segundo e pelo comprimento de passo de cada modo de andar.
	void Sample()
	{
		if (!g_thePlayer) { g_have = false; return; }

		float x = g_thePlayer->pos.x;
		float y = g_thePlayer->pos.y;
		if (!g_have)
		{
			g_x = x; g_y = y; g_have = true;
			return;
		}
		float dx = x - g_x, dy = y - g_y;
		g_x = x; g_y = y;
		float dist = sqrtf(dx * dx + dy * dy);
		if (dist < 1.0f || dist > 1500.0f)		// parado, ou teletransporte / viagem rapida
			return;

		UInt32 f = g_thePlayer->flags04;
		if (f & 0x400)								// a nadar: uma braçada cada ~80 unidades
			Needs::AddActivity(kRun, dist / 80.0f * 0.05f);
		else if (f & 0x100)							// sprint
			Needs::AddActivity(kSprint, dist / 130.0f * 0.1f);
		else if (f & 0x40)							// correr (modo normal do jogo)
			Needs::AddActivity(kRun, dist / 90.0f * 0.05f);
		else										// andar
			Needs::AddActivity(kWalk, dist / 60.0f * 0.025f);
	}

	void Reset()
	{
		g_have = false;
	}
}
