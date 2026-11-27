// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_Policy.h"

#include "Malterlib_Git_Policy_RuleParsing.hpp"

namespace NMib::NGit
{
	namespace
	{
		TCUnsafeFuture<TCMap<CStr, CGitHostingProvider::CGenericRuleset>> fg_ParseGenericRules(CEJsonSorted const &_Rules)
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

	TCFuture<bool> CGitPolicyActor::f_ApplyPolicy_GenericRules(CApplyPolicyContext _Context, CEJsonSorted _Rules)
	{
		co_return co_await fp_ApplyPolicy_GenericRules(_Context, _Rules);
	}

	/// Returns false when it skipped a ruleset _Rules lists, because updating it would lose what the policy can't express
	TCUnsafeFuture<bool> CGitPolicyActor::fp_ApplyPolicy_GenericRules(CApplyPolicyContext const &_Context, CEJsonSorted const &_Rules)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto WantedGenericRules = co_await fg_ParseGenericRules(_Rules);

		auto CurrentRulesets = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetGenericRulesets, _Context.m_Repository);

		TCSet<CStr> AlreadyCreated;
		bool bAllApplied = true;

		for (auto &RulesetEntry : CurrentRulesets.f_Entries())
		{
			auto &RuleID = RulesetEntry.f_Key();
			auto &Ruleset = RulesetEntry.f_Value();

			if (!Ruleset.m_Name)
				continue;

			auto &Name = *Ruleset.m_Name;
			AlreadyCreated[Name];

			if (auto *pWantedRuleset = WantedGenericRules.f_FindEqual(Name))
			{
				if (!Ruleset.m_UnmanageableReasons.f_IsEmpty())
				{
					if (_Context.m_fOnUnmanageable)
						co_await _Context.m_fOnUnmanageable(Name, CStr::fs_Join(Ruleset.m_UnmanageableReasons, "\n"));

					bAllApplied = false;
					continue;
				}

				// The policy can't express these, so it can't mean to remove them. Populating drops those that belong to rules the policy
				// removes, since an update can't write them without their rules.
				pWantedRuleset->m_Unmodelled = Ruleset.m_Unmodelled;

				// An update that sends one ref name filter sends both, so one the policy leaves out keeps what the ruleset has. With neither the
				// update leaves the conditions out, so that it doesn't restore filters changed since they were read.
				if (pWantedRuleset->m_IncludeRefNames && !pWantedRuleset->m_ExcludeRefNames)
					pWantedRuleset->m_ExcludeRefNames = Ruleset.m_ExcludeRefNames;
				else if (pWantedRuleset->m_ExcludeRefNames && !pWantedRuleset->m_IncludeRefNames)
					pWantedRuleset->m_IncludeRefNames = Ruleset.m_IncludeRefNames;

				*pWantedRuleset = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_PopulateGenericRulesetIDs, _Context.m_Repository, fg_Move(*pWantedRuleset));

				// A pull request rule that doesn't say which merge methods it allows keeps the ones it has
				if (pWantedRuleset->m_Rules && Ruleset.m_Rules)
				{
					for (auto &WantedRule : *pWantedRuleset->m_Rules)
					{
						if (!WantedRule.f_IsOfType<CGitHostingProvider::CGenericRule_PullRequest>())
							continue;

						auto &WantedPullRequest = WantedRule.f_GetAsType<CGitHostingProvider::CGenericRule_PullRequest>();
						if (WantedPullRequest.m_AllowedMergeMethods)
							continue;

						for (auto &CurrentRule : *Ruleset.m_Rules)
						{
							if (CurrentRule.f_IsOfType<CGitHostingProvider::CGenericRule_PullRequest>())
								WantedPullRequest.m_AllowedMergeMethods = CurrentRule.f_GetAsType<CGitHostingProvider::CGenericRule_PullRequest>().m_AllowedMergeMethods;
						}
					}

					pWantedRuleset->m_Rules->f_Sort();
				}

				CStr UpdatedValues;
				if (Ruleset.f_IsUpdated(*pWantedRuleset, UpdatedValues))
				{
					// Without a rule list the update leaves every rule as it is, so it neither keeps nor removes any
					if (pWantedRuleset->m_Rules)
					{
						auto &Kept = pWantedRuleset->m_Unmodelled;
						if (!Kept.f_IsEmpty())
							fg_AddStrSep(UpdatedValues, "Kept, since the policy can't express them: {}"_f << Kept.f_GetDescription(), "\n");

						CGitHostingProvider::CGenericRulesetUnmodelled Removed;
						for (auto &Parameters : Ruleset.m_Unmodelled.m_RuleParameters)
						{
							auto &RuleType = Ruleset.m_Unmodelled.m_RuleParameters.fs_GetKey(Parameters);
							if (!Kept.m_RuleParameters.f_FindEqual(RuleType))
								Removed.m_RuleParameters[RuleType] = Parameters;
						}

						if (!Removed.f_IsEmpty())
							fg_AddStrSep(UpdatedValues, "Removed with their rules: {}"_f << Removed.f_GetDescription(), "\n");
					}

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

			// Pretending looks up the IDs as applying does, so it fails where applying would, before anything is deleted
			if (_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_PopulateGenericRulesetIDs, _Context.m_Repository, WantedRuleset);
			else
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_CreateGenericRuleset, _Context.m_Repository, WantedRuleset);

			if (_Context.m_fOnCreate)
				co_await _Context.m_fOnCreate(WantedRulesetEntry.f_Key(), CStr());
		}

		co_return bAllApplied;
	}

	TCUnsafeFuture<void> CGitPolicyActor::fp_RemoveOther_GenericRules(CApplyPolicyContext const &_Context, CEJsonSorted const &_Rules)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto WantedGenericRules = co_await fg_ParseGenericRules(_Rules);

		auto CurrentNames = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetGenericRulesetNames, _Context.m_Repository);

		for (auto &Name : CurrentNames)
		{
			if (WantedGenericRules.f_FindEqual(Name))
				continue;

			if (!_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_DeleteGenericRuleset, _Context.m_Repository, CurrentNames.fs_GetKey(Name));

			if (_Context.m_fOnDelete)
				co_await _Context.m_fOnDelete(Name, CStr());
		}

		co_return {};
	}
}
