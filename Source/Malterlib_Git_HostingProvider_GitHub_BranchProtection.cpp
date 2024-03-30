// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NGit
{
	namespace
	{
		ch8 const *g_pGraphQl_GetBranchProtection = R"-----(
			query GetBranchProtectionRules($owner: String!, $name: String!)
			{
				repository(owner: $owner, name: $name)
				{
					branchProtectionRules(first:100)
					{
						nodes
						{
							id
							, pattern
							, allowsForcePushes
							, allowsDeletions
							, blocksCreations
							, bypassForcePushAllowances(first: 100)
							{
								nodes
								{
									actor
									{
										__typename
										, ... on App
										{
											id
											, slug
											, name
										}
										, ... on Team
										{
											id
											, slug
											, name
										}
										, ... on User
										{
											id
											, login
											, name
										}
									}
								}
							}
							, bypassPullRequestAllowances(first: 100)
							{
								nodes
								{
									actor
									{
										__typename
										, ... on App
										{
											id
											, slug
											, name
										}
										, ... on Team
										{
											id
											, slug
											, name
										}
										, ... on User
										{
											id
											, login
											, name
										}
									}
								}
							}
							, creator
							{
								login
							}
							, dismissesStaleReviews
							, isAdminEnforced
							, pushAllowances(first: 100)
							{
								nodes
								{
									actor
									{
										__typename
										, ... on App
										{
											id
											, slug
											, name
										}
										, ... on Team
										{
											id
											, slug
											, name
										}
										, ... on User
										{
											id
											, login
											, name
										}
									}
								}
							}
							, requiredApprovingReviewCount
							, requiredStatusCheckContexts
							, requiredStatusChecks
							{
								app {
									id
									, slug
									, name
								}
								, context
							}
							, requiresApprovingReviews
							, requiresCodeOwnerReviews
							, requiresCommitSignatures
							, requiresConversationResolution
							, requiresLinearHistory
							, requiresStatusChecks
							, requiresStrictStatusChecks
							, restrictsPushes
							, restrictsReviewDismissals
							, reviewDismissalAllowances(first: 100)
							{
								nodes
								{
									actor
									{
										__typename
										, ... on App
										{
											id
											, slug
											, name
										}
										, ... on Team
										{
											id
											, slug
											, name
										}
										, ... on User
										{
											id
											, login
											, name
										}
									}
								}
							}
						}
					}
				}
			}
			)-----"
		;

		TCOptional<TCVector<CGitHostingProvider::CGitActor>> fg_ParseActorList(CJSONSorted const &_JSON)
		{
			TCVector<CGitHostingProvider::CGitActor> OutActors;
			for (auto &ActorNode : _JSON["nodes"].f_Array())
			{
				auto &Actor = ActorNode["actor"];
				auto &Type = Actor["__typename"].f_String();

				if (Type == "User")
				{
					CGitHostingProvider::CUser User;
					User.m_ID = Actor["id"].f_String();
					User.m_Login = Actor["login"].f_String();
					User.m_Name = Actor["name"].f_String();
					OutActors.f_Insert(fg_Move(User));
				}
				else if (Type == "Team")
				{
					CGitHostingProvider::CTeam Team;
					Team.m_ID = Actor["id"].f_String();
					Team.m_Slug = Actor["slug"].f_String();
					Team.m_Name = Actor["name"].f_String();
					OutActors.f_Insert(fg_Move(Team));
				}
				else if (Type == "App")
				{
					CGitHostingProvider::CApp App;
					App.m_ID = Actor["id"].f_String();
					App.m_Slug = Actor["slug"].f_String();
					App.m_Name = Actor["name"].f_String();
					OutActors.f_Insert(fg_Move(App));
				}
				else
					DMibError("Unknown actor type: {}"_f << Type);
			}

			OutActors.f_Sort();

			return OutActors;
		}
	}

	auto CGitHostingProvider_GitHub::f_GetBranchProtectionRules(CStr const &_Repository) -> TCFuture<TCMap<CStr, CBranchProtectionRule>>
	{
		TCMap<CStr, CBranchProtectionRule> OutRules;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto CaptureScope = co_await g_CaptureExceptions;

		auto const Data = co_await
			(
				fp_GraphQlApi(g_pGraphQl_GetBranchProtection, {"owner"_j= RepositorySlug.m_Owner, "name"_j= RepositorySlug.m_Name})
				% ("Failed to get branch protection rules for repository '{}'"_f << _Repository)
			)
		;

		auto &Rules = Data["data"]["repository"]["branchProtectionRules"]["nodes"].f_Array();

		for (auto &Rule : Rules)
		{
			auto &OutRule = OutRules[Rule["id"].f_String()];

			OutRule.m_Pattern = Rule["pattern"].f_String();
			OutRule.m_Creator = Rule["creator"]["login"].f_String();

			OutRule.m_PushAllowances = fg_ParseActorList(Rule["pushAllowances"]);
			OutRule.m_ReviewDismissalAllowances = fg_ParseActorList(Rule["reviewDismissalAllowances"]);
			OutRule.m_BypassForcePushAllowances = fg_ParseActorList(Rule["bypassForcePushAllowances"]);
			OutRule.m_BypassPullRequestAllowances = fg_ParseActorList(Rule["bypassPullRequestAllowances"]);

			{
				auto Value = Rule["requiredStatusCheckContexts"].f_StringArray();
				Value.f_Sort();
				OutRule.m_RequiredStatusCheckContexts = fg_Move(Value);
			}

			{
				TCVector<CRequiredStatusCheck> RequiredChecks;
				for (auto &Check : Rule["requiredStatusChecks"].f_Array())
				{
					auto &OutCheck = RequiredChecks.f_Insert();
					OutCheck.m_Context = Check["context"].f_String();

					auto &App = Check["app"];
					if (!App.f_IsNull())
					{
						CApp OutApp;
						OutApp.m_ID = App["id"].f_String();
						OutApp.m_Slug = App["slug"].f_String();
						OutApp.m_Name = App["name"].f_String();
						OutCheck.m_App = OutApp;
					}
				}
				RequiredChecks.f_Sort();
				OutRule.m_RequiredStatusChecks = fg_Move(RequiredChecks);
			}

			if (auto pValue = Rule.f_GetMember("requiredApprovingReviewCount", EJSONType_Integer))
				OutRule.m_RequiredApprovingReviewCount = pValue->f_Integer();

			OutRule.m_AllowsForcePushes = Rule["allowsForcePushes"].f_Boolean();
			OutRule.m_AllowsDeletions = Rule["allowsDeletions"].f_Boolean();
			OutRule.m_BlocksCreations = Rule["blocksCreations"].f_Boolean();
			OutRule.m_DismissesStaleReviews = Rule["dismissesStaleReviews"].f_Boolean();
			OutRule.m_IsAdminEnforced = Rule["isAdminEnforced"].f_Boolean();
			OutRule.m_RequiresApprovingReviews = Rule["requiresApprovingReviews"].f_Boolean();
			OutRule.m_RequiresCodeOwnerReviews = Rule["requiresCodeOwnerReviews"].f_Boolean();
			OutRule.m_RequiresCommitSignatures = Rule["requiresCommitSignatures"].f_Boolean();
			OutRule.m_RequiresConversationResolution = Rule["requiresConversationResolution"].f_Boolean();
			OutRule.m_RequiresLinearHistory = Rule["requiresLinearHistory"].f_Boolean();
			OutRule.m_RequiresStatusChecks = Rule["requiresStatusChecks"].f_Boolean();
			OutRule.m_RequiresStrictStatusChecks = Rule["requiresStrictStatusChecks"].f_Boolean();
			OutRule.m_RestrictsPushes = Rule["restrictsPushes"].f_Boolean();
			OutRule.m_RestrictsReviewDismissals = Rule["restrictsReviewDismissals"].f_Boolean();
		}

		co_return fg_Move(OutRules);
	}

	TCFuture<CJSONSorted> CGitHostingProvider_GitHub::fp_PopulateGraphQl_BranchProtectionRule(CStr _Organization, CBranchProtectionRule _Rule)
	{
		CJSONSorted Output;

		auto fAddOptional = [&Output, this, &_Organization](CStr const &_Name, auto const &_OptionalValue) -> TCFuture<void>
			{
				co_await ECoroutineFlag_AllowReferences;

				if (!_OptionalValue)
					co_return {};

				auto &Value = *_OptionalValue;
				using CType = typename NTraits::TCRemoveReferenceAndQualifiers<decltype(Value)>::CType;

				if constexpr (TCIsSame<CType, CStr>::mc_Value)
					Output[_Name] = Value;
				else if constexpr (TCIsSame<CType, TCVector<CStr>>::mc_Value)
					Output[_Name] = Value;
				else if constexpr (TCIsSame<CType, uint32>::mc_Value)
					Output[_Name] = Value;
				else if constexpr (TCIsSame<CType, bool>::mc_Value)
					Output[_Name] = Value;
				else if constexpr (TCIsSame<CType, TCVector<CGitActor>>::mc_Value)
				{
					TCVector<CStr> ActorIDs;

					for (auto &Actor : Value)
						ActorIDs.f_Insert(co_await fp_GetActorID(_Organization, Actor, true, nullptr));

					Output[_Name] = fg_Move(ActorIDs);
				}
				else if constexpr (TCIsSame<CType, TCVector<CRequiredStatusCheck>>::mc_Value)
				{
					TCVector<CJSONSorted> OutValues;

					for (auto &Actor : Value)
					{
						CJSONSorted Value;
						Value["context"] = Actor.m_Context;
						if (Actor.m_App)
							Value["appId"] = co_await fp_GetAppID(*Actor.m_App, true);
						else
							Value["appId"] = "any";
						OutValues.f_Insert(fg_Move(Value));
					}

					Output[_Name] = fg_Move(OutValues);
				}
				else
					static_assert(TCIsSame<CType, void>::mc_Value, "Unknown type");

				co_return {};
			}
		;

		co_await fAddOptional("pattern", _Rule.m_Pattern);
		co_await fAddOptional("pushActorIds", _Rule.m_PushAllowances);
		co_await fAddOptional("reviewDismissalActorIds", _Rule.m_ReviewDismissalAllowances);
		co_await fAddOptional("bypassForcePushActorIds", _Rule.m_BypassForcePushAllowances);
		co_await fAddOptional("bypassPullRequestActorIds", _Rule.m_BypassPullRequestAllowances);
		co_await fAddOptional("requiredStatusCheckContexts", _Rule.m_RequiredStatusCheckContexts);
		co_await fAddOptional("requiredStatusChecks", _Rule.m_RequiredStatusChecks);
		co_await fAddOptional("requiredApprovingReviewCount", _Rule.m_RequiredApprovingReviewCount);
		co_await fAddOptional("allowsForcePushes", _Rule.m_AllowsForcePushes);
		co_await fAddOptional("allowsDeletions", _Rule.m_AllowsDeletions);
		co_await fAddOptional("blocksCreations", _Rule.m_BlocksCreations);
		co_await fAddOptional("dismissesStaleReviews", _Rule.m_DismissesStaleReviews);
		co_await fAddOptional("isAdminEnforced", _Rule.m_IsAdminEnforced);
		co_await fAddOptional("requiresApprovingReviews", _Rule.m_RequiresApprovingReviews);
		co_await fAddOptional("requiresCodeOwnerReviews", _Rule.m_RequiresCodeOwnerReviews);
		co_await fAddOptional("requiresCommitSignatures", _Rule.m_RequiresCommitSignatures);
		co_await fAddOptional("requiresConversationResolution", _Rule.m_RequiresConversationResolution);
		co_await fAddOptional("requiresLinearHistory", _Rule.m_RequiresLinearHistory);
		co_await fAddOptional("requiresStatusChecks", _Rule.m_RequiresStatusChecks);
		co_await fAddOptional("requiresStrictStatusChecks", _Rule.m_RequiresStrictStatusChecks);
		co_await fAddOptional("restrictsPushes", _Rule.m_RestrictsPushes);
		co_await fAddOptional("restrictsReviewDismissals", _Rule.m_RestrictsReviewDismissals);

		co_return fg_Move(Output);
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::f_CreateBranchProtectionRule(CStr const &_Repository, CBranchProtectionRule const &_Rule)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJSONSorted Values = co_await fp_PopulateGraphQl_BranchProtectionRule(RepositorySlug.m_Owner, _Rule);
		Values["repositoryId"] = co_await fp_GetRepositoryID(_Repository, true);

		auto const Data = co_await
			(
				fp_GraphQlApi
				(
					R"-----(
					mutation CreateBranchProtectionRule($input: CreateBranchProtectionRuleInput!)
					{
						createBranchProtectionRule
						(
							input: $input
						)
						{
							branchProtectionRule
							{
								id
							}
						}
					}
					)-----"
					,
					{
						"input"_j= fg_Move(Values)
					}
				)
				% ("Failed to create branch protection rule for repository '{}'"_f << _Repository)
			)
		;

		co_return Data["data"]["createBranchProtectionRule"]["branchProtectionRule"]["id"].f_String();
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_UpdateBranchProtectionRule(CStr const &_Repository, CStr const &_RuleID, CBranchProtectionRule const &_Rule)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJSONSorted Values = co_await fp_PopulateGraphQl_BranchProtectionRule(RepositorySlug.m_Owner, _Rule);
		Values["branchProtectionRuleId"] = _RuleID;

		co_await
			(
				fp_GraphQlApi
				(
					R"-----(
					mutation UpdateBranchProtectionRule($input: UpdateBranchProtectionRuleInput!)
					{
						updateBranchProtectionRule
						(
							input: $input
						)
						{
							clientMutationId
						}
					}
					)-----"
					,
					{
						"input"_j= fg_Move(Values)
					}
				)
				% ("Failed to update branch protection rule for repository '{}'"_f << _Repository)
			)
		;

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DeleteBranchProtectionRule(CStr const &_Repository, CStr const &_RuleID)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		co_await
			(
				fp_GraphQlApi
				(
					R"-----(
					mutation DeleteBranchProtectionRule($branchProtectionRuleId: String!)
					{
						deleteBranchProtectionRule(input: {branchProtectionRuleId: $branchProtectionRuleId})
						{
							clientMutationId
						}
					}
					)-----"
					,
					{
						"branchProtectionRuleId"_j= _RuleID
					}
				)
				% ("Failed to delete branch protection rule for repository '{}'"_f << _Repository)
			)
		;

		co_return {};
	}
}
