#include <SKSE.h>
#include <SKSE/PluginAPI.h>
#include <SKSE/DebugLog.h>
#include <SKSE/GameMenus.h>
#include <SKSE/GameEvents.h>
#include <SKSE/Scaleform.h>
#include <Skyrim/Menus/MenuManager.h>
#include <Skyrim/Menus/UIManager.h>
#include <Skyrim/Menus/UIMessage.h>
#include <Skyrim/Events/MenuEvent.h>
#include <cmath>
#include <windows.h>

#include "Widget.h"
#include "Needs.h"
#include "Settings.h"
#include "Overlay.h"

namespace
{
	// 0 = widget principal (fome, sede, sono) ; 1 = widget opcional (corpo)
	struct WidgetCfg
	{
		const char *menuName;
		const char *swf;
		const char *enableKey;
		const char *alphaKey;
		const char *scaleKey;
		const char *posXKey;
		const char *posYKey;
		float		defX, defY;
	};

	const WidgetCfg kCfg[2] =
	{
		{ "ESD Widget Menu",    "EatingSleepingDrinking\\KNNWidgetMeter",          "bIsEnableWidgetPrimary",   "fPrimaryAlpha",   "fPrimaryScale",   "fPrimaryPosX",   "fPrimaryPosY",   -650.0f, 330.0f },
		{ "ESD Widget Op Menu", "EatingSleepingDrinking\\KNNWidgetMeter_Optional", "bIsEnableWidgetSecondary", "fSecondaryAlpha", "fSecondaryScale", "fSecondaryPosX", "fSecondaryPosY", -650.0f, 390.0f }
	};

	class IWidgetMenu;
	IWidgetMenu	*g_menu[2] = { nullptr, nullptr };
	bool		g_registered = false;
	bool		g_userHidden = false;				// escondido pelo jogador (tecla)
	DWORD		g_lastReopen = 0;
	int			g_last[4] = { 9999, 9999, 9999, 9999 };		// ultimos valores enviados: fome, sede, sono, corpo

	bool Enabled(int id)
	{
		return ESDSettings::GetFloat(kCfg[id].enableKey, 0.0f) != 0.0f;
	}

	// (percentagem 1..100, lado positivo/negativo) como no original
	void Compute(float v, float threshold, float upperScale, float lowDenominator, int &mag, bool &positive)
	{
		float pct;
		if (v > threshold)
		{
			pct = floorf((v - threshold) / upperScale * 100.0f);
			positive = true;
		}
		else if (ceilf(v) == ceilf(threshold))
		{
			pct = 1.0f;
			positive = true;
		}
		else
		{
			pct = ceilf((v - threshold) / lowDenominator * 100.0f);
			positive = false;
		}
		int m = (int)fabsf(pct);
		if (m < 1) m = 1;
		if (m > 100) m = 100;
		mag = m;
	}

	void SendMeter(GFxMovieView *view, const char *fn, int slot, int mag, bool positive, bool force)
	{
		int signedValue = positive ? mag : -mag;
		if (!force && g_last[slot] == signedValue)
			return;
		GFxValue args[2];
		args[0].SetNumber((double)mag);
		args[1].SetNumber(positive ? 1.0 : 0.0);
		if (view->Invoke(fn, nullptr, args, 2))
			g_last[slot] = signedValue;
		else
			_MESSAGE("ESD: falhou ao chamar %s no widget", fn);
	}

	// setBodyHealthValue(percentagem): so um argumento
	void SendBody(GFxMovieView *view, int mag, bool force)
	{
		if (!force && g_last[3] == mag)
			return;
		GFxValue arg;
		arg.SetNumber((double)mag);
		if (view->Invoke("_root.widget.setBodyHealthValue", nullptr, &arg, 1))
			g_last[3] = mag;
		else
			_MESSAGE("ESD: falhou ao chamar setBodyHealthValue no widget opcional");
	}

	void InvokeNumber(GFxMovieView *view, const char *fn, double v)
	{
		GFxValue arg;
		arg.SetNumber(v);
		if (!view->Invoke(fn, nullptr, &arg, 1))
			_MESSAGE("ESD: falhou ao chamar %s no widget", fn);
	}

	void ApplyValues(GFxMovieView *view, int id, bool force)
	{
		ESDSettings::Core s = ESDSettings::GetCore();
		float dayToDie = ESDSettings::GetFloat("fDayToDie", 0.0f);
		int mag;
		bool pos;

		if (id == 0)
		{
			if (s.hungry)
			{
				Compute(Needs::Get(Needs::kHungry), 1440.0f, 1440.0f, (dayToDie > 0.0f) ? 4320.0f * dayToDie : 1440.0f, mag, pos);
				SendMeter(view, "_root.widget.setHungryValue", 0, mag, pos, force);
			}
			if (s.thirsty)
			{
				Compute(Needs::Get(Needs::kThirsty), 1440.0f, 1440.0f, (dayToDie > 0.0f) ? 4320.0f * dayToDie : 1440.0f, mag, pos);
				SendMeter(view, "_root.widget.setThirstyValue", 1, mag, pos, force);
			}
			if (s.sleepiness)
			{
				Compute(Needs::Get(Needs::kSleepy), 720.0f, 2160.0f, (dayToDie > 0.0f) ? 1440.0f * dayToDie : 1440.0f, mag, pos);
				SendMeter(view, "_root.widget.setSleepinessValue", 2, mag, pos, force);
			}
		}
		else if (s.bodyHealth)
		{
			// corpo: percentagem = valor / (intervalo * 1440) * 100, entre 1 e 100 (1 se o valor for <= 0)
			float v = Needs::Get(Needs::kBody);
			float denom = s.washBodyInterval * 1440.0f;
			float pct = (v <= 0.0f || denom <= 0.0f) ? 1.0f : floorf(v / denom * 100.0f);
			int m = (int)fabsf(pct);
			if (m < 1) m = 1;
			if (m > 100) m = 100;
			SendBody(view, m, force);
		}
	}

	double StageSize(GFxMovieView *view, const char *path, double fallback)
	{
		GFxValue v;
		if (view->GetVariable(&v, path) && v.IsNumber() && v.GetNumber() > 1.0)
			return v.GetNumber();
		return fallback;
	}

	// Garante o estado de visibilidade: visivel se ativado e nenhum menu que pause o jogo estiver aberto.
	void UpdateVisibility(GFxMovieView *view, int id)
	{
		MenuManager *mm = MenuManager::GetSingleton();
		bool pauseOpen = mm && mm->numPauseGame != 0;
		bool show = Enabled(id) && !pauseOpen && !g_userHidden;
		if (view->GetVisible() != show)
			view->SetVisible(show);
	}

	void ApplyConfig(GFxMovieView *view, int id)
	{
		const WidgetCfg &c = kCfg[id];
		bool enabled = Enabled(id);

		GFxValue flag;
		flag.SetBoolean(enabled);
		bool okInit = view->Invoke("_root.widget.initNumbers", nullptr, &flag, 1);

		InvokeNumber(view, "_root.widget.setAlpha", ESDSettings::GetFloat(c.alphaKey, 90.0f));
		InvokeNumber(view, "_root.widget.setScale", ESDSettings::GetFloat(c.scaleKey, 80.0f));

		// As posicoes das definicoes sao relativas ao centro de uma area de 1280x720; converte-se para o palco do SWF
		// e limita-se a ficar dentro dele.
		double sw = StageSize(view, "Stage.width", 550.0);
		double sh = StageSize(view, "Stage.height", 400.0);
		double x = sw * 0.5 + ESDSettings::GetFloat(c.posXKey, c.defX) * sw / 1280.0;
		double y = sh * 0.5 + ESDSettings::GetFloat(c.posYKey, c.defY) * sh / 720.0;
		if (x < 20.0) x = 20.0;
		if (x > sw - 20.0) x = sw - 20.0;
		if (y < 20.0) y = 20.0;
		if (y > sh - 20.0) y = sh - 20.0;

		GFxValue pos[2];
		pos[0].SetNumber(x);
		pos[1].SetNumber(y);
		bool okPos = view->Invoke("_root.widget.setPosition", nullptr, pos, 2);

		GFxValue gx, gy, gv;
		gx.SetNumber(x);
		gy.SetNumber(y);
		gv.SetBoolean(true);
		view->SetVariable("_root.widget._x", gx);
		view->SetVariable("_root.widget._y", gy);
		view->SetVariable("_root.widget._visible", gv);

		UpdateVisibility(view, id);
		ApplyValues(view, id, true);

		_MESSAGE("ESD: widget %d configurado (ativado=%d, palco %gx%g, posicao %g,%g, initNumbers=%d, setPosition=%d, visivel=%d)",
			id, enabled ? 1 : 0, sw, sh, x, y, okInit ? 1 : 0, okPos ? 1 : 0, view->GetVisible() ? 1 : 0);
	}

	//-----------------------------------------------------------
	// menus
	//-----------------------------------------------------------
	class IWidgetMenu : public IMenu
	{
	public:
		using GRefCountBaseStatImpl::operator new;
		using GRefCountBaseStatImpl::operator delete;
		int m_id;

		IWidgetMenu(int id) : m_id(id)
		{
			if (LoadMovie(kCfg[id].swf, GFxMovieView::SM_ShowAll, 0.0f))
			{
				menuDepth = (UInt8)(2 + id);
				flags = kType_DoNotDeleteOnClose | kType_Unk0100 | kType_DoNotPreventGameSave;
				_MESSAGE("ESD: widget %d carregado (Interface\\%s.swf)", id, kCfg[id].swf);
			}
			else
				_MESSAGE("ERRO: nao consegui carregar Interface\\%s.swf", kCfg[id].swf);
		}

		virtual ~IWidgetMenu()
		{
			if (g_menu[m_id] == this) g_menu[m_id] = nullptr;
		}

		virtual UInt32 ProcessMessage(UIMessage *message) override
		{
			if (message->type == UIMessage::kMessage_Open)
			{
				g_menu[m_id] = this;
				Widget::QueueConfigure();
			}
			else if (message->type == UIMessage::kMessage_Close)
			{
				_MESSAGE("ESD: o jogo fechou o widget %d", m_id);
				if (g_menu[m_id] == this) g_menu[m_id] = nullptr;
			}
			return 2;
		}
	};

	class WidgetUITask : public UIDelegate
	{
		bool m_configure;
	public:
		WidgetUITask(bool configure) : m_configure(configure) {}

		TES_FORMHEAP_REDEFINE_NEW();

		void Run() override
		{
			for (int id = 0; id < 2; id++)
			{
				if (!g_menu[id]) continue;
				GFxMovieView *view = g_menu[id]->GetMovieView();
				if (!view) continue;
				if (m_configure) ApplyConfig(view, id);
				else
				{
					ApplyValues(view, id, false);
					UpdateVisibility(view, id);		// vigia: nunca fica escondido por engano
				}
			}
		}

		void Dispose() override { delete this; }
	};

	void QueueTask(bool configure)
	{
		const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
		const SKSETaskInterface *task = plugin ? plugin->GetInterface(SKSETaskInterface::Version_2) : nullptr;
		if (task)
			task->AddUITask(new WidgetUITask(configure));
	}

	// esconde os widgets enquanto houver menus que pausam o jogo (diario, inventario...), mostra-os ao fechar
	class WidgetMenuSink : public BSTEventSink<MenuOpenCloseEvent>
	{
	public:
		EventResult ReceiveEvent(MenuOpenCloseEvent *evn, BSTEventSource<MenuOpenCloseEvent> *src) override
		{
			if (!evn) return kEvent_Continue;
			MenuManager *mm = MenuManager::GetSingleton();
			if (!mm) return kEvent_Continue;

			for (int id = 0; id < 2; id++)
			{
				if (!g_menu[id]) continue;
				GFxMovieView *view = g_menu[id]->GetMovieView();
				if (!view) continue;

				if (evn->opening)
				{
					IMenu *menu = mm->GetMenu(evn->menuName);
					if (menu && (menu->flags & IMenu::kType_PauseGame) != 0)
						view->SetVisible(false);
				}
				else if (mm->numPauseGame == 0 && Enabled(id) && !g_userHidden)
					view->SetVisible(true);
			}
			return kEvent_Continue;
		}
	};

	WidgetMenuSink g_sink;
}

namespace Widget
{
	void Register()
	{
		if (g_registered) return;
		MenuManager *mm = MenuManager::GetSingleton();
		if (!mm) return;
		mm->Register(kCfg[0].menuName, []() -> IMenu * { return new IWidgetMenu(0); });
		mm->Register(kCfg[1].menuName, []() -> IMenu * { return new IWidgetMenu(1); });
		mm->GetMenuOpenCloseEventSource()->AddEventSink(&g_sink);
		g_registered = true;
		g_userHidden = ESDSettings::GetFloat("bWidgetUserHidden", 0.0f) != 0.0f;
		_MESSAGE("ESD: menus dos widgets registados");
	}

	void Open()
	{
		if (!g_registered) return;
		UIManager *ui = UIManager::GetSingleton();
		if (!ui) return;
		for (int id = 0; id < 2; id++)
			if (!g_menu[id] && Enabled(id))
				ui->AddMessage(kCfg[id].menuName, UIMessage::kMessage_Open, nullptr);
	}

	void QueueConfigure()
	{
		bool anyOpen = g_menu[0] || g_menu[1];
		if (anyOpen) QueueTask(true);
		Open();			// abre o que esteja ativado e ainda fechado (ao abrir, configura-se sozinho)
	}

	void QueueUpdate()
	{
		if (g_menu[0] || g_menu[1]) QueueTask(false);
	}

	// Se o jogo fechar um widget ativado (por exemplo em combate ou numa cena), volta a abri-lo.
	void Watchdog()
	{
		if (!g_registered || !Needs::IsReady()) return;		// nunca no menu principal nem durante o carregamento
		DWORD now = GetTickCount();
		if (g_lastReopen != 0 && now - g_lastReopen < 8000) return;
		for (int id = 0; id < 2; id++)
		{
			if (!g_menu[id] && Enabled(id))
			{
				g_lastReopen = now;
				_MESSAGE("ESD: widget %d estava fechado: a reabrir", id);
				Open();
				return;
			}
		}
	}

	void ToggleUserHidden()
	{
		g_userHidden = !g_userHidden;
		ESDSettings::SetFloat("bWidgetUserHidden", g_userHidden ? 1.0f : 0.0f);
		ESDSettings::Save();
		Overlay::Push(g_userHidden ? "ESD HUD: hidden" : "ESD HUD: shown");
		QueueTask(false);			// aplica a visibilidade ja
	}
}
