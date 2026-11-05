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
					auto Unedited = CGitHub::fs_ParseCommentEdits
						(
							CJsonSorted::fs_FromString(R"-({"data": {"node": {"author": {"login": "octocat"}, "editor": null, "lastEditedAt": null}}})-")
						)
					;
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
