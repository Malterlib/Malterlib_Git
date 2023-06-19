// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Git/HostingProvider>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	namespace
	{
		TCFuture<void> fg_ParseRuleSetting(CEJSONSorted const &_Rule, CStr const &_Name, auto &o_Value)
		{
			co_await ECoroutineFlag_CaptureExceptions;

			auto *pValue = _Rule.f_GetMember(_Name);
			if (!pValue)
				co_return {};

			auto &OutValue = o_Value.template f_Set<1>();

			using CType = typename TCRemoveReferenceAndQualifiers<decltype(OutValue)>::CType;

			if constexpr (TCIsSame<CType, CStr>::mc_Value)
				OutValue = pValue->f_String();
			else if constexpr (TCIsSame<CType, uint32>::mc_Value)
				OutValue = pValue->f_Integer();
			else if constexpr (TCIsSame<CType, bool>::mc_Value)
				OutValue = pValue->f_Boolean();
			else if constexpr (TCIsSame<CType, TCVector<CStr>>::mc_Value)
			{
				OutValue = pValue->f_StringArray();
				OutValue.f_Sort();
			}
			else if constexpr (TCIsSame<CType, TCVector<CGitHostingProvider::CGitActor>>::mc_Value)
			{
				for (auto &Actor : pValue->f_Array())
				{
					auto &Type = Actor["Type"].f_String();
					if (Type == "User")
					{
						CGitHostingProvider::CUser User;
						User.m_Login = Actor["Value"].f_String();
						OutValue.f_Insert(fg_Move(User));
					}
					else if (Type == "Team")
					{
						CGitHostingProvider::CTeam Team;
						Team.m_Slug = Actor["Value"].f_String();
						OutValue.f_Insert(fg_Move(Team));
					}
					else if (Type == "App")
					{
						CGitHostingProvider::CApp App;
						App.m_Slug = Actor["Value"].f_String();
						OutValue.f_Insert(fg_Move(App));
					}
					else
						co_return DMibErrorInstance("Invalid GitHub actor type: {}"_f << Type);
				}

				OutValue.f_Sort();
			}
			else if constexpr (TCIsSame<CType, TCVector<CGitHostingProvider::CRequiredStatusCheck>>::mc_Value)
			{
				for (auto &RequiredCheck : pValue->f_Array())
				{
					auto &OutCheck = OutValue.f_Insert();
					OutCheck.m_Context = RequiredCheck["Context"].f_String();

					if (auto pApp = RequiredCheck.f_GetMember("App"))
					{
						CGitHostingProvider::CApp App;
						App.m_Slug = pApp->f_String();
						OutCheck.m_App = fg_Move(App);
					}
				}

				OutValue.f_Sort();
			}
			else
				static_assert(TCIsSame<CType, void>::mc_Value, "Unsupported type");

			co_return {};
		}

		TCFuture<TCMap<CStr, CGitHostingProvider::CBranchProtectionRule>> fg_ParseBranchProtectionRules(CEJSONSorted const &_BranchProtection)
		{
			TCMap<CStr, CGitHostingProvider::CBranchProtectionRule> OutRules;

			for (auto &Rule : _BranchProtection.f_Object())
			{
				auto &RulePattern = Rule.f_Name();

				if (RulePattern.f_IsEmpty())
					DMibError("Branch protection patterns cannot be an empty string");

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

	TCFuture<void> CGitPolicyManagerActor::fp_ApplyPolicies_BranchProtection
		(
			CEJSONSorted _BranchProtection
			, CStr _Repository
			, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider
			, CStr _PolicyName
		)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto Auditor = mp_State.f_Auditor();

		auto WantedBranchProtectionRules = co_await fg_ParseBranchProtectionRules(_BranchProtection);
		
		auto CurrentRules = co_await _HostingProvider(&CGitHostingProvider::f_GetBranchProtectionRules, _Repository);

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
					if (!mp_bPretend)
						co_await _HostingProvider(&CGitHostingProvider::f_UpdateBranchProtectionRule, _Repository, RuleID, *pWantedRule);

					Auditor.f_Warning
						(
							"{}Appying policy '{}' resulted in updated branch protection rule for branch pattern '{}' on '{}'. Updated values:\n{}"_f
							<< fp_PretendDescription()
							<< _PolicyName
							<< Pattern
							<< _Repository
							<< UpdatedValues.f_Indent("    ")
						)
					;
				}
			}
		}

		for (auto &WantedRule : WantedBranchProtectionRules)
		{
			if (AlreadyCreated.f_FindEqual(WantedBranchProtectionRules.fs_GetKey(WantedRule)))
				continue;

			if (!mp_bPretend)
				co_await _HostingProvider(&CGitHostingProvider::f_CreateBranchProtectionRule, _Repository, WantedRule);

			Auditor.f_Info
				(
					"{}Appying policy '{}' resulted in created branch protection rule for branch pattern '{}' on '{}'"_f
					<< fp_PretendDescription()
					<< _PolicyName
					<< *WantedRule.m_Pattern
					<< _Repository
				)
			;
		}

		co_return {};
	}
}
