// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		using CProvider = CGitHostingProvider;

		/// An operator the model doesn't know goes to o_UnmanageableReasons: the default it leaves would read as contains
		CProvider::CStringMatch fg_ParseStringMatch(CJsonSorted const *_pParameters, TCVector<CStr> &o_UnmanageableReasons)
		{
			if (!_pParameters)
				return {};

			auto &Parameters = *_pParameters;

			CProvider::CStringMatch Return;
			Return.m_Pattern = Parameters["pattern"].f_String();

			auto &Operator = Parameters["operator"].f_String();
			if (Operator == "starts_with")
				Return.m_Operator = CProvider::EStringMatchOperator::mc_StartsWith;
			else if (Operator == "ends_with")
				Return.m_Operator = CProvider::EStringMatchOperator::mc_EndsWith;
			else if (Operator == "contains")
				Return.m_Operator = CProvider::EStringMatchOperator::mc_Contains;
			else if (Operator == "regex")
				Return.m_Operator = CProvider::EStringMatchOperator::mc_Regex;
			else
				o_UnmanageableReasons.f_Insert("String match operator '{}'"_f << Operator);

			if (auto *pValue = Parameters.f_GetMember("name"))
				Return.m_Name = pValue->f_String();

			if (auto *pValue = Parameters.f_GetMember("negate"))
				Return.m_Negate = pValue->f_Boolean();

			return Return;
		}

		void fg_StringMatchJson(CProvider::CStringMatch const &_StringMatch, CJsonSorted &o_Parameters)
		{
			if (_StringMatch.m_Name)
				o_Parameters["name"] = *_StringMatch.m_Name;

			if (_StringMatch.m_Negate)
				o_Parameters["negate"] = *_StringMatch.m_Negate;

			switch (_StringMatch.m_Operator)
			{
			case CProvider::EStringMatchOperator::mc_StartsWith: o_Parameters["operator"] = "starts_with"; break;
			case CProvider::EStringMatchOperator::mc_EndsWith: o_Parameters["operator"] = "ends_with"; break;
			case CProvider::EStringMatchOperator::mc_Contains: o_Parameters["operator"] = "contains"; break;
			case CProvider::EStringMatchOperator::mc_Regex: o_Parameters["operator"] = "regex"; break;
			}

			o_Parameters["pattern"] = _StringMatch.m_Pattern;
		}

		// GitHub takes the IDs of actors, apps and repositories as numbers, which fp_ResolveGenericRulesetIDs looks up from their names
		int64 fg_JsonID(CStr const &_ID, CStr const &_Description)
		{
			if (_ID.f_IsEmpty())
				DMibError("{} has no ID"_f << _Description);

			return _ID.f_ToInt(int64(0));
		}

		CJsonSorted fg_BypassActorJson(CProvider::CGenericRuleBypassActor const &_BypassActor)
		{
			CJsonSorted Output;

			auto &Actor = _BypassActor.m_Actor;
			if (Actor.f_IsOfType<CProvider::CTeam>())
			{
				auto &Team = Actor.f_GetAsType<CProvider::CTeam>();
				Output["actor_type"] = "Team";
				Output["actor_id"] = fg_JsonID(Team.m_ID, "Bypass team '{}'"_f << Team.m_Slug);
			}
			else if (Actor.f_IsOfType<CProvider::CApp>())
			{
				auto &App = Actor.f_GetAsType<CProvider::CApp>();
				Output["actor_type"] = "Integration";
				Output["actor_id"] = fg_JsonID(App.m_ID, "Bypass app '{}'"_f << App.m_Slug);
			}
			else if (Actor.f_IsOfType<CProvider::CRepositoryRole>())
			{
				auto &Role = Actor.f_GetAsType<CProvider::CRepositoryRole>();
				Output["actor_type"] = "RepositoryRole";
				Output["actor_id"] = fg_JsonID(Role.m_ID, "Bypass repository role '{}'"_f << Role.m_Name);
			}
			else if (Actor.f_IsOfType<CProvider::COrganizationAdmin>())
			{
				Output["actor_type"] = "OrganizationAdmin";
				Output["actor_id"] = 1;
			}
			else if (Actor.f_IsOfType<CProvider::CUser>())
			{
				auto &User = Actor.f_GetAsType<CProvider::CUser>();
				Output["actor_type"] = "User";
				Output["actor_id"] = fg_JsonID(User.m_ID, "Bypass user '{}'"_f << User.m_Login);
			}
			else if (Actor.f_IsOfType<CProvider::CDeployKey>())
			{
				Output["actor_type"] = "DeployKey";
				Output["actor_id"] = nullptr;
			}

			switch (_BypassActor.m_BypassMode)
			{
			case CProvider::EGenericRuleBypassMode::mc_Always: Output["bypass_mode"] = "always"; break;
			case CProvider::EGenericRuleBypassMode::mc_PullRequest: Output["bypass_mode"] = "pull_request"; break;
			}

			return Output;
		}

		CStr fg_RuleType(CProvider::CGenericRule const &_Rule)
		{
			if (_Rule.f_IsOfType<CProvider::CGenericRule_Creation>())
				return gc_Str<"creation">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_Update>())
				return gc_Str<"update">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_Deletion>())
				return gc_Str<"deletion">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_LinearHistory>())
				return gc_Str<"required_linear_history">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_Deployments>())
				return gc_Str<"required_deployments">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_Signatures>())
				return gc_Str<"required_signatures">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_PullRequest>())
				return gc_Str<"pull_request">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_StatusChecks>())
				return gc_Str<"required_status_checks">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_FastForwardOnly>())
				return gc_Str<"non_fast_forward">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_CommitMessage>())
				return gc_Str<"commit_message_pattern">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_CommitAuthorEmail>())
				return gc_Str<"commit_author_email_pattern">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_CommitterEmail>())
				return gc_Str<"committer_email_pattern">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_BranchName>())
				return gc_Str<"branch_name_pattern">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_TagName>())
				return gc_Str<"tag_name_pattern">;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_Workflow>())
				return gc_Str<"workflows">;

			return {};
		}

		CJsonSorted fg_RuleJson(CProvider::CGenericRule const &_Rule, CProvider::CGenericRulesetUnmodelled const &_Unmodelled)
		{
			CStr RuleType = fg_RuleType(_Rule);
			CJsonSorted Parameters;

			if (_Rule.f_IsOfType<CProvider::CGenericRule_Update>())
				Parameters["update_allows_fetch_and_merge"] = _Rule.f_GetAsType<CProvider::CGenericRule_Update>().m_bAllowFetchAndMerge;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_Deployments>())
				Parameters["required_deployment_environments"] = _Rule.f_GetAsType<CProvider::CGenericRule_Deployments>().m_RequiredDeploymentEnvironments;
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_PullRequest>())
			{
				auto &PullRequest = _Rule.f_GetAsType<CProvider::CGenericRule_PullRequest>();
				Parameters["dismiss_stale_reviews_on_push"] = PullRequest.m_bDismissStaleReviewsOnPush;
				Parameters["require_code_owner_review"] = PullRequest.m_bRequireCodeOwnerReview;
				Parameters["require_last_push_approval"] = PullRequest.m_bRequireLastPushApproval;
				Parameters["required_approving_review_count"] = PullRequest.m_RequiredApprovingReviewCount;
				Parameters["required_review_thread_resolution"] = PullRequest.m_bRequireReviewThreadResolution;

				if (PullRequest.m_AllowedMergeMethods)
				{
					auto &AllowedMergeMethods = Parameters["allowed_merge_methods"];
					AllowedMergeMethods = EJsonType_Array;
					for (auto &Method : *PullRequest.m_AllowedMergeMethods)
					{
						switch (Method)
						{
						case CProvider::EGenericRuleMergeMethod::mc_Merge: AllowedMergeMethods.f_Array().f_Insert("merge"); break;
						case CProvider::EGenericRuleMergeMethod::mc_Squash: AllowedMergeMethods.f_Array().f_Insert("squash"); break;
						case CProvider::EGenericRuleMergeMethod::mc_Rebase: AllowedMergeMethods.f_Array().f_Insert("rebase"); break;
						}
					}
				}
			}
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_StatusChecks>())
			{
				auto &StatusChecks = _Rule.f_GetAsType<CProvider::CGenericRule_StatusChecks>();
				Parameters["strict_required_status_checks_policy"] = StatusChecks.m_bPullRequestsMustBeTestedWithLatestCode;

				auto &Checks = Parameters["required_status_checks"];
				Checks = EJsonType_Array;
				for (auto &Check : StatusChecks.m_RequiredStatusChecks)
				{
					auto &OutCheck = Checks.f_Array().f_Insert();
					OutCheck["context"] = Check.m_Context;
					if (Check.m_App)
						OutCheck["integration_id"] = fg_JsonID(Check.m_App->m_ID, "App '{}' of status check '{}'"_f << Check.m_App->m_Slug << Check.m_Context);
				}
			}
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_CommitMessage>())
				fg_StringMatchJson(_Rule.f_GetAsType<CProvider::CGenericRule_CommitMessage>().m_StringMatch, Parameters);
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_CommitAuthorEmail>())
				fg_StringMatchJson(_Rule.f_GetAsType<CProvider::CGenericRule_CommitAuthorEmail>().m_StringMatch, Parameters);
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_CommitterEmail>())
				fg_StringMatchJson(_Rule.f_GetAsType<CProvider::CGenericRule_CommitterEmail>().m_StringMatch, Parameters);
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_BranchName>())
				fg_StringMatchJson(_Rule.f_GetAsType<CProvider::CGenericRule_BranchName>().m_StringMatch, Parameters);
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_TagName>())
				fg_StringMatchJson(_Rule.f_GetAsType<CProvider::CGenericRule_TagName>().m_StringMatch, Parameters);
			else if (_Rule.f_IsOfType<CProvider::CGenericRule_Workflow>())
			{
				auto &Workflows = Parameters["workflows"];
				Workflows = EJsonType_Array;
				for (auto &Workflow : _Rule.f_GetAsType<CProvider::CGenericRule_Workflow>().m_Workflows)
				{
					auto &OutWorkflow = Workflows.f_Array().f_Insert();
					OutWorkflow["path"] = Workflow.m_Path;
					OutWorkflow["repository_id"] = fg_JsonID
						(
							Workflow.m_WorkflowRepository.m_ID
							, "Repository '{}' of workflow '{}'"_f << Workflow.m_WorkflowRepository.m_Slug << Workflow.m_Path
						)
					;

					if (Workflow.m_Ref)
						OutWorkflow["ref"] = *Workflow.m_Ref;

					if (Workflow.m_Sha)
						OutWorkflow["sha"] = *Workflow.m_Sha;
				}
			}

			if (auto *pUnmodelledParameters = _Unmodelled.m_RuleParameters.f_FindEqual(RuleType))
			{
				for (auto &Parameter : pUnmodelledParameters->f_Object())
				{
					if (!Parameters.f_IsValid() || !Parameters.f_GetMember(Parameter.f_Name()))
						Parameters[Parameter.f_Name()] = Parameter.f_Value();
				}
			}

			CJsonSorted Output;
			Output["type"] = RuleType;
			if (Parameters.f_IsValid())
				Output["parameters"] = fg_Move(Parameters);

			return Output;
		}
	}

	/// The ruleset GitHub returns for rulesets/{id}. Rules and parameters the model doesn't know are kept in m_Unmodelled. Values it
	/// can't keep through an update, such as a bypass actor of a type it doesn't know, are listed in m_UnmanageableReasons.
	auto CGitHostingProvider_GitHub::fs_ParseGenericRuleset(CJsonSorted const &_Json) -> CGenericRuleset
	{
		CGenericRuleset OutRuleset;

		auto fUnmanageable = [&](CStr const &_Reason)
			{
				OutRuleset.m_UnmanageableReasons.f_Insert(_Reason);
			}
		;

		// An update writes the entries of an array parameter from the model, so it would drop the fields the model doesn't know
		auto fUnmodelledFields = [&](CJsonSorted const &_Entry, TCVector<CStr> const &_ModelledFields, CStr const &_Description)
			{
				if (!_Entry.f_IsObject())
					return;

				for (auto &Field : _Entry.f_Object())
				{
					if (_ModelledFields.f_Contains(Field.f_Name()) < 0)
						fUnmanageable("{} field '{}'"_f << _Description << Field.f_Name());
				}
			}
		;

		OutRuleset.m_Name = _Json["name"].f_String();
		{
			auto &Enforcement = _Json["enforcement"].f_String();
			if (Enforcement == "disabled")
				OutRuleset.m_Enforcement = EGenericRuleEnforcement::mc_Disabled;
			else if (Enforcement == "active")
				OutRuleset.m_Enforcement = EGenericRuleEnforcement::mc_Active;
			else if (Enforcement == "evaluate")
				OutRuleset.m_Enforcement = EGenericRuleEnforcement::mc_Evaluate;
			else
				fUnmanageable("Enforcement '{}'"_f << Enforcement);
		}

		if (auto *pTarget = _Json.f_GetMember("target"))
		{
			auto &Target = pTarget->f_String();
			if (Target == "branch")
				OutRuleset.m_Target = EGenericRuleTarget::mc_Branch;
			else if (Target == "tag")
				OutRuleset.m_Target = EGenericRuleTarget::mc_Tag;
			else
				fUnmanageable("Target '{}'"_f << Target);
		}

		if (auto *pBypassActors = _Json.f_GetMember("bypass_actors"))
		{
			TCVector<CGenericRuleBypassActor> Actors;
			for (auto &BypassActor : pBypassActors->f_Array())
			{
				CGenericRuleBypassActor OutActor;

				auto &ActorType = BypassActor["actor_type"].f_String();

				auto fGetActorID = [&]() -> CStr
					{
						return CStr::fs_ToStr(BypassActor["actor_id"].f_Integer());
					}
				;

				if (ActorType == "RepositoryRole")
					OutActor.m_Actor = CRepositoryRole{.m_ID = fGetActorID()};
				else if (ActorType == "Team")
					OutActor.m_Actor = CTeam{.m_ID = fGetActorID()};
				else if (ActorType == "Integration")
					OutActor.m_Actor = CApp{.m_ID = fGetActorID()};
				else if (ActorType == "OrganizationAdmin")
					OutActor.m_Actor = COrganizationAdmin{};
				else if (ActorType == "User")
					OutActor.m_Actor = CUser{.m_ID = fGetActorID()};
				else if (ActorType == "DeployKey")
					OutActor.m_Actor = CDeployKey{};
				else
				{
					fUnmanageable("Bypass actor of type '{}'"_f << ActorType);
					continue;
				}

				auto &BypassMode = BypassActor["bypass_mode"].f_String();
				if (BypassMode == "always")
					OutActor.m_BypassMode = EGenericRuleBypassMode::mc_Always;
				else if (BypassMode == "pull_request")
					OutActor.m_BypassMode = EGenericRuleBypassMode::mc_PullRequest;
				else
				{
					fUnmanageable("Bypass mode '{}' of {}"_f << BypassMode << OutActor.m_Actor);
					continue;
				}

				Actors.f_Insert(fg_Move(OutActor));
			}

			Actors.f_Sort();
			OutRuleset.m_BypassActors = fg_Move(Actors);
		}

		if (auto *pConditions = _Json.f_GetMember("conditions"))
		{
			if (auto *pRefName = pConditions->f_GetMember("ref_name"))
			{
				// Their order means nothing, and the policy sorts them, so sort them to compare
				if (auto *pInclude = pRefName->f_GetMember("include"))
				{
					OutRuleset.m_IncludeRefNames = pInclude->f_StringArray();
					OutRuleset.m_IncludeRefNames->f_Sort();
				}

				if (auto *pExclude = pRefName->f_GetMember("exclude"))
				{
					OutRuleset.m_ExcludeRefNames = pExclude->f_StringArray();
					OutRuleset.m_ExcludeRefNames->f_Sort();
				}
			}
		}

		if (auto *pRules = _Json.f_GetMember("rules"))
		{
			TCVector<CGenericRule> Rules;
			TCSet<CStr> RuleTypes;
			for (auto &Rule : pRules->f_Array())
			{
				auto &RuleType = Rule["type"].f_String();
				auto *pParameters = Rule.f_GetMember("parameters");

				// m_Unmodelled keeps unknown rules and parameters by rule type, so it can't keep two rules of one type through an update
				if (!RuleTypes(RuleType).f_WasCreated())
					fUnmanageable("Rule type '{}' more than once"_f << RuleType);

				TCVector<CStr> ModelledParameters;
				auto const StringMatchParameters = TCVector<CStr>{"name", "negate", "operator", "pattern"};

				if (RuleType == "creation")
					Rules.f_Insert(CGenericRule_Creation{});
				else if (RuleType == "update")
				{
					CGenericRule_Update Update;
					if (pParameters)
						Update.m_bAllowFetchAndMerge = (*pParameters)["update_allows_fetch_and_merge"].f_Boolean();
					Rules.f_Insert(fg_Move(Update));
					ModelledParameters = {"update_allows_fetch_and_merge"};
				}
				else if (RuleType == "deletion")
					Rules.f_Insert(CGenericRule_Deletion{});
				else if (RuleType == "required_linear_history")
					Rules.f_Insert(CGenericRule_LinearHistory{});
				else if (RuleType == "required_deployments")
				{
					CGenericRule_Deployments Deployments;
					if (pParameters)
					{
						Deployments.m_RequiredDeploymentEnvironments = (*pParameters)["required_deployment_environments"].f_StringArray();
						Deployments.m_RequiredDeploymentEnvironments.f_Sort();
					}
					Rules.f_Insert(fg_Move(Deployments));
					ModelledParameters = {"required_deployment_environments"};
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

						if (auto *pAllowedMergeMethods = pParameters->f_GetMember("allowed_merge_methods"))
						{
							auto &AllowedMergeMethods = PullRequest.m_AllowedMergeMethods.f_CreateNew();
							for (auto &Method : pAllowedMergeMethods->f_StringArray())
							{
								if (Method == "merge")
									AllowedMergeMethods.f_Insert(EGenericRuleMergeMethod::mc_Merge);
								else if (Method == "squash")
									AllowedMergeMethods.f_Insert(EGenericRuleMergeMethod::mc_Squash);
								else if (Method == "rebase")
									AllowedMergeMethods.f_Insert(EGenericRuleMergeMethod::mc_Rebase);
								else
									fUnmanageable("Allowed merge method '{}'"_f << Method);
							}
							AllowedMergeMethods.f_Sort();
						}
					}
					Rules.f_Insert(fg_Move(PullRequest));
					ModelledParameters =
						{
							"allowed_merge_methods"
							, "dismiss_stale_reviews_on_push"
							, "require_code_owner_review"
							, "require_last_push_approval"
							, "required_approving_review_count"
							, "required_review_thread_resolution"
						}
					;
				}
				else if (RuleType == "required_status_checks")
				{
					CGenericRule_StatusChecks StatusChecks;
					if (pParameters)
					{
						StatusChecks.m_bPullRequestsMustBeTestedWithLatestCode = (*pParameters)["strict_required_status_checks_policy"].f_Boolean();
						for (auto &Check : (*pParameters)["required_status_checks"].f_Array())
						{
							fUnmodelledFields(Check, {"context", "integration_id"}, "Required status check");

							auto &OutCheck = StatusChecks.m_RequiredStatusChecks.f_Insert();
							OutCheck.m_Context = Check["context"].f_String();
							if (auto *pIntegrationID = Check.f_GetMember("integration_id"))
								OutCheck.m_App = CApp{.m_ID = CStr::fs_ToStr(pIntegrationID->f_Integer())};
						}
						StatusChecks.m_RequiredStatusChecks.f_Sort();
					}
					Rules.f_Insert(fg_Move(StatusChecks));
					ModelledParameters = {"required_status_checks", "strict_required_status_checks_policy"};
				}
				else if (RuleType == "non_fast_forward")
					Rules.f_Insert(CGenericRule_FastForwardOnly{});
				else if (RuleType == "commit_message_pattern")
				{
					Rules.f_Insert(CGenericRule_CommitMessage{.m_StringMatch = fg_ParseStringMatch(pParameters, OutRuleset.m_UnmanageableReasons)});
					ModelledParameters = StringMatchParameters;
				}
				else if (RuleType == "commit_author_email_pattern")
				{
					Rules.f_Insert(CGenericRule_CommitAuthorEmail{.m_StringMatch = fg_ParseStringMatch(pParameters, OutRuleset.m_UnmanageableReasons)});
					ModelledParameters = StringMatchParameters;
				}
				else if (RuleType == "committer_email_pattern")
				{
					Rules.f_Insert(CGenericRule_CommitterEmail{.m_StringMatch = fg_ParseStringMatch(pParameters, OutRuleset.m_UnmanageableReasons)});
					ModelledParameters = StringMatchParameters;
				}
				else if (RuleType == "branch_name_pattern")
				{
					Rules.f_Insert(CGenericRule_BranchName{.m_StringMatch = fg_ParseStringMatch(pParameters, OutRuleset.m_UnmanageableReasons)});
					ModelledParameters = StringMatchParameters;
				}
				else if (RuleType == "tag_name_pattern")
				{
					Rules.f_Insert(CGenericRule_TagName{.m_StringMatch = fg_ParseStringMatch(pParameters, OutRuleset.m_UnmanageableReasons)});
					ModelledParameters = StringMatchParameters;
				}
				else if (RuleType == "workflows")
				{
					CGenericRule_Workflow Workflow;
					if (pParameters)
					{
						for (auto &WorkflowObj : (*pParameters)["workflows"].f_Array())
						{
							fUnmodelledFields(WorkflowObj, {"path", "ref", "repository_id", "sha"}, "Workflow");

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
					ModelledParameters = {"workflows"};
				}
				else
				{
					OutRuleset.m_Unmodelled.m_Rules[RuleType] = Rule;
					continue;
				}

				if (!pParameters || !pParameters->f_IsObject())
					continue;

				CJsonSorted UnmodelledParameters;
				for (auto &Parameter : pParameters->f_Object())
				{
					if (ModelledParameters.f_Contains(Parameter.f_Name()) < 0)
						UnmodelledParameters[Parameter.f_Name()] = Parameter.f_Value();
				}

				if (UnmodelledParameters.f_IsValid())
					OutRuleset.m_Unmodelled.m_RuleParameters[RuleType] = fg_Move(UnmodelledParameters);
			}

			Rules.f_Sort();

			OutRuleset.m_Rules = fg_Move(Rules);
		}

		return OutRuleset;
	}

	/// The body of a ruleset create or update. Its teams, apps, roles and users, status check apps and workflow repositories must have their
	/// IDs, which fp_ResolveGenericRulesetIDs looks up. Unknown rules go in unchanged when the ruleset lists its rules, and unknown
	/// parameters with the rules they belong to.
	CJsonSorted CGitHostingProvider_GitHub::fs_GenericRulesetJson(CGenericRuleset const &_Ruleset)
	{
		CJsonSorted Output;

		if (_Ruleset.m_Name)
			Output["name"] = *_Ruleset.m_Name;

		if (_Ruleset.m_BypassActors)
		{
			auto &BypassActors = Output["bypass_actors"];
			BypassActors = EJsonType_Array;
			for (auto &BypassActor : *_Ruleset.m_BypassActors)
				BypassActors.f_Array().f_Insert(fg_BypassActorJson(BypassActor));
		}

		if (_Ruleset.m_IncludeRefNames || _Ruleset.m_ExcludeRefNames)
		{
			auto &RefNames = Output["conditions"]["ref_name"];
			RefNames["include"] = EJsonType_Array;
			RefNames["exclude"] = EJsonType_Array;

			if (_Ruleset.m_IncludeRefNames)
				RefNames["include"] = *_Ruleset.m_IncludeRefNames;

			if (_Ruleset.m_ExcludeRefNames)
				RefNames["exclude"] = *_Ruleset.m_ExcludeRefNames;
		}

		if (_Ruleset.m_Rules)
		{
			auto &Rules = Output["rules"];
			Rules = EJsonType_Array;
			for (auto &Rule : *_Ruleset.m_Rules)
				Rules.f_Array().f_Insert(fg_RuleJson(Rule, _Ruleset.m_Unmodelled));

			for (auto &Rule : _Ruleset.m_Unmodelled.m_Rules)
				Rules.f_Array().f_Insert(Rule);
		}

		if (_Ruleset.m_Target)
		{
			switch (*_Ruleset.m_Target)
			{
			case EGenericRuleTarget::mc_Branch: Output["target"] = "branch"; break;
			case EGenericRuleTarget::mc_Tag: Output["target"] = "tag"; break;
			}
		}

		if (_Ruleset.m_Enforcement)
		{
			switch (*_Ruleset.m_Enforcement)
			{
			case EGenericRuleEnforcement::mc_Disabled: Output["enforcement"] = "disabled"; break;
			case EGenericRuleEnforcement::mc_Active: Output["enforcement"] = "active"; break;
			case EGenericRuleEnforcement::mc_Evaluate: Output["enforcement"] = "evaluate"; break;
			}
		}

		return Output;
	}

	/// Also drops the unmodelled parameters of the rules _Ruleset doesn't list, since an update can only write them with their rules.
	auto CGitHostingProvider_GitHub::fp_ResolveGenericRulesetIDs(CStr _Organization, CGenericRuleset _Ruleset) -> TCFuture<CGenericRuleset>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

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
						Actor.m_ID = co_await fp_GetActorID(_Organization, BypassActor.m_Actor, false, pRoleCache);
						Actor.m_Slug.f_Clear();
					}
				}
				else if (BypassActor.m_Actor.f_IsOfType<CApp>())
				{
					auto &Actor = BypassActor.m_Actor.f_GetAsType<CApp>();
					if (!Actor.m_ID)
					{
						Actor.m_ID = co_await fp_GetActorID(_Organization, BypassActor.m_Actor, false, pRoleCache);
						Actor.m_Slug.f_Clear();
					}
				}
				else if (BypassActor.m_Actor.f_IsOfType<CRepositoryRole>())
				{
					auto &Actor = BypassActor.m_Actor.f_GetAsType<CRepositoryRole>();
					if (!Actor.m_ID)
					{
						Actor.m_ID = co_await fp_GetActorID(_Organization, BypassActor.m_Actor, false, pRoleCache);
						Actor.m_Name.f_Clear();
					}
				}
				else if (BypassActor.m_Actor.f_IsOfType<CUser>())
				{
					auto &Actor = BypassActor.m_Actor.f_GetAsType<CUser>();
					if (!Actor.m_ID)
					{
						Actor.m_ID = co_await fp_GetActorID(_Organization, BypassActor.m_Actor, false, pRoleCache);
						Actor.m_Login.f_Clear();
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
				else if (Rule.f_IsOfType<CGenericRule_Workflow>())
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
			}

			Ruleset.m_Rules->f_Sort();

			TCSet<CStr> RuleTypes;
			for (auto &Rule : *Ruleset.m_Rules)
				RuleTypes[fg_RuleType(Rule)];

			TCVector<CStr> RemovedRuleTypes;
			for (auto &Parameters : Ruleset.m_Unmodelled.m_RuleParameters)
			{
				auto &RuleType = Ruleset.m_Unmodelled.m_RuleParameters.fs_GetKey(Parameters);
				if (!RuleTypes.f_FindEqual(RuleType))
					RemovedRuleTypes.f_Insert(RuleType);
			}

			for (auto &RuleType : RemovedRuleTypes)
				Ruleset.m_Unmodelled.m_RuleParameters.f_Remove(RuleType);
		}

		co_return fg_Move(Ruleset);
	}

	auto CGitHostingProvider_GitHub::f_PopulateGenericRulesetIDs(CStr _Repository, CGenericRuleset _Ruleset) -> TCFuture<CGenericRuleset>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		co_return co_await fp_ResolveGenericRulesetIDs(RepositorySlug.m_Owner, fg_Move(_Ruleset));
	}

	auto CGitHostingProvider_GitHub::f_GetGenericRulesets(NStr::CStr _Repository) -> NConcurrency::TCFuture<NContainer::TCMap<NStr::CStr, CGenericRuleset>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto CaptureScope = co_await (g_CaptureExceptions % ("Error parsing rulesets for repository '{}'"_f << _Repository));

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto const RuleSetEnum = co_await
			(
				fp_RestApi("repos/{}/{}/rulesets"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name, "Failed to get rulesets from repository '{}'"_f << _Repository)
			)
		;

		NContainer::TCMap<NStr::CStr, CGenericRuleset> OutRulesets;

		TCFutureVector<CJsonSorted> Results;

		for (auto &Rule : RuleSetEnum.f_Array())
		{
			fp_RestApi
				(
					"repos/{}/{}/rulesets/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << Rule["id"].f_Integer()
					, "Failed to get ruleset from repository '{}'"_f << _Repository
				)
				> Results
			;
		}

		auto const Rulesets = co_await fg_AllDone(Results);

		for (auto &Ruleset : Rulesets)
		{
			if (auto *pSourceType = Ruleset.f_GetMember("source_type"))
			{
				if (*pSourceType != "Repository")
					continue;
			}

			OutRulesets[CStr::fs_ToStr(Ruleset["id"].f_Integer())] = fs_ParseGenericRuleset(Ruleset);
		}

		co_return fg_Move(OutRulesets);
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

	TCFuture<void> CGitHostingProvider_GitHub::f_UpdateGenericRuleset(CStr _Repository, CStr _ID, CGenericRuleset _Ruleset)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if (!_Ruleset.m_UnmanageableReasons.f_IsEmpty())
		{
			co_return DMibErrorInstance
				(
					"Generic ruleset '{}' of '{}' wasn't updated, since that would lose what the model can't express: {}"_f
					<< _ID
					<< _Repository
					<< CStr::fs_Join(_Ruleset.m_UnmanageableReasons, ", ")
				)
			;
		}

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJsonSorted RulesetJson = fs_GenericRulesetJson(co_await fp_ResolveGenericRulesetIDs(RepositorySlug.m_Owner, fg_Move(_Ruleset)));

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

	TCFuture<CStr> CGitHostingProvider_GitHub::f_CreateGenericRuleset(CStr _Repository, CGenericRuleset _Ruleset)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJsonSorted RulesetJson = fs_GenericRulesetJson(co_await fp_ResolveGenericRulesetIDs(RepositorySlug.m_Owner, fg_Move(_Ruleset)));

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

	/// From the listing, without reading the rulesets in full. Only the repository's own: GitHub lists the rulesets it inherits from its
	/// organization as well unless told not to, and a ruleset that doesn't say it's the repository's isn't the repository's to delete.
	auto CGitHostingProvider_GitHub::f_GetGenericRulesetNames(CStr _Repository) -> TCFuture<TCMap<CStr, CStr>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto const Rulesets = co_await fp_RestApi
			(
				"repos/{}/{}/rulesets"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
				, "Failed to get rulesets from repository '{}'"_f << _Repository
				, {{"includes_parents", "false"}}
			)
		;

		auto CaptureScope = co_await (g_CaptureExceptions % ("Error parsing rulesets for repository '{}'"_f << _Repository));

		TCMap<CStr, CStr> Names;
		for (auto &Ruleset : Rulesets.f_Array())
		{
			if (fsp_GetJsonString(Ruleset, "source_type") == "Repository")
				Names[CStr::fs_ToStr(Ruleset["id"].f_Integer())] = Ruleset["name"].f_String();
		}

		co_return fg_Move(Names);
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DeleteGenericRuleset(CStr _Repository, CStr _ID)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		co_await fp_RestApiDelete("repos/{}/{}/rulesets/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _ID, "Failed to delete generic ruleset");

		co_return {};
	}
}
