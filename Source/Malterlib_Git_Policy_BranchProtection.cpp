// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_Policy.h"
#include "Malterlib_Git_Policy_RuleParsing.hpp"

namespace NMib::NGit
{
	namespace
	{
		TCUnsafeFuture<TCMap<CStr, CGitHostingProvider::CBranchProtectionRule>> fg_ParseBranchProtectionRules(CEJsonSorted const &_BranchProtection)
		{
			TCMap<CStr, CGitHostingProvider::CBranchProtectionRule> OutRules;

			for (auto &Rule : _BranchProtection.f_Object())
			{
				auto &RulePattern = Rule.f_Name();

				if (RulePattern.f_IsEmpty())
					co_return DMibErrorInstance("Branch protection patterns cannot be an empty string");

				auto &OutRule = OutRules[RulePattern];

				OutRule.m_Pattern = RulePattern;
				auto &RuleValue = Rule.f_Value();

				co_await fg_ParseRuleSetting(RuleValue, "Creator", OutRule.m_Creator);
				co_await fg_ParseRuleSetting(RuleValue, "PushAllowances", OutRule.m_PushAllowances);
				co_await fg_ParseRuleSetting(RuleValue, "ReviewDismissalAllowances", OutRule.m_ReviewDismissalAllowances);
				co_await fg_ParseRuleSetting(RuleValue, "BypassForcePushAllowances", OutRule.m_BypassForcePushAllowances);
				co_await fg_ParseRuleSetting(RuleValue, "BypassPullRequestAllowances", OutRule.m_BypassPullRequestAllowances);
				co_await fg_ParseRuleSetting(RuleValue, "RequiredStatusCheckContexts", OutRule.m_RequiredStatusCheckContexts);
				co_await fg_ParseRuleSetting(RuleValue, "RequiredStatusChecks", OutRule.m_RequiredStatusChecks);
				co_await fg_ParseRuleSetting(RuleValue, "RequiredApprovingReviewCount", OutRule.m_RequiredApprovingReviewCount);
				co_await fg_ParseRuleSetting(RuleValue, "AllowsForcePushes", OutRule.m_AllowsForcePushes);
				co_await fg_ParseRuleSetting(RuleValue, "AllowsDeletions", OutRule.m_AllowsDeletions);
				co_await fg_ParseRuleSetting(RuleValue, "BlocksCreations", OutRule.m_BlocksCreations);
				co_await fg_ParseRuleSetting(RuleValue, "DismissesStaleReviews", OutRule.m_DismissesStaleReviews);
				co_await fg_ParseRuleSetting(RuleValue, "IsAdminEnforced", OutRule.m_IsAdminEnforced);
				co_await fg_ParseRuleSetting(RuleValue, "RequiresApprovingReviews", OutRule.m_RequiresApprovingReviews);
				co_await fg_ParseRuleSetting(RuleValue, "RequiresCodeOwnerReviews", OutRule.m_RequiresCodeOwnerReviews);
				co_await fg_ParseRuleSetting(RuleValue, "RequiresCommitSignatures", OutRule.m_RequiresCommitSignatures);
				co_await fg_ParseRuleSetting(RuleValue, "RequiresConversationResolution", OutRule.m_RequiresConversationResolution);
				co_await fg_ParseRuleSetting(RuleValue, "RequiresLinearHistory", OutRule.m_RequiresLinearHistory);
				co_await fg_ParseRuleSetting(RuleValue, "RequiresStatusChecks", OutRule.m_RequiresStatusChecks);
				co_await fg_ParseRuleSetting(RuleValue, "RequiresStrictStatusChecks", OutRule.m_RequiresStrictStatusChecks);
				co_await fg_ParseRuleSetting(RuleValue, "RestrictsPushes", OutRule.m_RestrictsPushes);
				co_await fg_ParseRuleSetting(RuleValue, "RestrictsReviewDismissals", OutRule.m_RestrictsReviewDismissals);
			}

			co_return fg_Move(OutRules);
		};
	}

	TCFuture<void> CGitPolicyActor::f_ApplyPolicy_BranchProtection(CApplyPolicyContext _Context, CEJsonSorted _BranchProtection)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto WantedBranchProtectionRules = co_await fg_ParseBranchProtectionRules(_BranchProtection);

		auto CurrentRules = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetBranchProtectionRules, _Context.m_Repository);

		TCSet<CStr> AlreadyCreated;

		for (auto &Rule : CurrentRules)
		{
			auto &RuleID = CurrentRules.fs_GetKey(Rule);
			auto &Pattern = *Rule.m_Pattern;
			AlreadyCreated[Pattern];

			if (auto *pWantedRule = WantedBranchProtectionRules.f_FindEqual(Pattern))
			{
				CStr UpdatedValues;
				if (Rule.f_IsUpdated(*pWantedRule, UpdatedValues))
				{
					if (!_Context.m_bPretend)
						co_await _Context.m_HostingProvider(&CGitHostingProvider::f_UpdateBranchProtectionRule, _Context.m_Repository, RuleID, *pWantedRule);

					if (_Context.m_fOnUpdate)
						co_await _Context.m_fOnUpdate(Pattern, UpdatedValues);
				}
			}
		}

		for (auto &WantedRule : WantedBranchProtectionRules)
		{
			if (AlreadyCreated.f_FindEqual(WantedBranchProtectionRules.fs_GetKey(WantedRule)))
				continue;

			if (!_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_CreateBranchProtectionRule, _Context.m_Repository, WantedRule);

			if (_Context.m_fOnCreate)
				co_await _Context.m_fOnCreate(*WantedRule.m_Pattern, CStr());
		}

		co_return {};
	}
}
