// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Cryptography/PublicCrypto>
#include <Mib/Encoding/Base64>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/Test/Exception>
#include <Mib/Web/HttpClient>

// The provider headers use the namespace shortcuts their library is compiled with, which tests are not
using namespace NMib;
using namespace NMib::NConcurrency;
using namespace NMib::NContainer;
using namespace NMib::NEncoding;
using namespace NMib::NException;
using namespace NMib::NStorage;
using namespace NMib::NStr;
using namespace NMib::NWeb;

#include "../Source/Malterlib_Git_HostingProvider_GitHub.h"

namespace
{
	using CGitHub = NMib::NGit::CGitHostingProvider_GitHub;
	using EPullRequestFeedbackKind = NMib::NGit::CGitHostingProvider::EPullRequestFeedbackKind;

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

	struct CHostingProviderGitHub_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
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
					DMibExpect(Open.m_HeadRef, ==, "fav-165419-update-vulnerable-dependencies");
					DMibExpect(Open.m_HeadSha, ==, "5e2f1c0b9a8d7e6f5a4b3c2d1e0f9a8b7c6d5e4f");
					DMibExpect(Open.m_BaseRef, ==, "master");
					DMibExpect(Open.m_User, ==, "octocat");
					DMibExpect(Open.m_HtmlUrl, ==, "https://github.com/octo-org/octo-repo/pull/2520");
					DMibExpect(Open.m_Labels, ==, (TCSet<CStr>{"dependencies", "security"}));
					DMibExpect(Open.m_CreatedAt, ==, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 9, 15, 0));

					auto &Merged = PullRequests[1];
					DMibExpect(Merged.m_Number, ==, 2518);
					DMibExpect(Merged.m_State, ==, "closed");
					DMibExpect(Merged.m_Body, ==, "");
					DMibExpectFalse(Merged.m_bDraft);
					DMibExpectTrue(Merged.m_bMerged);
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
						auto Feedback = CGitHub::fs_ParsePullRequestFeedback
							(
								Reviews
								, ReviewComments
								, IssueComments
								, NTime::CTimeConvert::fs_CreateTime(2026, 10, 12, 10, 0, 0)
							)
						;
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
					};
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
					// GitHub hands out App private keys as PKCS #1 PEM, which is the DER above in base64 lines between the RSA markers
					CStr Base64 = fg_Base64Encode(PrivateKey);
					CStr Pem = "-----BEGIN RSA PRIVATE KEY-----\n";
					for (umint iStart = 0; iStart < Base64.f_GetLen(); iStart += 64)
						Pem += "{}\n"_f << Base64.f_Slice(iStart, fg_Min(iStart + 64, Base64.f_GetLen()));
					Pem += "-----END RSA PRIVATE KEY-----\n";

					NContainer::CSecureByteVector PemData;
					PemData.f_Insert((uint8 const *)Pem.f_GetStr(), Pem.f_GetLen());

					DMibExpectTrue(NCryptography::CPublicCrypto::fs_ConvertToDer_PrivateKey(PemData) == PrivateKey);
				};
			};
		}
	};
}

DMibTestRegister(CHostingProviderGitHub_Tests, Malterlib::Git);
