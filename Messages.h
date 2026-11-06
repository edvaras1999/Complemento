#pragma once

// Mensagens do ESD (Messages_English.json etc.), mostradas como notificacao no canto do ecra.
// So aparecem se bIsEnableMessage estiver ativo (como no original).
namespace Messages
{
	void Load();									// le o ficheiro indicado em sLanguageFile
	void Show(const char *key);						// ex.: "sGetHungry"
	void ShowItem(const char *key, const char *itemName);	// substitui o "{}" pelo nome do item
}
