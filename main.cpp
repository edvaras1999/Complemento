#include <SKSE.h>
#include <SKSE/PluginAPI.h>
#include <SKSE/DebugLog.h>
#include <windows.h>

#include "Needs.h"
#include "Settings.h"
#include "Papyrus.h"

namespace
{
	const UInt32 kUniqueId = 0x4553444C;		// 'ESDL'

	//-------------------------------------------------------
	// tarefa periodica: corre na thread principal do jogo
	//-------------------------------------------------------
	class UpdateTask : public TaskDelegate
	{
	public:
		virtual void Run() override { Needs::Update(); }
		virtual void Dispose() override {}
	};

	UpdateTask g_task;

	DWORD WINAPI Worker(LPVOID)
	{
		for (;;)
		{
			Sleep(5000);
			const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
			const SKSETaskInterface *task = plugin ? plugin->GetInterface<SKSETaskInterface>() : NULL;
			if (task)
				task->AddTask(&g_task);
		}
		return 0;
	}

	//-------------------------------------------------------
	// cosave
	//-------------------------------------------------------
	void CosaveSave(SKSESerializationInterface *intfc)	{ Needs::Save(intfc); }
	void CosaveLoad(SKSESerializationInterface *intfc)	{ Needs::Load(intfc); }
	void CosaveRevert(SKSESerializationInterface *)		{ Needs::Revert(); }

	//-------------------------------------------------------
	// mensagens do SKSE
	//-------------------------------------------------------
	void OnMessage(SKSEMessagingInterface::Message *msg)
	{
		if (!msg) return;
		switch (msg->type)
		{
		case SKSEMessagingInterface::kMessage_PreLoadGame:
			Needs::SetReady(false);
			break;
		case SKSEMessagingInterface::kMessage_PostLoadGame:
		case SKSEMessagingInterface::kMessage_NewGame:
			Needs::ResyncClock();				// nao aplicar o tempo passado antes do carregamento
			Needs::SetReady(true);
			break;
		}
	}
}

class ESDPluginMain : public SKSEPlugin
{
public:
	virtual bool InitInstance() override
	{
		if (!Requires(kSKSEVersion_1_7_1, SKSETaskInterface::Version_2, SKSEPapyrusInterface::Version_1,
			SKSESerializationInterface::Version_1, SKSEMessagingInterface::Version_1))
		{
			gLog << "ERROR: your skse version is too old." << std::endl;
			return false;
		}

		SetName("ESDPlugin");
		SetVersion(1);
		return true;
	}

	virtual bool OnLoad() override
	{
		SKSEPlugin::OnLoad();

		const SKSEPapyrusInterface *papyrus = GetInterface(SKSEPapyrusInterface::Version_1);
		if (papyrus)
			papyrus->Register(&Papyrus::RegisterFunctions);

		const SKSESerializationInterface *ser = GetInterface(SKSESerializationInterface::Version_1);
		if (ser)
		{
			ser->SetUniqueID(kUniqueId);
			ser->SetRevertCallback(&CosaveRevert);
			ser->SetSaveCallback(&CosaveSave);
			ser->SetLoadCallback(&CosaveLoad);
		}

		const SKSEMessagingInterface *msg = GetInterface(SKSEMessagingInterface::Version_1);
		if (msg)
			msg->RegisterListener("SKSE", &OnMessage);

		_MESSAGE("");
		return true;
	}

	virtual void OnModLoaded() override
	{
		Needs::Init();
		ESDSettings::Load();
		CreateThread(NULL, 0, &Worker, NULL, 0, NULL);
	}
} thePlugin;
