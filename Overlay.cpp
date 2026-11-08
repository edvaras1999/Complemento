#include <SKSE.h>
#include <SKSE/PluginAPI.h>
#include <SKSE/DebugLog.h>
#include <SKSE/GameMenus.h>
#include <SKSE/Scaleform.h>
#include <Skyrim/Menus/MenuManager.h>
#include <Skyrim/Menus/UIManager.h>
#include <Skyrim/Menus/UIMessage.h>
#include <windows.h>
#include <string>
#include <vector>

#include "Overlay.h"
#include "Settings.h"

namespace
{
	const char *kMenuName = "ESD Message Menu";
	const char *kSwfPath = "EatingSleepingDrinking\\ESDMessage";		// Data\Interface\EatingSleepingDrinking\ESDMessage.swf

	DWORD LifeMs()
	{
		float s = ESDSettings::GetFloat("fMessageSeconds", 4.5f);
		if (s < 1.0f) s = 1.0f;
		if (s > 30.0f) s = 30.0f;
		return (DWORD)(s * 1000.0f);
	}
	DWORD FadeStartMs() { DWORD l = LifeMs(); return l > 1200 ? l - 900 : l / 2; }	// ultimos ~0,9 s: desvanece
	const size_t kMaxLines = 4;

	struct Line
	{
		std::string	text;
		DWORD		start;
	};

	class MessageMenu;
	MessageMenu			*g_menu = nullptr;
	bool				g_registered = false;
	CRITICAL_SECTION	g_lock;
	bool				g_lockInit = false;
	std::vector<Line>	g_lines;

	std::string Escape(const std::string &s)
	{
		std::string r;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] == '&') r += "&amp;";
			else if (s[i] == '<') r += "&lt;";
			else if (s[i] == '>') r += "&gt;";
			else r += s[i];
		}
		return r;
	}

	std::string Html(const std::vector<Line> &lines, const char *color)
	{
		std::string h = "<p align=\"center\"><font face=\"$EverywhereMediumFont\" size=\"24\" color=\"";
		h += color;
		h += "\">";
		for (size_t i = 0; i < lines.size(); i++)
		{
			if (i) h += "<br>";
			h += Escape(lines[i].text);
		}
		h += "</font></p>";
		return h;
	}

	class MessageMenu : public IMenu
	{
		std::string	m_lastHtml;
		int			m_lastAlpha;
		float		m_lastOffset;
	public:
		using GRefCountBaseStatImpl::operator new;
		using GRefCountBaseStatImpl::operator delete;

		MessageMenu() : m_lastAlpha(-1), m_lastOffset(-99999.0f)
		{
			if (LoadMovie(kSwfPath, GFxMovieView::SM_ShowAll, 0.0f))
			{
				menuDepth = 4;
				flags = kType_DoNotDeleteOnClose | kType_Unk0100 | kType_DoNotPreventGameSave;
				_MESSAGE("ESD: menu de mensagens carregado (Interface\\%s.swf)", kSwfPath);
			}
			else
				_MESSAGE("ERRO: nao consegui carregar Interface\\%s.swf", kSwfPath);
		}

		virtual ~MessageMenu()
		{
			if (g_menu == this) g_menu = nullptr;
		}

		virtual UInt32 ProcessMessage(UIMessage *message) override
		{
			if (message->type == UIMessage::kMessage_Open)
			{
				g_menu = this;
				_MESSAGE("ESD: menu de mensagens aberto");
			}
			else if (message->type == UIMessage::kMessage_Close)
			{
				if (g_menu == this) g_menu = nullptr;
			}
			return 2;
		}

		// chamado a cada frame: apaga linhas antigas, atualiza o texto e a transparencia
		virtual void NextFrame(UInt32 arg0, UInt32 arg1) override
		{
			IMenu::NextFrame(arg0, arg1);
			if (!g_lockInit || !view) return;

			DWORD now = GetTickCount();
			std::vector<Line> lines;
			EnterCriticalSection(&g_lock);
			for (size_t i = 0; i < g_lines.size();)
			{
				if (now - g_lines[i].start > LifeMs()) g_lines.erase(g_lines.begin() + i);
				else i++;
			}
			lines = g_lines;
			LeaveCriticalSection(&g_lock);

			std::string html;
			int alpha = 100;
			if (!lines.empty())
			{
				DWORD age = now - lines.back().start;
				DWORD life = LifeMs(), fade = FadeStartMs();
				if (age > fade)
				{
					alpha = 100 - (int)((age - fade) * 100 / (life - fade));
					if (alpha < 0) alpha = 0;
				}
				html = Html(lines, "#FFFFFF");
			}

			// posicao vertical (configuravel): 298 e a posicao base do campo no SWF (area de 720 de altura)
			float offset = ESDSettings::GetFloat("fMessageOffsetY", 0.0f);
			if (offset != m_lastOffset)
			{
				GFxValue y1, y2;
				y1.SetNumber(298.0 + offset);
				y2.SetNumber(300.0 + offset);
				view->SetVariable("_root.msg._y", y1);
				view->SetVariable("_root.shadow._y", y2);
				m_lastOffset = offset;
			}

			if (html != m_lastHtml)
			{
				std::string shadow = lines.empty() ? std::string() : Html(lines, "#000000");
				GFxValue v;
				v.SetString(html.c_str());
				view->SetVariable("_root.msg.htmlText", v);
				GFxValue s;
				s.SetString(shadow.c_str());
				view->SetVariable("_root.shadow.htmlText", s);
				m_lastHtml = html;
			}
			if (alpha != m_lastAlpha)
			{
				GFxValue a;
				a.SetNumber((double)alpha);
				view->SetVariable("_root.msg._alpha", a);
				view->SetVariable("_root.shadow._alpha", a);
				m_lastAlpha = alpha;
			}
		}
	};
}

namespace Overlay
{
	void Register()
	{
		if (g_registered) return;
		if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
		MenuManager *mm = MenuManager::GetSingleton();
		if (!mm) return;
		mm->Register(kMenuName, []() -> IMenu * { return new MessageMenu(); });
		g_registered = true;
		_MESSAGE("ESD: menu de mensagens registado");
	}

	void Open()
	{
		if (!g_registered || g_menu) return;
		UIManager *ui = UIManager::GetSingleton();
		if (ui)
			ui->AddMessage(kMenuName, UIMessage::kMessage_Open, nullptr);
	}

	bool IsOpen()
	{
		return g_menu != nullptr;
	}

	void Push(const char *utf8Text)
	{
		if (!g_lockInit || !utf8Text || !*utf8Text) return;
		EnterCriticalSection(&g_lock);
		Line l;
		l.text = utf8Text;
		l.start = GetTickCount();
		g_lines.push_back(l);
		while (g_lines.size() > kMaxLines)
			g_lines.erase(g_lines.begin());
		LeaveCriticalSection(&g_lock);
	}
}
