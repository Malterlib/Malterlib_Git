// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NGit
{
	static CGitHostingProvider::CStringMatch fg_ParseStringMatch(CJSONSorted const *_pParameters)
	{
		if (!_pParameters)
			return {};

		auto &Parameters = *_pParameters;

		CGitHostingProvider::CStringMatch Return;
		Return.m_Pattern = Parameters["pattern"].f_String();

		auto &Operator = Parameters["operator"].f_String();
		if (Operator == "starts_with")
			Return.m_Operator = CGitHostingProvider::EStringMatchOperator::mc_StartsWith;
		else if (Operator == "ends_with")
			Return.m_Operator = CGitHostingProvider::EStringMatchOperator::mc_EndsWith;
		else if (Operator == "contains")
			Return.m_Operator = CGitHostingProvider::EStringMatchOperator::mc_Contains;
		else if (Operator == "regex")
			Return.m_Operator = CGitHostingProvider::EStringMatchOperator::mc_Regex;

		if (auto *pValue = Parameters.f_GetMember("name"))
			Return.m_Name = pValue->f_String();

		if (auto *pValue = Parameters.f_GetMember("negate"))
			Return.m_Negate = pValue->f_Boolean();

		return Return;
	}

	auto CGitHostingProvider_GitHub::f_PopulateGenericRulesetIDs(CStr const &_Repository, CGenericRuleset &&_Ruleset) -> TCFuture<CGenericRuleset>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Ruleset = fg_Move(_Ruleset);

		TCSharedPointer<CCustomRepositoryRoleCache> pRoleCache = fg_Construct();

		if (Ruleset.m_BypassActors)
		{
			for (auto &BypassActor : *Ruleset.m_BypassActors)
			{
				if (BypassActor.m_Actor.f_IsOfType<CTeam>())
				{
					auto &Actor = BypassActor.m_Actor.f_GetAsType<CTeam>();
					if (!Actor.m_ID)
					{
						Actor.m_ID = co_await fp_GetActorID(RepositorySlug.m_Owner, BypassActor.m_Actor, false, pRoleCache);
						Actor.m_Slug.f_Clear();
					}
				}
				else if (BypassActor.m_Actor.f_IsOfType<CApp>())
				{
					auto &Actor = BypassActor.m_Actor.f_GetAsType<CApp>();
					if (!Actor.m_ID)
					{
						Actor.m_ID = co_await fp_GetActorID(RepositorySlug.m_Owner, BypassActor.m_Actor, false, pRoleCache);
						Actor.m_Slug.f_Clear();
					}
				}
				else if (BypassActor.m_Actor.f_IsOfType<CRepositoryRole>())
				{
					auto &Actor = BypassActor.m_Actor.f_GetAsType<CRepositoryRole>();
					if (!Actor.m_ID)
					{
						Actor.m_ID = co_await fp_GetActorID(RepositorySlug.m_Owner, BypassActor.m_Actor, false, pRoleCache);
						Actor.m_Name.f_Clear();
					}
				}
			}

			Ruleset.m_BypassActors->f_Sort();
		}

		if (Ruleset.m_Rules)
		{
			for (auto &Rule : *Ruleset.m_Rules)
			{
				if (Rule.f_IsOfType<CGenericRule_StatusChecks>())
				{
					auto &TypedRule = Rule.f_GetAsType<CGenericRule_StatusChecks>();

					for (auto &Check : TypedRule.m_RequiredStatusChecks)
					{
						if (!Check.m_App)
							continue;

						auto &App = *Check.m_App;
						if (!App.m_ID)
						{
							App.m_ID = co_await fp_GetAppID(*Check.m_App, false);
							App.m_Slug.f_Clear();
						}
					}

					TypedRule.m_RequiredStatusChecks.f_Sort();
				}
				if (Rule.f_IsOfType<CGenericRule_Workflow>())
				{
					auto &TypedRule = Rule.f_GetAsType<CGenericRule_Workflow>();

					for (auto &Workflow : TypedRule.m_Workflows)
					{
						auto &Repository = Workflow.m_WorkflowRepository;
						if (!Repository.m_ID)
						{
							Repository.m_ID = co_await fp_GetRepositoryID(Repository.m_Slug, false);
							Repository.m_Slug.f_Clear();
						}
					}

					TypedRule.m_Workflows.f_Sort();
				}

				Ruleset.m_Rules->f_Sort();
			}
		}

		co_return fg_Move(Ruleset);
	}

	auto CGitHostingProvider_GitHub::f_GetGenericRulesets(NStr::CStr const &_Repository) -> NConcurrency::TCFuture<NContainer::TCMap<NStr::CStr, CGenericRuleset>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);
		
		auto const RuleSetEnum = co_await
			(
				fp_RestApi("repos/{}/{}/rulesets"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name, "Failed to get rulesets from repository '{}'"_f << _Repository)
			)
		;

		NContainer::TCMap<NStr::CStr, CGenericRuleset> OutRulesets;

		TCActorResultVector<CJSONSorted> Results;

		for (auto &Rule : RuleSetEnum.f_Array())
		{
			fp_RestApi
				(
					"repos/{}/{}/rulesets/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << Rule["id"].f_Integer()
					, "Failed to get ruleset from repository '{}'"_f << _Repository
				) 
				> Results.f_AddResult()
			;
		}

		auto const Rulesets = co_await Results.f_GetUnwrappedResults();

		CJSONSorted RuleSetsJson;

		for (auto &Ruleset : Rulesets)
		{
			if (auto *pSourceType = Ruleset.f_GetMember("source_type"))
			{
				if (*pSourceType != "Repository")
					continue;
			}

			RuleSetsJson.f_Insert(Ruleset);

			auto RuleID = CStr::fs_ToStr(Ruleset["id"].f_Integer());

			auto &OutRuleset = OutRulesets[RuleID];

			OutRuleset.m_Name = Ruleset["name"].f_String();
			{
				auto &Enforcement = Ruleset["enforcement"].f_String();
				if (Enforcement == "disabled")
					OutRuleset.m_Enforcement = EGenericRuleEnforcement::mc_Disabled;
				else if (Enforcement == "active")
					OutRuleset.m_Enforcement = EGenericRuleEnforcement::mc_Active;
				else if (Enforcement == "evaluate")
					OutRuleset.m_Enforcement = EGenericRuleEnforcement::mc_Evaluate;
				else
					co_return DMibErrorInstance("Invalid enforcement: {}"_f << Enforcement);
			}

			if (auto *pTarget = Ruleset.f_GetMember("target"))
			{
				auto &Target = pTarget->f_String();
				if (Target == "branch")
					OutRuleset.m_Target = EGenericRuleTarget::mc_Branch;
				else if (Target == "tag")
					OutRuleset.m_Target = EGenericRuleTarget::mc_Tag;
				else
					co_return DMibErrorInstance("Invalid target: {}"_f << Target);
			}

			if (auto *pBypassActors = Ruleset.f_GetMember("bypass_actors"))
			{
				TCVector<CGenericRuleBypassActor> Actors;
				for (auto &BypassActor : pBypassActors->f_Array())
				{
					auto &OutActor = Actors.f_Insert();

					auto &ActorType = BypassActor["actor_type"].f_String();
					auto &ActorID = BypassActor["actor_id"].f_Integer();

					if (ActorType == "RepositoryRole")
						OutActor.m_Actor = CRepositoryRole{.m_ID = CStr::fs_ToStr(ActorID)};
					else if (ActorType == "Team")
						OutActor.m_Actor = CTeam{.m_ID = CStr::fs_ToStr(ActorID)};
					else if (ActorType == "Integration")
						OutActor.m_Actor = CApp{.m_ID = CStr::fs_ToStr(ActorID)};
					else if (ActorType == "OrganizationAdmin")
						OutActor.m_Actor = COrganizationAdmin{};
					else
						co_return DMibErrorInstance("Invalid actor type: {}"_f << ActorType);

					auto &BypassMode = BypassActor["bypass_mode"].f_String();
					if (BypassMode == "always")
						OutActor.m_BypassMode = EGenericRuleBypassMode::mc_Always;
					else if (BypassMode == "pull_request")
						OutActor.m_BypassMode = EGenericRuleBypassMode::mc_PullRequest;
					else
						co_return DMibErrorInstance("Invalid bypass mode: {}"_f << BypassMode);
				}

				Actors.f_Sort();
				OutRuleset.m_BypassActors = fg_Move(Actors);
			}

			if (auto *pConditions = Ruleset.f_GetMember("conditions"))
			{
				if (auto *pRefName = pConditions->f_GetMember("ref_name"))
				{
					if (auto *pInclude = pRefName->f_GetMember("include"))
						OutRuleset.m_IncludeRefNames = pInclude->f_StringArray();

					if (auto *pExclude = pRefName->f_GetMember("exclude"))
						OutRuleset.m_ExcludeRefNames = pExclude->f_StringArray();
				}
			}
			if (auto *pRules = Ruleset.f_GetMember("rules"))
			{
				TCVector<CGenericRule> Rules;
				for (auto &Rule : pRules->f_Array())
				{
					auto &RuleType = Rule["type"].f_String();
					auto *pParameters = Rule.f_GetMember("parameters");
					if (RuleType == "creation")
						Rules.f_Insert(CGenericRule_Creation{});
					else if (RuleType == "update")
					{
						CGenericRule_Update Update;
						if (pParameters)
							Update.m_bAllowFetchAndMerge = (*pParameters)["update_allows_fetch_and_merge"].f_Boolean();
						Rules.f_Insert(fg_Move(Update));
					}
					else if (RuleType == "deletion")
						Rules.f_Insert(CGenericRule_Deletion{});
					else if (RuleType == "required_linear_history")
						Rules.f_Insert(CGenericRule_LinearHistory{});
					else if (RuleType == "required_deployments")
					{
						CGenericRule_Deployments Deployments;
						if (pParameters)
							Deployments.m_RequiredDeploymentEnvironments = (*pParameters)["required_deployment_environments"].f_StringArray();
						Rules.f_Insert(fg_Move(Deployments));
					}
					else if (RuleType == "required_signatures")
						Rules.f_Insert(CGenericRule_Signatures{});
					else if (RuleType == "pull_request")
					{
						CGenericRule_PullRequest PullRequest;
						if (pParameters)
						{
							PullRequest.m_RequiredApprovingReviewCount = (*pParameters)["required_approving_review_count"].f_Integer();
							PullRequest.m_bDismissStaleReviewsOnPush = (*pParameters)["dismiss_stale_reviews_on_push"].f_Boolean();
							PullRequest.m_bRequireCodeOwnerReview = (*pParameters)["require_code_owner_review"].f_Boolean();
							PullRequest.m_bRequireLastPushApproval = (*pParameters)["require_last_push_approval"].f_Boolean();
							PullRequest.m_bRequireReviewThreadResolution = (*pParameters)["required_review_thread_resolution"].f_Boolean();
						}
						Rules.f_Insert(fg_Move(PullRequest));
					}
					else if (RuleType == "required_status_checks")
					{
						CGenericRule_StatusChecks StatusChecks;
						if (pParameters)
						{
							StatusChecks.m_bPullRequestsMustBeTestedWithLatestCode = (*pParameters)["strict_required_status_checks_policy"].f_Boolean();
							for (auto &Check : (*pParameters)["required_status_checks"].f_Array())
							{
								auto &OutCheck = StatusChecks.m_RequiredStatusChecks.f_Insert();
								OutCheck.m_Context = Check["context"].f_String();
								if (auto *pIntegrationID = Check.f_GetMember("integration_id"))
									OutCheck.m_App = CApp{.m_ID = CStr::fs_ToStr(pIntegrationID->f_Integer())};
							}
							StatusChecks.m_RequiredStatusChecks.f_Sort();
						}
						Rules.f_Insert(fg_Move(StatusChecks));
					}
					else if (RuleType == "non_fast_forward")
						Rules.f_Insert(CGenericRule_FastForwardOnly{});
					else if (RuleType == "commit_message_pattern")
						Rules.f_Insert(CGenericRule_CommitMessage{.m_StringMatch = fg_ParseStringMatch(pParameters)});
					else if (RuleType == "commit_author_email_pattern")
						Rules.f_Insert(CGenericRule_CommitAuthorEmail{.m_StringMatch = fg_ParseStringMatch(pParameters)});
					else if (RuleType == "committer_email_pattern")
						Rules.f_Insert(CGenericRule_CommitterEmail{.m_StringMatch = fg_ParseStringMatch(pParameters)});
					else if (RuleType == "branch_name_pattern")
						Rules.f_Insert(CGenericRule_BranchName{.m_StringMatch = fg_ParseStringMatch(pParameters)});
					else if (RuleType == "tag_name_pattern")
						Rules.f_Insert(CGenericRule_TagName{.m_StringMatch = fg_ParseStringMatch(pParameters)});
					else if (RuleType == "workflows")
					{
						CGenericRule_Workflow Workflow;
						if (pParameters)
						{
							for (auto &WorkflowObj : (*pParameters)["workflows"].f_Array())
							{
								auto &OutWorkflow = Workflow.m_Workflows.f_Insert();
								OutWorkflow.m_Path = WorkflowObj["path"].f_String();
								OutWorkflow.m_WorkflowRepository.m_ID = CStr::fs_ToStr(WorkflowObj["repository_id"].f_Integer());

								if (auto *pValue = WorkflowObj.f_GetMember("ref"))
									OutWorkflow.m_Ref = pValue->f_String();

								if (auto *pValue = WorkflowObj.f_GetMember("sha"))
									OutWorkflow.m_Sha = pValue->f_String();
							}
							Workflow.m_Workflows.f_Sort();
						}
						Rules.f_Insert(fg_Move(Workflow));
					}
				}

				Rules.f_Sort();

				OutRuleset.m_Rules = fg_Move(Rules);
			}
		}

		co_return fg_Move(OutRulesets);
	}

	TCFuture<CJSONSorted> CGitHostingProvider_GitHub::fp_PopulateRest_GenericRuleset(CStr _Organization, CGenericRuleset _Ruleset)
	{
		TCSharedPointer<CCustomRepositoryRoleCache> pRoleCache = fg_Construct();

		auto fAddOptional = [&](auto &&_fThis, CJSONSorted &o_Output, CStr const &_Name, auto const &_OptionalValue) -> TCFuture<void>
			{
				co_await ECoroutineFlag_AllowReferences;

				using CValueType = typename TCRemoveReferenceAndQualifiers<decltype(_OptionalValue)>::CType;
				using CType = TCOptionalType<CValueType>;

				CType const *pValue;
				if constexpr (cIsOptional<CValueType>)
				{
					if (!_OptionalValue)
						co_return {};

					pValue = &*_OptionalValue;
				}
				else
					pValue = &_OptionalValue;

				auto &Value = *pValue;

				CJSONSorted *pOutput = &o_Output;
				if (_Name)
					pOutput = &o_Output[_Name];

				auto &Output = *pOutput;

				if constexpr (cIsSame<CType, CStr>)
					Output = Value;
				else if constexpr (cIsSame<CType, TCVector<CStr>>)
					Output = Value;
				else if constexpr (cIsSame<CType, uint8>)
					Output = Value;
				else if constexpr (cIsSame<CType, uint32>)
					Output = Value;
				else if constexpr (cIsSame<CType, bool>)
					Output = Value;
				else if constexpr (cIsVector<CType>)
				{
					auto &OutputArray = Output.f_Array();

					for (auto &Item : Value)
						co_await _fThis(_fThis, OutputArray.f_Insert(), {}, Item);
				}
				else if constexpr (cIsSame<CType, EGenericRuleTarget>)
				{
					switch (Value)
					{
					case EGenericRuleTarget::mc_Branch: Output = "branch"; break;
					case EGenericRuleTarget::mc_Tag: Output = "tag"; break;
					}
				}
				else if constexpr (cIsSame<CType, EGenericRuleEnforcement>)
				{
					switch (Value)
					{
					case EGenericRuleEnforcement::mc_Disabled: Output = "disabled"; break;
					case EGenericRuleEnforcement::mc_Active: Output = "active"; break;
					case EGenericRuleEnforcement::mc_Evaluate: Output = "evaluate"; break;
					}
				}
				else if constexpr (cIsSame<CType, EGenericRuleBypassMode>)
				{
					switch (Value)
					{
					case EGenericRuleBypassMode::mc_Always: Output = "always"; break;
					case EGenericRuleBypassMode::mc_PullRequest: Output = "pull_request"; break;
					}
				}
				else if constexpr (cIsSame<CType, EStringMatchOperator>)
				{
					switch (Value)
					{
					case EStringMatchOperator::mc_StartsWith: Output = "starts_with"; break;
					case EStringMatchOperator::mc_EndsWith: Output = "ends_with"; break;
					case EStringMatchOperator::mc_Contains: Output = "contains"; break;
					case EStringMatchOperator::mc_Regex: Output = "regex"; break;
					}
				}
				else if constexpr (cIsSame<CType, CGenericRuleGitActor>)
				{
					CStr ActorID = co_await fp_GetActorID(_Organization, Value, false, pRoleCache);
					CStr ActorType;
					if (Value.template f_IsOfType<CTeam>())
						ActorType = "Team";
					else if (Value.template f_IsOfType<CApp>())
						ActorType = "Integration";
					else if (Value.template f_IsOfType<CRepositoryRole>())
						ActorType = "RepositoryRole";
					else if (Value.template f_IsOfType<COrganizationAdmin>())
						ActorType = "OrganizationAdmin";

					Output["actor_type"] = fg_Move(ActorType);
					Output["actor_id"] = ActorID.f_ToInt(int64(0));
				}
				else if constexpr (cIsSame<CType, CGenericRuleBypassActor>)
				{
					co_await _fThis(_fThis, Output, "bypass_mode", Value.m_BypassMode);
					co_await _fThis(_fThis, Output, {}, Value.m_Actor);
				}
				else if constexpr (cIsSame<CType, CStringMatch>)
				{
					co_await _fThis(_fThis, Output, "name", Value.m_Name);
					co_await _fThis(_fThis, Output, "negate", Value.m_Negate);
					co_await _fThis(_fThis, Output, "operator", Value.m_Operator);
					co_await _fThis(_fThis, Output, "pattern", Value.m_Pattern);
				}
				else if constexpr (cIsSame<CType, CGenericRule_StatusChecks::CRequiredStatusCheck>)
				{
					Output["context"] = Value.m_Context;
					if (Value.m_App)
						Output["integration_id"] = co_await fp_GetAppID(*Value.m_App, false);
				}
				else if constexpr (cIsSame<CType, CGenericRule_Workflow::CRequiredWorkflow>)
				{
					Output["path"] = Value.m_Path;

					if (Value.m_WorkflowRepository.m_ID)
						Output["repository_id"] = Value.m_WorkflowRepository.m_ID;
					else
						Output["repository_id"] = co_await fp_GetRepositoryID(Value.m_WorkflowRepository.m_Slug, false);

					co_await _fThis(_fThis, Output, "ref", Value.m_Ref);
					co_await _fThis(_fThis, Output, "sha", Value.m_Sha);
				}
				else if constexpr (cIsSame<CType, CGenericRule>)
				{
					CStr RuleType;
					CJSONSorted Parameters;
					if (Value.template f_IsOfType<CGenericRule_Creation>())
						RuleType = "creation";
					else if (Value.template f_IsOfType<CGenericRule_Update>())
					{
						RuleType = "update";
						CGenericRule_Update const &Update = Value.template f_GetAsType<CGenericRule_Update>();
						Parameters["update_allows_fetch_and_merge"] = Update.m_bAllowFetchAndMerge;
					}
					else if (Value.template f_IsOfType<CGenericRule_Deletion>())
						RuleType = "deletion";
					else if (Value.template f_IsOfType<CGenericRule_LinearHistory>())
						RuleType = "required_linear_history";
					else if (Value.template f_IsOfType<CGenericRule_Deployments>())
					{
						RuleType = "required_deployments";
						CGenericRule_Deployments const &Deployments = Value.template f_GetAsType<CGenericRule_Deployments>();
						Parameters["required_deployment_environments"] = Deployments.m_RequiredDeploymentEnvironments;
					}
					else if (Value.template f_IsOfType<CGenericRule_Signatures>())
						RuleType = "required_signatures";
					else if (Value.template f_IsOfType<CGenericRule_PullRequest>())
					{
						RuleType = "pull_request";
						CGenericRule_PullRequest const &PullRequest = Value.template f_GetAsType<CGenericRule_PullRequest>();
						Parameters["dismiss_stale_reviews_on_push"] = PullRequest.m_bDismissStaleReviewsOnPush;
						Parameters["require_code_owner_review"] = PullRequest.m_bRequireCodeOwnerReview;
						Parameters["require_last_push_approval"] = PullRequest.m_bRequireLastPushApproval;
						Parameters["required_approving_review_count"] = PullRequest.m_RequiredApprovingReviewCount;
						Parameters["required_review_thread_resolution"] = PullRequest.m_bRequireReviewThreadResolution;
					}
					else if (Value.template f_IsOfType<CGenericRule_StatusChecks>())
					{
						RuleType = "required_status_checks";
						CGenericRule_StatusChecks const &StatusChecks = Value.template f_GetAsType<CGenericRule_StatusChecks>();
						Parameters["strict_required_status_checks_policy"] = StatusChecks.m_bPullRequestsMustBeTestedWithLatestCode;
						co_await _fThis(_fThis, Parameters, "required_status_checks", StatusChecks.m_RequiredStatusChecks);
					}
					else if (Value.template f_IsOfType<CGenericRule_FastForwardOnly>())
						RuleType = "non_fast_forward";
					else if (Value.template f_IsOfType<CGenericRule_CommitMessage>())
					{
						RuleType = "commit_message_pattern";
						co_await _fThis(_fThis, Parameters, {}, Value.template f_GetAsType<CGenericRule_CommitMessage>().m_StringMatch);
					}
					else if (Value.template f_IsOfType<CGenericRule_CommitAuthorEmail>())
					{
						RuleType = "commit_author_email_pattern";
						co_await _fThis(_fThis, Parameters, {}, Value.template f_GetAsType<CGenericRule_CommitAuthorEmail>().m_StringMatch);
					}
					else if (Value.template f_IsOfType<CGenericRule_CommitterEmail>())
					{
						RuleType = "committer_email_pattern";
						co_await _fThis(_fThis, Parameters, {}, Value.template f_GetAsType<CGenericRule_CommitterEmail>().m_StringMatch);
					}
					else if (Value.template f_IsOfType<CGenericRule_BranchName>())
					{
						RuleType = "branch_name_pattern";
						co_await _fThis(_fThis, Parameters, {}, Value.template f_GetAsType<CGenericRule_BranchName>().m_StringMatch);
					}
					else if (Value.template f_IsOfType<CGenericRule_TagName>())
					{
						RuleType = "tag_name_pattern";
						co_await _fThis(_fThis, Parameters, {}, Value.template f_GetAsType<CGenericRule_TagName>().m_StringMatch);
					}
					else if (Value.template f_IsOfType<CGenericRule_Workflow>())
					{
						RuleType = "workflows";
						CGenericRule_Workflow const &Workflow = Value.template f_GetAsType<CGenericRule_Workflow>();

						co_await _fThis(_fThis, Parameters, "workflows", Workflow.m_Workflows);
					}

					Output["type"] = RuleType;
					if (Parameters.f_IsValid())
						Output["parameters"] = fg_Move(Parameters);
				}
				else
					static_assert(cIsSame<CType, void>, "Unknown type");

				co_return {};
			}
		;

		CJSONSorted Output;

		co_await fAddOptional(fAddOptional, Output, "name", _Ruleset.m_Name);
		co_await fAddOptional(fAddOptional, Output, "bypass_actors", _Ruleset.m_BypassActors);
		if (_Ruleset.m_IncludeRefNames || _Ruleset.m_ExcludeRefNames)
		{
			auto &ConditionsOut = Output["conditions"]["ref_name"];
			ConditionsOut["include"] = EJSONType_Array;
			ConditionsOut["exclude"] = EJSONType_Array;
			co_await fAddOptional(fAddOptional, ConditionsOut, "include", _Ruleset.m_IncludeRefNames);
			co_await fAddOptional(fAddOptional, ConditionsOut, "exclude", _Ruleset.m_ExcludeRefNames);
		}
		co_await fAddOptional(fAddOptional, Output, "rules", _Ruleset.m_Rules);
		co_await fAddOptional(fAddOptional, Output, "target", _Ruleset.m_Target);
		co_await fAddOptional(fAddOptional, Output, "enforcement", _Ruleset.m_Enforcement);

		co_return fg_Move(Output);
	}

	static constexpr CGitHostingProvider_GitHub::CFieldTranslationPair gc_FieldTranslations_Ruleset[] =
		{
			{gc_Str<"bypass_actors">, gc_Str<"BypassActors">}
			, {gc_Str<"enforcement">, gc_Str<"Enforcement">}
			, {gc_Str<"exclude">, gc_Str<"ExcludeRefNames">}
			, {gc_Str<"include">, gc_Str<"IncludeRefNames">}
			, {gc_Str<"name">, gc_Str<"Name">}
			, {gc_Str<"rules">, gc_Str<"Rules">}
			, {gc_Str<"target">, gc_Str<"Target">}
		}
	;

	TCFuture<void> CGitHostingProvider_GitHub::f_UpdateGenericRuleset(CStr const &_Repository, CStr const &_ID, CGenericRuleset const &_Ruleset)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJSONSorted RulesetJson = co_await fp_PopulateRest_GenericRuleset(RepositorySlug.m_Owner, _Ruleset);

		co_await fp_RestApiPut
			(
				"repos/{}/{}/rulesets/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _ID
				, fg_Move(RulesetJson)
				, "Failed to update generic ruleset"
#ifdef DCompiler_MSVC_Workaround
				, fsp_FieldTranslations(gc_FieldTranslations_Ruleset)
#else
				, fsp_FieldTranslations<gc_FieldTranslations_Ruleset>()
#endif
				, 200
			)
		;

		co_return {};
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::f_CreateGenericRuleset(CStr const &_Repository, CGenericRuleset const &_Ruleset)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJSONSorted RulesetJson = co_await fp_PopulateRest_GenericRuleset(RepositorySlug.m_Owner, _Ruleset);

		co_await fp_RestApiPost
			(
				"repos/{}/{}/rulesets"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
				, fg_Move(RulesetJson)
				, "Failed to create generic ruleset"
#ifdef DCompiler_MSVC_Workaround
				, fsp_FieldTranslations(gc_FieldTranslations_Ruleset)
#else
				, fsp_FieldTranslations<gc_FieldTranslations_Ruleset>()
#endif
			)
		;

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DeleteGenericRuleset(CStr const &_Repository, CStr const &_ID)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		co_await fp_RestApiDelete("repos/{}/{}/rulesets/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _ID, "Failed to delete generic ruleset");

		co_return {};
	}
}
