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

#include "Widget.h"
#include "Needs.h"
#include "Settings.h"

namespace
{
	const char *kMenuName = "ESD Widget Menu";
	const char *kSwfPath = "EatingSleepingDrinking\\KNNWidgetMeter";		// Data\Interface\EatingSleepingDrinking\KNNWidgetMeter.swf

	class WidgetMenu;
	WidgetMenu	*g_menu = nullptr;
	bool		g_hiddenByMenu = false;
	bool		g_registered = false;
	int			g_last[3] = { 9999, 9999, 9999 };		// ultimo valor enviado (com sinal) de fome, sede, sono

	bool Enabled()
	{
		return ESDSettings::GetFloat("bIsEnableWidgetPrimary", 0.0f) != 0.0f;
	}

	// Converte o valor de uma necessidade em (percentagem 1..100, lado positivo/negativo), como o original:
	//  acima do limiar  -> (v - limiar) / escalaSuperior * 100 (lado positivo)
	//  igual ao limiar  -> 1 (lado positivo)
	//  abaixo do limiar -> (v - limiar) / denominador * 100 (lado negativo)
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

	void InvokeNumber(GFxMovieView *view, const char *fn, double v)
	{
		GFxValue arg;
		arg.SetNumber(v);
		if (!view->Invoke(fn, nullptr, &arg, 1))
			_MESSAGE("ESD: falhou ao chamar %s no widget", fn);
	}

	void ApplyValues(GFxMovieView *view, bool force)
	{
		ESDSettings::Core s = ESDSettings::GetCore();
		float dayToDie = ESDSettings::GetFloat("fDayToDie", 0.0f);
		int mag;
		bool pos;

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

	void ApplyConfig(GFxMovieView *view)
	{
		bool enabled = Enabled();

		GFxValue flag;
		flag.SetBoolean(enabled);
		view->Invoke("_root.widget.initNumbers", nullptr, &flag, 1);

		InvokeNumber(view, "_root.widget.setAlpha", ESDSettings::GetFloat("fPrimaryAlpha", 90.0f));
		InvokeNumber(view, "_root.widget.setScale", ESDSettings::GetFloat("fPrimaryScale", 80.0f));

		GFxValue pos[2];
		pos[0].SetNumber(ESDSettings::GetFloat("fPrimaryPosX", -650.0f));
		pos[1].SetNumber(ESDSettings::GetFloat("fPrimaryPosY", 330.0f));
		view->Invoke("_root.widget.setPosition", nullptr, pos, 2);

		view->SetVisible(enabled && !g_hiddenByMenu);
		ApplyValues(view, true);
	}

	//-----------------------------------------------------------
	// menu
	//-----------------------------------------------------------
	class WidgetMenu : public IMenu
	{
	public:
		using GRefCountBaseStatImpl::operator new;
		using GRefCountBaseStatImpl::operator delete;

		WidgetMenu()
		{
			if (LoadMovie(kSwfPath, GFxMovieView::SM_ShowAll, 0.0f))
			{
				menuDepth = 2;
				flags = kType_DoNotDeleteOnClose | kType_DoNotPreventGameSave;
				_MESSAGE("ESD: widget carregado (Interface\\%s.swf)", kSwfPath);
			}
			else
				_MESSAGE("ERRO: nao consegui carregar Interface\\%s.swf", kSwfPath);
		}

		virtual ~WidgetMenu()
		{
			if (g_menu == this) g_menu = nullptr;
		}

		virtual UInt32 ProcessMessage(UIMessage *message) override
		{
			if (message->type == UIMessage::kMessage_Open)
			{
				g_menu = this;
				Widget::QueueConfigure();
			}
			else if (message->type == UIMessage::kMessage_Close)
			{
				if (g_menu == this) g_menu = nullptr;
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
			if (!g_menu) return;
			GFxMovieView *view = g_menu->GetMovieView();
			if (!view) return;
			if (m_configure) ApplyConfig(view);
			else ApplyValues(view, false);
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

	// esconde o widget enquanto houver menus que pausam o jogo (diario, inventario...), como o QuickLoot
	class WidgetMenuSink : public BSTEventSink<MenuOpenCloseEvent>
	{
	public:
		EventResult ReceiveEvent(MenuOpenCloseEvent *evn, BSTEventSource<MenuOpenCloseEvent> *src) override
		{
			if (!g_menu || !evn) return kEvent_Continue;
			GFxMovieView *view = g_menu->GetMovieView();
			MenuManager *mm = MenuManager::GetSingleton();
			if (!view || !mm) return kEvent_Continue;

			if (evn->opening)
			{
				IMenu *menu = mm->GetMenu(evn->menuName);
				if (menu && (menu->flags & IMenu::kType_PauseGame) != 0)
				{
					g_hiddenByMenu = true;
					view->SetVisible(false);
				}
			}
			else if (mm->numPauseGame == 0)
			{
				g_hiddenByMenu = false;
				if (Enabled())
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
		mm->Register(kMenuName, []() -> IMenu * { return new WidgetMenu(); });
		mm->GetMenuOpenCloseEventSource()->AddEventSink(&g_sink);
		g_registered = true;
		_MESSAGE("ESD: menu do widget registado");
	}

	void Open()
	{
		if (!g_registered || g_menu || !Enabled()) return;
		UIManager *ui = UIManager::GetSingleton();
		if (ui)
			ui->AddMessage(kMenuName, UIMessage::kMessage_Open, nullptr);
	}

	void QueueConfigure()
	{
		if (g_menu) QueueTask(true);
		else Open();
	}

	void QueueUpdate()
	{
		if (g_menu) QueueTask(false);
	}
}
