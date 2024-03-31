// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Git/HostingProvider>

#include "Malterlib_Git_App_GitPolicyManager.h"
#include "Malterlib_Git_App_GitPolicyManager_RuleParsing.hpp"

namespace NMib::NGit::NGitPolicyManager
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
	
	TCFuture<void> CGitPolicyManagerActor::fp_ApplyPolicies_GenericRules
		(
			CEJSONSorted _Rules
			, CStr _Repository
			, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider
			, CStr _PolicyName
		)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto Auditor = mp_State.f_Auditor();

		auto WantedGenericRules = co_await fg_ParseGenericRules(_Rules);

		auto CurrentRulesets = co_await _HostingProvider(&CGitHostingProvider::f_GetGenericRulesets, _Repository);

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
				*pWantedRuleset = co_await _HostingProvider(&CGitHostingProvider::f_PopulateGenericRulesetIDs, _Repository, fg_Move(*pWantedRuleset));

				CStr UpdatedValues;
				if (Ruleset.f_IsUpdated(*pWantedRuleset, UpdatedValues))
				{
					if (!mp_bPretend)
						co_await _HostingProvider(&CGitHostingProvider::f_UpdateGenericRuleset, _Repository, RuleID, *pWantedRuleset);

					Auditor.f_Warning
						(
							"{}Appying policy '{}' resulted in updated generic ruleset for branch pattern '{}' on '{}'. Updated values:\n{}"_f
							<< fp_PretendDescription()
							<< _PolicyName
							<< Name
							<< _Repository
							<< UpdatedValues.f_Indent("    ")
						)
					;
				}
			}
		}

		for (auto &WantedRulesetEntry : WantedGenericRules.f_Entries())
		{
			if (AlreadyCreated.f_FindEqual(WantedRulesetEntry.f_Key()))
				continue;

			auto &WantedRuleset = WantedRulesetEntry.f_Value();

			if (!mp_bPretend)
				co_await _HostingProvider(&CGitHostingProvider::f_CreateGenericRuleset, _Repository, WantedRuleset);

			Auditor.f_Info
				(
					"{}Appying policy '{}' resulted in created generic ruleset for branch pattern '{}' on '{}'"_f
					<< fp_PretendDescription()
					<< _PolicyName
					<< WantedRulesetEntry.f_Key()
					<< _Repository
				)
			;
		}

		co_return {};
	}
}
