#pragma once

// Contadores de desempenho: de minuto a minuto o log diz quantas vezes cada parte do plugin correu e quanto tempo gastou.
#include <windows.h>
#include <cstdio>
#include <SKSE/DebugLog.h>

namespace Perf
{
	enum Id { kNeeds = 0, kDebuff, kWidgetUI, kOverlay, kSample, kEvents, kCount };

	struct Slot { volatile LONG calls; volatile LONG micros; };

	inline Slot *Slots() { static Slot s[kCount]; return s; }

	inline LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
	inline LONGLONG Freq() { static LONGLONG f = 0; if (!f) { LARGE_INTEGER x; QueryPerformanceFrequency(&x); f = x.QuadPart; } return f; }

	inline void Count(int id) { InterlockedIncrement(&Slots()[id].calls); }

	class Scope
	{
		int			m_id;
		LONGLONG	m_t0;
	public:
		explicit Scope(int id) : m_id(id), m_t0(Now()) {}
		~Scope()
		{
			Slot &s = Slots()[m_id];
			InterlockedIncrement(&s.calls);
			InterlockedExchangeAdd(&s.micros, (LONG)((Now() - m_t0) * 1000000LL / Freq()));
		}
	};

	inline void LogAndReset()
	{
		static const char *names[kCount] = { "necessidades", "efeitos", "widgetUI", "mensagens", "atividade", "eventos" };
		char line[300];
		int n = sprintf(line, "ESD: [perf] ultimo minuto:");
		for (int i = 0; i < kCount; i++)
		{
			LONG c = InterlockedExchange(&Slots()[i].calls, 0);
			LONG us = InterlockedExchange(&Slots()[i].micros, 0);
			n += sprintf(line + n, " %s=%ldx/%.1fms", names[i], (long)c, us / 1000.0);
		}
		_MESSAGE("%s", line);
	}
}
