#include <SKSE.h>
#include <SKSE/PluginAPI.h>
#include <SKSE/DebugLog.h>
#include <windows.h>

#include "Needs.h"
#include "Settings.h"
#include "Papyrus.h"
#include "Food.h"
#include <Skyrim/Events/ScriptEvent.h>
#include <Skyrim/Events/MenuEvent.h>
#include <Skyrim/Menus/MenuManager.h>
#include <Skyrim/Menus/UIStringHolder.h>
#include <cstring>
#include <Skyrim/Forms/PlayerCharacter.h>

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
	// equipar comida/bebida (TESEquipEvent). Layout 32 bits: ator, item base, ref original, id unico, equipado
	//-------------------------------------------------------
	struct ESDEquipEvent
	{
		TESObjectREFR	*reference;		// 00
		UInt32			baseObject;		// 04
		UInt32			originalRefr;	// 08
		UInt16			uniqueId;		// 0C
		UInt8			equipped;		// 0E
		UInt8			pad0F;
	};

	class EquipTask : public TaskDelegate
	{
		UInt32 m_id;
	public:
		EquipTask(UInt32 id) : m_id(id) {}
		virtual void Run() override { Food::OnEquip(m_id); }
		virtual void Dispose() override { delete this; }
	};

	class EquipSink : public BSTEventSink<TESEquipEvent>
	{
	public:
		EventResult ReceiveEvent(TESEquipEvent *evn, BSTEventSource<TESEquipEvent> *src) override
		{
			const ESDEquipEvent *e = (const ESDEquipEvent *)evn;
			if (e && e->equipped && e->baseObject && g_thePlayer && e->reference == (TESObjectREFR *)g_thePlayer)
			{
				const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
				const SKSETaskInterface *task = plugin ? plugin->GetInterface<SKSETaskInterface>() : NULL;
				if (task)
					task->AddTask(new EquipTask(e->baseObject));
			}
			return kEvent_Continue;
		}
	};

	EquipSink g_equipSink;
	bool g_equipSinkAdded = false;

	//-------------------------------------------------------
	// sono: inicio (guarda a duracao) e fecho do menu Dormir/Esperar (repoe o sono)
	//-------------------------------------------------------
	class SleepStartSink : public BSTEventSink<TESSleepStartEvent>
	{
	public:
		EventResult ReceiveEvent(TESSleepStartEvent *evn, BSTEventSource<TESSleepStartEvent> *src) override
		{
			if (evn)
				Needs::SleepStart(evn->endTime - evn->startTime);
			return kEvent_Continue;
		}
	};

	class WakeTask : public TaskDelegate
	{
	public:
		virtual void Run() override { Needs::SleepMenuClosed(); }
		virtual void Dispose() override { delete this; }
	};

	class MenuSink : public BSTEventSink<MenuOpenCloseEvent>
	{
	public:
		EventResult ReceiveEvent(MenuOpenCloseEvent *evn, BSTEventSource<MenuOpenCloseEvent> *src) override
		{
			if (evn && !evn->opening)
			{
				UIStringHolder *holder = UIStringHolder::GetSingleton();
				if (holder && evn->menuName.c_str() && holder->sleepWaitMenu.c_str() &&
					strcmp(evn->menuName.c_str(), holder->sleepWaitMenu.c_str()) == 0)
				{
					const SKSEPlugin *plugin = SKSEPlugin::GetSingleton();
					const SKSETaskInterface *task = plugin ? plugin->GetInterface<SKSETaskInterface>() : NULL;
					if (task)
						task->AddTask(new WakeTask());
				}
			}
			return kEvent_Continue;
		}
	};

	SleepStartSink	g_sleepSink;
	MenuSink		g_menuSink;
	bool			g_sleepSinkAdded = false;
	bool			g_menuSinkAdded = false;

	void AddSleepSinks()
	{
		if (!g_sleepSinkAdded)
		{
			g_sleepStartEventSource.AddEventSink(&g_sleepSink);
			g_sleepSinkAdded = true;
		}
		if (!g_menuSinkAdded)
		{
			MenuManager *mm = MenuManager::GetSingleton();
			if (mm)
			{
				mm->GetMenuOpenCloseEventSource()->AddEventSink(&g_menuSink);
				g_menuSinkAdded = true;
			}
		}
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
		case SKSEMessagingInterface::kMessage_DataLoaded:
			Food::FindLists();
			if (!g_equipSinkAdded) { g_equipEventSource.AddEventSink(&g_equipSink); g_equipSinkAdded = true; }
			AddSleepSinks();
			break;
		case SKSEMessagingInterface::kMessage_PreLoadGame:
			Needs::SetReady(false);
			break;
		case SKSEMessagingInterface::kMessage_PostLoadGame:
		case SKSEMessagingInterface::kMessage_NewGame:
			if (!Food::HasLists()) Food::FindLists();
			AddSleepSinks();
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
