#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <SKSE/PapyrusEvents.h>
#include <Skyrim/SkyrimVM.h>
#include <cstdio>
#include <map>
#include <string>

#include "Messages.h"
#include "Settings.h"
#include "Overlay.h"

namespace
{
	std::map<std::string, std::string>	g_messages;
	bool								g_notifyFailed = false;

	// Argumentos de Debug.Notification(string): um BSFixedString empacotado como o SKSE faz nos eventos Papyrus
	class NotifyArgs : public SKSEScript::FunctionArguments<BSFixedString>
	{
	public:
		NotifyArgs(VMState *state, const BSFixedString &text) : SKSEScript::FunctionArguments<BSFixedString>(text)
		{
			m_state = state;
		}
	};

	int g_failures = 0;

	// Chama Debug.Notification(texto) pela maquina virtual do Papyrus (chamada estatica na fila da VM)
	void Notify(const char *text)
	{
		if (g_notifyFailed || !g_skyrimVM || !text || !*text)
			return;
		VMState *state = g_skyrimVM->GetState();
		if (!state)
			return;

		NotifyArgs args(state, BSFixedString(text));
		BSScript::BSScriptVariable result;
		if (!state->CallStaticFunction(BSFixedString("Debug"), BSFixedString("Notification"), &args, result))
		{
			_MESSAGE("ESD: a chamada a Debug.Notification falhou (%d)", ++g_failures);
			if (g_failures >= 5)
				g_notifyFailed = true;
		}
	}

	// mensagem no centro do ecra; se esse menu nao estiver aberto, usa a notificacao do canto
	void Deliver(const char *text)
	{
		if (Overlay::IsOpen())
			Overlay::Push(text);
		else
			Notify(text);
	}

	// le uma string JSON a partir de p (que aponta para a aspa de abertura)
	bool ReadString(const char *&p, const char *end, std::string &out)
	{
		out.clear();
		if (p >= end || *p != '"') return false;
		p++;
		while (p < end && *p != '"')
		{
			if (*p == '\\' && p + 1 < end)
			{
				p++;
				if (*p == 'n') { out += '\n'; p++; continue; }
			}
			out += *p++;
		}
		if (p >= end) return false;
		p++;
		return true;
	}
}

namespace Messages
{
	void Load()
	{
		g_messages.clear();
		std::string file = ESDSettings::GetString("sLanguageFile", "Messages_English.json");
		std::string path = "Data\\SKSE\\Plugins\\EatingSleepingDrinking\\" + file;

		FILE *f = fopen(path.c_str(), "rb");
		if (!f)
		{
			_MESSAGE("ESD: ficheiro de mensagens nao encontrado: %s", path.c_str());
			return;
		}
		std::string text;
		char buf[2048];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
		fclose(f);

		const char *p = text.c_str();
		const char *end = p + text.size();
		std::string key, value;
		// pares "chave": "valor"
		while (p < end)
		{
			while (p < end && *p != '"') p++;
			if (!ReadString(p, end, key)) break;
			while (p < end && *p != '"' && *p != '}') p++;
			if (p >= end || *p != '"') break;
			if (!ReadString(p, end, value)) break;
			g_messages[key] = value;
		}
		_MESSAGE("ESD: %u mensagens carregadas de %s", (unsigned)g_messages.size(), file.c_str());
	}

	void Show(const char *key)
	{
		if (ESDSettings::GetFloat("bIsEnableMessage", 0.0f) == 0.0f) return;
		std::map<std::string, std::string>::iterator it = g_messages.find(key);
		if (it == g_messages.end()) return;
		Deliver(it->second.c_str());
	}

	void ShowItem(const char *key, const char *itemName)
	{
		if (ESDSettings::GetFloat("bIsEnableMessage", 0.0f) == 0.0f) return;
		std::map<std::string, std::string>::iterator it = g_messages.find(key);
		if (it == g_messages.end()) return;
		std::string s = it->second;
		size_t pos = s.find("{}");
		if (pos != std::string::npos) s.replace(pos, 2, itemName ? itemName : "");
		Deliver(s.c_str());
	}
}
