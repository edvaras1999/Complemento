#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <SKSE/PapyrusInvoke.h>
#include <Skyrim/SkyrimVM.h>
#include <cstdio>
#include <map>
#include <string>

#include "Messages.h"
#include "Settings.h"

namespace
{
	std::map<std::string, std::string>	g_messages;
	bool								g_notifyFailed = false;

	typedef _NativeFunction<BSScript::StaticFunctionTag, void, BSFixedString> NotificationNative;

	// Debug.Notification(string) - funcao nativa do jogo
	void Notify(const char *text)
	{
		if (g_notifyFailed || !g_skyrimVM || !text || !*text)
			return;
		VMState *state = g_skyrimVM->GetState();
		if (!state)
			return;
		NotificationNative *fn = (NotificationNative *)SKSEScript::GetFunction(state, BSFixedString("Debug"), BSFixedString("Notification"));
		if (!fn || !fn->Validate(state))
		{
			_MESSAGE("ESD: nao encontrei Debug.Notification (funcao=%p); as mensagens ficam desligadas", fn);
			g_notifyFailed = true;
			return;
		}
		(*fn)(state, 0, NULL, BSFixedString(text));
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
		Notify(it->second.c_str());
	}

	void ShowItem(const char *key, const char *itemName)
	{
		if (ESDSettings::GetFloat("bIsEnableMessage", 0.0f) == 0.0f) return;
		std::map<std::string, std::string>::iterator it = g_messages.find(key);
		if (it == g_messages.end()) return;
		std::string s = it->second;
		size_t pos = s.find("{}");
		if (pos != std::string::npos) s.replace(pos, 2, itemName ? itemName : "");
		Notify(s.c_str());
	}
}
