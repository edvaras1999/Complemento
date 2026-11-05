#include <SKSE.h>
#include <SKSE/DebugLog.h>
#include <Skyrim/Forms/TESForm.h>
#include <Skyrim/Forms/BGSListForm.h>
#include <Skyrim/Forms/AlchemyItem.h>
#include <windows.h>

#include "Food.h"
#include "Needs.h"
#include "Settings.h"

namespace
{
	// IDs locais (no .esp do ESD) das listas. A ordem e o tipo seguem GetAndFindFoodType do original:
	// 0x853 comidas=1, 0x854 bebidas=2, 0x855 sopas=4, 0x856 comidas cruas=8, 0x857 "nem comida nem bebida"=0x10
	const UInt32 kLocalIds[5] = { 0x853, 0x854, 0x855, 0x856, 0x857 };
	const UInt32 kTypeOf[5]   = { 1, 2, 4, 8, 0x10 };
	// listas extra do mesmo plugin, usadas so para confirmar que encontramos o plugin certo
	const UInt32 kCheckIds[2] = { 0x87C, 0x87D };

	UInt32	g_lists[5] = { 0, 0, 0, 0, 0 };
	bool	g_found = false;

	BGSListForm *AsList(UInt32 formId)
	{
		TESForm *f = LookupFormByID(formId);
		if (f && f->formType == FormType::List)
			return static_cast<BGSListForm *>(f);
		return NULL;
	}

	// quantas das 7 listas conhecidas existem para esta base de FormID
	int Score(UInt32 base, UInt32 mask)
	{
		int n = 0;
		for (int i = 0; i < 5; i++)
			if (AsList(base | (kLocalIds[i] & mask))) n++;
		for (int i = 0; i < 2; i++)
			if (AsList(base | (kCheckIds[i] & mask))) n++;
		return n;
	}

	bool ListHas(BGSListForm *list, UInt32 formId)
	{
		if (!list) return false;
		for (BSTArray<TESForm *>::const_iterator it = list->forms.begin(); it != list->forms.end(); ++it)
		{
			TESForm *f = *it;
			if (f && f->formID == formId) return true;
		}
		if (list->addedForms)
		{
			for (BSTArray<UInt32>::const_iterator it = list->addedForms->begin(); it != list->addedForms->end(); ++it)
				if (*it == formId) return true;
		}
		return false;
	}

	UInt32 Classify(UInt32 formId)
	{
		for (int i = 0; i < 5; i++)
			if (ListHas(AsList(g_lists[i]), formId))
				return kTypeOf[i];
		return 0;
	}
}

namespace Food
{
	// Procura o plugin do ESD sem depender do indice de carga: tenta os plugins normais (indice 00..FD)
	// e os light (FE000000 | indice<<12), e fica com o que tem as listas do ESD.
	void FindLists()
	{
		g_found = false;
		int best = 0;
		UInt32 bestBase = 0, bestMask = 0;

		for (UInt32 idx = 0; idx <= 0xFD; idx++)
		{
			UInt32 base = idx << 24;
			int s = Score(base, 0x00FFFFFF);
			if (s > best) { best = s; bestBase = base; bestMask = 0x00FFFFFF; }
		}
		for (UInt32 idx = 0; idx <= 0xFFF; idx++)
		{
			UInt32 base = 0xFE000000 | (idx << 12);
			int s = Score(base, 0x00000FFF);
			if (s > best) { best = s; bestBase = base; bestMask = 0x00000FFF; }
		}

		if (best < 5)
		{
			_MESSAGE("ESD: listas de alimentos do ESD nao encontradas (melhor resultado %d de 7)", best);
			return;
		}
		for (int i = 0; i < 5; i++)
			g_lists[i] = bestBase | (kLocalIds[i] & bestMask);
		g_found = true;
		_MESSAGE("ESD: listas de alimentos encontradas (base %08X, %d de 7)", bestBase, best);
		for (int i = 0; i < 5; i++)
		{
			BGSListForm *l = AsList(g_lists[i]);
			_MESSAGE("ESD:   lista tipo %u -> %08X (%u itens)", kTypeOf[i], g_lists[i], l ? (unsigned)l->forms.size() : 0u);
		}
	}

	bool HasLists() { return g_found; }

	void OnEquip(UInt32 baseFormId)
	{
		if (!g_found || !baseFormId) return;

		TESForm *form = LookupFormByID(baseFormId);
		if (!form || form->formType != FormType::Potion) return;

		float weight = (float)form->GetWeight();
		if (!(weight > 0.0f)) return;
		AlchemyItem *item = static_cast<AlchemyItem *>(form);
		if (!item->IsFood()) return;

		UInt32 type = Classify(baseFormId);
		if (!type || type == 0x10) return;

		ESDSettings::Core s = ESDSettings::GetCore();
		float intFoods = ESDSettings::GetFloat("fIntensityFoods", 1.0f);
		float intDrinks = ESDSettings::GetFloat("fIntensityDrinks", 1.0f);

		Needs::ValueData data;
		for (int i = 0; i < 8; i++) data.v[i] = 0.0f;
		UInt32 mask = 0;

		// "bonus" de alimentos (IsFoodBonus no original) ainda nao implementado: 0
		const float bonus = 0.0f;

		switch (type)
		{
		case 1:		// comida: (peso + bonus) * intensidade * 1000
		case 8:		// comida crua: igual, mas so 10%
			if (s.hungry)
			{
				data.v[0] = (weight + bonus) * intFoods * 1000.0f * ((type == 8) ? 0.1f : 1.0f);
				mask |= Needs::kHungry;
			}
			break;
		case 2:		// bebida: peso * intensidade * 1500
			if (s.thirsty)
			{
				data.v[1] = weight * intDrinks * 1500.0f;
				mask |= Needs::kThirsty;
			}
			break;
		case 4:		// sopa: fome como comida + 25% de sede
			if (s.hungry)
			{
				data.v[0] = (weight + bonus) * intFoods * 1000.0f;
				mask |= Needs::kHungry;
			}
			if (s.thirsty)
			{
				data.v[1] = weight * intDrinks * 1500.0f * 0.25f;
				mask |= Needs::kThirsty;
			}
			break;
		}

		if (!mask) return;
		Needs::ModMask(mask, data, true);
		_MESSAGE("ESD: consumiu item %08X (tipo %u, peso %.2f): fome=%.0f sede=%.0f",
			baseFormId, type, weight, Needs::Get(Needs::kHungry), Needs::Get(Needs::kThirsty));
		// FALTA: mensagens, debuffs/widget, bonus de alimentos, itens especiais (HearthFires, garrafas).
	}
}
