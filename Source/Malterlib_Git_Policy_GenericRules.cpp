// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_Policy.h"

#include "Malterlib_Git_Policy_RuleParsing.hpp"

namespace NMib::NGit
{
	namespace
	{
		TCFuture<TCMap<CStr, CGitHostingProvider::CGenericRuleset>> fg_ParseGenericRules(CEJSONSorted _Rules)
		{
			TCMap<CStr, CGitHostingProvider::CGenericRuleset> OutRules;

			for (auto &Rule : _Rules.f_Object())
			{
				auto &Name = Rule.f_Name();

				if (Name.f_IsEmpty())
					co_return DMibErrorInstance("Generic rule name cannot be an empty string");

				auto &OutRule = OutRules[Name];

				auto &RuleValue = Rule.f_Value();

				OutRule.m_Name = Name;

				co_await fg_ParseRuleSetting(RuleValue, "Target", OutRule.m_Target);
				co_await fg_ParseRuleSetting(RuleValue, "IncludeRefNames", OutRule.m_IncludeRefNames);
				co_await fg_ParseRuleSetting(RuleValue, "ExcludeRefNames", OutRule.m_ExcludeRefNames);
				co_await fg_ParseRuleSetting(RuleValue, "BypassActors", OutRule.m_BypassActors);
				co_await fg_ParseRuleSetting(RuleValue, "Rules", OutRule.m_Rules);
				co_await fg_ParseRuleSetting(RuleValue, "Enforcement", OutRule.m_Enforcement);

			}

			co_return fg_Move(OutRules);
		};
	}

	TCFuture<void> CGitPolicyActor::f_ApplyPolicy_GenericRules(CApplyPolicyContext &&_Context, CEJSONSorted &&_Rules)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto WantedGenericRules = co_await fg_ParseGenericRules(_Rules);

		auto CurrentRulesets = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetGenericRulesets, _Context.m_Repository);

		TCSet<CStr> AlreadyCreated;

		for (auto &RulesetEntry : CurrentRulesets.f_Entries())
		{
			auto &RuleID = RulesetEntry.f_Key();
			auto &Ruleset = RulesetEntry.f_Value();

			if (!Ruleset.m_Name)
				continue;;

			auto &Name = *Ruleset.m_Name;
			AlreadyCreated[Name];

			if (auto *pWantedRuleset = WantedGenericRules.f_FindEqual(Name))
			{
				*pWantedRuleset = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_PopulateGenericRulesetIDs, _Context.m_Repository, fg_Move(*pWantedRuleset));

				CStr UpdatedValues;
				if (Ruleset.f_IsUpdated(*pWantedRuleset, UpdatedValues))
				{
					if (!_Context.m_bPretend)
						co_await _Context.m_HostingProvider(&CGitHostingProvider::f_UpdateGenericRuleset, _Context.m_Repository, RuleID, *pWantedRuleset);

					if (_Context.m_fOnUpdate)
						co_await _Context.m_fOnUpdate(Name, UpdatedValues);
				}
			}
		}

		for (auto &WantedRulesetEntry : WantedGenericRules.f_Entries())
		{
			if (AlreadyCreated.f_FindEqual(WantedRulesetEntry.f_Key()))
				continue;

			auto &WantedRuleset = WantedRulesetEntry.f_Value();

			if (!_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_CreateGenericRuleset, _Context.m_Repository, WantedRuleset);

			if (_Context.m_fOnCreate)
				co_await _Context.m_fOnCreate(WantedRulesetEntry.f_Key(), CStr());
		}

		co_return {};
	}
}
