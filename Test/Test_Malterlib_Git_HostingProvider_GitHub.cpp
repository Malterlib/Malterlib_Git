// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Core/OnScopeExitCatch>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Cryptography/PublicCrypto>
#include <Mib/Encoding/Base64>
#include <Mib/File/File>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/Test/Exception>
#include <Mib/Web/HttpClient>

// The provider headers use the namespace shortcuts their library is compiled with, which tests are not
using namespace NMib;
using namespace NMib::NConcurrency;
using namespace NMib::NContainer;
using namespace NMib::NEncoding;
using namespace NMib::NException;
using namespace NMib::NFile;
using namespace NMib::NStorage;
using namespace NMib::NStr;
using namespace NMib::NWeb;

#include "../Source/Malterlib_Git_HostingProvider_GitHub.h"
#include "../Source/Malterlib_Git_Policy.h"

// Futures of types in an anonymous namespace have no type hash, so the mocked provider is in a named one
namespace NGitHubPolicyTests
{
	using CGitHub = NMib::NGit::CGitHostingProvider_GitHub;
	using CProvider = NMib::NGit::CGitHostingProvider;

	/// The GitHub provider with the read of an App private key file held until f_ReleaseKey
	struct CKeyGateGitHub : public CGitHub
	{
		TCFuture<void> f_WaitForKeyRead();
		TCFuture<void> f_ReleaseKey(CSecureByteVector _Key);

		TCFuture<CSecureByteVector> fp_ReadAppPrivateKeyFile(CStr _Path) override;

		TCPromiseFuturePair<void> m_KeyReadStarted;
		TCOptional<TCPromise<CSecureByteVector>> m_Key;
	};

	TCFuture<void> CKeyGateGitHub::f_WaitForKeyRead()
	{
		co_await fg_Move(m_KeyReadStarted.m_Future);
		co_return {};
	}

	TCFuture<void> CKeyGateGitHub::f_ReleaseKey(CSecureByteVector _Key)
	{
		if (!m_Key)
			co_return DMibErrorInstance("No key read is waiting");

		m_Key->f_SetResult(fg_Move(_Key));
		co_return {};
	}

	TCFuture<CSecureByteVector> CKeyGateGitHub::fp_ReadAppPrivateKeyFile(CStr _Path)
	{
		TCPromiseFuturePair<CSecureByteVector> Key;
		m_Key = fg_Move(Key.m_Promise);
		m_KeyReadStarted.m_Promise.f_SetResult();

		co_return co_await fg_Move(Key.m_Future);
	}

	struct CMockGitHubState
	{
		TCVector<CStr> m_Rulesets;                                             // The JSON of rulesets/{id}, as GitHub returns it
		TCMap<CStr, CProvider::CBranchProtectionRule> m_BranchProtectionRules; // By rule ID

		TCMap<CStr, CJsonSorted> m_UpdatedRulesets; // By ID: the body of the update
		TCVector<CStr> m_CreatedRulesets;
		TCVector<CStr> m_DeletedRulesets;
		TCVector<CStr> m_UpdatedBranchProtectionRules;
		TCVector<CStr> m_CreatedBranchProtectionRules;
		TCVector<CStr> m_DeletedBranchProtectionRules;
		TCVector<CStr> m_Changes;                   // Every change, in the order they were made
		TCSet<CStr> m_FailCreateRulesets;           // Names of rulesets that fail to be created
		TCSet<CStr> m_FailDeleteRulesets;           // IDs of rulesets that fail to be deleted
		TCSet<CStr> m_FailDeleteBranchProtection;   // IDs of branch protection rules that fail to be deleted
		bool m_bFailRulesetRead = false;            // Reading the rulesets in full fails, as for a ruleset it can't decode
		bool m_bFailBranchProtectionRead = false;   // Reading the branch protection rules in full fails, as for a rule it can't decode
		TCSet<CStr> m_UnknownTeams;                 // Slugs of teams whose IDs can't be looked up, which fails a create or update as on GitHub
		bool m_bFailRepositoryID = false;           // Looking up the repository's ID fails, which fails creating a branch protection rule as on GitHub
	};

	/// Resolving names to IDs isn't mocked, so policies in the tests use only actors it resolves without asking GitHub.
	struct CMockGitHub : public CGitHub
	{
		CMockGitHub(CMockGitHubState _State);

		TCFuture<TCMap<CStr, CBranchProtectionRule>> f_GetBranchProtectionRules(CStr _Repository) override;
		TCFuture<TCMap<CStr, CStr>> f_GetBranchProtectionRulePatterns(CStr _Repository) override;
		TCFuture<void> f_UpdateBranchProtectionRule(CStr _Repository, CStr _RuleID, CBranchProtectionRule _Rule) override;
		TCFuture<CStr> f_CreateBranchProtectionRule(CStr _Repository, CBranchProtectionRule _Rule) override;
		TCFuture<void> f_DeleteBranchProtectionRule(CStr _Repository, CStr _RuleID) override;
		TCFuture<void> f_ResolveBranchProtectionRuleIDs(CStr _Repository, TCOptional<CStr> _RuleID, CBranchProtectionRule _Rule) override;

		TCFuture<TCMap<CStr, CGenericRuleset>> f_GetGenericRulesets(CStr _Repository) override;
		TCFuture<TCMap<CStr, CStr>> f_GetGenericRulesetNames(CStr _Repository) override;
		TCFuture<CGenericRuleset> f_PopulateGenericRulesetIDs(CStr _Repository, CGenericRuleset _Ruleset) override;
		TCFuture<void> f_UpdateGenericRuleset(CStr _Repository, CStr _ID, CGenericRuleset _Ruleset) override;
		TCFuture<CStr> f_CreateGenericRuleset(CStr _Repository, CGenericRuleset _Ruleset) override;
		TCFuture<void> f_DeleteGenericRuleset(CStr _Repository, CStr _ID) override;

		TCFuture<CMockGitHubState> f_GetState();
		CExceptionPointer f_FindUnknownTeam(CBranchProtectionRule const &_Rule) const;
		CExceptionPointer f_FindUnknownTeam(CGenericRuleset const &_Ruleset) const;

		CMockGitHubState m_State;
	};

	CExceptionPointer fg_FindUnknownTeam(CMockGitHubState const &_State, auto const &_Actor)
	{
		using CTeam = NMib::NGit::CGitHostingProvider::CTeam;

		if (_Actor.template f_IsOfType<CTeam>() && _State.m_UnknownTeams.f_FindEqual(_Actor.template f_GetAsType<CTeam>().m_Slug))
			return DMibErrorInstance("Could not find team '{}'"_f << _Actor.template f_GetAsType<CTeam>().m_Slug);

		return {};
	}

	CMockGitHub::CMockGitHub(CMockGitHubState _State)
		: m_State(fg_Move(_State))
	{
	}

	auto CMockGitHub::f_GetBranchProtectionRules(CStr _Repository) -> TCFuture<TCMap<CStr, CBranchProtectionRule>>
	{
		if (m_State.m_bFailBranchProtectionRead)
			co_return DMibErrorInstance("A branch protection rule couldn't be read");

		co_return m_State.m_BranchProtectionRules;
	}

	auto CMockGitHub::f_GetBranchProtectionRulePatterns(CStr _Repository) -> TCFuture<TCMap<CStr, CStr>>
	{
		TCMap<CStr, CStr> Patterns;
		for (auto &Rule : m_State.m_BranchProtectionRules)
			Patterns[m_State.m_BranchProtectionRules.fs_GetKey(Rule)] = *Rule.m_Pattern;

		co_return fg_Move(Patterns);
	}

	TCFuture<void> CMockGitHub::f_UpdateBranchProtectionRule(CStr _Repository, CStr _RuleID, CBranchProtectionRule _Rule)
	{
		if (auto pError = f_FindUnknownTeam(_Rule))
			co_return pError;

		m_State.m_UpdatedBranchProtectionRules.f_Insert(_RuleID);
		m_State.m_Changes.f_Insert("Update branch protection {}"_f << _RuleID);
		co_return {};
	}

	TCFuture<CStr> CMockGitHub::f_CreateBranchProtectionRule(CStr _Repository, CBranchProtectionRule _Rule)
	{
		if (auto pError = f_FindUnknownTeam(_Rule))
			co_return pError;

		if (m_State.m_bFailRepositoryID)
			co_return DMibErrorInstance("Could not find repository '{}'"_f << _Repository);

		m_State.m_CreatedBranchProtectionRules.f_Insert(*_Rule.m_Pattern);
		m_State.m_Changes.f_Insert("Create branch protection {}"_f << *_Rule.m_Pattern);
		co_return CStr();
	}

	TCFuture<void> CMockGitHub::f_ResolveBranchProtectionRuleIDs(CStr _Repository, TCOptional<CStr> _RuleID, CBranchProtectionRule _Rule)
	{
		if (auto pError = f_FindUnknownTeam(_Rule))
			co_return pError;

		if (!_RuleID && m_State.m_bFailRepositoryID)
			co_return DMibErrorInstance("Could not find repository '{}'"_f << _Repository);

		co_return {};
	}

	TCFuture<void> CMockGitHub::f_DeleteBranchProtectionRule(CStr _Repository, CStr _RuleID)
	{
		if (m_State.m_FailDeleteBranchProtection.f_FindEqual(_RuleID))
			co_return DMibErrorInstance("Deleting branch protection rule '{}' was rejected"_f << _RuleID);

		m_State.m_DeletedBranchProtectionRules.f_Insert(_RuleID);
		m_State.m_Changes.f_Insert("Delete branch protection {}"_f << _RuleID);
		co_return {};
	}

	auto CMockGitHub::f_GetGenericRulesets(CStr _Repository) -> TCFuture<TCMap<CStr, CGenericRuleset>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if (m_State.m_bFailRulesetRead)
			co_return DMibErrorInstance("A ruleset couldn't be read");

		TCMap<CStr, CGenericRuleset> Rulesets;
		for (auto &RulesetJson : m_State.m_Rulesets)
		{
			auto Ruleset = CJsonSorted::fs_FromString(RulesetJson);
			if (Ruleset["source_type"].f_String() == "Repository")
				Rulesets[CStr::fs_ToStr(Ruleset["id"].f_Integer())] = fs_ParseGenericRuleset(Ruleset);
		}

		co_return fg_Move(Rulesets);
	}

	auto CMockGitHub::f_GetGenericRulesetNames(CStr _Repository) -> TCFuture<TCMap<CStr, CStr>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		TCMap<CStr, CStr> Names;
		for (auto &RulesetJson : m_State.m_Rulesets)
		{
			auto Ruleset = CJsonSorted::fs_FromString(RulesetJson);
			if (Ruleset["source_type"].f_String() == "Repository")
				Names[CStr::fs_ToStr(Ruleset["id"].f_Integer())] = Ruleset["name"].f_String();
		}

		co_return fg_Move(Names);
	}

	TCFuture<void> CMockGitHub::f_UpdateGenericRuleset(CStr _Repository, CStr _ID, CGenericRuleset _Ruleset)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if (auto pError = f_FindUnknownTeam(_Ruleset))
			co_return pError;

		m_State.m_UpdatedRulesets[_ID] = fs_GenericRulesetJson(_Ruleset);
		m_State.m_Changes.f_Insert("Update ruleset {}"_f << _ID);
		co_return {};
	}

	TCFuture<CStr> CMockGitHub::f_CreateGenericRuleset(CStr _Repository, CGenericRuleset _Ruleset)
	{
		if (auto pError = f_FindUnknownTeam(_Ruleset))
			co_return pError;

		if (m_State.m_FailCreateRulesets.f_FindEqual(*_Ruleset.m_Name))
			co_return DMibErrorInstance("Ruleset '{}' was rejected"_f << *_Ruleset.m_Name);

		m_State.m_CreatedRulesets.f_Insert(*_Ruleset.m_Name);
		m_State.m_Changes.f_Insert("Create ruleset {}"_f << *_Ruleset.m_Name);
		co_return CStr();
	}

	/// Resolving the IDs of other actors isn't mocked, so the tests use only those it resolves without asking GitHub
	auto CMockGitHub::f_PopulateGenericRulesetIDs(CStr _Repository, CGenericRuleset _Ruleset) -> TCFuture<CGenericRuleset>
	{
		if (auto pError = f_FindUnknownTeam(_Ruleset))
			co_return pError;

		co_return co_await CGitHub::f_PopulateGenericRulesetIDs(fg_Move(_Repository), fg_Move(_Ruleset));
	}

	TCFuture<void> CMockGitHub::f_DeleteGenericRuleset(CStr _Repository, CStr _ID)
	{
		if (m_State.m_FailDeleteRulesets.f_FindEqual(_ID))
			co_return DMibErrorInstance("Deleting ruleset '{}' was rejected"_f << _ID);

		m_State.m_DeletedRulesets.f_Insert(_ID);
		m_State.m_Changes.f_Insert("Delete ruleset {}"_f << _ID);
		co_return {};
	}

	TCFuture<CMockGitHubState> CMockGitHub::f_GetState()
	{
		co_return m_State;
	}

	CExceptionPointer CMockGitHub::f_FindUnknownTeam(CBranchProtectionRule const &_Rule) const
	{
		for (auto *pAllowances : {&_Rule.m_PushAllowances, &_Rule.m_ReviewDismissalAllowances, &_Rule.m_BypassForcePushAllowances, &_Rule.m_BypassPullRequestAllowances})
		{
			if (!*pAllowances)
				continue;

			for (auto &Actor : **pAllowances)
			{
				if (auto pError = fg_FindUnknownTeam(m_State, Actor))
					return pError;
			}
		}

		return {};
	}

	CExceptionPointer CMockGitHub::f_FindUnknownTeam(CGenericRuleset const &_Ruleset) const
	{
		if (!_Ruleset.m_BypassActors)
			return {};

		for (auto &BypassActor : *_Ruleset.m_BypassActors)
		{
			if (auto pError = fg_FindUnknownTeam(m_State, BypassActor.m_Actor))
				return pError;
		}

		return {};
	}

	struct CGraphQlRequest
	{
		CStr m_Query;
		CJsonSorted m_Variables;
	};

	/// The GitHub provider with its GraphQL requests recorded instead of sent. They get the responses f_AddResponses gave, in order, and
	/// then an empty data object.
	struct CGraphQlCaptureGitHub : public CGitHub
	{
		TCFuture<void> f_AddResponses(TCVector<CJsonSorted> _Responses);
		TCFuture<TCVector<CGraphQlRequest>> f_GetRequests();

		TCFuture<CJsonSorted> fp_GraphQlApi(CStr _Query, CJsonSorted _Variables) override;

		TCVector<CJsonSorted> m_Responses;
		TCVector<CGraphQlRequest> m_Requests;
	};

	TCFuture<void> CGraphQlCaptureGitHub::f_AddResponses(TCVector<CJsonSorted> _Responses)
	{
		m_Responses.f_Insert(fg_Move(_Responses));
		co_return {};
	}

	TCFuture<TCVector<CGraphQlRequest>> CGraphQlCaptureGitHub::f_GetRequests()
	{
		co_return m_Requests;
	}

	TCFuture<CJsonSorted> CGraphQlCaptureGitHub::fp_GraphQlApi(CStr _Query, CJsonSorted _Variables)
	{
		m_Requests.f_Insert(CGraphQlRequest{.m_Query = fg_Move(_Query), .m_Variables = fg_Move(_Variables)});

		if (m_Responses.f_IsEmpty())
			co_return CJsonSorted::fs_FromString(R"-----({"data": {}})-----");

		auto Response = fg_Move(m_Responses[0]);
		m_Responses.f_Remove(0);
		co_return fg_Move(Response);
	}

	/// _Rules are (ID, pattern) pairs. An empty _NextCursor makes it the last page.
	CJsonSorted fg_BranchProtectionRulesPage(TCVector<TCTuple<CStr, CStr>> const &_Rules, CStr const &_NextCursor)
	{
		CJsonSorted Page;
		auto &Rules = Page["data"]["repository"]["branchProtectionRules"];
		auto &PageInfo = Rules["pageInfo"];
		PageInfo["hasNextPage"] = !_NextCursor.f_IsEmpty();
		if (_NextCursor.f_IsEmpty())
			PageInfo["endCursor"] = nullptr;
		else
			PageInfo["endCursor"] = _NextCursor;

		auto &Nodes = Rules["nodes"];
		Nodes = EJsonType_Array;
		for (auto &[ID, Pattern] : _Rules)
		{
			auto Node = CJsonSorted::fs_FromString
				(
					R"-----(
					{
						"allowsDeletions": false,
						"allowsForcePushes": false,
						"blocksCreations": false,
						"bypassForcePushAllowances": {"nodes": []},
						"bypassPullRequestAllowances": {"nodes": []},
						"creator": {"login": "octocat"},
						"dismissesStaleReviews": false,
						"isAdminEnforced": true,
						"pushAllowances": {"nodes": []},
						"requiredApprovingReviewCount": 0,
						"requiredStatusCheckContexts": [],
						"requiredStatusChecks": [],
						"requiresApprovingReviews": false,
						"requiresCodeOwnerReviews": false,
						"requiresCommitSignatures": false,
						"requiresConversationResolution": false,
						"requiresLinearHistory": true,
						"requiresStatusChecks": false,
						"requiresStrictStatusChecks": false,
						"restrictsPushes": false,
						"restrictsReviewDismissals": false,
						"reviewDismissalAllowances": {"nodes": []}
					}
					)-----"
				)
			;
			Node["id"] = ID;
			Node["pattern"] = Pattern;
			Nodes.f_Array().f_Insert(fg_Move(Node));
		}

		return Page;
	}

	struct CRestRequest
	{
		CStr m_Path;
		TCMap<CStr, CStr> m_QueryParams;
	};

	/// The GitHub provider with its REST reads recorded instead of sent, each answered with the response f_SetResponse gave its path
	struct CRestCaptureGitHub : public CGitHub
	{
		TCFuture<void> f_SetResponse(CStr _Path, CJsonSorted _Response);
		TCFuture<TCVector<CRestRequest>> f_GetRequests();

		auto fp_RestApi(CStr _Path, CStr _ErrorDescription, TCMap<CStr, CStr> _QueryParams, uint32 _EmptyStatus, uint32 _ExpectedStatus, CStr _PagedArrayMember, umint _MaxItems)
			-> TCFuture<CJsonSorted> override
		;

		TCMap<CStr, CJsonSorted> m_Responses; // By path
		TCVector<CRestRequest> m_Requests;
	};

	TCFuture<void> CRestCaptureGitHub::f_SetResponse(CStr _Path, CJsonSorted _Response)
	{
		m_Responses[_Path] = fg_Move(_Response);
		co_return {};
	}

	TCFuture<TCVector<CRestRequest>> CRestCaptureGitHub::f_GetRequests()
	{
		co_return m_Requests;
	}

	auto CRestCaptureGitHub::fp_RestApi
		(
			CStr _Path
			, CStr _ErrorDescription
			, TCMap<CStr, CStr> _QueryParams
			, uint32 _EmptyStatus
			, uint32 _ExpectedStatus
			, CStr _PagedArrayMember
			, umint _MaxItems
		)
		-> TCFuture<CJsonSorted>
	{
		auto *pResponse = m_Responses.f_FindEqual(_Path);
		m_Requests.f_Insert(CRestRequest{.m_Path = fg_Move(_Path), .m_QueryParams = fg_Move(_QueryParams)});
		if (!pResponse)
			co_return DMibErrorInstance("No response for '{}'"_f << m_Requests.f_GetLast().m_Path);

		co_return *pResponse;
	}

	struct CPolicyOptions
	{
		bool m_bPretend = false;
		bool m_bRemoveOther = false;
	};

	struct CPolicyResult
	{
		CMockGitHubState m_State;
		TCVector<CStr> m_Created;
		TCVector<CStr> m_Updated;      // Name, then the updated values
		TCVector<CStr> m_Deleted;
		TCVector<CStr> m_Unmanageable; // Name, then the reasons
		CStr m_Error;                  // Why applying failed; empty when it succeeded
		bool m_bApplied = false;       // Whether everything the policy lists was applied
	};

	TCFuture<CPolicyResult> fg_ApplyPolicyRulesTo(TCActor<NMib::NGit::CGitHostingProvider> _Provider, CStr _Policy, bool _bPretend)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto Provider = _Provider;

		TCActor<NMib::NGit::CGitPolicyActor> PolicyActor = fg_Construct();
		auto DestroyPolicyActor = co_await fg_AsyncDestroy(PolicyActor);

		TCSharedPointer<CPolicyResult> pResult = fg_Construct();

		auto fContext = [&]
			{
				return NMib::NGit::CGitPolicyActor::CApplyPolicyContext
					{
						.m_Repository = "octo-org/octo-repo"
						, .m_HostingProvider = Provider
						, .m_fOnCreate = g_ActorFunctor / [pResult](CStr _Name, CStr _CreatedValues) -> TCFuture<void>
						{
							pResult->m_Created.f_Insert(_Name);
							co_return {};
						}
						, .m_fOnUpdate = g_ActorFunctor / [pResult](CStr _Name, CStr _UpdatedValues) -> TCFuture<void>
						{
							pResult->m_Updated.f_Insert("{}: {}"_f << _Name << _UpdatedValues);
							co_return {};
						}
						, .m_fOnDelete = g_ActorFunctor / [pResult](CStr _Name, CStr _DeletedValues) -> TCFuture<void>
						{
							pResult->m_Deleted.f_Insert(_Name);
							co_return {};
						}
						, .m_fOnUnmanageable = g_ActorFunctor / [pResult](CStr _Name, CStr _Reasons) -> TCFuture<void>
						{
							pResult->m_Unmanageable.f_Insert("{}: {}"_f << _Name << _Reasons);
							co_return {};
						}
						, .m_bPretend = _bPretend
					}
				;
			}
		;

		auto Result = co_await PolicyActor(&NMib::NGit::CGitPolicyActor::f_ApplyPolicy_Rules, fContext(), fContext(), CEJsonSorted::fs_FromString(_Policy)).f_Wrap();

		if (Result)
			pResult->m_bApplied = *Result;
		else
			pResult->m_Error = Result.f_GetExceptionStr();

		co_return fg_Move(*pResult);
	}

	TCFuture<CPolicyResult> fg_ApplyPolicyRules(CMockGitHubState _State, CStr _Policy, bool _bPretend)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		TCActor<CMockGitHub> Provider = fg_Construct(fg_Move(_State));
		auto DestroyProvider = co_await fg_AsyncDestroy(Provider);

		auto Result = co_await fg_ApplyPolicyRulesTo(Provider, fg_Move(_Policy), _bPretend);
		Result.m_State = co_await Provider(&CMockGitHub::f_GetState);

		co_return fg_Move(Result);
	}

	TCFuture<CPolicyResult> fg_ApplyPolicy(CMockGitHubState _State, CStr _Section, CStr _Policy, CPolicyOptions _Options)
	{
		CEJsonSorted Policy;
		Policy[_Section] = CEJsonSorted::fs_FromString(_Policy);
		if (_Options.m_bRemoveOther)
			Policy["RemoveOther"][_Section] = true;

		co_return co_await fg_ApplyPolicyRules(fg_Move(_State), Policy.f_ToString(), _Options.m_bPretend);
	}
}

namespace
{
	using CGitHub = NMib::NGit::CGitHostingProvider_GitHub;
	using EPullRequestFeedbackKind = NMib::NGit::CGitHostingProvider::EPullRequestFeedbackKind;
	using namespace NGitHubPolicyTests;

	constexpr ch8 const gc_WorkflowRunsJson[] = R"-----(
	{
		"total_count": 4147,
		"workflow_runs":
		[
			{
				"id": 38223160412,
				"name": "Tests",
				"node_id": "WFR_kwLOG2ONas8AAAAI5kcLXA",
				"head_branch": "master",
				"head_sha": "0c1d9b7f30a6c0d6e1c4b9f2a8e7d6c5b4a39281",
				"path": ".github/workflows/tests.yml",
				"display_title": "Update dependencies",
				"run_number": 19344,
				"event": "schedule",
				"status": "in_progress",
				"conclusion": null,
				"workflow_id": 19963856,
				"url": "https://api.github.com/repos/octo-org/octo-repo/actions/runs/38223160412",
				"html_url": "https://github.com/octo-org/octo-repo/actions/runs/38223160412",
				"pull_requests": [],
				"created_at": "2026-10-12T20:08:44Z",
				"updated_at": "2026-10-12T20:31:02Z",
				"actor": {"login": "octocat", "id": 583231, "type": "User"},
				"run_attempt": 2,
				"run_started_at": "2026-10-12T20:25:10Z"
			},
			{
				"id": 38219304852,
				"name": "Tests",
				"node_id": "WFR_kwLOG2ONas8AAAAI5gxPlA",
				"head_branch": "master",
				"head_sha": "af702c3c923192f18fc22d852fa3d2e02e9106a3",
				"path": ".github/workflows/tests.yml",
				"display_title": "Show what failed when the environment cannot be prepared",
				"run_number": 19342,
				"event": "push",
				"status": "completed",
				"conclusion": "failure",
				"workflow_id": 19963856,
				"url": "https://api.github.com/repos/octo-org/octo-repo/actions/runs/38219304852",
				"html_url": "https://github.com/octo-org/octo-repo/actions/runs/38219304852",
				"pull_requests": [],
				"created_at": "2026-10-12T07:43:04Z",
				"updated_at": "2026-10-12T08:00:53Z",
				"actor": {"login": "octocat", "id": 583231, "type": "User"},
				"run_attempt": 1,
				"run_started_at": "2026-10-12T07:43:04Z"
			}
		]
	}
	)-----";

	constexpr ch8 const gc_WorkflowJobsJson[] = R"-----(
	{
		"total_count": 3,
		"jobs":
		[
			{
				"id": 114713159234,
				"run_id": 38219304852,
				"workflow_name": "Tests",
				"head_branch": "master",
				"run_url": "https://api.github.com/repos/octo-org/octo-repo/actions/runs/38219304852",
				"run_attempt": 1,
				"node_id": "CR_kwDOG2ONas8AAAAatXMVQg",
				"head_sha": "af702c3c923192f18fc22d852fa3d2e02e9106a3",
				"url": "https://api.github.com/repos/octo-org/octo-repo/actions/jobs/114713159234",
				"html_url": "https://github.com/octo-org/octo-repo/actions/runs/38219304852/job/114713159234",
				"status": "completed",
				"conclusion": "failure",
				"created_at": "2026-10-12T07:48:45Z",
				"started_at": "2026-10-12T07:48:47Z",
				"completed_at": "2026-10-12T07:59:24Z",
				"name": "Dependency Audit",
				"steps":
				[
					{"name": "Set up job", "status": "completed", "conclusion": "success", "number": 1, "started_at": "2026-10-12T07:48:48Z", "completed_at": "2026-10-12T07:48:49Z"},
					{"name": "Sync Repos", "status": "completed", "conclusion": "success", "number": 6, "started_at": "2026-10-12T07:50:00Z", "completed_at": "2026-10-12T07:50:08Z"},
					{"name": "Dependency Audit", "status": "completed", "conclusion": "failure", "number": 7, "started_at": "2026-10-12T07:50:08Z", "completed_at": "2026-10-12T07:59:21Z"},
					{"name": "Post Cache node", "status": "completed", "conclusion": "skipped", "number": 11, "started_at": "2026-10-12T07:59:21Z", "completed_at": "2026-10-12T07:59:21Z"}
				],
				"check_run_url": "https://api.github.com/repos/octo-org/octo-repo/check-runs/114713159234",
				"labels": ["ubuntu-latest"],
				"runner_id": 113486,
				"runner_name": "runner-1",
				"runner_group_id": 1,
				"runner_group_name": "Default"
			},
			{
				"id": 114713159311,
				"run_id": 38219304852,
				"workflow_name": "Tests",
				"head_branch": "master",
				"run_attempt": 1,
				"head_sha": "af702c3c923192f18fc22d852fa3d2e02e9106a3",
				"html_url": "https://github.com/octo-org/octo-repo/actions/runs/38219304852/job/114713159311",
				"status": "in_progress",
				"conclusion": null,
				"created_at": "2026-10-12T07:48:45Z",
				"started_at": "2026-10-12T07:49:20Z",
				"completed_at": null,
				"name": "Unit Tests",
				"steps":
				[
					{"name": "Set up job", "status": "completed", "conclusion": "success", "number": 1, "started_at": "2026-10-12T07:49:21Z", "completed_at": "2026-10-12T07:49:22Z"},
					{"name": "Run tests", "status": "in_progress", "conclusion": null, "number": 2, "started_at": "2026-10-12T07:49:22Z", "completed_at": null},
					{"name": "Upload results", "status": "pending", "conclusion": null, "number": 3, "started_at": null, "completed_at": null}
				],
				"labels": ["ubuntu-latest"],
				"runner_id": 113490,
				"runner_name": "runner-2",
				"runner_group_id": 1,
				"runner_group_name": "Default"
			},
			{
				"id": 114716931500,
				"run_id": 38219304852,
				"workflow_name": "Tests",
				"head_branch": "master",
				"run_attempt": 1,
				"head_sha": "af702c3c923192f18fc22d852fa3d2e02e9106a3",
				"html_url": "https://github.com/octo-org/octo-repo/actions/runs/38219304852/job/114716931500",
				"status": "queued",
				"conclusion": null,
				"created_at": "2026-10-12T08:00:36Z",
				"started_at": "2026-10-12T08:00:36Z",
				"completed_at": null,
				"name": "Generate Test Report",
				"steps": [],
				"labels": ["ubuntu-latest"],
				"runner_id": null,
				"runner_name": null,
				"runner_group_id": null,
				"runner_group_name": null
			}
		]
	}
	)-----";

	constexpr ch8 const gc_PullRequestsJson[] = R"-----(
	[
		{
			"url": "https://api.github.com/repos/octo-org/octo-repo/pulls/2520",
			"id": 4761000001,
			"node_id": "PR_kwDOG2ONas8AAAABG8aQQQ",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2520",
			"number": 2520,
			"state": "open",
			"locked": false,
			"title": "(Fav-165419) Update vulnerable dependencies",
			"user": {"login": "octocat", "id": 583231, "type": "User"},
			"body": "Updates the dependencies the audit reports.",
			"created_at": "2026-10-12T09:15:00Z",
			"updated_at": "2026-10-12T09:20:00Z",
			"closed_at": null,
			"merged_at": null,
			"merge_commit_sha": null,
			"labels":
			[
				{"id": 1, "node_id": "LA_1", "name": "dependencies", "color": "0366d6", "default": false},
				{"id": 2, "node_id": "LA_2", "name": "security", "color": "d73a4a", "default": false}
			],
			"draft": true,
			"head": {"label": "octo-org:fav-165419-update-vulnerable-dependencies", "ref": "fav-165419-update-vulnerable-dependencies", "sha": "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f"},
			"base": {"label": "octo-org:master", "ref": "master", "sha": "af702c3c923192f18fc22d852fa3d2e02e9106a3"},
			"author_association": "MEMBER",
			"auto_merge": null,
			"active_lock_reason": null
		},
		{
			"url": "https://api.github.com/repos/octo-org/octo-repo/pulls/2518",
			"id": 4756614628,
			"node_id": "PR_kwDOG2ONas8AAAABG4Qt5A",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2518",
			"number": 2518,
			"state": "closed",
			"locked": false,
			"title": "(Fav-165203) Publish the server",
			"user": {"login": "hubot", "id": 75567967, "type": "User"},
			"body": null,
			"created_at": "2026-10-06T09:11:00Z",
			"updated_at": "2026-10-09T12:53:22Z",
			"closed_at": "2026-10-09T12:53:20Z",
			"merged_at": "2026-10-09T12:53:19Z",
			"merge_commit_sha": "f0378fdef55e2c86e7bf382ce013c7d03b976b6b",
			"labels": [],
			"draft": false,
			"head": {"label": "octo-org:fav-165203", "ref": "fav-165203", "sha": "74edeb7742b69b496764d763e51a71310cccc03e"},
			"base": {"label": "octo-org:master", "ref": "master", "sha": "9d615acbd81a203628eddfc752f69c30ee93b45c"},
			"author_association": "MEMBER",
			"auto_merge": null,
			"active_lock_reason": null
		}
	]
	)-----";

	constexpr ch8 const gc_CollaboratorPermissionJson[] = R"-----(
	{
		"permission": "write",
		"role_name": "maintain",
		"user":
		{
			"login": "octocat",
			"id": 583231,
			"type": "User",
			"role_name": "maintain"
		}
	}
	)-----";

	constexpr ch8 const gc_PullRequestJson[] = R"-----(
	{
		"url": "https://api.github.com/repos/octo-org/octo-repo/pulls/2514",
		"id": 4740000002,
		"html_url": "https://github.com/octo-org/octo-repo/pull/2514",
		"number": 2514,
		"state": "closed",
		"title": "(Fav-165100) Abandoned change",
		"user": {"login": "octocat", "id": 583231, "type": "User"},
		"body": "",
		"created_at": "2026-10-01T10:00:00Z",
		"closed_at": "2026-10-02T10:00:00Z",
		"merged_at": null,
		"labels": [],
		"draft": false,
		"head": {"ref": "fav-165100", "sha": "1111111111111111111111111111111111111111"},
		"base": {"ref": "master", "sha": "2222222222222222222222222222222222222222"},
		"author_association": "MEMBER",
		"merged": false,
		"mergeable": null,
		"comments": 0,
		"review_comments": 0,
		"commits": 1
	}
	)-----";

	constexpr ch8 const gc_ReviewsJson[] = R"-----(
	[
		{
			"id": 5429972316,
			"node_id": "PRR_kwDOG2ONas8AAAABQ6bNXA",
			"user": {"login": "hubot", "id": 75567967, "type": "User"},
			"body": "Looks fine so far.",
			"state": "COMMENTED",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2520#pullrequestreview-5429972316",
			"pull_request_url": "https://api.github.com/repos/octo-org/octo-repo/pulls/2520",
			"author_association": "MEMBER",
			"submitted_at": "2026-10-12T09:30:00Z",
			"commit_id": "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f"
		},
		{
			"id": 5452575226,
			"node_id": "PRR_kwDOG2ONas8AAAABRP8z-g",
			"user": {"login": "hubot", "id": 75567967, "type": "User"},
			"body": "Pin the patched version instead of a range.",
			"state": "CHANGES_REQUESTED",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2520#pullrequestreview-5452575226",
			"pull_request_url": "https://api.github.com/repos/octo-org/octo-repo/pulls/2520",
			"author_association": "COLLABORATOR",
			"submitted_at": "2026-10-12T11:00:00Z",
			"commit_id": "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f"
		},
		{
			"id": 5452575999,
			"node_id": "PRR_kwDOG2ONas8AAAABRP80_w",
			"user": {"login": "octocat", "id": 583231, "type": "User"},
			"body": "",
			"state": "PENDING",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2520#pullrequestreview-5452575999",
			"pull_request_url": "https://api.github.com/repos/octo-org/octo-repo/pulls/2520",
			"author_association": "MEMBER",
			"commit_id": "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f"
		}
	]
	)-----";

	constexpr ch8 const gc_ReviewCommentsJson[] = R"-----(
	[
		{
			"url": "https://api.github.com/repos/octo-org/octo-repo/pulls/comments/4215688800",
			"pull_request_review_id": 5452575226,
			"id": 4215688800,
			"node_id": "PRRC_kwDOG2ONas77Rk4g",
			"diff_hunk": "@@ -18,3 +18,3 @@",
			"path": "MainApp/package.json",
			"commit_id": "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f",
			"original_commit_id": "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f",
			"user": {"login": "hubot", "id": 75567967, "type": "User"},
			"body": "Use 4.17.21 here.",
			"created_at": "2026-10-12T11:00:00Z",
			"updated_at": "2026-10-12T11:00:00Z",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2520#discussion_r4215688800",
			"author_association": "COLLABORATOR",
			"start_line": null,
			"original_start_line": null,
			"line": 20,
			"original_line": 20,
			"side": "RIGHT",
			"subject_type": "line"
		},
		{
			"url": "https://api.github.com/repos/octo-org/octo-repo/pulls/comments/4215688824",
			"pull_request_review_id": 5452575300,
			"id": 4215688824,
			"node_id": "PRRC_kwDOG2ONas77Rk54",
			"diff_hunk": "@@ -16,3 +16,3 @@",
			"path": "MainApp/package.json",
			"commit_id": "6f3a2d1c0b9e8f7a6b5c4d3e2f1a0b9c8d7e6f5a",
			"original_commit_id": "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f",
			"user": {"login": "octocat", "id": 583231, "type": "User"},
			"body": "Done.",
			"created_at": "2026-10-12T11:45:00Z",
			"updated_at": "2026-10-12T11:46:00Z",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2520#discussion_r4215688824",
			"in_reply_to_id": 4215688800,
			"author_association": "MEMBER",
			"start_line": null,
			"original_start_line": null,
			"line": null,
			"original_line": 18,
			"side": "RIGHT",
			"subject_type": "line"
		}
	]
	)-----";

	constexpr ch8 const gc_IssueCommentsJson[] = R"-----(
	[
		{
			"url": "https://api.github.com/repos/octo-org/octo-repo/issues/comments/6018466902",
			"html_url": "https://github.com/octo-org/octo-repo/pull/2520#issuecomment-6018466902",
			"issue_url": "https://api.github.com/repos/octo-org/octo-repo/issues/2520",
			"id": 6018466902,
			"node_id": "IC_kwDOG2ONas8AAAABZrqEVg",
			"user": {"login": "hubot", "id": 75567967, "type": "User"},
			"created_at": "2026-10-12T10:15:00Z",
			"updated_at": "2026-10-12T10:16:00Z",
			"author_association": "OWNER",
			"body": "Can you also check the server package?"
		}
	]
	)-----";

	constexpr ch8 const gc_SearchIssuesJson[] = R"-----(
	{
		"total_count": 2,
		"incomplete_results": false,
		"items":
		[
			{
				"url": "https://api.github.com/repos/Malterlib/Malterlib/issues/612",
				"repository_url": "https://api.github.com/repos/Malterlib/Malterlib",
				"number": 612,
				"state": "open",
				"user": {"login": "octocat", "id": 583231, "type": "User"},
				"created_at": "2026-10-20T08:00:00Z",
				"updated_at": "2026-10-26T09:30:00Z",
				"pull_request": {"url": "https://api.github.com/repos/Malterlib/Malterlib/pulls/612"}
			},
			{
				"url": "https://api.github.com/repos/Malterlib/Web/issues/88",
				"repository_url": "https://api.github.com/repos/Malterlib/Web",
				"number": 88,
				"state": "closed",
				"user": {"login": "hubot", "id": 75567967, "type": "User"},
				"created_at": "2026-10-19T08:00:00Z",
				"updated_at": "2026-10-26T09:00:00Z"
			}
		]
	}
	)-----";

	constexpr ch8 const gc_ReviewListJson[] = R"-----(
	[
		{
			"id": 6100000001,
			"node_id": "PRR_kwDOAAAAAc5aaaaa",
			"user": {"login": "octocat", "id": 583231, "type": "User"},
			"body": "@malterlib-review-bot review",
			"state": "COMMENTED",
			"html_url": "https://github.com/Malterlib/Malterlib/pull/612#pullrequestreview-6100000001",
			"author_association": "MEMBER",
			"submitted_at": "2026-10-26T09:31:00Z",
			"commit_id": "1111111111111111111111111111111111111111"
		},
		{
			"id": 6100000002,
			"node_id": "PRR_kwDOAAAAAc5bbbbb",
			"user": {"login": "malterlib-review-bot[bot]", "id": 1, "type": "Bot"},
			"body": "",
			"state": "PENDING",
			"commit_id": "2222222222222222222222222222222222222222"
		}
	]
	)-----";

	constexpr ch8 const gc_PullRequestFilesJson[] = R"-----(
	[
		{
			"sha": "aaaa",
			"filename": "Source/Malterlib_Web_Acme.cpp",
			"status": "modified",
			"additions": 3,
			"deletions": 1,
			"patch": "@@ -1,2 +1,4 @@\n-a\n+b\n+c\n+d"
		},
		{
			"sha": "bbbb",
			"filename": "Source/New.cpp",
			"previous_filename": "Source/Old.cpp",
			"status": "renamed",
			"additions": 0,
			"deletions": 0
		}
	]
	)-----";

	constexpr ch8 const gc_IssueEventsJson[] = R"-----(
	[
		{"id": 1, "event": "closed", "actor": {"login": "octocat"}, "created_at": "2026-10-20T10:00:00Z"},
		{"id": 2, "event": "reopened", "actor": {"login": "octocat"}, "created_at": "2026-10-25T10:00:00Z"}
	]
	)-----";

	constexpr ch8 const gc_BranchProtectionJson[] = R"-----(
	{
		"url": "https://api.github.com/repos/Malterlib/Malterlib/branches/master/protection",
		"required_linear_history": {"enabled": true},
		"allow_force_pushes": {"enabled": false},
		"restrictions":
		{
			"users": [{"login": "octocat"}],
			"teams": [{"slug": "pushers", "name": "Pushers"}],
			"apps": [{"slug": "malterlib-review-bot"}]
		}
	}
	)-----";

	constexpr ch8 const gc_CombinedStatusJson[] = R"-----(
	{
		"state": "pending",
		"sha": "1111111111111111111111111111111111111111",
		"statuses":
		[
			{
				"state": "pending",
				"context": "MalterlibReviewBot",
				"description": "Round 1 running",
				"target_url": "https://github.com/Malterlib/Malterlib/pull/612",
				"creator": {"login": "malterlib-review-bot[bot]"},
				"created_at": "2026-10-26T09:32:00Z",
				"updated_at": "2026-10-26T09:32:00Z"
			}
		]
	}
	)-----";

	constexpr ch8 const gc_CheckRunsJson[] = R"-----(
	{
		"total_count": 1,
		"check_runs":
		[
			{
				"id": 4,
				"name": "Build Linux",
				"head_sha": "1111111111111111111111111111111111111111",
				"status": "completed",
				"conclusion": "failure",
				"html_url": "https://github.com/Malterlib/Malterlib/runs/4",
				"details_url": "https://github.com/Malterlib/Malterlib/actions/runs/9/job/4",
				"app": {"slug": "github-actions"},
				"started_at": "2026-10-26T09:40:00Z",
				"completed_at": "2026-10-26T10:10:00Z"
			}
		]
	}
	)-----";

	constexpr ch8 const gc_RulesetBypassActorsJson[] = R"-----(
	{
		"id": 4210,
		"name": "Default branch",
		"target": "branch",
		"source_type": "Repository",
		"source": "octo-org/octo-repo",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
		"rules": [{"type": "deletion"}, {"type": "non_fast_forward"}],
		"bypass_actors":
		[
			{"actor_id": 583231, "actor_type": "User", "bypass_mode": "always"},
			{"actor_id": 1, "actor_type": "OrganizationAdmin", "bypass_mode": "always"},
			{"actor_id": 7, "actor_type": "Team", "bypass_mode": "pull_request"},
			{"actor_id": null, "actor_type": "DeployKey", "bypass_mode": "always"}
		],
		"current_user_can_bypass": "never",
		"node_id": "RRS_lACqUmVwb3NpdG9yec4AAAAAzgAAEHI",
		"created_at": "2026-11-24T16:44:51.030Z",
		"updated_at": "2026-11-24T16:44:51.030Z"
	}
	)-----";

	constexpr ch8 const gc_RulesetUnknownActorsJson[] = R"-----(
	{
		"id": 4211,
		"name": "Release tags",
		"target": "tag",
		"source_type": "Repository",
		"source": "octo-org/octo-repo",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["refs/tags/v*"], "exclude": []}},
		"rules": [{"type": "deletion"}],
		"bypass_actors":
		[
			{"actor_id": null, "actor_type": "DeployKey", "bypass_mode": "always"},
			{"actor_id": 1, "actor_type": "EnterpriseOwner", "bypass_mode": "always"},
			{"actor_id": 4, "actor_type": "RepositoryRole", "bypass_mode": "exempt"}
		]
	}
	)-----";

	constexpr ch8 const gc_RulesetLfsNoDeleteJson[] = R"-----(
	{
		"id": 4212,
		"name": "LFS NoDelete",
		"target": "tag",
		"source_type": "Repository",
		"source": "octo-org/octo-repo",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["refs/tags/lfs/*"], "exclude": []}},
		"rules": [{"type": "deletion"}, {"type": "non_fast_forward"}],
		"bypass_actors": []
	}
	)-----";

	constexpr ch8 const gc_RulesetOrganizationJson[] = R"-----(
	{
		"id": 4213,
		"name": "Organization",
		"target": "branch",
		"source_type": "Organization",
		"source": "octo-org",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["~ALL"], "exclude": []}},
		"rules": [{"type": "deletion"}],
		"bypass_actors": []
	}
	)-----";

	constexpr ch8 const gc_RulesetSquashOnlyJson[] = R"-----(
	{
		"id": 4216,
		"name": "Squash only",
		"target": "branch",
		"source_type": "Repository",
		"source": "octo-org/octo-repo",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
		"bypass_actors": [],
		"rules":
		[
			{
				"type": "pull_request",
				"parameters":
				{
					"allowed_merge_methods": ["squash"],
					"dismiss_stale_reviews_on_push": false,
					"require_code_owner_review": false,
					"require_last_push_approval": false,
					"required_approving_review_count": 1,
					"required_review_thread_resolution": false
				}
			}
		]
	}
	)-----";

	constexpr ch8 const gc_RulesetUnknownRulesJson[] = R"-----(
	{
		"id": 4214,
		"name": "Protected branches",
		"target": "branch",
		"source_type": "Repository",
		"source": "octo-org/octo-repo",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
		"bypass_actors": [{"actor_id": 4, "actor_type": "RepositoryRole", "bypass_mode": "always"}],
		"rules":
		[
			{"type": "deletion"},
			{
				"type": "merge_queue",
				"parameters":
				{
					"check_response_timeout_minutes": 60,
					"grouping_strategy": "ALLGREEN",
					"max_entries_to_build": 5,
					"max_entries_to_merge": 5,
					"merge_method": "REBASE",
					"min_entries_to_merge": 1,
					"min_entries_to_merge_wait_minutes": 5
				}
			},
			{"type": "copilot_code_review", "parameters": {"review_draft_pull_requests": false, "review_on_push": true}},
			{
				"type": "pull_request",
				"parameters":
				{
					"allowed_merge_methods": ["rebase"],
					"dismiss_stale_reviews_on_push": true,
					"require_code_owner_review": false,
					"require_last_push_approval": false,
					"required_approving_review_count": 1,
					"required_review_thread_resolution": true,
					"required_reviewers": [{"file_patterns": ["*.md"], "minimum_approvals": 1, "reviewer": {"id": 7, "type": "Team"}}]
				}
			},
			{
				"type": "required_status_checks",
				"parameters":
				{
					"do_not_enforce_on_create": true,
					"required_status_checks": [{"context": "Build Linux", "integration_id": 15368}],
					"strict_required_status_checks_policy": false
				}
			}
		]
	}
	)-----";

	constexpr ch8 const gc_RulesetListOrderJson[] = R"-----(
	{
		"id": 4215,
		"name": "Release branches",
		"target": "branch",
		"source_type": "Repository",
		"source": "octo-org/octo-repo",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["refs/heads/release/*", "refs/heads/hotfix/*"], "exclude": ["refs/heads/release/old", "refs/heads/hotfix/old"]}},
		"bypass_actors": [{"actor_id": 1, "actor_type": "OrganizationAdmin", "bypass_mode": "always"}],
		"rules":
		[
			{"type": "required_deployments", "parameters": {"required_deployment_environments": ["staging", "production"]}},
			{
				"type": "pull_request",
				"parameters":
				{
					"allowed_merge_methods": ["squash", "merge"],
					"dismiss_stale_reviews_on_push": false,
					"require_code_owner_review": false,
					"require_last_push_approval": false,
					"required_approving_review_count": 1,
					"required_review_thread_resolution": false
				}
			}
		]
	}
	)-----";

	constexpr ch8 const gc_RulesetGoneActorsJson[] = R"-----(
	{
		"id": 4217,
		"name": "Gone actors",
		"target": "branch",
		"source_type": "Repository",
		"source": "octo-org/octo-repo",
		"enforcement": "active",
		"conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
		"rules": [{"type": "deletion"}],
		"bypass_actors":
		[
			null,
			{"actor_id": null, "actor_type": "Team", "bypass_mode": "always"},
			{"actor_type": "User", "bypass_mode": "pull_request"},
			{"actor_id": null, "actor_type": "Integration", "bypass_mode": "always"},
			{"actor_id": 4, "actor_type": "RepositoryRole", "bypass_mode": "always"}
		]
	}
	)-----";

	/// The rules of a ruleset body by type, since the order GitHub lists them in isn't kept
	TCMap<CStr, CJsonSorted> fg_RulesByType(CJsonSorted const &_Rules)
	{
		TCMap<CStr, CJsonSorted> Rules;
		for (auto &Rule : _Rules.f_Array())
			Rules[Rule["type"].f_String()] = Rule;

		return Rules;
	}

	/// GitHub hands out App private keys as PKCS #1 PEM, which is the DER in base64 lines between the RSA markers
	CStr fg_RsaPrivateKeyPem(NContainer::CSecureByteVector const &_PrivateKey)
	{
		CStr Base64 = fg_Base64Encode(_PrivateKey);
		CStr Pem = "-----BEGIN RSA PRIVATE KEY-----\n";
		for (umint iStart = 0; iStart < Base64.f_GetLen(); iStart += 64)
			Pem += "{}\n"_f << Base64.f_Slice(iStart, fg_Min(iStart + 64, Base64.f_GetLen()));
		Pem += "-----END RSA PRIVATE KEY-----\n";

		return Pem;
	}

	struct CHostingProviderGitHub_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CHostingProviderGitHub_Tests::f_DoTests()
	{
		DMibTestSuite("Parse")
		{
			DMibTestCategory("WorkflowRuns")
			{
				auto Runs = CGitHub::fs_ParseWorkflowRuns(CJsonSorted::fs_FromString(gc_WorkflowRunsJson));
				DMibAssert(Runs.f_GetLen(), ==, 2);

				auto &InProgress = Runs[0];
				DMibExpect(InProgress.m_ID, ==, 38223160412);
				DMibExpect(InProgress.m_RunAttempt, ==, 2);
				DMibExpect(InProgress.m_Name, ==, "Tests");
				DMibExpect(InProgress.m_Event, ==, "schedule");
				DMibExpect(InProgress.m_HeadBranch, ==, "master");
				DMibExpect(InProgress.m_HeadSha, ==, "0c1d9b7f30a6c0d6e1c4b9f2a8e7d6c5b4a39281");
				DMibExpect(InProgress.m_Status, ==, "in_progress");
				DMibExpect(InProgress.m_Conclusion, ==, "");
				DMibExpect(InProgress.m_HtmlUrl, ==, "https://github.com/octo-org/octo-repo/actions/runs/38223160412");
				DMibExpect(InProgress.m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 20, 8, 44));
				DMibExpect(InProgress.m_UpdatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 20, 31, 2));

				auto &Completed = Runs[1];
				DMibExpect(Completed.m_ID, ==, 38219304852);
				DMibExpect(Completed.m_RunAttempt, ==, 1);
				DMibExpect(Completed.m_Event, ==, "push");
				DMibExpect(Completed.m_Status, ==, "completed");
				DMibExpect(Completed.m_Conclusion, ==, "failure");
				DMibExpect(Completed.m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 7, 43, 4));
			};

			DMibTestCategory("WorkflowJobs")
			{
				auto Jobs = CGitHub::fs_ParseWorkflowJobs(CJsonSorted::fs_FromString(gc_WorkflowJobsJson));
				DMibAssert(Jobs.f_GetLen(), ==, 3);

				auto &Audit = Jobs[0];
				DMibExpect(Audit.m_ID, ==, 114713159234);
				DMibExpect(Audit.m_RunID, ==, 38219304852);
				DMibExpect(Audit.m_RunAttempt, ==, 1);
				DMibExpect(Audit.m_Name, ==, "Dependency Audit");
				DMibExpect(Audit.m_Status, ==, "completed");
				DMibExpect(Audit.m_Conclusion, ==, "failure");
				DMibExpect(Audit.m_HtmlUrl, ==, "https://github.com/octo-org/octo-repo/actions/runs/38219304852/job/114713159234");
				DMibExpect(Audit.m_HeadSha, ==, "af702c3c923192f18fc22d852fa3d2e02e9106a3");
				DMibExpect(Audit.m_StartedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 7, 48, 47));
				DMibExpect(Audit.m_CompletedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 7, 59, 24));
				DMibAssert(Audit.m_Steps.f_GetLen(), ==, 4);
				DMibExpect(Audit.m_Steps[1].m_Number, ==, 6);
				DMibExpect(Audit.m_Steps[1].m_Name, ==, "Sync Repos");
				DMibExpect(Audit.m_Steps[1].m_Conclusion, ==, "success");
				DMibExpect(Audit.m_Steps[2].m_Number, ==, 7);
				DMibExpect(Audit.m_Steps[2].m_Name, ==, "Dependency Audit");
				DMibExpect(Audit.m_Steps[2].m_Status, ==, "completed");
				DMibExpect(Audit.m_Steps[2].m_Conclusion, ==, "failure");
				DMibExpect(Audit.m_Steps[3].m_Conclusion, ==, "skipped");

				auto &InProgress = Jobs[1];
				DMibExpect(InProgress.m_Status, ==, "in_progress");
				DMibExpect(InProgress.m_Conclusion, ==, "");
				DMibExpectFalse(InProgress.m_CompletedAt.f_IsValid());
				DMibAssert(InProgress.m_Steps.f_GetLen(), ==, 3);
				DMibExpect(InProgress.m_Steps[1].m_Status, ==, "in_progress");
				DMibExpect(InProgress.m_Steps[1].m_Conclusion, ==, "");
				DMibExpect(InProgress.m_Steps[2].m_Status, ==, "pending");

				auto &Queued = Jobs[2];
				DMibExpect(Queued.m_Name, ==, "Generate Test Report");
				DMibExpect(Queued.m_Status, ==, "queued");
				DMibExpect(Queued.m_Conclusion, ==, "");
				DMibExpectTrue(Queued.m_Steps.f_IsEmpty());
			};

			DMibTestCategory("PullRequests")
			{
				auto PullRequests = CGitHub::fs_ParsePullRequests(CJsonSorted::fs_FromString(gc_PullRequestsJson));
				DMibAssert(PullRequests.f_GetLen(), ==, 2);

				auto &Open = PullRequests[0];
				DMibExpect(Open.m_Number, ==, 2520);
				DMibExpect(Open.m_Title, ==, "(Fav-165419) Update vulnerable dependencies");
				DMibExpect(Open.m_Body, ==, "Updates the dependencies the audit reports.");
				DMibExpect(Open.m_State, ==, "open");
				DMibExpectTrue(Open.m_bDraft);
				DMibExpectFalse(Open.m_bMerged);
				DMibExpect(Open.m_MergeCommitSha, ==, "");
				DMibExpect(Open.m_HeadRef, ==, "fav-165419-update-vulnerable-dependencies");
				DMibExpect(Open.m_HeadSha, ==, "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f");
				DMibExpect(Open.m_BaseRef, ==, "master");
				DMibExpect(Open.m_User, ==, "octocat");
				DMibExpect(Open.m_HtmlUrl, ==, "https://github.com/octo-org/octo-repo/pull/2520");
				DMibExpect(Open.m_Labels, ==, (TCSet<CStr>{"dependencies", "security"}));
				DMibExpect(Open.m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 9, 15, 0));
				DMibExpect(Open.m_UpdatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 9, 20, 0));

				auto &Merged = PullRequests[1];
				DMibExpect(Merged.m_Number, ==, 2518);
				DMibExpect(Merged.m_State, ==, "closed");
				DMibExpect(Merged.m_Body, ==, "");
				DMibExpectFalse(Merged.m_bDraft);
				DMibExpectTrue(Merged.m_bMerged);
				DMibExpect(Merged.m_MergeCommitSha, ==, "f0378fdef55e2c86e7bf382ce013c7d03b976b6b");
				DMibExpectTrue(Merged.m_Labels.f_IsEmpty());

				auto Closed = CGitHub::fs_ParsePullRequest(CJsonSorted::fs_FromString(gc_PullRequestJson));
				DMibExpect(Closed.m_Number, ==, 2514);
				DMibExpect(Closed.m_State, ==, "closed");
				DMibExpectFalse(Closed.m_bMerged);
				DMibExpect(Closed.m_HeadRef, ==, "fav-165100");
			};

			DMibTestCategory("PullRequestFeedback")
			{
				auto Reviews = CJsonSorted::fs_FromString(gc_ReviewsJson);
				auto ReviewComments = CJsonSorted::fs_FromString(gc_ReviewCommentsJson);
				auto IssueComments = CJsonSorted::fs_FromString(gc_IssueCommentsJson);

				DMibTestCategory("All")
				{
					auto Feedback = CGitHub::fs_ParsePullRequestFeedback(Reviews, ReviewComments, IssueComments, NTime::CTime());
					DMibAssert(Feedback.f_GetLen(), ==, 5);

					DMibExpectTrue(Feedback[0].m_Kind == EPullRequestFeedbackKind::mc_Review);
					DMibExpect(Feedback[0].m_ID, ==, 5429972316);
					DMibExpectTrue(Feedback[1].m_Kind == EPullRequestFeedbackKind::mc_IssueComment);
					DMibExpectTrue(Feedback[2].m_Kind == EPullRequestFeedbackKind::mc_Review);
					DMibExpectTrue(Feedback[3].m_Kind == EPullRequestFeedbackKind::mc_ReviewComment);
					DMibExpectTrue(Feedback[4].m_Kind == EPullRequestFeedbackKind::mc_ReviewComment);
				};

				DMibTestCategory("Since")
				{
					auto Feedback = CGitHub::fs_ParsePullRequestFeedback(Reviews, ReviewComments, IssueComments, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 10, 0, 0));
					DMibAssert(Feedback.f_GetLen(), ==, 4);

					auto &IssueComment = Feedback[0];
					DMibExpectTrue(IssueComment.m_Kind == EPullRequestFeedbackKind::mc_IssueComment);
					DMibExpect(IssueComment.m_ID, ==, 6018466902);
					DMibExpect(IssueComment.m_User, ==, "hubot");
					DMibExpect(IssueComment.m_AuthorAssociation, ==, "OWNER");
					DMibExpect(IssueComment.m_Body, ==, "Can you also check the server package?");
					DMibExpect(IssueComment.m_HtmlUrl, ==, "https://github.com/octo-org/octo-repo/pull/2520#issuecomment-6018466902");
					DMibExpect(IssueComment.m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 10, 15, 0));
					DMibExpect(IssueComment.m_UpdatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 10, 16, 0));

					auto &Review = Feedback[1];
					DMibExpectTrue(Review.m_Kind == EPullRequestFeedbackKind::mc_Review);
					DMibExpect(Review.m_ID, ==, 5452575226);
					DMibExpect(Review.m_ReviewID, ==, 5452575226);
					DMibExpect(Review.m_State, ==, "CHANGES_REQUESTED");
					DMibExpect(Review.m_AuthorAssociation, ==, "COLLABORATOR");
					DMibExpect(Review.m_Body, ==, "Pin the patched version instead of a range.");
					DMibExpect(Review.m_NodeID, ==, "PRR_kwDOG2ONas8AAAABRP8z-g");
					DMibExpect(Review.m_CommitID, ==, "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f");
					DMibExpect(Review.m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 11, 0, 0));

					auto &ReviewComment = Feedback[2];
					DMibExpectTrue(ReviewComment.m_Kind == EPullRequestFeedbackKind::mc_ReviewComment);
					DMibExpect(ReviewComment.m_ID, ==, 4215688800);
					DMibExpect(ReviewComment.m_ReviewID, ==, 5452575226);
					DMibExpect(ReviewComment.m_InReplyToID, ==, 0);
					DMibExpect(ReviewComment.m_Path, ==, "MainApp/package.json");
					DMibExpect(ReviewComment.m_Line, ==, 20);
					DMibExpect(ReviewComment.m_State, ==, "");

					auto &Reply = Feedback[3];
					DMibExpectTrue(Reply.m_Kind == EPullRequestFeedbackKind::mc_ReviewComment);
					DMibExpect(Reply.m_InReplyToID, ==, 4215688800);
					DMibExpect(Reply.m_User, ==, "octocat");
					DMibExpect(Reply.m_AuthorAssociation, ==, "MEMBER");
					DMibExpect(Reply.m_Line, ==, 18);
					DMibExpect(Reply.m_UpdatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 11, 46, 0));
					DMibExpect(Reply.m_NodeID, ==, "PRRC_kwDOG2ONas77Rk54");
					DMibExpect(Reply.m_CommitID, ==, "6f3a2d1c0b9e8f7a6b5c4d3e2f1a0b9c8d7e6f5a");
				};
			};

			DMibTestCategory("SearchIssues")
			{
				auto Results = CGitHub::fs_ParseIssueSearchResults(CJsonSorted::fs_FromString(gc_SearchIssuesJson));
				DMibAssert(Results.f_GetLen(), ==, 2);

				DMibExpect(Results[0].m_Repository, ==, "Malterlib/Malterlib");
				DMibExpect(Results[0].m_Number, ==, 612);
				DMibExpectTrue(Results[0].m_bPullRequest);
				DMibExpect(Results[0].m_State, ==, "open");
				DMibExpect(Results[0].m_User, ==, "octocat");
				DMibExpect(Results[0].m_UpdatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 26, 9, 30, 0));

				DMibExpect(Results[1].m_Repository, ==, "Malterlib/Web");
				DMibExpectFalse(Results[1].m_bPullRequest);
			};

			DMibTestCategory("ReviewThreads")
			{
				constexpr ch8 const *c_pThreadsJson = R"-({
					"pageInfo": {"hasNextPage": false, "endCursor": "Y3Vyc29y"},
					"nodes": [
						{
							"id": "PRRT_kwDOAAAAAc5bbbbb",
							"isResolved": false,
							"path": "Source/Malterlib_Web_ACME.cpp",
							"line": 61,
							"comments":
							{
								"nodes":
								[
									{"databaseId": 2500000001, "body": "**[P2] Finding**", "author": {"login": "malterlib-review-bot", "__typename": "Bot"}},
									{"databaseId": 2500000002, "body": "Fixed", "author": {"login": "octocat", "__typename": "User"}}
								]
							}
						},
						{
							"id": "PRRT_kwDOAAAAAc5ccccc",
							"isResolved": true,
							"path": "Source/Other.cpp",
							"line": null,
							"comments": {"nodes": []}
						}
					]
				})-";

				auto Threads = CGitHub::fs_ParseReviewThreads(CJsonSorted::fs_FromString(c_pThreadsJson));
				DMibAssert(Threads.f_GetLen(), ==, 2);
				DMibExpect(Threads[0].m_ID, ==, "PRRT_kwDOAAAAAc5bbbbb");
				DMibExpectFalse(Threads[0].m_bResolved);
				DMibExpect(Threads[0].m_Path, ==, "Source/Malterlib_Web_ACME.cpp");
				DMibExpect(Threads[0].m_Line, ==, 61);
				DMibExpect(Threads[0].m_FirstCommentID, ==, 2500000001);
				DMibExpect(Threads[0].m_FirstCommentAuthor, ==, "malterlib-review-bot");
				DMibExpectTrue(Threads[0].m_bFirstCommentAuthorIsBot);
				DMibExpectFalse(Threads[0].m_Comments[1].m_bAuthorIsBot);
				DMibExpect(Threads[0].m_FirstCommentBody, ==, "**[P2] Finding**");
				DMibAssert(Threads[0].m_Comments.f_GetLen(), ==, 2);
				DMibExpect(Threads[0].m_Comments[1].m_ID, ==, 2500000002);
				DMibExpect(Threads[0].m_Comments[1].m_Author, ==, "octocat");
				DMibExpect(Threads[0].m_Comments[1].m_Body, ==, "Fixed");
				DMibExpectTrue(Threads[1].m_bResolved);
				DMibExpect(Threads[1].m_Line, ==, 0);
				DMibExpect(Threads[1].m_FirstCommentID, ==, 0);
			};

			DMibTestCategory("CommentEdits")
			{
				auto Unedited = CGitHub::fs_ParseCommentEdits(CJsonSorted::fs_FromString(R"-({"data": {"node": {"author": {"login": "octocat"}, "editor": null, "lastEditedAt": null}}})-"));
				DMibAssertTrue(bool(Unedited));
				DMibExpect(Unedited->m_Author, ==, "octocat");
				DMibExpect(Unedited->m_Editor, ==, "");
				DMibExpectFalse(Unedited->m_LastEditedAt.f_IsValid());

				auto Edited = CGitHub::fs_ParseCommentEdits
					(
						CJsonSorted::fs_FromString(R"-({"data": {"node": {"author": {"login": "octocat"}, "editor": {"login": "hubot"}, "lastEditedAt": "2026-10-12T07:43:04Z"}}})-")
					)
				;
				DMibAssertTrue(bool(Edited));
				DMibExpect(Edited->m_Editor, ==, "hubot");
				DMibExpectTrue(Edited->m_LastEditedAt.f_IsValid());

				auto Deleted = CGitHub::fs_ParseCommentEdits(CJsonSorted::fs_FromString(R"-({"data": {"node": {"author": null}}})-"));
				DMibAssertTrue(bool(Deleted));
				DMibExpect(Deleted->m_Author, ==, "");

				DMibExpectFalse(bool(CGitHub::fs_ParseCommentEdits(CJsonSorted::fs_FromString(R"-({"data": {"node": null}})-"))));
				DMibExpectFalse(bool(CGitHub::fs_ParseCommentEdits(CJsonSorted::fs_FromString(R"-({"errors": [{"message": "Could not resolve"}]})-"))));
			};

			DMibTestCategory("ApiRateLimits")
			{
				auto Limits = CGitHub::fs_ParseApiRateLimits
					(
						CJsonSorted::fs_FromString
						(
							R"-({"resources": {"core": {"limit": 5000, "remaining": 4990, "reset": 1793110000, "used": 10},)-"
							R"-( "graphql": {"limit": 5000, "remaining": 5000, "reset": 1793110100}}, "rate": {}})-"
						)
					)
				;
				DMibAssert(Limits.f_GetLen(), ==, 2);
				DMibExpect(Limits[0].m_Resource, ==, "core");
				DMibExpect(Limits[0].m_Limit, ==, 5000);
				DMibExpect(Limits[0].m_Remaining, ==, 4990);
				DMibExpect(NTime::CTimeConvert(Limits[0].m_ResetTime).f_UnixSeconds(), ==, 1793110000);
				DMibExpect(Limits[1].m_Resource, ==, "graphql");
			};

			DMibTestCategory("PullRequestReviews")
			{
				auto Reviews = CGitHub::fs_ParsePullRequestReviews(CJsonSorted::fs_FromString(gc_ReviewListJson));
				DMibAssert(Reviews.f_GetLen(), ==, 2);

				DMibExpect(Reviews[0].m_ID, ==, 6100000001);
				DMibExpect(Reviews[0].m_NodeID, ==, "PRR_kwDOAAAAAc5aaaaa");
				DMibExpect(Reviews[0].m_User, ==, "octocat");
				DMibExpect(Reviews[0].m_AuthorAssociation, ==, "MEMBER");
				DMibExpect(Reviews[0].m_State, ==, "COMMENTED");
				DMibExpect(Reviews[0].m_Body, ==, "@malterlib-review-bot review");
				DMibExpect(Reviews[0].m_CommitID, ==, "1111111111111111111111111111111111111111");
				DMibExpect(Reviews[0].m_SubmittedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 26, 9, 31, 0));

				DMibExpect(Reviews[1].m_State, ==, "PENDING");
				DMibExpectFalse(Reviews[1].m_SubmittedAt.f_IsValid());
			};

			DMibTestCategory("PullRequestFiles")
			{
				auto Files = CGitHub::fs_ParsePullRequestFiles(CJsonSorted::fs_FromString(gc_PullRequestFilesJson));
				DMibAssert(Files.f_GetLen(), ==, 2);

				DMibExpect(Files[0].m_FileName, ==, "Source/Malterlib_Web_Acme.cpp");
				DMibExpect(Files[0].m_Status, ==, "modified");
				DMibExpect(Files[0].m_nAdditions, ==, 3);
				DMibExpect(Files[0].m_nDeletions, ==, 1);
				DMibExpect(Files[0].m_Patch, ==, "@@ -1,2 +1,4 @@\n-a\n+b\n+c\n+d");

				DMibExpect(Files[1].m_PreviousFileName, ==, "Source/Old.cpp");
				DMibExpect(Files[1].m_Patch, ==, "");
			};

			DMibTestCategory("IssueComments")
			{
				auto Comments = CGitHub::fs_ParseIssueComments(CJsonSorted::fs_FromString(gc_IssueCommentsJson));
				DMibAssert(Comments.f_GetLen(), ==, 1);

				DMibExpect(Comments[0].m_ID, ==, 6018466902);
				DMibExpect(Comments[0].m_NodeID, ==, "IC_kwDOG2ONas8AAAABZrqEVg");
				DMibExpect(Comments[0].m_User, ==, "hubot");
				DMibExpect(Comments[0].m_Body, ==, "Can you also check the server package?");
				DMibExpect(Comments[0].m_UpdatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 10, 16, 0));
			};

			DMibTestCategory("CreatedIssueComment")
			{
				auto Comment = CGitHub::fs_ParseIssueComment
					(
						CJsonSorted::fs_FromString
						(
							R"-----({"id": 6018470001, "node_id": "IC_kwDOG2ONas8AAAABZrqVcQ", "user": {"login": "malterlib-review[bot]"},
							"body": "Starting a review round.", "created_at": "2026-10-28T08:00:00Z", "updated_at": "2026-10-28T08:00:00Z",
							"html_url": "https://github.com/octo-org/octo-repo/pull/2520#issuecomment-6018470001"})-----"
						)
					)
				;
				DMibExpect(Comment.m_ID, ==, 6018470001);
				DMibExpect(Comment.m_User, ==, "malterlib-review[bot]");
				DMibExpect(Comment.m_Body, ==, "Starting a review round.");
				DMibExpect(Comment.m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 28, 8, 0, 0));
			};

			DMibTestCategory("CommitStatus")
			{
				CGitHub::CCommitStatus Status;
				Status.m_State = "success";
				Status.m_Context = "malterlib-review";
				auto Json = CGitHub::fs_CommitStatusJson(Status);
				DMibExpect(Json["state"].f_String(), ==, "success");
				DMibExpectFalse(Json.f_GetMember("description"));
				DMibExpectFalse(Json.f_GetMember("target_url"));

				for (umint iCharacter = 0; iCharacter < 150; ++iCharacter)
					Status.m_Description += "x";
				Status.m_TargetUrl = "https://example.com/round";
				Json = CGitHub::fs_CommitStatusJson(Status);
				DMibExpect(Json["description"].f_String().f_GetLen(), ==, 140);
				DMibExpectTrue(Json["description"].f_String().f_EndsWith("..."));
				DMibExpect(Json["target_url"].f_String(), ==, "https://example.com/round");

				Status.m_Description.f_Clear();
				for (umint iCharacter = 0; iCharacter < 141; ++iCharacter)
					Status.m_Description += "é";
				Json = CGitHub::fs_CommitStatusJson(Status);
				CStr Expected;
				for (umint iCharacter = 0; iCharacter < 137; ++iCharacter)
					Expected += "é";
				DMibExpect(Json["description"].f_String(), ==, Expected + "...");

				Status.m_Description = Expected + "ééé";
				Json = CGitHub::fs_CommitStatusJson(Status);
				DMibExpect(Json["description"].f_String(), ==, Status.m_Description);
			};

			DMibTestCategory("FileContentsPath")
			{
				DMibExpect(CGitHub::fs_FileContentsPath("o", "r", "Dir/a?b#c.txt", "topic&fix#1"), ==, "repos/o/r/contents/Dir/a%3fb%23c.txt?ref=topic%26fix%231");

				NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << CGitHub::fs_FileContentsPath("o", "r", "Dir/a?b#c.txt", "topic&fix#1"));
				DMibAssert(Url.f_GetQuery().f_GetLen(), ==, 1);
				DMibExpect(Url.f_GetQuery()[0].m_Key, ==, "ref");
				DMibExpect(Url.f_GetQuery()[0].m_Value, ==, "topic&fix#1");
				DMibExpect(Url.f_GetPath().f_GetLast(), ==, "a?b#c.txt");
				DMibExpect(Url.f_GetFragment(), ==, "");
			};

			DMibTestCategory("ReactionPath")
			{
				using EReactionSubject = CGitHub::EReactionSubject;
				DMibExpect(CGitHub::fs_ReactionPath("o", "r", EReactionSubject::mc_IssueComment, 5), ==, "repos/o/r/issues/comments/5/reactions");
				DMibExpect(CGitHub::fs_ReactionPath("o", "r", EReactionSubject::mc_ReviewComment, 5), ==, "repos/o/r/pulls/comments/5/reactions");
				DMibExpect(CGitHub::fs_ReactionPath("o", "r", EReactionSubject::mc_Issue, 5), ==, "repos/o/r/issues/5/reactions");
			};

			DMibTestCategory("IssueEvents")
			{
				auto Events = CGitHub::fs_ParseIssueEvents(CJsonSorted::fs_FromString(gc_IssueEventsJson));
				DMibAssert(Events.f_GetLen(), ==, 2);

				DMibExpect(Events[1].m_Event, ==, "reopened");
				DMibExpect(Events[1].m_Actor, ==, "octocat");
				DMibExpect(Events[1].m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 25, 10, 0, 0));
			};

			DMibTestCategory("ActionsSettingsPatternOrder")
			{
				auto Settings = CGitHub::fs_ParseActionsSettings
					(
						CJsonSorted::fs_FromString(R"-----({"enabled": true, "allowed_actions": "selected"})-----")
						, CJsonSorted::fs_FromString(R"-----({"github_owned_allowed": true, "verified_allowed": false, "patterns_allowed": ["octo-org/*", "actions/checkout@*"]})-----")
						, CJsonSorted()
						, CJsonSorted()
					)
				;

				DMibAssertTrue(bool(Settings.m_AllowedActions));
				DMibAssertTrue(Settings.m_AllowedActions->f_IsOfType<NMib::NGit::CGitHostingProvider::CAllowedAction_Selected>());
				DMibExpect
					(
						Settings.m_AllowedActions->f_GetAsType<NMib::NGit::CGitHostingProvider::CAllowedAction_Selected>().m_PatternsAllowed
						, ==
						, (TCVector<CStr>{"actions/checkout@*", "octo-org/*"})
					)
				;
			};

			DMibTestCategory("TeamMembership")
			{
				using ETeamMembershipState = NMib::NGit::CGitHostingProvider::ETeamMembershipState;

				DMibExpectTrue(CGitHub::fs_ParseTeamMembership(CJsonSorted::fs_FromString(R"-----({"state": "active", "role": "member"})-----")) == ETeamMembershipState::mc_Active);
				DMibExpectTrue(CGitHub::fs_ParseTeamMembership(CJsonSorted::fs_FromString(R"-----({"state": "pending", "role": "member"})-----")) == ETeamMembershipState::mc_Pending);
				DMibExpectTrue(CGitHub::fs_ParseTeamMembership(CJsonSorted()) == ETeamMembershipState::mc_None);
			};

			DMibTestCategory("BranchRequirements")
			{
				DMibTestCategory("ProtectionAndRules")
				{
					NMib::NGit::CGitHostingProvider::CBranchRequirements Requirements;
					CGitHub::fs_ParseBranchProtection(CJsonSorted::fs_FromString(gc_BranchProtectionJson), Requirements);

					DMibExpectTrue(Requirements.m_bProtected);
					DMibExpectTrue(Requirements.m_bPushRestricted);
					DMibExpect(Requirements.m_PushUsers, ==, TCVector<CStr>{"octocat"});
					DMibExpect(Requirements.m_PushTeams, ==, TCVector<CStr>{"pushers"});
					DMibExpect(Requirements.m_PushApps, ==, TCVector<CStr>{"malterlib-review-bot"});
					DMibExpectTrue(Requirements.m_bRequiresLinearHistory);
					DMibExpectFalse(Requirements.m_bAllowsForcePushes);
				};

				DMibTestCategory("RulesOnly")
				{
					NMib::NGit::CGitHostingProvider::CBranchRequirements Requirements;
					CGitHub::fs_ParseBranchProtection(CJsonSorted(), Requirements);
					DMibExpectFalse(Requirements.m_bProtected);
					DMibExpectFalse(Requirements.m_bRequiresLinearHistory);
					DMibExpectTrue(Requirements.m_bAllowsForcePushes);

					CGitHub::fs_ParseBranchRules(CJsonSorted::fs_FromString(R"-----([{"type": "non_fast_forward"}, {"type": "required_linear_history"}])-----"), Requirements);
					DMibExpectTrue(Requirements.m_bRequiresLinearHistory);
					DMibExpectFalse(Requirements.m_bPushRestricted);
					DMibExpectFalse(Requirements.m_bAllowsForcePushes);
				};

				DMibTestCategory("RulesForbidForcePushes")
				{
					NMib::NGit::CGitHostingProvider::CBranchRequirements Requirements;
					CGitHub::fs_ParseBranchProtection(CJsonSorted::fs_FromString(R"-----({"allow_force_pushes": {"enabled": true}})-----"), Requirements);
					DMibExpectTrue(Requirements.m_bAllowsForcePushes);

					CGitHub::fs_ParseBranchRules(CJsonSorted::fs_FromString(R"-----([{"type": "non_fast_forward"}])-----"), Requirements);
					DMibExpectFalse(Requirements.m_bAllowsForcePushes);
					DMibExpectFalse(Requirements.m_bUpdatesRestrictedByRuleset);

					CGitHub::fs_ParseBranchRules(CJsonSorted::fs_FromString(R"-----([{"type": "update", "parameters": {"update_allows_fetch_and_merge": false}}])-----"), Requirements);
					DMibExpectTrue(Requirements.m_bUpdatesRestrictedByRuleset);
				};
			};

			DMibTestCategory("CombinedStatus")
			{
				auto Combined = CGitHub::fs_ParseCombinedStatus(CJsonSorted::fs_FromString(gc_CombinedStatusJson));
				DMibExpect(Combined.m_State, ==, "pending");
				DMibExpect(Combined.m_Sha, ==, "1111111111111111111111111111111111111111");
				DMibAssert(Combined.m_Statuses.f_GetLen(), ==, 1);
				DMibExpect(Combined.m_Statuses[0].m_Context, ==, "MalterlibReviewBot");
				DMibExpect(Combined.m_Statuses[0].m_Description, ==, "Round 1 running");
				DMibExpect(Combined.m_Statuses[0].m_Creator, ==, "malterlib-review-bot[bot]");
			};

			DMibTestCategory("CheckRuns")
			{
				auto CheckRuns = CGitHub::fs_ParseCheckRuns(CJsonSorted::fs_FromString(gc_CheckRunsJson));
				DMibAssert(CheckRuns.f_GetLen(), ==, 1);
				DMibExpect(CheckRuns[0].m_ID, ==, 4);
				DMibExpect(CheckRuns[0].m_Name, ==, "Build Linux");
				DMibExpect(CheckRuns[0].m_Conclusion, ==, "failure");
				DMibExpect(CheckRuns[0].m_AppSlug, ==, "github-actions");
				DMibExpect(CheckRuns[0].m_CompletedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 26, 10, 10, 0));
			};

			DMibTestCategory("CreatePullRequestReviewJson")
			{
				NMib::NGit::CGitHostingProvider::CCreatePullRequestReview Review;
				Review.m_CommitID = "1111111111111111111111111111111111111111";
				Review.m_Event = "REQUEST_CHANGES";
				Review.m_Body = "Round 1";
				Review.m_Comments.f_Insert({.m_Path = "a.cpp", .m_Line = 10, .m_Body = "One line"});
				Review.m_Comments.f_Insert({.m_Path = "b.cpp", .m_Line = 20, .m_StartLine = 15, .m_Body = "Several lines"});

				auto Json = CGitHub::fs_CreatePullRequestReviewJson(Review);
				DMibExpect(Json["commit_id"].f_String(), ==, "1111111111111111111111111111111111111111");
				DMibExpect(Json["event"].f_String(), ==, "REQUEST_CHANGES");
				DMibAssert(Json["comments"].f_Array().f_GetLen(), ==, 2);

				auto &OneLine = Json["comments"].f_Array()[0];
				DMibExpect(OneLine["line"].f_Integer(), ==, 10);
				DMibExpect(OneLine["side"].f_String(), ==, "RIGHT");
				DMibExpectFalse(OneLine.f_GetMember("start_line"));

				auto &SeveralLines = Json["comments"].f_Array()[1];
				DMibExpect(SeveralLines["start_line"].f_Integer(), ==, 15);
				DMibExpect(SeveralLines["start_side"].f_String(), ==, "RIGHT");
			};

			DMibTestCategory("RestrictedAccessTokenJson")
			{
				NMib::NGit::CGitHostingProvider::CRestrictedAccessTokenRequest Request;
				Request.m_Permissions["contents"] = "read";
				Request.m_Permissions["pull_requests"] = "read";
				Request.m_Repositories = {"Malterlib", "Web"};

				auto Json = CGitHub::fs_RestrictedAccessTokenJson(Request);
				DMibExpect(Json["permissions"]["contents"].f_String(), ==, "read");
				DMibExpect(Json["permissions"]["pull_requests"].f_String(), ==, "read");
				DMibAssert(Json["repositories"].f_Array().f_GetLen(), ==, 2);
				DMibExpect(Json["repositories"].f_Array()[1].f_String(), ==, "Web");

				DMibExpectTrue(CGitHub::fs_RestrictedAccessTokenJson({}).f_Object().f_IsEmpty());
			};

			DMibTestCategory("CollaboratorPermission")
			{
				DMibExpect(CGitHub::fs_ParseCollaboratorPermission(CJsonSorted::fs_FromString(gc_CollaboratorPermissionJson)), ==, "write");
				DMibExpect(CGitHub::fs_ParseCollaboratorPermission(CJsonSorted::fs_FromString(R"-----({"permission": "none", "role_name": "none"})-----")), ==, "none");
				DMibExpectException
					(
						CGitHub::fs_ParseCollaboratorPermission(CJsonSorted::fs_FromString(R"-----({"role_name": "triage"})-----"))
						, DMibErrorInstance("GitHub returned an unknown collaborator permission ''")
					)
				;
			};

			DMibTestCategory("Time")
			{
				auto Time = CGitHub::fs_ParseTime("2026-10-12T07:43:04Z");
				DMibExpect(Time, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 7, 43, 4));
				DMibExpect(CGitHub::fs_FormatTime(Time), ==, "2026-10-12T07:43:04Z");
				DMibExpectFalse(CGitHub::fs_ParseTime("").f_IsValid());
				DMibExpectFalse(CGitHub::fs_ParseTime("2026-13-12T07:43:04Z").f_IsValid());
				DMibExpectFalse(CGitHub::fs_ParseTime("yesterday").f_IsValid());
			};
		};

		DMibTestSuite("GenericRules")
		{
			DMibTestCategory("BypassActors")
			{
				auto Ruleset = CGitHub::fs_ParseGenericRuleset(CJsonSorted::fs_FromString(gc_RulesetBypassActorsJson));

				DMibAssertTrue(bool(Ruleset.m_BypassActors));
				auto &Actors = *Ruleset.m_BypassActors;
				DMibAssert(Actors.f_GetLen(), ==, 4);
				DMibAssertTrue(Actors[2].m_Actor.f_IsOfType<CProvider::CUser>());
				DMibExpect(Actors[2].m_Actor.f_GetAsType<CProvider::CUser>().m_ID, ==, "583231");
				DMibExpectTrue(Actors[3].m_Actor.f_IsOfType<CProvider::CDeployKey>());
				DMibExpectTrue(Ruleset.m_UnmanageableReasons.f_IsEmpty());

				auto Json = CGitHub::fs_GenericRulesetJson(Ruleset);
				DMibExpect
					(
						Json["bypass_actors"]
						, ==
						, CJsonSorted::fs_FromString
						(
							R"-----(
							[
								{"actor_id": 7, "actor_type": "Team", "bypass_mode": "pull_request"},
								{"actor_id": 1, "actor_type": "OrganizationAdmin", "bypass_mode": "always"},
								{"actor_id": 583231, "actor_type": "User", "bypass_mode": "always"},
								{"actor_id": null, "actor_type": "DeployKey", "bypass_mode": "always"}
							]
							)-----"
						)
					)
				;
				DMibExpect(Json["conditions"], ==, CJsonSorted::fs_FromString(R"-----({"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}})-----"));
				DMibExpect(Json["name"].f_String(), ==, "Default branch");
				DMibExpect(Json["target"].f_String(), ==, "branch");
				DMibExpect(Json["enforcement"].f_String(), ==, "active");

				CProvider::CGenericRuleset Unresolved;
				Unresolved.m_BypassActors = TCVector<CProvider::CGenericRuleBypassActor>{{.m_Actor = CProvider::CUser{.m_Login = "octocat"}}};
				DMibExpectException(CGitHub::fs_GenericRulesetJson(Unresolved), DMibErrorInstance("Bypass user 'octocat' has no ID"));
			};

			DMibTestCategory("UnknownRules")
			{
				auto Input = CJsonSorted::fs_FromString(gc_RulesetUnknownRulesJson);
				auto Ruleset = CGitHub::fs_ParseGenericRuleset(Input);

				DMibExpectTrue(Ruleset.m_UnmanageableReasons.f_IsEmpty());
				DMibAssertTrue(bool(Ruleset.m_Rules));
				DMibExpect(Ruleset.m_Rules->f_GetLen(), ==, 3);
				DMibExpect
					(
						Ruleset.m_Unmodelled.f_GetDescription()
						, ==
						, "copilot_code_review rule, merge_queue rule, pull_request parameter required_reviewers, required_status_checks parameter do_not_enforce_on_create"
					)
				;

				for (auto &Rule : *Ruleset.m_Rules)
				{
					if (Rule.f_IsOfType<CProvider::CGenericRule_PullRequest>())
					{
						DMibExpect
							(
								Rule.f_GetAsType<CProvider::CGenericRule_PullRequest>().m_AllowedMergeMethods
								, ==
								, TCOptional<TCVector<CProvider::EGenericRuleMergeMethod>>(TCVector<CProvider::EGenericRuleMergeMethod>{CProvider::EGenericRuleMergeMethod::mc_Rebase})
							)
						;
					}
				}

				auto Json = CGitHub::fs_GenericRulesetJson(Ruleset);
				DMibExpect(fg_RulesByType(Json["rules"]), ==, fg_RulesByType(Input["rules"]));

				DMibTestCategory("RulesNotListed")
				{
					auto WithoutRules = Ruleset;
					WithoutRules.m_Rules.f_Clear();
					DMibExpectFalse(bool(CGitHub::fs_GenericRulesetJson(WithoutRules).f_GetMember("rules")));
				};

				DMibTestCategory("DefaultMergeMethods")
				{
					auto WithoutMergeMethods = Input;
					for (auto &Rule : WithoutMergeMethods["rules"].f_Array())
					{
						if (Rule["type"].f_String() == "pull_request")
							Rule["parameters"].f_RemoveMember("allowed_merge_methods");
					}

					// A rule that doesn't say which merge methods it allows has GitHub's default, which an update or create leaves to GitHub
					auto Parsed = CGitHub::fs_ParseGenericRuleset(WithoutMergeMethods);
					auto ParsedJson = CGitHub::fs_GenericRulesetJson(Parsed);
					DMibExpectFalse(bool(fg_RulesByType(ParsedJson["rules"])["pull_request"]["parameters"].f_GetMember("allowed_merge_methods")));
				};
			};

			DMibTestCategory("GoneBypassActors")
			{
				auto Ruleset = CGitHub::fs_ParseGenericRuleset(CJsonSorted::fs_FromString(gc_RulesetGoneActorsJson));
				DMibExpectTrue(Ruleset.m_UnmanageableReasons.f_IsEmpty());
				DMibAssertTrue(bool(Ruleset.m_BypassActors));
				DMibAssert(Ruleset.m_BypassActors->f_GetLen(), ==, 1);
				DMibExpect(CStr("{}"_f << (*Ruleset.m_BypassActors)[0]), ==, "Actor: RepositoryRole: 4 () Mode: Always");

				// An entry without a type or mode could grant anything, so the ruleset can't be updated, also when its actor is gone. So could an
				// entry that is neither an object nor null.
				auto Incomplete = CGitHub::fs_ParseGenericRuleset
					(
						CJsonSorted::fs_FromString
						(
							R"-----(
							{
								"name": "Incomplete",
								"enforcement": "active",
								"bypass_actors":
								[
									{"actor_id": 7},
									{"actor_id": 1, "actor_type": "OrganizationAdmin"},
									{"actor_id": null, "actor_type": "Team"},
									{"actor_id": null, "bypass_mode": "always"},
									{"actor_id": "42", "actor_type": "Team", "bypass_mode": "always"},
									{"actor_id": "1", "actor_type": "OrganizationAdmin", "bypass_mode": "always"},
									{"actor_id": true, "actor_type": "DeployKey", "bypass_mode": "always"},
									5,
									null
								]
							}
							)-----"
						)
					)
				;
				DMibExpect
					(
						Incomplete.m_UnmanageableReasons
						, ==
						,
						(
							TCVector<CStr>
							{
								"Bypass actor of type ''"
								, "Bypass mode '' of OrganizationAdmin"
								, "Bypass mode '' of Team:  ()"
								, "Bypass actor of type ''"
								, "Bypass actor of type 'Team' with an actor_id that isn't an integer: \"42\""
								, "Bypass actor of type 'OrganizationAdmin' with an actor_id that isn't an integer: \"1\""
								, "Bypass actor of type 'DeployKey' with an actor_id that isn't an integer: true"
								, "Bypass actor that isn't an object: 5"
							}
						)
					)
				;
				DMibExpectTrue(bool(Incomplete.m_BypassActors));
				DMibExpectTrue(Incomplete.m_BypassActors->f_IsEmpty());
			};

			DMibTestCategory("UnknownBypassActors")
			{
				auto Ruleset = CGitHub::fs_ParseGenericRuleset(CJsonSorted::fs_FromString(gc_RulesetUnknownActorsJson));

				DMibExpect(Ruleset.m_UnmanageableReasons, ==, (TCVector<CStr>{"Bypass actor of type 'EnterpriseOwner'", "Bypass mode 'exempt' of RepositoryRole: 4 ()"}));
				DMibAssertTrue(bool(Ruleset.m_BypassActors));
				DMibAssert(Ruleset.m_BypassActors->f_GetLen(), ==, 1);
				DMibExpectTrue((*Ruleset.m_BypassActors)[0].m_Actor.f_IsOfType<CProvider::CDeployKey>());
				DMibExpectTrue(bool(Ruleset.m_Rules));
			};

			DMibTestCategory("UnknownStringMatchOperator")
			{
				auto Ruleset = CGitHub::fs_ParseGenericRuleset
					(
						CJsonSorted::fs_FromString
						(
							R"-----(
							{
								"id": 4215,
								"name": "Commit messages",
								"target": "branch",
								"enforcement": "active",
								"rules":
								[
									{"type": "commit_message_pattern", "parameters": {"operator": "glob", "pattern": "fix: *"}},
									{"type": "branch_name_pattern", "parameters": {"operator": "starts_with", "pattern": "feature/"}}
								]
							}
							)-----"
						)
					)
				;

				// Taken as the default operator, it would read as contains and look applied to a policy that asks for that
				DMibExpect(Ruleset.m_UnmanageableReasons, ==, TCVector<CStr>{"String match operator 'glob'"});
			};

			DMibTestCategory("UnknownRuleEntryFields")
			{
				auto Ruleset = CGitHub::fs_ParseGenericRuleset
					(
						CJsonSorted::fs_FromString
						(
							R"-----(
							{
								"id": 4216,
								"name": "Checks",
								"target": "branch",
								"enforcement": "active",
								"rules":
								[
									{
										"type": "required_status_checks",
										"parameters":
										{
											"strict_required_status_checks_policy": true,
											"required_status_checks": [{"context": "Lint"}, {"context": "Tests", "integration_id": 15368, "enforcement": "required"}]
										}
									},
									{
										"type": "workflows",
										"parameters":
										{
											"workflows": [{"path": ".github/workflows/ci.yml", "repository_id": 42, "ref": "refs/heads/main", "sha": "0c1d9b7f", "name": "CI"}]
										}
									}
								]
							}
							)-----"
						)
					)
				;

				// An update writes these entries from the model, so it would drop the fields the model doesn't know
				DMibExpect(Ruleset.m_UnmanageableReasons, ==, (TCVector<CStr>{"Required status check field 'enforcement'", "Workflow field 'name'"}));
			};
		};

		DMibTestSuite("Policy") -> TCFuture<void>
		{
			DMibTestCategory("GoneBypassActors") -> TCFuture<void>
			{
				CMockGitHubState State;
				State.m_Rulesets = {gc_RulesetGoneActorsJson};

				auto Result = co_await fg_ApplyPolicy
					(
						fg_Move(State)
						, "GenericRules"
						, R"-----(
						{
							"Gone actors":
							{
								"Target": "Branch",
								"IncludeRefNames": ["~DEFAULT_BRANCH"],
								"ExcludeRefNames": [],
								"BypassActors": [{"Actor": {"Type": "RepositoryRole", "Value": "Write"}, "Mode": "Always"}],
								"Rules": [{"Type": "Deletion"}],
								"Enforcement": "Active"
							}
						}
						)-----"
						, {}
					)
				;

				DMibExpect(Result.m_Error, ==, "");
				DMibExpect(Result.m_Updated, ==, TCVector<CStr>{});
				DMibExpect(Result.m_Unmanageable, ==, TCVector<CStr>{});
				DMibExpectTrue(Result.m_State.m_UpdatedRulesets.f_IsEmpty());

				co_return {};
			};

			DMibTestCategory("UpdateUnmanageableRuleset") -> TCFuture<void>
			{
				TCActor<CGitHub> Provider = fg_Construct();
				auto DestroyProvider = co_await fg_AsyncDestroy(Provider);

				auto Ruleset = CGitHub::fs_ParseGenericRuleset(CJsonSorted::fs_FromString(gc_RulesetUnknownActorsJson));
				Ruleset.m_Enforcement = CProvider::EGenericRuleEnforcement::mc_Evaluate;

				// A repository that isn't owner/repo fails before any request, so nothing can reach GitHub whatever the order of the checks
				auto Result = co_await Provider(&CGitHub::f_UpdateGenericRuleset, "octo-org", "4211", fg_Move(Ruleset)).f_Wrap();

				DMibAssertFalse(bool(Result));
				DMibExpect
					(
						Result.f_GetExceptionStr()
						, ==
						, "Generic ruleset '4211' of 'octo-org' wasn't updated, since that would lose what the model can't express: Bypass actor of type "
						"'EnterpriseOwner', Bypass mode 'exempt' of RepositoryRole: 4 ()"
					)
				;
				co_return {};
			};

			DMibTestCategory("OmittedRefNameFilter") -> TCFuture<void>
			{
				TCMap<CStr, CStr> Policies =
					{
						{"OmitExclude", R"-----({"LFS NoDelete": {"IncludeRefNames": ["refs/tags/lfs/*"], "Enforcement": "Evaluate"}})-----"}
						, {"OmitInclude", R"-----({"LFS NoDelete": {"ExcludeRefNames": ["refs/tags/lfs/keep/*"], "Enforcement": "Evaluate"}})-----"}
						, {"OmitBoth", R"-----({"LFS NoDelete": {"Enforcement": "Evaluate"}})-----"}
					}
				;

				for (auto &Policy : Policies)
				{
					auto &Case = Policies.fs_GetKey(Policy);
					DMibTestPath(Case);

					auto Current = CJsonSorted::fs_FromString(gc_RulesetLfsNoDeleteJson);
					Current["conditions"]["ref_name"]["exclude"] = CJsonSorted::fs_FromString(R"-----(["refs/tags/lfs/keep/*"])-----");

					CMockGitHubState State;
					State.m_Rulesets = {Current.f_ToString()};

					auto Result = co_await fg_ApplyPolicy(fg_Move(State), "GenericRules", Policy, {});

					DMibExpect(Result.m_Error, ==, "");
					auto *pUpdated = Result.m_State.m_UpdatedRulesets.f_FindEqual("4212");
					DMibAssertTrue(pUpdated);

					// An update with one filter sends both, so the other must keep what it has. One with neither must not send what it read,
					// which could restore filters changed since.
					if (Case == "OmitBoth")
						DMibExpectFalse(bool(pUpdated->f_GetMember("conditions")));
					else
					{
						DMibExpect
							(
								(*pUpdated)["conditions"]["ref_name"]
								, ==
								, CJsonSorted::fs_FromString(R"-----({"include": ["refs/tags/lfs/*"], "exclude": ["refs/tags/lfs/keep/*"]})-----")
							)
						;
					}
				}

				co_return {};
			};

			DMibTestCategory("UnmanageableRuleset") -> TCFuture<void>
			{
				CMockGitHubState State;
				State.m_Rulesets = {gc_RulesetUnknownActorsJson, gc_RulesetLfsNoDeleteJson};

				auto Result = co_await fg_ApplyPolicy
					(
						fg_Move(State)
						, "GenericRules"
						, R"-----(
						{
							"Release tags":
							{
								"Target": "Tag",
								"IncludeRefNames": ["refs/tags/v*"],
								"BypassActors": [{"Actor": {"Type": "DeployKey"}, "Mode": "Always"}],
								"Rules": [{"Type": "Deletion"}, {"Type": "Creation"}],
								"Enforcement": "Active"
							},
							"LFS NoDelete":
							{
								"Target": "Tag",
								"IncludeRefNames": ["refs/tags/lfs/*"],
								"BypassActors": [{"Actor": {"Type": "RepositoryRole", "Value": "Write"}, "Mode": "Always"}],
								"Rules": [{"Type": "Deletion"}, {"Type": "FastForwardOnly"}],
								"Enforcement": "Active"
							}
						}
						)-----"
						, {}
					)
				;

				DMibExpect(Result.m_Unmanageable, ==, TCVector<CStr>{"Release tags: Bypass actor of type 'EnterpriseOwner'\nBypass mode 'exempt' of RepositoryRole: 4 ()"});
				DMibExpectFalse(Result.m_bApplied);
				DMibExpectTrue(Result.m_Created.f_IsEmpty());
				DMibExpectTrue(Result.m_State.m_CreatedRulesets.f_IsEmpty());
				DMibExpect(Result.m_State.m_UpdatedRulesets.f_GetLen(), ==, 1);
				DMibExpectTrue(Result.m_State.m_UpdatedRulesets.f_FindEqual("4212"));
				DMibExpectFalse(Result.m_State.m_UpdatedRulesets.f_FindEqual("4211"));

				co_return {};
			};

			DMibTestCategory("RemoveOtherRulesets") -> TCFuture<void>
			{
				auto fState = []
					{
						CMockGitHubState State;
						State.m_Rulesets = {gc_RulesetUnknownActorsJson, gc_RulesetLfsNoDeleteJson, gc_RulesetOrganizationJson};
						return State;
					}
				;

				CStr Policy = R"-----(
					{
						"LFS NoDelete":
						{
							"Target": "Tag",
							"IncludeRefNames": ["refs/tags/lfs/*"],
							"ExcludeRefNames": [],
							"BypassActors": [],
							"Rules": [{"Type": "Deletion"}, {"Type": "FastForwardOnly"}],
							"Enforcement": "Active"
						}
					}
					)-----"
				;

				DMibTestCategory("Remove") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicy(fState(), "GenericRules", Policy, {.m_bRemoveOther = true});
					DMibExpect(Result.m_Deleted, ==, TCVector<CStr>{"Release tags"});
					DMibExpect(Result.m_State.m_DeletedRulesets, ==, TCVector<CStr>{"4211"});
					DMibExpectTrue(Result.m_State.m_UpdatedRulesets.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("Pretend") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicy(fState(), "GenericRules", Policy, {.m_bPretend = true, .m_bRemoveOther = true});
					DMibExpect(Result.m_Deleted, ==, TCVector<CStr>{"Release tags"});
					DMibExpectTrue(Result.m_State.m_DeletedRulesets.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("Keep") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicy(fState(), "GenericRules", Policy, {});
					DMibExpectTrue(Result.m_Deleted.f_IsEmpty());
					DMibExpectTrue(Result.m_State.m_DeletedRulesets.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("RemoveAll") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicy(fState(), "GenericRules", "{}", {.m_bRemoveOther = true});
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"Release tags", "LFS NoDelete"}));
					DMibExpect(Result.m_State.m_DeletedRulesets, ==, (TCVector<CStr>{"4211", "4212"}));
					co_return {};
				};

				co_return {};
			};

			DMibTestCategory("RemoveOtherWithoutReading") -> TCFuture<void>
			{
				CMockGitHubState State;
				for (CStr Pattern : TCVector<CStr>{"*", "master"})
				{
					CProvider::CBranchProtectionRule Rule;
					Rule.m_Pattern = Pattern;
					State.m_BranchProtectionRules[CStr("ID {}"_f << Pattern)] = fg_Move(Rule);
				}
				State.m_bFailBranchProtectionRead = true;

				// A policy that lists no branch protection rules deletes them by their patterns alone
				auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), R"-----({"BranchProtection": {}, "RemoveOther": {"BranchProtection": true}})-----", false);
				DMibExpect(Result.m_Error, ==, "");
				DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"*", "master"}));
				co_return {};
			};

			DMibTestCategory("RulesetNames") -> TCFuture<void>
			{
				TCActor<CRestCaptureGitHub> Provider = fg_Construct();
				auto DestroyProvider = co_await fg_AsyncDestroy(Provider);

				// GitHub lists inherited rulesets too unless told not to; only the repository's own are its to delete
				co_await Provider
					(
						&CRestCaptureGitHub::f_SetResponse
						, "repos/octo-org/octo-repo/rulesets"
						, CJsonSorted::fs_FromString
						(
							R"-----(
							[
								{"id": 4211, "name": "Release tags", "source_type": "Repository", "source": "octo-org/octo-repo"},
								{"id": 4213, "name": "Organization", "source_type": "Organization", "source": "octo-org"},
								{"id": 4218, "name": "Unknown source"}
							]
							)-----"
						)
					)
				;

				auto Names = co_await Provider(&CGitHub::f_GetGenericRulesetNames, "octo-org/octo-repo");
				DMibExpect(Names, ==, (TCMap<CStr, CStr>{{"4211", "Release tags"}}));

				auto Requests = co_await Provider(&CRestCaptureGitHub::f_GetRequests);
				DMibAssert(Requests.f_GetLen(), ==, 1);
				DMibExpect(Requests[0].m_Path, ==, "repos/octo-org/octo-repo/rulesets");
				DMibExpect(Requests[0].m_QueryParams, ==, (TCMap<CStr, CStr>{{"includes_parents", "false"}}));
				co_return {};
			};

			DMibTestCategory("OwnRulesets") -> TCFuture<void>
			{
				TCActor<CRestCaptureGitHub> Provider = fg_Construct();
				auto DestroyProvider = co_await fg_AsyncDestroy(Provider);

				co_await Provider
					(
						&CRestCaptureGitHub::f_SetResponse
						, "repos/octo-org/octo-repo/rulesets"
						, CJsonSorted::fs_FromString
						(
							R"-----(
							[
								{"id": 4212, "name": "LFS NoDelete", "source_type": "Repository", "source": "octo-org/octo-repo"},
								{"id": 4213, "name": "Organization", "source_type": "Organization", "source": "octo-org"},
								{"id": 4214, "name": "Other repository", "source_type": "Repository", "source": "octo-org/other-repo"},
								{"id": 4218, "name": "Unknown source"},
								{"id": 4219, "name": "No source in details", "source_type": "Repository", "source": "octo-org/octo-repo"}
							]
							)-----"
						)
					)
				;
				co_await Provider(&CRestCaptureGitHub::f_SetResponse, "repos/octo-org/octo-repo/rulesets/4212", CJsonSorted::fs_FromString(gc_RulesetLfsNoDeleteJson));

				auto NoSource = CJsonSorted::fs_FromString(gc_RulesetLfsNoDeleteJson);
				NoSource["id"] = 4219;
				NoSource["name"] = "No source in details";
				NoSource.f_RemoveMember("source_type");
				NoSource.f_RemoveMember("source");
				co_await Provider(&CRestCaptureGitHub::f_SetResponse, "repos/octo-org/octo-repo/rulesets/4219", fg_Move(NoSource));

				// Only the repository's own rulesets are read, and one whose details don't say whose it is can't be updated
				auto Rulesets = co_await Provider(&CGitHub::f_GetGenericRulesets, "octo-org/octo-repo");
				DMibAssert(Rulesets.f_GetLen(), ==, 2);
				DMibAssertTrue(Rulesets.f_FindEqual("4212"));
				DMibExpectTrue(Rulesets["4212"].m_UnmanageableReasons.f_IsEmpty());
				DMibAssertTrue(Rulesets.f_FindEqual("4219"));
				DMibExpect(Rulesets["4219"].m_UnmanageableReasons, ==, TCVector<CStr>{"Details that don't say whose ruleset it is"});

				auto Requests = co_await Provider(&CRestCaptureGitHub::f_GetRequests);
				DMibAssert(Requests.f_GetLen(), ==, 3);
				DMibExpect(Requests[0].m_QueryParams, ==, (TCMap<CStr, CStr>{{"includes_parents", "false"}}));
				co_return {};
			};

			DMibTestCategory("OwnRulesetsWithoutSourceType") -> TCFuture<void>
			{
				TCActor<CRestCaptureGitHub> Provider = fg_Construct();
				auto DestroyProvider = co_await fg_AsyncDestroy(Provider);

				// GitHub documents source_type as optional, and compares the names of owners and repositories without case
				co_await Provider
					(
						&CRestCaptureGitHub::f_SetResponse
						, "repos/octo-org/octo-repo/rulesets"
						, CJsonSorted::fs_FromString
						(
							R"-----(
							[
								{"id": 4212, "name": "LFS NoDelete", "source": "Octo-Org/Octo-Repo"},
								{"id": 4213, "name": "Organization", "source": "octo-org"},
								{"id": 4214, "name": "Other repository", "source": "octo-org/other-repo"}
							]
							)-----"
						)
					)
				;

				auto Names = co_await Provider(&CGitHub::f_GetGenericRulesetNames, "octo-org/octo-repo");
				DMibExpect(Names, ==, (TCMap<CStr, CStr>{{"4212", "LFS NoDelete"}}));

				auto Details = CJsonSorted::fs_FromString(gc_RulesetLfsNoDeleteJson);
				Details.f_RemoveMember("source_type");
				Details["source"] = "OCTO-ORG/octo-repo";
				co_await Provider(&CRestCaptureGitHub::f_SetResponse, "repos/octo-org/octo-repo/rulesets/4212", fg_Move(Details));

				auto Rulesets = co_await Provider(&CGitHub::f_GetGenericRulesets, "octo-org/octo-repo");
				DMibAssert(Rulesets.f_GetLen(), ==, 1);
				DMibAssertTrue(Rulesets.f_FindEqual("4212"));
				DMibExpectTrue(Rulesets["4212"].m_UnmanageableReasons.f_IsEmpty());
				co_return {};
			};

			DMibTestCategory("RemoveOtherRulesetsWithoutReading") -> TCFuture<void>
			{
				CMockGitHubState State;
				State.m_Rulesets = {gc_RulesetUnknownActorsJson, gc_RulesetLfsNoDeleteJson, gc_RulesetOrganizationJson};
				State.m_bFailRulesetRead = true;

				// A policy that lists no rulesets deletes them from the listing alone
				auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), R"-----({"GenericRules": {}, "RemoveOther": {"GenericRules": true}})-----", false);
				DMibExpect(Result.m_Error, ==, "");
				DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"Release tags", "LFS NoDelete"}));
				DMibExpect(Result.m_State.m_DeletedRulesets, ==, (TCVector<CStr>{"4211", "4212"}));
				co_return {};
			};

			DMibTestCategory("RemoveOtherBranchProtection") -> TCFuture<void>
			{
				auto fState = []
					{
						CMockGitHubState State;
						for (CStr Pattern : TCVector<CStr>{"*", "master", "release/*"})
						{
							CProvider::CBranchProtectionRule Rule;
							Rule.m_Pattern = Pattern;
							Rule.m_RequiresLinearHistory = true;
							State.m_BranchProtectionRules[CStr("ID {}"_f << Pattern)] = fg_Move(Rule);
						}
						return State;
					}
				;

				CStr Policy = R"-----({"master": {"RequiresLinearHistory": true}})-----";

				DMibTestCategory("Remove") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicy(fState(), "BranchProtection", Policy, {.m_bRemoveOther = true});
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"*", "release/*"}));
					DMibExpect(Result.m_State.m_DeletedBranchProtectionRules, ==, (TCVector<CStr>{"ID *", "ID release/*"}));
					DMibExpectTrue(Result.m_State.m_UpdatedBranchProtectionRules.f_IsEmpty());
					DMibExpectTrue(Result.m_State.m_CreatedBranchProtectionRules.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("Pretend") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicy(fState(), "BranchProtection", Policy, {.m_bPretend = true, .m_bRemoveOther = true});
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"*", "release/*"}));
					DMibExpectTrue(Result.m_State.m_DeletedBranchProtectionRules.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("Keep") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicy(fState(), "BranchProtection", Policy, {});
					DMibExpectTrue(Result.m_Deleted.f_IsEmpty());
					DMibExpectTrue(Result.m_State.m_DeletedBranchProtectionRules.f_IsEmpty());
					co_return {};
				};

				co_return {};
			};

			DMibTestCategory("RemoveOtherRejected") -> TCFuture<void>
			{
				DMibTestCategory("BranchProtection") -> TCFuture<void>
				{
					CMockGitHubState State;
					for (CStr Pattern : TCVector<CStr>{"*", "master", "release/*"})
					{
						CProvider::CBranchProtectionRule Rule;
						Rule.m_Pattern = Pattern;
						State.m_BranchProtectionRules[CStr("ID {}"_f << Pattern)] = fg_Move(Rule);
					}
					State.m_FailDeleteBranchProtection["ID release/*"];

					auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), R"-----({"RemoveOther": {"BranchProtection": true}})-----", false);
					DMibExpect(Result.m_Error.f_Find("Deleting branch protection rule 'ID release/*' was rejected"), >=, 0);
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"*", "master"}));
					co_return {};
				};

				DMibTestCategory("GenericRules") -> TCFuture<void>
				{
					CMockGitHubState State;
					State.m_Rulesets = {gc_RulesetUnknownActorsJson, gc_RulesetLfsNoDeleteJson};
					State.m_FailDeleteRulesets["4212"];

					auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), R"-----({"RemoveOther": {"GenericRules": true}})-----", false);
					DMibExpect(Result.m_Error.f_Find("Deleting ruleset '4212' was rejected"), >=, 0);
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"Release tags"}));
					co_return {};
				};

				co_return {};
			};

			DMibTestCategory("RemoveOtherAfterApplying") -> TCFuture<void>
			{
				auto fState = []
					{
						CMockGitHubState State;
						for (CStr Pattern : TCVector<CStr>{"*", "master"})
						{
							CProvider::CBranchProtectionRule Rule;
							Rule.m_Pattern = Pattern;
							State.m_BranchProtectionRules[CStr("ID {}"_f << Pattern)] = fg_Move(Rule);
						}
						return State;
					}
				;

				CStr Policy = R"-----(
					{
						"GenericRules":
						{
							"Linear history":
							{
								"Target": "Branch",
								"IncludeRefNames": ["~ALL"],
								"ExcludeRefNames": [],
								"BypassActors": [],
								"Rules": [{"Type": "LinearHistory"}],
								"Enforcement": "Active"
							}
						},
						"RemoveOther": {"BranchProtection": true}
					}
					)-----"
				;

				DMibTestCategory("Order") -> TCFuture<void>
				{
					auto Result = co_await fg_ApplyPolicyRules(fState(), Policy, false);
					DMibExpect(Result.m_Error, ==, "");
					DMibExpect(Result.m_State.m_Changes, ==, (TCVector<CStr>{"Create ruleset Linear history", "Delete branch protection ID *", "Delete branch protection ID master"}));
					co_return {};
				};

				DMibTestCategory("ReplacementFails") -> TCFuture<void>
				{
					auto State = fState();
					State.m_FailCreateRulesets["Linear history"];

					auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), Policy, false);
					DMibExpect(Result.m_Error.f_Find("Ruleset 'Linear history' was rejected"), >=, 0);
					DMibExpectTrue(Result.m_State.m_Changes.f_IsEmpty());
					DMibExpectTrue(Result.m_Deleted.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("ReplacementUnknownTeam") -> TCFuture<void>
				{
					TCMap<CStr, CStr> Policies =
						{
							{
								"CreateBranchProtection"
								, R"-----(
								{
									"BranchProtection": {"release/*": {"PushAllowances": [{"Type": "Team", "Value": "ghosts"}], "RestrictsPushes": true}},
									"RemoveOther": {"BranchProtection": true}
								}
								)-----"
							}
							, {
								"CreateRuleset"
								, R"-----(
								{
									"GenericRules":
									{
										"Linear history":
										{
											"Target": "Branch",
											"IncludeRefNames": ["~ALL"],
											"ExcludeRefNames": [],
											"BypassActors": [{"Actor": {"Type": "Team", "Value": "ghosts"}, "Mode": "Always"}],
											"Rules": [{"Type": "LinearHistory"}],
											"Enforcement": "Active"
										}
									},
									"RemoveOther": {"BranchProtection": true}
								}
								)-----"
							}
							, {
								"UpdateBranchProtection"
								, R"-----(
								{
									"BranchProtection": {"master": {"PushAllowances": [{"Type": "Team", "Value": "ghosts"}], "RestrictsPushes": true}},
									"RemoveOther": {"BranchProtection": true}
								}
								)-----"
							}
						}
					;

					// Applying fails looking up the team before it deletes anything, so pretending must fail too rather than report deletions
					for (auto &Policy : Policies)
					{
						for (bool bPretend : {false, true})
						{
							DMibTestPath("{} {}"_f << Policies.fs_GetKey(Policy) << (bPretend ? "Pretend" : "Apply"));

							auto State = fState();
							State.m_UnknownTeams["ghosts"];

							auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), Policy, bPretend);
							DMibExpect(Result.m_Error.f_Find("Could not find team 'ghosts'"), >=, 0);
							DMibExpectTrue(Result.m_Deleted.f_IsEmpty());
							DMibExpectTrue(Result.m_State.m_Changes.f_IsEmpty());
						}
					}

					co_return {};
				};

				DMibTestCategory("ReplacementUnknownRepository") -> TCFuture<void>
				{
					CStr Policy = R"-----({"BranchProtection": {"release/*": {"RequiresLinearHistory": true}}, "RemoveOther": {"BranchProtection": true}})-----";

					// Creating a branch protection rule looks up the repository's ID, so pretending must too rather than report deletions
					for (bool bPretend : {false, true})
					{
						DMibTestPath(bPretend ? "Pretend" : "Apply");

						auto State = fState();
						State.m_bFailRepositoryID = true;

						auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), Policy, bPretend);
						DMibExpect(Result.m_Error.f_Find("Could not find repository 'octo-org/octo-repo'"), >=, 0);
						DMibExpectTrue(Result.m_Deleted.f_IsEmpty());
						DMibExpectTrue(Result.m_State.m_Changes.f_IsEmpty());
					}

					co_return {};
				};

				DMibTestCategory("ResolveBranchProtectionRuleIDs") -> TCFuture<void>
				{
					TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
					auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
					co_await Provider
						(
							&CGraphQlCaptureGitHub::f_AddResponses
							, TCVector<CJsonSorted>{CJsonSorted::fs_FromString(R"-----({"data": {"repository": {"id": "R_1", "databaseId": 1}}})-----")}
						)
					;

					CProvider::CBranchProtectionRule Rule;
					Rule.m_Pattern = "release/*";

					// Only a create needs the repository's ID, so only its lookup asks for it
					co_await Provider(&CGitHub::f_ResolveBranchProtectionRuleIDs, "octo-org/octo-repo", TCOptional<CStr>("BPR_1"), Rule);
					DMibExpectTrue((co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests)).f_IsEmpty());

					co_await Provider(&CGitHub::f_ResolveBranchProtectionRuleIDs, "octo-org/octo-repo", TCOptional<CStr>(), Rule);
					auto Requests = co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests);
					DMibAssert(Requests.f_GetLen(), ==, 1);
					DMibExpect(Requests[0].m_Query.f_Find("GetRepositoryId"), >=, 0);
					co_return {};
				};

				DMibTestCategory("ReplacementUnmanageable") -> TCFuture<void>
				{
					auto State = fState();
					State.m_Rulesets = {gc_RulesetUnknownActorsJson};

					auto Result = co_await fg_ApplyPolicyRules
						(
							fg_Move(State)
							, R"-----(
							{
								"GenericRules": {"Release tags": {"Rules": [{"Type": "Deletion"}]}},
								"RemoveOther": {"BranchProtection": true}
							}
							)-----"
							, false
						)
					;
					DMibExpect(Result.m_Error, ==, "Nothing the policy doesn't list was deleted, since a generic ruleset it lists was left unchanged");
					DMibExpectTrue(Result.m_State.m_DeletedBranchProtectionRules.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("ReplacementUnmanageableRulesets") -> TCFuture<void>
				{
					CStr Policy = R"-----(
						{
							"GenericRules": {"Release tags": {"Enforcement": "Active", "Rules": [{"Type": "Deletion"}]}},
							"RemoveOther": {"GenericRules": true, "BranchProtection": true}
						}
						)-----"
					;

					for (bool bPretend : {false, true})
					{
						DMibTestPath(bPretend ? "Pretend" : "Apply");

						auto State = fState();
						State.m_Rulesets = {gc_RulesetUnknownActorsJson, gc_RulesetLfsNoDeleteJson};

						auto Result = co_await fg_ApplyPolicyRules(fg_Move(State), Policy, bPretend);
						DMibExpect(Result.m_Error, ==, "Nothing the policy doesn't list was deleted, since a generic ruleset it lists was left unchanged");
						DMibExpectTrue(Result.m_Deleted.f_IsEmpty());
						DMibExpectTrue(Result.m_State.m_DeletedRulesets.f_IsEmpty());
						DMibExpectTrue(Result.m_State.m_DeletedBranchProtectionRules.f_IsEmpty());
					}

					co_return {};
				};

				DMibTestCategory("RulesetsAfterApplying") -> TCFuture<void>
				{
					auto State = fState();
					State.m_Rulesets = {gc_RulesetLfsNoDeleteJson};

					auto Result = co_await fg_ApplyPolicyRules
						(
							fg_Move(State)
							, R"-----(
							{
								"GenericRules": {"Linear history": {"Target": "Branch", "Rules": [{"Type": "LinearHistory"}]}},
								"RemoveOther": {"GenericRules": true}
							}
							)-----"
							, false
						)
					;
					DMibExpect(Result.m_Error, ==, "");
					DMibExpect(Result.m_State.m_Changes, ==, (TCVector<CStr>{"Create ruleset Linear history", "Delete ruleset 4212"}));
					co_return {};
				};

				co_return {};
			};

			DMibTestCategory("ListOrder") -> TCFuture<void>
			{
				CMockGitHubState State;
				State.m_Rulesets = {gc_RulesetListOrderJson};

				auto Result = co_await fg_ApplyPolicy
					(
						fg_Move(State)
						, "GenericRules"
						, R"-----(
						{
							"Release branches":
							{
								"Target": "Branch",
								"IncludeRefNames": ["refs/heads/release/*", "refs/heads/hotfix/*"],
								"ExcludeRefNames": ["refs/heads/release/old", "refs/heads/hotfix/old"],
								"BypassActors": [{"Actor": {"Type": "OrganizationAdmin"}, "Mode": "Always"}],
								"Rules":
								[
									{"Type": "Deployments", "RequiredDeploymentEnvironments": ["staging", "production"]},
									{
										"Type": "PullRequest",
										"RequiredApprovingReviewCount": 1,
										"DismissStaleReviewsOnPush": false,
										"RequireCodeOwnerReview": false,
										"RequireLastPushApproval": false,
										"RequireReviewThreadResolution": false,
										"AllowedMergeMethods": ["Squash", "Merge"]
									}
								],
								"Enforcement": "Active"
							}
						}
						)-----"
						, {}
					)
				;

				DMibExpect(Result.m_Error, ==, "");
				DMibExpect(Result.m_Updated, ==, TCVector<CStr>{});
				DMibExpectTrue(Result.m_State.m_UpdatedRulesets.f_IsEmpty());

				co_return {};
			};

			DMibTestCategory("RepeatedRuleTypes") -> TCFuture<void>
			{
				auto Ruleset = CJsonSorted::fs_FromString(gc_RulesetUnknownRulesJson);
				auto &Rules = Ruleset["rules"].f_Array();
				Rules.f_Insert(CJsonSorted::fs_FromString(R"-----({"type": "merge_queue", "parameters": {"merge_method": "SQUASH"}})-----"));

				auto Parsed = CGitHub::fs_ParseGenericRuleset(Ruleset);
				DMibExpect(Parsed.m_UnmanageableReasons, ==, TCVector<CStr>{"Rule type 'merge_queue' more than once"});

				CMockGitHubState State;
				State.m_Rulesets = {Ruleset.f_ToString()};

				auto Result = co_await fg_ApplyPolicy
					(
						fg_Move(State)
						, "GenericRules"
						, R"-----({"Protected branches": {"Enforcement": "Evaluate", "Rules": [{"Type": "Deletion"}]}})-----"
						, {}
					)
				;
				DMibExpect(Result.m_Unmanageable, ==, TCVector<CStr>{"Protected branches: Rule type 'merge_queue' more than once"});
				DMibExpectTrue(Result.m_State.m_UpdatedRulesets.f_IsEmpty());
				co_return {};
			};

			DMibTestCategory("UnknownRulesKept") -> TCFuture<void>
			{
				CMockGitHubState State;
				State.m_Rulesets = {gc_RulesetUnknownRulesJson};

				auto Result = co_await fg_ApplyPolicy
					(
						fg_Move(State)
						, "GenericRules"
						, R"-----(
						{
							"Protected branches":
							{
								"Target": "Branch",
								"IncludeRefNames": ["~DEFAULT_BRANCH"],
								"ExcludeRefNames": [],
								"BypassActors": [{"Actor": {"Type": "RepositoryRole", "Value": "Write"}, "Mode": "Always"}],
								"Rules":
								[
									{"Type": "Deletion"},
									{
										"Type": "PullRequest",
										"RequiredApprovingReviewCount": 2,
										"DismissStaleReviewsOnPush": true,
										"RequireCodeOwnerReview": false,
										"RequireLastPushApproval": false,
										"RequireReviewThreadResolution": true,
										"AllowedMergeMethods": ["Rebase"]
									}
								],
								"Enforcement": "Active"
							}
						}
						)-----"
						, {}
					)
				;

				DMibExpectTrue(Result.m_bApplied);
				DMibAssert(Result.m_Updated.f_GetLen(), ==, 1);
				auto UpdatedLines = Result.m_Updated[0].f_Split("\n");
				DMibExpect
					(
						UpdatedLines.f_Contains("Kept, since the policy can't express them: copilot_code_review rule, merge_queue rule, pull_request parameter required_reviewers")
						, >=
						, 0
					)
				;
				DMibExpect(UpdatedLines.f_Contains("Removed with their rules: required_status_checks parameter do_not_enforce_on_create"), >=, 0);

				auto *pUpdated = Result.m_State.m_UpdatedRulesets.f_FindEqual("4214");
				DMibAssertTrue(pUpdated);

				auto Expected = fg_RulesByType(CJsonSorted::fs_FromString(gc_RulesetUnknownRulesJson)["rules"]);
				Expected.f_Remove("required_status_checks");
				Expected["pull_request"]["parameters"]["required_approving_review_count"] = 2;

				DMibExpect(fg_RulesByType((*pUpdated)["rules"]), ==, Expected);

				co_return {};
			};

			DMibTestCategory("MergeMethodsNotStated") -> TCFuture<void>
			{
				auto fPolicy = [](uint32 _RequiredApprovingReviewCount) -> CStr
					{
						return CStr(R"-----(
							{
								"Squash only":
								{
									"Rules":
									[
										{
											"Type": "PullRequest",
											"RequiredApprovingReviewCount": COUNT,
											"DismissStaleReviewsOnPush": false,
											"RequireCodeOwnerReview": false,
											"RequireLastPushApproval": false,
											"RequireReviewThreadResolution": false
										}
									]
								}
							}
							)-----").f_Replace("COUNT", CStr::fs_ToStr(_RequiredApprovingReviewCount))
						;
					}
				;

				DMibTestCategory("Unchanged") -> TCFuture<void>
				{
					CMockGitHubState State;
					State.m_Rulesets = {gc_RulesetSquashOnlyJson};

					auto Result = co_await fg_ApplyPolicy(fg_Move(State), "GenericRules", fPolicy(1), {});
					DMibExpect(Result.m_Error, ==, "");
					DMibExpect(Result.m_Updated, ==, TCVector<CStr>{});
					DMibExpectTrue(Result.m_State.m_UpdatedRulesets.f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("Kept") -> TCFuture<void>
				{
					CMockGitHubState State;
					State.m_Rulesets = {gc_RulesetSquashOnlyJson};

					auto Result = co_await fg_ApplyPolicy(fg_Move(State), "GenericRules", fPolicy(2), {});
					auto *pUpdated = Result.m_State.m_UpdatedRulesets.f_FindEqual("4216");
					DMibAssertTrue(pUpdated);

					auto Parameters = fg_RulesByType((*pUpdated)["rules"])["pull_request"]["parameters"];
					DMibExpect(Parameters["required_approving_review_count"].f_Integer(), ==, 2);
					DMibExpect(Parameters["allowed_merge_methods"], ==, CJsonSorted::fs_FromString(R"-----(["squash"])-----"));
					co_return {};
				};

				co_return {};
			};

			DMibTestCategory("UnknownRulesWithoutRuleList") -> TCFuture<void>
			{
				CMockGitHubState State;
				State.m_Rulesets = {gc_RulesetUnknownRulesJson};

				auto Result = co_await fg_ApplyPolicy(fg_Move(State), "GenericRules", R"-----({"Protected branches": {"Enforcement": "Evaluate"}})-----", {});

				DMibExpect(Result.m_Updated, ==, TCVector<CStr>{"Protected branches: Enforcement (Active -> Evaluate)"});

				auto *pUpdated = Result.m_State.m_UpdatedRulesets.f_FindEqual("4214");
				DMibAssertTrue(pUpdated);
				DMibExpectFalse(bool(pUpdated->f_GetMember("rules")));

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Rest")
		{
			DMibTestCategory("ObjectWrappedPaging")
			{
				auto Page1 = CJsonSorted::fs_FromString(R"-----({"total_count": 3, "workflow_runs": [{"id": 3}, {"id": 2}]})-----");
				auto Page2 = CJsonSorted::fs_FromString(R"-----({"total_count": 3, "workflow_runs": [{"id": 1}]})-----");

				DMibTestCategory("TwoPages")
				{
					CJsonSorted Result;
					DMibExpectFalse(CGitHub::fs_MergeRestPage(Result, fg_TempCopy(Page1), "workflow_runs", 0));
					DMibExpectFalse(CGitHub::fs_MergeRestPage(Result, fg_TempCopy(Page2), "workflow_runs", 0));

					auto Runs = CGitHub::fs_ParseWorkflowRuns(Result);
					DMibAssert(Runs.f_GetLen(), ==, 3);
					DMibExpect(Runs[0].m_ID, ==, 3);
					DMibExpect(Runs[1].m_ID, ==, 2);
					DMibExpect(Runs[2].m_ID, ==, 1);
					DMibExpect(Result["total_count"].f_Integer(), ==, 3);
				};

				DMibTestCategory("MaxItemsOnSecondPage")
				{
					CJsonSorted Result;
					DMibExpectFalse(CGitHub::fs_MergeRestPage(Result, fg_TempCopy(Page1), "workflow_runs", 3));
					DMibExpectTrue(CGitHub::fs_MergeRestPage(Result, fg_TempCopy(Page2), "workflow_runs", 3));
					DMibExpect(Result["workflow_runs"].f_GetLen(), ==, 3);
				};

				DMibTestCategory("MaxItemsTruncates")
				{
					CJsonSorted Result;
					DMibExpectFalse(CGitHub::fs_MergeRestPage(Result, fg_TempCopy(Page1), "workflow_runs", 0));
					DMibExpectTrue(CGitHub::fs_MergeRestPage(Result, fg_TempCopy(Page2), "workflow_runs", 2));

					auto Runs = CGitHub::fs_ParseWorkflowRuns(Result);
					DMibAssert(Runs.f_GetLen(), ==, 2);
					DMibExpect(Runs[1].m_ID, ==, 2);

					CJsonSorted FirstPageResult;
					DMibExpectTrue(CGitHub::fs_MergeRestPage(FirstPageResult, fg_TempCopy(Page1), "workflow_runs", 1));
					DMibExpect(FirstPageResult["workflow_runs"].f_GetLen(), ==, 1);
				};

				DMibTestCategory("MissingMember")
				{
					CJsonSorted Result;
					CGitHub::fs_MergeRestPage(Result, fg_TempCopy(Page1), "workflow_runs", 0);
					DMibExpectException
						(
							CGitHub::fs_MergeRestPage(Result, CJsonSorted::fs_FromString(R"-----({"jobs": []})-----"), "workflow_runs", 0)
							, DMibErrorInstance("Unexpected paged GitHub request where new result has no 'workflow_runs' array")
						)
					;
				};
			};

			DMibTestCategory("ArrayPaging")
			{
				CJsonSorted Result;
				DMibExpectFalse(CGitHub::fs_MergeRestPage(Result, CJsonSorted::fs_FromString("[1, 2]"), "", 0));
				DMibExpectFalse(CGitHub::fs_MergeRestPage(Result, CJsonSorted::fs_FromString("[3]"), "", 0));
				DMibExpect(Result.f_GetLen(), ==, 3);

				CJsonSorted Object;
				DMibExpectFalse(CGitHub::fs_MergeRestPage(Object, CJsonSorted::fs_FromString(R"-----({"id": 1})-----"), "", 0));
				DMibExpect(Object["id"].f_Integer(), ==, 1);
			};

			DMibTestCategory("NextPageUrl")
			{
				DMibExpect
					(
						CGitHub::fs_GetNextPageUrl
						(
							"<https://api.github.com/repositories/459509098/actions/workflows/tests.yml/runs?per_page=3&page=2>; rel=\"next\", "
							"<https://api.github.com/repositories/459509098/actions/workflows/tests.yml/runs?per_page=3&page=1383>; rel=\"last\""
						)
						, ==
						, "https://api.github.com/repositories/459509098/actions/workflows/tests.yml/runs?per_page=3&page=2"
					)
				;
				DMibExpect
					(
						CGitHub::fs_GetNextPageUrl
						(
							"<https://api.github.com/repositories/459509098/pulls?page=1>; rel=\"prev\", "
							"<https://api.github.com/repositories/459509098/pulls?page=1>; rel=\"first\""
						)
						, ==
						, ""
					)
				;
				DMibExpect(CGitHub::fs_GetNextPageUrl("<https://example.com/next>; rel=\"next\""), ==, "");
			};

			DMibTestCategory("RateLimit")
			{
				auto Now = NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 12, 0, 0);

				DMibTestCategory("PrimaryLimit")
				{
					TCMap<CStr, CStr, CCompareNoCase> Headers;
					Headers["x-ratelimit-limit"] = "5000";
					Headers["x-ratelimit-remaining"] = "0";
					Headers["x-ratelimit-reset"] = "1791809490";
					Headers["x-ratelimit-resource"] = "core";

					auto RateLimit = CGitHub::fs_GetRateLimit(403, Headers, {}, Now);
					DMibAssertTrue(bool(RateLimit));
					DMibExpect(RateLimit->m_ResetTime, ==, NTime::CTimeConvert::fs_FromUnixSeconds(1791809490));
					DMibAssertTrue(bool(RateLimit->m_Remaining));
					DMibExpect(*RateLimit->m_Remaining, ==, 0);
					DMibExpectFalse(bool(RateLimit->m_RetryAfterSeconds));
				};

				DMibTestCategory("SecondaryLimit")
				{
					TCMap<CStr, CStr, CCompareNoCase> Headers;
					Headers["retry-after"] = "60";
					Headers["x-ratelimit-remaining"] = "4211";
					Headers["x-ratelimit-reset"] = "1791809490";

					auto RateLimit = CGitHub::fs_GetRateLimit(429, Headers, {}, Now);
					DMibAssertTrue(bool(RateLimit));
					DMibExpect(RateLimit->m_ResetTime, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 12, 1, 0));
					DMibAssertTrue(bool(RateLimit->m_RetryAfterSeconds));
					DMibExpect(*RateLimit->m_RetryAfterSeconds, ==, 60);
					DMibAssertTrue(bool(RateLimit->m_Remaining));
					DMibExpect(*RateLimit->m_Remaining, ==, 4211);
				};

				DMibTestCategory("TooManyRequestsWithoutHeaders")
				{
					auto RateLimit = CGitHub::fs_GetRateLimit(429, {}, {}, Now);
					DMibAssertTrue(bool(RateLimit));
					DMibExpectFalse(RateLimit->m_ResetTime.f_IsValid());
				};

				DMibTestCategory("NotRateLimited")
				{
					TCMap<CStr, CStr, CCompareNoCase> Headers;
					Headers["x-ratelimit-remaining"] = "4996";
					Headers["x-ratelimit-reset"] = "1791809490";

					DMibExpectFalse(bool(CGitHub::fs_GetRateLimit(403, Headers, {}, Now)));
					DMibExpectFalse(bool(CGitHub::fs_GetRateLimit(200, Headers, {}, Now)));

					Headers["x-ratelimit-remaining"] = "0";
					DMibExpectFalse(bool(CGitHub::fs_GetRateLimit(404, Headers, {}, Now)));
				};

				DMibTestCategory("SecondaryLimitWithoutRetryAfter")
				{
					TCMap<CStr, CStr, CCompareNoCase> Headers;
					Headers["x-ratelimit-remaining"] = "4996";

					CStr Body = R"({"message": "You have exceeded a secondary rate limit. Please wait a few minutes before you try again."})";
					auto RateLimit = CGitHub::fs_GetRateLimit(403, Headers, Body, Now);
					DMibAssertTrue(bool(RateLimit));
					DMibExpect(RateLimit->m_ResetTime, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 12, 1, 0));

					DMibExpectFalse(bool(CGitHub::fs_GetRateLimit(403, Headers, R"({"message": "Resource not accessible by integration"})", Now)));
				};
			};
		};

		DMibTestSuite("App")
		{
			NContainer::CSecureByteVector PrivateKey;
			NContainer::CSecureByteVector PublicKey;
			NCryptography::CPublicCrypto::fs_GenerateKeys(PrivateKey, PublicKey, NCryptography::CPublicKeySettings_RSA(2048));

			auto fBase64UrlDecode = [](CStr const &_Encoded)
				{
					CStr Base64 = _Encoded.f_ReplaceChar('-', '+').f_ReplaceChar('_', '/');
					while (Base64.f_GetLen() % 4)
						Base64 += "=";

					CByteVector Data;
					fg_Base64Decode(Base64, Data);
					return Data;
				}
			;

			DMibTestCategory("Jwt")
			{
				auto Now = NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 12, 0, 0);
				CStr Jwt = CGitHub::fs_CreateAppJwt("123456", PrivateKey, Now);

				auto Parts = Jwt.f_Split(".");
				DMibAssert(Parts.f_GetLen(), ==, umint(3));

				auto fJson = [&](CStr const &_Part)
					{
						auto Data = fBase64UrlDecode(_Part);
						return CJsonSorted::fs_FromString(CStr((ch8 const *)Data.f_GetArray(), Data.f_GetLen()));
					}
				;

				auto Header = fJson(Parts[0]);
				DMibExpect(Header["alg"].f_String(), ==, "RS256");
				DMibExpect(Header["typ"].f_String(), ==, "JWT");

				auto Payload = fJson(Parts[1]);
				int64 UnixNow = int64(NTime::CTimeConvert(Now).f_UnixSeconds());
				DMibExpect(Payload["iss"].f_String(), ==, "123456");
				DMibExpect(Payload["iat"].f_Integer(), ==, UnixNow - 60);
				DMibExpect(Payload["exp"].f_Integer(), ==, UnixNow + 540);

				CStr SigningInput = "{}.{}"_f << Parts[0] << Parts[1];
				NContainer::CSecureByteVector Message;
				Message.f_Insert((uint8 const *)SigningInput.f_GetStr(), SigningInput.f_GetLen());

				auto SignatureData = fBase64UrlDecode(Parts[2]);
				NContainer::CSecureByteVector Signature;
				Signature.f_Insert(SignatureData.f_GetArray(), SignatureData.f_GetLen());

				DMibExpectTrue(NCryptography::CPublicCrypto::fs_VerifySignature(Message, PublicKey, Signature, NCryptography::EDigestType_SHA256));
			};

			DMibTestCategory("PemPrivateKey")
			{
				CStr Pem = fg_RsaPrivateKeyPem(PrivateKey);

				NContainer::CSecureByteVector PemData;
				PemData.f_Insert((uint8 const *)Pem.f_GetStr(), Pem.f_GetLen());

				DMibExpectTrue(NCryptography::CPublicCrypto::fs_ConvertToDer_PrivateKey(PemData) == PrivateKey);
			};
		};

		DMibTestSuite("GraphQl") -> TCFuture<void>
		{
			DMibTestCategory("DeleteBranchProtectionRule") -> TCFuture<void>
			{
				TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
				auto DestroyProvider = co_await fg_AsyncDestroy(Provider);

				co_await Provider(&CGitHub::f_DeleteBranchProtectionRule, "octo-org/octo-repo", "BPR_kwDOG2ONas4ABCDE");

				auto Requests = co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests);
				DMibAssert(Requests.f_GetLen(), ==, 1);

				// DeleteBranchProtectionRuleInput.branchProtectionRuleId is an ID!, and GitHub rejects a variable declared as anything else
				DMibExpect(Requests[0].m_Query.f_Find("($branchProtectionRuleId: ID!)"), >=, 0);
				DMibExpect(Requests[0].m_Query.f_Find("deleteBranchProtectionRule(input: {branchProtectionRuleId: $branchProtectionRuleId})"), >=, 0);
				DMibExpect(Requests[0].m_Variables, ==, CJsonSorted::fs_FromString(R"-----({"branchProtectionRuleId": "BPR_kwDOG2ONas4ABCDE"})-----"));

				co_return {};
			};

			DMibTestCategory("BranchProtectionRulePages") -> TCFuture<void>
			{
				TCVector<CJsonSorted> Pages =
					{
						fg_BranchProtectionRulesPage({{"BPR_1", "*"}, {"BPR_2", "master"}}, "Y3Vyc29yOnYyOpHOAAAAAg==")
						, fg_BranchProtectionRulesPage({{"BPR_3", "release/*"}}, "")
					}
				;

				DMibTestCategory("Read") -> TCFuture<void>
				{
					TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
					auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
					co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, Pages);

					auto Rules = co_await Provider(&CGitHub::f_GetBranchProtectionRules, "octo-org/octo-repo");
					DMibAssert(Rules.f_GetLen(), ==, 3);
					DMibAssertTrue(Rules.f_FindEqual("BPR_3"));
					DMibExpect(*Rules["BPR_3"].m_Pattern, ==, "release/*");

					auto Requests = co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests);
					DMibAssert(Requests.f_GetLen(), ==, 2);
					DMibExpectTrue(Requests[0].m_Variables["after"].f_IsNull());
					DMibExpect(Requests[1].m_Variables["after"], ==, CJsonSorted("Y3Vyc29yOnYyOpHOAAAAAg=="));
					DMibExpect(Requests[1].m_Query.f_Find("branchProtectionRules(first: 100, after: $after)"), >=, 0);
					co_return {};
				};

				DMibTestCategory("RemoveOther") -> TCFuture<void>
				{
					TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
					auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
					co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, Pages);

					auto Result = co_await fg_ApplyPolicyRulesTo(Provider, R"-----({"RemoveOther": {"BranchProtection": true}})-----", false);
					DMibExpect(Result.m_Error, ==, "");
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"*", "master", "release/*"}));

					auto Requests = co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests);
					DMibAssert(Requests.f_GetLen(), ==, 5);
					DMibExpect(Requests[4].m_Variables, ==, CJsonSorted::fs_FromString(R"-----({"branchProtectionRuleId": "BPR_3"})-----"));
					co_return {};
				};

				DMibTestCategory("RemoveOtherPretend") -> TCFuture<void>
				{
					TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
					auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
					co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, Pages);

					auto Result = co_await fg_ApplyPolicyRulesTo(Provider, R"-----({"RemoveOther": {"BranchProtection": true}})-----", true);
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"*", "master", "release/*"}));

					auto Requests = co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests);
					DMibExpect(Requests.f_GetLen(), ==, 2);
					co_return {};
				};

				DMibTestCategory("RemoveOtherNullCreator") -> TCFuture<void>
				{
					// GitHub returns no creator for a rule whose creator's account is gone
					auto NullCreatorPages = Pages;
					NullCreatorPages[1]["data"]["repository"]["branchProtectionRules"]["nodes"].f_Array()[0]["creator"] = nullptr;

					TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
					auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
					co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, NullCreatorPages);

					auto Result = co_await fg_ApplyPolicyRulesTo(Provider, R"-----({"RemoveOther": {"BranchProtection": true}})-----", false);
					DMibExpect(Result.m_Error, ==, "");
					DMibExpect(Result.m_Deleted, ==, (TCVector<CStr>{"*", "master", "release/*"}));
					co_return {};
				};

				DMibTestCategory("NoCreator") -> TCFuture<void>
				{
					// GitHub returns a null creator, or none, when the account that created a rule is gone
					auto NoCreatorPages = Pages;
					NoCreatorPages[0]["data"]["repository"]["branchProtectionRules"]["nodes"].f_Array()[1].f_RemoveMember("creator");
					NoCreatorPages[1]["data"]["repository"]["branchProtectionRules"]["nodes"].f_Array()[0]["creator"] = nullptr;

					DMibTestCategory("Read") -> TCFuture<void>
					{
						TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
						auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
						co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, NoCreatorPages);

						auto Rules = co_await Provider(&CGitHub::f_GetBranchProtectionRules, "octo-org/octo-repo");
						DMibAssert(Rules.f_GetLen(), ==, 3);
						DMibExpect(Rules["BPR_1"].m_Creator, ==, TCOptional<CStr>(CStr("octocat")));
						DMibExpectFalse(bool(Rules["BPR_2"].m_Creator));
						DMibExpectFalse(bool(Rules["BPR_3"].m_Creator));
						DMibExpect(*Rules["BPR_3"].m_Pattern, ==, "release/*");
						co_return {};
					};

					DMibTestCategory("Apply") -> TCFuture<void>
					{
						TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
						auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
						co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, NoCreatorPages);

						auto Result = co_await fg_ApplyPolicyRulesTo
							(
								Provider
								, R"-----({"BranchProtection": {"master": {"RequiresLinearHistory": true}, "release/*": {"RequiresLinearHistory": true}}})-----"
								, false
							)
						;
						DMibExpect(Result.m_Error, ==, "");
						DMibExpect(Result.m_Updated, ==, TCVector<CStr>{});
						DMibExpect(Result.m_Created, ==, TCVector<CStr>{});

						auto Requests = co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests);
						DMibExpect(Requests.f_GetLen(), ==, 2);
						co_return {};
					};

					co_return {};
				};

				DMibTestCategory("GoneActors") -> TCFuture<void>
				{
					// GitHub returns no actor, or one without the field that names it, for a user, team or app that is gone
					auto GoneActorPages = Pages;
					auto &Master = GoneActorPages[0]["data"]["repository"]["branchProtectionRules"]["nodes"].f_Array()[1];
					Master["pushAllowances"] = CJsonSorted::fs_FromString
						(
							R"-----(
							{
								"nodes":
								[
									{"actor": null},
									{"actor": {"__typename": "Team", "id": "T_1"}},
									{"actor": {"__typename": "Team", "id": "T_2", "slug": "pushers", "name": "Pushers"}}
								]
							}
							)-----"
						)
					;
					Master["bypassForcePushAllowances"] = CJsonSorted::fs_FromString
						(
							R"-----({"nodes": [{"actor": null}, {"actor": {"__typename": "User", "id": "U_1"}}, {"actor": {"__typename": "User", "id": "U_2", "login": "octocat"}}]})-----"
						)
					;
					Master["bypassPullRequestAllowances"] = CJsonSorted::fs_FromString
						(
							R"-----(
							{
								"nodes":
								[
									{"actor": null},
									{"actor": {"__typename": "App", "id": "A_1"}},
									{"actor": {"__typename": "App", "id": "A_2", "slug": "malterlib-review-bot"}}
								]
							}
							)-----"
						)
					;
					Master["reviewDismissalAllowances"] = CJsonSorted::fs_FromString
						(
							R"-----({"nodes": [null, {"actor": {}}, {"actor": null}, {"actor": {"__typename": "User", "id": "U_2", "login": "octocat", "name": null}}]})-----"
						)
					;

					DMibTestCategory("Read") -> TCFuture<void>
					{
						TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
						auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
						co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, GoneActorPages);

						auto Rules = co_await Provider(&CGitHub::f_GetBranchProtectionRules, "octo-org/octo-repo");
						auto *pMaster = Rules.f_FindEqual("BPR_2");
						DMibAssertTrue(pMaster);

						auto fFormat = [](TCOptional<TCVector<CProvider::CGitActor>> const &_Actors) -> CStr
							{
								if (!_Actors)
									return "unset";

								CStr Actors;
								for (auto &Actor : *_Actors)
									fg_AddStrSep(Actors, CStr("{}"_f << Actor), ", ");

								return Actors;
							}
						;
						DMibExpect(fFormat(pMaster->m_PushAllowances), ==, "Team: T_2 (pushers)");
						DMibExpect(fFormat(pMaster->m_BypassForcePushAllowances), ==, "User: U_2 (octocat)");
						DMibExpect(fFormat(pMaster->m_BypassPullRequestAllowances), ==, "App: A_2 (malterlib-review-bot)");
						DMibExpect(fFormat(pMaster->m_ReviewDismissalAllowances), ==, "User: U_2 (octocat)");
						co_return {};
					};

					DMibTestCategory("Apply") -> TCFuture<void>
					{
						TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
						auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
						co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, GoneActorPages);

						auto Result = co_await fg_ApplyPolicyRulesTo
							(
								Provider
								, R"-----(
								{
									"BranchProtection":
									{
										"master":
										{
											"RequiresLinearHistory": true,
											"PushAllowances": [{"Type": "Team", "Value": "pushers"}],
											"BypassForcePushAllowances": [{"Type": "User", "Value": "octocat"}],
											"BypassPullRequestAllowances": [{"Type": "App", "Value": "malterlib-review-bot"}],
											"ReviewDismissalAllowances": [{"Type": "User", "Value": "octocat"}]
										}
									}
								}
								)-----"
								, false
							)
						;
						DMibExpect(Result.m_Error, ==, "");
						DMibExpect(Result.m_Updated, ==, TCVector<CStr>{});

						auto Requests = co_await Provider(&CGraphQlCaptureGitHub::f_GetRequests);
						DMibExpect(Requests.f_GetLen(), ==, 2);
						co_return {};
					};

					co_return {};
				};

				DMibTestCategory("NullLists") -> TCFuture<void>
				{
					auto NullListPages = Pages;
					auto &Master = NullListPages[0]["data"]["repository"]["branchProtectionRules"]["nodes"].f_Array()[1];
					Master["pushAllowances"] = nullptr;
					Master["reviewDismissalAllowances"]["nodes"] = nullptr;
					Master.f_RemoveMember("bypassForcePushAllowances");

					TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
					auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
					co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, NullListPages);

					auto Rules = co_await Provider(&CGitHub::f_GetBranchProtectionRules, "octo-org/octo-repo");
					auto *pMaster = Rules.f_FindEqual("BPR_2");
					DMibAssertTrue(pMaster);
					DMibExpectTrue(pMaster->m_PushAllowances && pMaster->m_PushAllowances->f_IsEmpty());
					DMibExpectTrue(pMaster->m_ReviewDismissalAllowances && pMaster->m_ReviewDismissalAllowances->f_IsEmpty());
					DMibExpectTrue(pMaster->m_BypassForcePushAllowances && pMaster->m_BypassForcePushAllowances->f_IsEmpty());
					co_return {};
				};

				DMibTestCategory("WrongTypedActors") -> TCFuture<void>
				{
					// Only an absent or null field means an actor is gone. Anything else fails the read rather than drop an actor that may exist.
					struct CCase
					{
						CStr m_Name;
						CStr m_List;
						CStr m_Nodes;
						CStr m_Error;
						bool m_bWholeList = false; // m_Nodes replaces the whole list rather than its nodes
					};

					TCVector<CCase> Cases =
						{
							{"TeamSlug", "pushAllowances", R"-----([{"actor": {"__typename": "Team", "id": "T_1", "slug": 5}}])-----", "'slug' isn't a string"}
							, {"UserLogin", "bypassForcePushAllowances", R"-----([{"actor": {"__typename": "User", "id": "U_1", "login": 7}}])-----", "'login' isn't a string"}
							, {"AppSlug", "bypassPullRequestAllowances", R"-----([{"actor": {"__typename": "App", "id": "A_1", "slug": true}}])-----", "'slug' isn't a string"}
							, {"TypeName", "pushAllowances", R"-----([{"actor": {"__typename": 3, "id": "T_1", "slug": "pushers"}}])-----", "'__typename' isn't a string"}
							, {"Actor", "bypassPullRequestAllowances", R"-----([{"actor": "malterlib-review-bot"}])-----", "actor that isn't an object"}
							, {"Node", "reviewDismissalAllowances", R"-----([5])-----", "node that isn't an object"}
							, {"List", "pushAllowances", R"-----(5)-----", "list that isn't an object", true}
							, {"Nodes", "pushAllowances", R"-----({"nodes": {"actor": null}})-----", "nodes that aren't an array", true}
							, {"UserID", "bypassForcePushAllowances", R"-----([{"actor": {"__typename": "User", "id": 5, "login": "octocat"}}])-----", "'id' isn't a string"}
							, {"TeamName", "pushAllowances", R"-----([{"actor": {"__typename": "Team", "id": "T_1", "slug": "pushers", "name": 7}}])-----", "'name' isn't a string"}
							, {"EmptyLogin", "bypassForcePushAllowances", R"-----([{"actor": {"__typename": "User", "id": "U_1", "login": ""}}])-----", "'login' is empty"}
							, {"EmptySlug", "pushAllowances", R"-----([{"actor": {"__typename": "Team", "id": "T_1", "slug": ""}}])-----", "'slug' is empty"}
							, {"EmptyTypeName", "pushAllowances", R"-----([{"actor": {"__typename": "", "id": "T_1", "slug": "pushers"}}])-----", "'__typename' is empty"}
							, {"UnnamedUserID", "bypassForcePushAllowances", R"-----([{"actor": {"__typename": "User", "login": null, "id": 5}}])-----", "'id' isn't a string"}
							, {"UntypedSlug", "pushAllowances", R"-----([{"actor": {"__typename": null, "slug": 7}}])-----", "'slug' isn't a string"}
						}
					;

					for (auto &Case : Cases)
					{
						DMibTestPath(Case.m_Name);

						auto CasePages = Pages;
						auto &Master = CasePages[0]["data"]["repository"]["branchProtectionRules"]["nodes"].f_Array()[1];
						if (Case.m_bWholeList)
							Master[Case.m_List] = CJsonSorted::fs_FromString(Case.m_Nodes);
						else
							Master[Case.m_List]["nodes"] = CJsonSorted::fs_FromString(Case.m_Nodes);

						TCActor<CGraphQlCaptureGitHub> Provider = fg_Construct();
						auto DestroyProvider = co_await fg_AsyncDestroy(Provider);
						co_await Provider(&CGraphQlCaptureGitHub::f_AddResponses, CasePages);

						auto Rules = co_await Provider(&CGitHub::f_GetBranchProtectionRules, "octo-org/octo-repo").f_Wrap();
						DMibExpectFalse(bool(Rules));
						if (!Rules)
							DMibExpect(Rules.f_GetExceptionStr().f_Find(Case.m_Error), >=, 0);
					}

					co_return {};
				};

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("AppLogin") -> TCFuture<void>
		{
			NContainer::CSecureByteVector PrivateKey;
			NContainer::CSecureByteVector PublicKey;
			NCryptography::CPublicCrypto::fs_GenerateKeys(PrivateKey, PublicKey, NCryptography::CPublicKeySettings_RSA(2048));

			CStr Directory = CFile::fs_GetProgramDirectory() / "GitHubAppLoginTests";
			if (CFile::fs_FileExists(Directory))
				CFile::fs_DeleteDirectoryRecursive(Directory);
			CFile::fs_CreateDirectory(Directory);

			auto Cleanup = g_OnScopeExitCatch / [Directory]
				{
					CFile::fs_DeleteDirectoryRecursive(Directory);
				}
			;

			CStr KeyFile = Directory / "GitHubApp.pem";
			CFile::fs_WriteStringToFile(KeyFile, fg_RsaPrivateKeyPem(PrivateKey), false);

			CStr InvalidKeyFile = Directory / "Invalid.pem";
			CFile::fs_WriteStringToFile(InvalidKeyFile, "Not a key", false);

			TCActor<CGitHub> Provider = fg_Construct();
			auto DestroyProvider = co_await fg_AsyncDestroy(Provider);

			auto fLoginDetails = [](CStr const &_PrivateKey, CStr const &_PrivateKeyFile)
				{
					CEJsonSorted Details;
					auto &App = Details["App"];
					App["AppID"] = "123456";
					App["InstallationID"] = "78901234";
					if (_PrivateKey)
						App["PrivateKey"] = _PrivateKey;
					if (_PrivateKeyFile)
						App["PrivateKeyFile"] = _PrivateKeyFile;

					return Details;
				}
			;

			DMibTestCategory("KeyFile") -> TCFuture<void>
			{
				auto Result = co_await Provider(&CGitHub::f_Login, fLoginDetails({}, KeyFile)).f_Wrap();
				DMibExpectTrue(bool(Result));
				co_return {};
			};

			DMibTestCategory("InvalidKeyFile") -> TCFuture<void>
			{
				auto Result = co_await Provider(&CGitHub::f_Login, fLoginDetails({}, InvalidKeyFile)).f_Wrap();
				DMibAssertFalse(bool(Result));
				DMibExpectTrue(Result.f_GetExceptionStr().f_StartsWith("The GitHub App private key is not a valid unencrypted PEM private key"));
				co_return {};
			};

			DMibTestCategory("MissingKeyFile") -> TCFuture<void>
			{
				CStr MissingKeyFile = Directory / "Missing.pem";
				auto Result = co_await Provider(&CGitHub::f_Login, fLoginDetails({}, MissingKeyFile)).f_Wrap();
				DMibAssertFalse(bool(Result));
				DMibExpectTrue(Result.f_GetExceptionStr().f_StartsWith(CStr("Could not read the GitHub App private key file '{}'"_f << MissingKeyFile)));
				co_return {};
			};

			DMibTestCategory("KeyAndKeyFile") -> TCFuture<void>
			{
				auto Result = co_await Provider(&CGitHub::f_Login, fLoginDetails(fg_RsaPrivateKeyPem(PrivateKey), KeyFile)).f_Wrap();
				DMibAssertFalse(bool(Result));
				DMibExpect(Result.f_GetExceptionStr(), ==, "The GitHub App login has both PrivateKey and PrivateKeyFile");
				co_return {};
			};

			DMibTestCategory("OverlappingCall") -> TCFuture<void>
			{
				TCActor<CKeyGateGitHub> GateProvider = fg_Construct();
				auto DestroyGateProvider = co_await fg_AsyncDestroy(GateProvider);

				CEJsonSorted OldLogin;
				OldLogin["Token"] = "OldToken";
				co_await GateProvider(&CGitHub::f_Login, fg_Move(OldLogin));

				auto NewLogin = fLoginDetails({}, "GitHubApp.pem");
				NewLogin["Token"] = "NewToken";
				NewLogin["TokenExpires"] = NTime::CTime::fs_NowUTC() + NTime::CTimeSpanConvert::fs_CreateSpanFromSeconds(3600.0);
				auto Login = GateProvider(&CGitHub::f_Login, fg_Move(NewLogin)).f_Call();

				co_await GateProvider(&CKeyGateGitHub::f_WaitForKeyRead);

				auto DuringLogin = co_await GateProvider(&CGitHub::f_GetAccessToken);
				DMibExpect(DuringLogin.m_Token, ==, "OldToken");
				DMibExpectFalse(DuringLogin.m_Expires.f_IsValid());

				CStr Pem = fg_RsaPrivateKeyPem(PrivateKey);
				CSecureByteVector PemData;
				PemData.f_Insert((uint8 const *)Pem.f_GetStr(), Pem.f_GetLen());
				co_await GateProvider(&CKeyGateGitHub::f_ReleaseKey, fg_Move(PemData));
				co_await fg_Move(Login);

				auto AfterLogin = co_await GateProvider(&CGitHub::f_GetAccessToken);
				DMibExpect(AfterLogin.m_Token, ==, "NewToken");
				DMibExpectTrue(AfterLogin.m_Expires.f_IsValid());

				co_return {};
			};

			DMibTestCategory("NoKey") -> TCFuture<void>
			{
				auto Result = co_await Provider(&CGitHub::f_Login, fLoginDetails({}, {})).f_Wrap();
				DMibAssertFalse(bool(Result));
				DMibExpect(Result.f_GetExceptionStr(), ==, "The GitHub App login has no PrivateKey or PrivateKeyFile");
				co_return {};
			};

			co_return {};
		};
	}
}

DMibTestRegister(CHostingProviderGitHub_Tests, Malterlib::Git);
