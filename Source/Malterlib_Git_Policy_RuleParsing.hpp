// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NGit
{
	namespace
	{
		TCUnsafeFuture<void> fg_ParseRuleSetting(CEJsonSorted const &_Rule, CStr const &_Name, auto &o_Value);

		TCUnsafeFuture<void> fg_ParseRuleSettingValue(CEJsonSorted const &_JsonValue, CStr const &_Name, auto &o_Value)
		{
			co_await ECoroutineFlag_CaptureExceptions;

			using CValueType = TCRemoveReferenceAndQualifiers<decltype(o_Value)>;
			using CType = TCOptionalType<CValueType>;

			CType *pOutValue;
			if constexpr (cIsOptional<CValueType>)
				pOutValue = &o_Value.template f_Set<1>();
			else
				pOutValue = &o_Value;

			auto &OutValue = *pOutValue;

			if constexpr (cIsSame<CType, CEJsonSorted>)
				OutValue = _JsonValue;
			else if constexpr (cIsSame<CType, CJsonSorted>)
				OutValue = _JsonValue.f_ToJson();
			else if constexpr (cIsSame<CType, CStr>)
				OutValue = _JsonValue.f_String();
			else if constexpr (cIsSame<CType, bool>)
				OutValue = _JsonValue.f_Boolean();
			else if constexpr (cIsInteger<CType>)
				OutValue = _JsonValue.f_Integer();
			else if constexpr (cIsSame<CType, TCVector<CStr>>)
			{
				OutValue = _JsonValue.f_StringArray();
				OutValue.f_Sort();
			}
			else if constexpr (cIsSame<CType, TCMap<CStr, TCOptional<CStr>>>)
			{
				if (!_JsonValue.f_IsObject())
					co_return DMibErrorInstance("Expected an object for '{}'"_f << _Name);

				for (auto &Value : _JsonValue.f_Object())
				{
					if (Value.f_Value().f_IsNull())
						OutValue[Value.f_Name()];
					else if (Value.f_Value().f_IsString())
						OutValue[Value.f_Name()] = Value.f_Value().f_String();
					else
						co_return DMibErrorInstance("Expected a string or null for '{}.{}'"_f << _Name << Value.f_Name());
				}
			}
			else if constexpr (cIsVector<CType>)
			{
				CStr Name = "{}.[]"_f << _Name;
				for (auto &Item : _JsonValue.f_Array())
					co_await fg_ParseRuleSettingValue(Item, Name, OutValue.f_Insert());

				OutValue.f_Sort();
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::EGenericRuleTarget>)
			{
				auto &Value = _JsonValue.f_String();
				if (Value == "Branch")
					OutValue = CGitHostingProvider::EGenericRuleTarget::mc_Branch;
				else if (Value == "Tag")
					OutValue = CGitHostingProvider::EGenericRuleTarget::mc_Tag;
				else
					co_return DMibErrorInstance("Invalid rule target in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::EGenericRuleBypassMode>)
			{
				auto &Value = _JsonValue.f_String();
				if (Value == "Always")
					OutValue = CGitHostingProvider::EGenericRuleBypassMode::mc_Always;
				else if (Value == "PullRequest")
					OutValue = CGitHostingProvider::EGenericRuleBypassMode::mc_PullRequest;
				else
					co_return DMibErrorInstance("Invalid rule bypass mode in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::EGenericRuleEnforcement>)
			{
				auto &Value = _JsonValue.f_String();
				if (Value == "Disabled")
					OutValue = CGitHostingProvider::EGenericRuleEnforcement::mc_Disabled;
				else if (Value == "Active")
					OutValue = CGitHostingProvider::EGenericRuleEnforcement::mc_Active;
				else if (Value == "Evaluate")
					OutValue = CGitHostingProvider::EGenericRuleEnforcement::mc_Evaluate;
				else
					co_return DMibErrorInstance("Invalid rule enforcement in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::EStringMatchOperator>)
			{
				auto &Value = _JsonValue.f_String();
				if (Value == "StartsWith")
					OutValue = CGitHostingProvider::EStringMatchOperator::mc_StartsWith;
				else if (Value == "EndsWith")
					OutValue = CGitHostingProvider::EStringMatchOperator::mc_EndsWith;
				else if (Value == "Contains")
					OutValue = CGitHostingProvider::EStringMatchOperator::mc_Contains;
				else if (Value == "Regex")
					OutValue = CGitHostingProvider::EStringMatchOperator::mc_Regex;
				else
					co_return DMibErrorInstance("Invalid string match operator in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CRepository::ESquashMergeCommitTitle>)
			{
				auto &Value = _JsonValue.f_String();

				if (Value == "PrTitle")
					OutValue= CGitHostingProvider::CRepository::ESquashMergeCommitTitle::mc_PrTitle;
				else if (Value == "CommitOrPrTitle")
					OutValue= CGitHostingProvider::CRepository::ESquashMergeCommitTitle::mc_CommitOrPrTitle;
				else
					co_return DMibErrorInstance("Invalid squash merge commit title in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CRepository::ESquashMergeCommitMessage>)
			{
				auto &Value = _JsonValue.f_String();

				if (Value == "PrBody")
					OutValue = CGitHostingProvider::CRepository::ESquashMergeCommitMessage::mc_PrBody;
				else if (Value == "CommitMessages")
					OutValue = CGitHostingProvider::CRepository::ESquashMergeCommitMessage::mc_CommitMessages;
				else if (Value == "Blank")
					OutValue = CGitHostingProvider::CRepository::ESquashMergeCommitMessage::mc_Blank;
				else
					co_return DMibErrorInstance("Invalid squash merge commit message in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CRepository::EMergeCommitTitle>)
			{
				auto &Value = _JsonValue.f_String();

				if (Value == "PrTitle")
					OutValue = CGitHostingProvider::CRepository::EMergeCommitTitle::mc_PrTitle;
				else if (Value == "MergeMessage")
					OutValue = CGitHostingProvider::CRepository::EMergeCommitTitle::mc_MergeMessage;
				else
					co_return DMibErrorInstance("Invalid merge commit title in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CRepository::EMergeCommitMessage>)
			{
				auto &Value = _JsonValue.f_String();

				if (Value == "PrBody")
					OutValue = CGitHostingProvider::CRepository::EMergeCommitMessage::mc_PrBody;
				else if (Value == "PrTitle")
					OutValue = CGitHostingProvider::CRepository::EMergeCommitMessage::mc_PrTitle;
				else if (Value == "Blank")
					OutValue = CGitHostingProvider::CRepository::EMergeCommitMessage::mc_Blank;
				else
					co_return DMibErrorInstance("Invalid merge commit message in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::EActionsWorkflowPermissions>)
			{
				auto &Value = _JsonValue.f_String();

				if (Value == "Read")
					OutValue = CGitHostingProvider::EActionsWorkflowPermissions::mc_Read;
				else if (Value == "Write")
					OutValue = CGitHostingProvider::EActionsWorkflowPermissions::mc_Write;
				else
					co_return DMibErrorInstance("Invalid actions workflow permissions in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::EActionsAccessOutsideOfRepository>)
			{
				auto &Value = _JsonValue.f_String();

				if (Value == "None")
					OutValue = CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_None;
				else if (Value == "User")
					OutValue = CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_User;
				else if (Value == "Organization")
					OutValue = CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_Organization;
				else
					co_return DMibErrorInstance("Invalid actions access outside of repository in '{}': {}"_f << _Name << Value);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CGitActor>)
			{
				auto &Type = _JsonValue["Type"].f_String();
				if (Type == "User")
				{
					CGitHostingProvider::CUser User;
					User.m_Login = _JsonValue["Value"].f_String();
					OutValue = fg_Move(User);
				}
				else if (Type == "Team")
				{
					CGitHostingProvider::CTeam Team;
					Team.m_Slug = _JsonValue["Value"].f_String();
					OutValue = fg_Move(Team);
				}
				else if (Type == "App")
				{
					CGitHostingProvider::CApp App;
					App.m_Slug = _JsonValue["Value"].f_String();
					OutValue = fg_Move(App);
				}
				else
					co_return DMibErrorInstance("Invalid actor type in '{}': {}"_f << _Name << Type);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CGenericRuleGitActor>)
			{
				auto &Type = _JsonValue["Type"].f_String();
				if (Type == "Team")
				{
					CGitHostingProvider::CTeam Team;
					Team.m_Slug = _JsonValue["Value"].f_String();
					OutValue = fg_Move(Team);
				}
				else if (Type == "App")
				{
					CGitHostingProvider::CApp App;
					App.m_Slug = _JsonValue["Value"].f_String();
					OutValue = fg_Move(App);
				}
				else if (Type == "RepositoryRole")
				{
					CGitHostingProvider::CRepositoryRole Role;
					Role.m_Name = _JsonValue["Value"].f_String();
					OutValue = fg_Move(Role);
				}
				else if (Type == "OrganizationAdmin")
				{
					CGitHostingProvider::COrganizationAdmin Admin;
					OutValue = fg_Move(Admin);
				}
				else
					co_return DMibErrorInstance("Invalid generic rule actor type in '{}': {}"_f << _Name << Type);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CGenericRule>)
			{
				auto &Type = _JsonValue["Type"].f_String();
				if (Type == "Creation")
				{
					CGitHostingProvider::CGenericRule_Creation Rule;
					OutValue = fg_Move(Rule);
				}
				else if (Type == "Update")
				{
					CGitHostingProvider::CGenericRule_Update Rule;
					co_await fg_ParseRuleSetting(_JsonValue, "AllowFetchAndMerge", Rule.m_bAllowFetchAndMerge);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "Deletion")
				{
					CGitHostingProvider::CGenericRule_Deletion Rule;
					OutValue = fg_Move(Rule);
				}
				else if (Type == "LinearHistory")
				{
					CGitHostingProvider::CGenericRule_LinearHistory Rule;
					OutValue = fg_Move(Rule);
				}
				else if (Type == "Deployments")
				{
					CGitHostingProvider::CGenericRule_Deployments Rule;
					co_await fg_ParseRuleSetting(_JsonValue, "RequiredDeploymentEnvironments", Rule.m_RequiredDeploymentEnvironments);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "Signatures")
				{
					CGitHostingProvider::CGenericRule_Signatures Rule;
					OutValue = fg_Move(Rule);
				}
				else if (Type == "PullRequest")
				{
					CGitHostingProvider::CGenericRule_PullRequest Rule;
					co_await fg_ParseRuleSetting(_JsonValue, "RequiredApprovingReviewCount", Rule.m_RequiredApprovingReviewCount);
					co_await fg_ParseRuleSetting(_JsonValue, "DismissStaleReviewsOnPush", Rule.m_bDismissStaleReviewsOnPush);
					co_await fg_ParseRuleSetting(_JsonValue, "RequireCodeOwnerReview", Rule.m_bRequireCodeOwnerReview);
					co_await fg_ParseRuleSetting(_JsonValue, "RequireLastPushApproval", Rule.m_bRequireLastPushApproval);
					co_await fg_ParseRuleSetting(_JsonValue, "RequireReviewThreadResolution", Rule.m_bRequireReviewThreadResolution);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "StatusChecks")
				{
					CGitHostingProvider::CGenericRule_StatusChecks Rule;
					co_await fg_ParseRuleSetting(_JsonValue, "RequiredStatusChecks", Rule.m_RequiredStatusChecks);
					co_await fg_ParseRuleSetting(_JsonValue, "PullRequestsMustBeTestedWithLatestCode", Rule.m_bPullRequestsMustBeTestedWithLatestCode);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "FastForwardOnly")
				{
					CGitHostingProvider::CGenericRule_FastForwardOnly Rule;
					OutValue = fg_Move(Rule);
				}
				else if (Type == "CommitMessage")
				{
					CGitHostingProvider::CGenericRule_CommitMessage Rule;
					co_await fg_ParseRuleSettingValue(_JsonValue, _Name, Rule.m_StringMatch);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "CommitAuthorEmail")
				{
					CGitHostingProvider::CGenericRule_CommitAuthorEmail Rule;
					co_await fg_ParseRuleSettingValue(_JsonValue, _Name, Rule.m_StringMatch);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "CommitterEmail")
				{
					CGitHostingProvider::CGenericRule_CommitterEmail Rule;
					co_await fg_ParseRuleSettingValue(_JsonValue, _Name, Rule.m_StringMatch);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "BranchName")
				{
					CGitHostingProvider::CGenericRule_BranchName Rule;
					co_await fg_ParseRuleSettingValue(_JsonValue, _Name, Rule.m_StringMatch);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "TagName")
				{
					CGitHostingProvider::CGenericRule_TagName Rule;
					co_await fg_ParseRuleSettingValue(_JsonValue, _Name, Rule.m_StringMatch);
					OutValue = fg_Move(Rule);
				}
				else if (Type == "Workflow")
				{
					CGitHostingProvider::CGenericRule_Workflow Rule;
					co_await fg_ParseRuleSetting(_JsonValue, "Workflows", Rule.m_Workflows);
					OutValue = fg_Move(Rule);
				}
				else
					co_return DMibErrorInstance("Invalid generic rule type in '{}': {}"_f << _Name << Type);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CAllowedActions>)
			{
				auto &Type = _JsonValue["Type"].f_String();
				if (Type == "All")
				{
					CGitHostingProvider::CAllowedAction_All AllowedAction;
					OutValue = fg_Move(AllowedAction);
				}
				else if (Type == "LocalOnly")
				{
					CGitHostingProvider::CAllowedAction_LocalOnly AllowedAction;
					OutValue = fg_Move(AllowedAction);
				}
				else if (Type == "Selected")
				{
					CGitHostingProvider::CAllowedAction_Selected AllowedAction;
					co_await fg_ParseRuleSetting(_JsonValue, "GithubOwnedAllowed", AllowedAction.m_bGithubOwnedAllowed);
					co_await fg_ParseRuleSetting(_JsonValue, "VerifiedAllowed", AllowedAction.m_bVerifiedAllowed);
					co_await fg_ParseRuleSetting(_JsonValue, "PatternsAllowed", AllowedAction.m_PatternsAllowed);
					OutValue = fg_Move(AllowedAction);
				}
				else
					co_return DMibErrorInstance("Invalid allowed action type in '{}': {}"_f << _Name << Type);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CGenericRuleBypassActor>)
			{
				co_await fg_ParseRuleSetting(_JsonValue, "Actor", OutValue.m_Actor);
				co_await fg_ParseRuleSetting(_JsonValue, "Mode", OutValue.m_BypassMode);
			}
			else if constexpr
				(
					cIsSame<CType, CGitHostingProvider::CRequiredStatusCheck> || cIsSame<CType, CGitHostingProvider::CGenericRule_StatusChecks::CRequiredStatusCheck>
				)
			{
				OutValue.m_Context = _JsonValue["Context"].f_String();

				if (auto pApp = _JsonValue.f_GetMember("App"))
				{
					CGitHostingProvider::CApp App;
					App.m_Slug = pApp->f_String();
					OutValue.m_App = fg_Move(App);
				}
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CStringMatch>)
			{
				co_await fg_ParseRuleSetting(_JsonValue, "Pattern", OutValue.m_Pattern);
				co_await fg_ParseRuleSetting(_JsonValue, "Operator", OutValue.m_Operator);
				co_await fg_ParseRuleSetting(_JsonValue, "Name", OutValue.m_Name);
				co_await fg_ParseRuleSetting(_JsonValue, "Negate", OutValue.m_Negate);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CGenericRule_Workflow::CRequiredWorkflow>)
			{
				co_await fg_ParseRuleSetting(_JsonValue, "Path", OutValue.m_Path);
				co_await fg_ParseRuleSetting(_JsonValue, "WorkflowRepository", OutValue.m_WorkflowRepository);
				co_await fg_ParseRuleSetting(_JsonValue, "Ref", OutValue.m_Ref);
				co_await fg_ParseRuleSetting(_JsonValue, "Sha", OutValue.m_Sha);
			}
			else if constexpr (cIsSame<CType, CGitHostingProvider::CRepositoryReference>)
				OutValue.m_Slug = _JsonValue.f_String();
			else
				static_assert(cIsSame<CType, void>, "Unsupported type");

			co_return {};
		}

		TCUnsafeFuture<void> fg_ParseRuleSetting(CEJsonSorted const &_Rule, CStr const &_Name, auto &o_Value)
		{
			co_await ECoroutineFlag_CaptureExceptions;

			auto *pValue = _Rule.f_GetMember(_Name);
			if (!pValue)
				co_return {};

			co_await fg_ParseRuleSettingValue(*pValue, _Name, o_Value);

			co_return {};
		}
	}
}
