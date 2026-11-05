// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		// repository_url is https://api.github.com/repos/<owner>/<name>
		CStr fg_RepositoryFromApiUrl(CStr const &_Url)
		{
			CStr Prefix = "https://api.github.com/repos/";
			if (!_Url.f_StartsWith(Prefix))
				return {};

			return _Url.f_Extract(Prefix.f_GetLen());
		}

		TCVector<CStr> fg_ParseLogins(CJsonSorted const &_Json, CStr const &_Member, CStr const &_Field)
		{
			TCVector<CStr> Logins;
			if (auto *pArray = _Json.f_GetMember(_Member, EJsonType_Array))
			{
				for (auto &Entry : pArray->f_Array())
				{
					if (auto *pValue = Entry.f_GetMember(_Field, EJsonType_String); pValue && !pValue->f_String().f_IsEmpty())
						Logins.f_Insert(pValue->f_String());
				}
			}

			return Logins;
		}

		bool fg_IsEnabled(CJsonSorted const &_Json, CStr const &_Member)
		{
			if (auto *pSetting = _Json.f_GetMember(_Member, EJsonType_Object))
			{
				if (auto *pEnabled = pSetting->f_GetMember("enabled", EJsonType_Boolean))
					return pEnabled->f_Boolean();
			}

			return false;
		}
	}

	auto CGitHostingProvider_GitHub::fs_ParsePullRequestReview(CJsonSorted const &_Json) -> CPullRequestReview
	{
		CPullRequestReview Review;

		Review.m_ID = fsp_GetJsonUnsigned(_Json, "id");
		Review.m_NodeID = fsp_GetJsonString(_Json, "node_id");
		Review.m_User = fsp_GetJsonUserLogin(_Json);
		Review.m_AuthorAssociation = fsp_GetJsonString(_Json, "author_association");
		Review.m_State = fsp_GetJsonString(_Json, "state");
		Review.m_Body = fsp_GetJsonString(_Json, "body");
		Review.m_CommitID = fsp_GetJsonString(_Json, "commit_id");
		Review.m_HtmlUrl = fsp_GetJsonString(_Json, "html_url");
		Review.m_SubmittedAt = fsp_GetJsonTime(_Json, "submitted_at");

		return Review;
	}

	auto CGitHostingProvider_GitHub::fs_ParsePullRequestReviews(CJsonSorted const &_Json) -> TCVector<CPullRequestReview>
	{
		if (!_Json.f_IsArray())
			DMibError("GitHub pull request review list is not an array");

		TCVector<CPullRequestReview> Reviews;
		for (auto &ReviewJson : _Json.f_Array())
			Reviews.f_Insert(fs_ParsePullRequestReview(ReviewJson));

		return Reviews;
	}

	auto CGitHostingProvider_GitHub::fs_ParsePullRequestFiles(CJsonSorted const &_Json) -> TCVector<CPullRequestFile>
	{
		if (!_Json.f_IsArray())
			DMibError("GitHub pull request file list is not an array");

		TCVector<CPullRequestFile> Files;
		for (auto &FileJson : _Json.f_Array())
		{
			auto &File = Files.f_Insert();
			File.m_FileName = fsp_GetJsonString(FileJson, "filename");
			File.m_PreviousFileName = fsp_GetJsonString(FileJson, "previous_filename");
			File.m_Status = fsp_GetJsonString(FileJson, "status");
			File.m_Sha = fsp_GetJsonString(FileJson, "sha");
			File.m_Patch = fsp_GetJsonString(FileJson, "patch");
			File.m_nAdditions = uint32(fsp_GetJsonUnsigned(FileJson, "additions"));
			File.m_nDeletions = uint32(fsp_GetJsonUnsigned(FileJson, "deletions"));
		}

		return Files;
	}

	auto CGitHostingProvider_GitHub::fs_ParseIssueSearchResults(CJsonSorted const &_Json) -> TCVector<CIssueSearchResult>
	{
		TCVector<CIssueSearchResult> Results;
		for (auto &ItemJson : fsp_GetJsonArray(_Json, "items"))
		{
			auto &Result = Results.f_Insert();
			Result.m_Repository = fg_RepositoryFromApiUrl(fsp_GetJsonString(ItemJson, "repository_url"));
			Result.m_Number = fsp_GetJsonUnsigned(ItemJson, "number");
			Result.m_bPullRequest = !!ItemJson.f_GetMember("pull_request", EJsonType_Object);
			Result.m_State = fsp_GetJsonString(ItemJson, "state");
			Result.m_User = fsp_GetJsonUserLogin(ItemJson);
			Result.m_CreatedAt = fsp_GetJsonTime(ItemJson, "created_at");
			Result.m_UpdatedAt = fsp_GetJsonTime(ItemJson, "updated_at");
		}

		return Results;
	}

	auto CGitHostingProvider_GitHub::fs_ParseIssueComment(CJsonSorted const &_Json) -> CIssueComment
	{
		CIssueComment Comment;

		Comment.m_ID = fsp_GetJsonUnsigned(_Json, "id");
		Comment.m_NodeID = fsp_GetJsonString(_Json, "node_id");
		Comment.m_User = fsp_GetJsonUserLogin(_Json);
		Comment.m_Body = fsp_GetJsonString(_Json, "body");
		Comment.m_HtmlUrl = fsp_GetJsonString(_Json, "html_url");
		Comment.m_CreatedAt = fsp_GetJsonTime(_Json, "created_at");
		Comment.m_UpdatedAt = fsp_GetJsonTime(_Json, "updated_at");

		return Comment;
	}

	auto CGitHostingProvider_GitHub::fs_ParseIssueComments(CJsonSorted const &_Json) -> TCVector<CIssueComment>
	{
		if (!_Json.f_IsArray())
			DMibError("GitHub issue comment list is not an array");

		TCVector<CIssueComment> Comments;
		for (auto &CommentJson : _Json.f_Array())
			Comments.f_Insert(fs_ParseIssueComment(CommentJson));

		return Comments;
	}

	auto CGitHostingProvider_GitHub::fs_ParseIssueEvents(CJsonSorted const &_Json) -> TCVector<CIssueEvent>
	{
		if (!_Json.f_IsArray())
			DMibError("GitHub issue event list is not an array");

		TCVector<CIssueEvent> Events;
		for (auto &EventJson : _Json.f_Array())
		{
			auto &Event = Events.f_Insert();
			Event.m_ID = fsp_GetJsonUnsigned(EventJson, "id");
			Event.m_Event = fsp_GetJsonString(EventJson, "event");
			if (auto *pActor = EventJson.f_GetMember("actor", EJsonType_Object))
				Event.m_Actor = fsp_GetJsonString(*pActor, "login");
			Event.m_CreatedAt = fsp_GetJsonTime(EventJson, "created_at");
		}

		return Events;
	}

	/// A pending invitation is a membership that isn't active yet
	auto CGitHostingProvider_GitHub::fs_ParseTeamMembership(CJsonSorted const &_Json) -> ETeamMembershipState
	{
		if (!_Json.f_IsObject())
			return ETeamMembershipState::mc_None;

		CStr State = fsp_GetJsonString(_Json, "state");
		if (State == "active")
			return ETeamMembershipState::mc_Active;

		if (State == "pending")
			return ETeamMembershipState::mc_Pending;

		return ETeamMembershipState::mc_None;
	}

	/// The branch protection rule of a branch. Restrictions are only present when the rule restricts who can push.
	void CGitHostingProvider_GitHub::fs_ParseBranchProtection(CJsonSorted const &_Json, CBranchRequirements &o_Requirements)
	{
		// Without branch protection GitHub allows force pushes, unless a ruleset forbids them
		if (!_Json.f_IsObject())
		{
			o_Requirements.m_bAllowsForcePushes = true;
			return;
		}

		o_Requirements.m_bProtected = true;

		if (auto *pRestrictions = _Json.f_GetMember("restrictions", EJsonType_Object))
		{
			o_Requirements.m_bPushRestricted = true;
			o_Requirements.m_PushUsers.f_Insert(fg_ParseLogins(*pRestrictions, "users", "login"));
			o_Requirements.m_PushTeams.f_Insert(fg_ParseLogins(*pRestrictions, "teams", "slug"));
			o_Requirements.m_PushApps.f_Insert(fg_ParseLogins(*pRestrictions, "apps", "slug"));
		}

		if (fg_IsEnabled(_Json, "required_linear_history"))
			o_Requirements.m_bRequiresLinearHistory = true;

		if (fg_IsEnabled(_Json, "allow_force_pushes"))
			o_Requirements.m_bAllowsForcePushes = true;
	}

	/// The rules of every ruleset that applies to a branch, flattened by GitHub. A ruleset's linear history requirement adds to the
	/// branch protection's, and its non_fast_forward rule forbids the force pushes the branch protection may allow. Its update rule lets
	/// only its bypass actors push, which the flattened rules don't name.
	void CGitHostingProvider_GitHub::fs_ParseBranchRules(CJsonSorted const &_Json, CBranchRequirements &o_Requirements)
	{
		if (!_Json.f_IsArray())
			return;

		for (auto &RuleJson : _Json.f_Array())
		{
			CStr Type = fsp_GetJsonString(RuleJson, "type");
			if (Type == "required_linear_history")
				o_Requirements.m_bRequiresLinearHistory = true;
			else if (Type == "non_fast_forward")
				o_Requirements.m_bAllowsForcePushes = false;
			else if (Type == "update")
				o_Requirements.m_bUpdatesRestrictedByRuleset = true;
		}
	}

	auto CGitHostingProvider_GitHub::fs_ParseCombinedStatus(CJsonSorted const &_Json) -> CCombinedStatus
	{
		CCombinedStatus Combined;
		Combined.m_State = fsp_GetJsonString(_Json, "state");
		Combined.m_Sha = fsp_GetJsonString(_Json, "sha");

		if (auto *pStatuses = _Json.f_GetMember("statuses", EJsonType_Array))
		{
			for (auto &StatusJson : pStatuses->f_Array())
			{
				auto &Status = Combined.m_Statuses.f_Insert();
				Status.m_State = fsp_GetJsonString(StatusJson, "state");
				Status.m_Context = fsp_GetJsonString(StatusJson, "context");
				Status.m_Description = fsp_GetJsonString(StatusJson, "description");
				Status.m_TargetUrl = fsp_GetJsonString(StatusJson, "target_url");
				if (auto *pCreator = StatusJson.f_GetMember("creator", EJsonType_Object))
					Status.m_Creator = fsp_GetJsonString(*pCreator, "login");
				Status.m_CreatedAt = fsp_GetJsonTime(StatusJson, "created_at");
				Status.m_UpdatedAt = fsp_GetJsonTime(StatusJson, "updated_at");
			}
		}

		return Combined;
	}

	auto CGitHostingProvider_GitHub::fs_ParseCheckRuns(CJsonSorted const &_Json) -> TCVector<CCheckRun>
	{
		TCVector<CCheckRun> CheckRuns;
		for (auto &RunJson : fsp_GetJsonArray(_Json, "check_runs"))
		{
			auto &Run = CheckRuns.f_Insert();
			Run.m_ID = fsp_GetJsonUnsigned(RunJson, "id");
			Run.m_Name = fsp_GetJsonString(RunJson, "name");
			Run.m_HeadSha = fsp_GetJsonString(RunJson, "head_sha");
			Run.m_Status = fsp_GetJsonString(RunJson, "status");
			Run.m_Conclusion = fsp_GetJsonString(RunJson, "conclusion");
			Run.m_HtmlUrl = fsp_GetJsonString(RunJson, "html_url");
			Run.m_DetailsUrl = fsp_GetJsonString(RunJson, "details_url");
			if (auto *pApp = RunJson.f_GetMember("app", EJsonType_Object))
				Run.m_AppSlug = fsp_GetJsonString(*pApp, "slug");
			Run.m_StartedAt = fsp_GetJsonTime(RunJson, "started_at");
			Run.m_CompletedAt = fsp_GetJsonTime(RunJson, "completed_at");
		}

		return CheckRuns;
	}

	CJsonSorted CGitHostingProvider_GitHub::fs_CreatePullRequestReviewJson(CCreatePullRequestReview const &_Review)
	{
		CJsonSorted Json =
			{
				"commit_id"_j= _Review.m_CommitID
				, "event"_j= _Review.m_Event
				, "body"_j= _Review.m_Body
			}
		;

		if (!_Review.m_Comments.f_IsEmpty())
		{
			auto &Comments = Json["comments"];
			Comments = CJsonSorted(EJsonType_Array);
			for (auto &Comment : _Review.m_Comments)
			{
				CJsonSorted CommentJson =
					{
						"path"_j= Comment.m_Path
						, "line"_j= int64(Comment.m_Line)
						, "side"_j= Comment.m_Side
						, "body"_j= Comment.m_Body
					}
				;

				if (Comment.m_StartLine && Comment.m_StartLine < Comment.m_Line)
				{
					CommentJson["start_line"] = int64(Comment.m_StartLine);
					CommentJson["start_side"] = Comment.m_Side;
				}

				Comments.f_Insert(fg_Move(CommentJson));
			}
		}

		return Json;
	}

	auto CGitHostingProvider_GitHub::f_GetPullRequestReviews(CStr _Repository, uint64 _Number) -> TCFuture<TCVector<CPullRequestReview>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/pulls/{}/reviews"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get reviews of pull request {} in repository '{}'"_f << _Number << _Repository
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParsePullRequestReviews(Data);
	}

	auto CGitHostingProvider_GitHub::f_CreatePullRequestReview(CStr _Repository, uint64 _Number, CCreatePullRequestReview _Review) -> TCFuture<CPullRequestReview>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApiPost
			(
				"repos/{}/{}/pulls/{}/reviews"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, fs_CreatePullRequestReviewJson(_Review)
				, "Failed to create a review of pull request {} in repository '{}'"_f << _Number << _Repository
				, {}
				, 200
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParsePullRequestReview(Data);
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DismissPullRequestReview(CStr _Repository, uint64 _Number, uint64 _ReviewID, CStr _Message)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		co_await fp_RestApiPut
			(
				"repos/{}/{}/pulls/{}/reviews/{}/dismissals"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number << _ReviewID
				, CJsonSorted{"message"_j= _Message, "event"_j= "DISMISS"}
				, "Failed to dismiss review {} of pull request {} in repository '{}'"_f << _ReviewID << _Number << _Repository
				, {}
				, 200
			)
		;

		co_return {};
	}

	/// The pullRequest.reviewThreads object of a GraphQL response: its nodes, each with its comments
	auto CGitHostingProvider_GitHub::fs_ParseReviewThreads(CJsonSorted const &_ReviewThreads) -> TCVector<CPullRequestReviewThread>
	{
		TCVector<CPullRequestReviewThread> Threads;
		auto *pNodes = _ReviewThreads.f_GetMember("nodes", EJsonType_Array);
		if (!pNodes)
			return Threads;

		for (auto &Node : pNodes->f_Array())
		{
			CPullRequestReviewThread Thread;
			if (auto *pID = Node.f_GetMember("id", EJsonType_String))
				Thread.m_ID = pID->f_String();
			if (auto *pResolved = Node.f_GetMember("isResolved", EJsonType_Boolean))
				Thread.m_bResolved = pResolved->f_Boolean();
			if (auto *pPath = Node.f_GetMember("path", EJsonType_String))
				Thread.m_Path = pPath->f_String();
			if (auto *pLine = Node.f_GetMember("line", EJsonType_Integer); pLine && pLine->f_Integer() > 0)
				Thread.m_Line = uint32(pLine->f_Integer());

			if (auto *pComments = Node.f_GetMember("comments", EJsonType_Object))
			{
				if (auto *pCommentNodes = pComments->f_GetMember("nodes", EJsonType_Array))
				{
					for (auto &CommentNode : pCommentNodes->f_Array())
					{
						CPullRequestReviewThreadComment Comment;
						if (auto *pDatabaseID = CommentNode.f_GetMember("databaseId", EJsonType_Integer); pDatabaseID && pDatabaseID->f_Integer() > 0)
							Comment.m_ID = uint64(pDatabaseID->f_Integer());
						if (auto *pBody = CommentNode.f_GetMember("body", EJsonType_String))
							Comment.m_Body = pBody->f_String();
						if (auto *pAuthor = CommentNode.f_GetMember("author", EJsonType_Object))
						{
							if (auto *pLogin = pAuthor->f_GetMember("login", EJsonType_String))
								Comment.m_Author = pLogin->f_String();
							if (auto *pType = pAuthor->f_GetMember("__typename", EJsonType_String))
								Comment.m_bAuthorIsBot = pType->f_String() == "Bot";
						}
						Thread.m_Comments.f_Insert(fg_Move(Comment));
					}

					if (!Thread.m_Comments.f_IsEmpty())
					{
						auto &First = Thread.m_Comments[0];
						Thread.m_FirstCommentID = First.m_ID;
						Thread.m_FirstCommentAuthor = First.m_Author;
						Thread.m_bFirstCommentAuthorIsBot = First.m_bAuthorIsBot;
						Thread.m_FirstCommentBody = First.m_Body;
					}
				}
			}

			if (!Thread.m_ID.f_IsEmpty())
				Threads.f_Insert(fg_Move(Thread));
		}

		return Threads;
	}

	auto CGitHostingProvider_GitHub::f_GetPullRequestReviewThreads(CStr _Repository, uint64 _Number) -> TCFuture<TCVector<CPullRequestReviewThread>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		constexpr ch8 const *c_pQuery =
			"query($Owner: String!, $Name: String!, $Number: Int!, $After: String) {"
			" repository(owner: $Owner, name: $Name) { pullRequest(number: $Number) {"
			" reviewThreads(first: 100, after: $After) { pageInfo { hasNextPage endCursor }"
			" nodes { id isResolved path line comments(first: 100) { nodes { databaseId body author { login __typename } } } } } } } }"
		;

		TCVector<CPullRequestReviewThread> Threads;
		CStr After;
		while (true)
		{
			CJsonSorted Variables;
			Variables["Owner"] = RepositorySlug.m_Owner;
			Variables["Name"] = RepositorySlug.m_Name;
			Variables["Number"] = int64(_Number);
			if (After.f_IsEmpty())
				Variables["After"] = nullptr;
			else
				Variables["After"] = After;

			auto Result = co_await fp_GraphQlApi(c_pQuery, fg_Move(Variables));

			auto *pReviewThreads = Result.f_GetMember("data", EJsonType_Object);
			for (ch8 const *pName : {"repository", "pullRequest", "reviewThreads"})
			{
				if (pReviewThreads)
					pReviewThreads = pReviewThreads->f_GetMember(pName, EJsonType_Object);
			}

			if (!pReviewThreads)
				co_return DMibErrorInstance("The review threads of pull request {} in repository '{}' are missing from the response"_f << _Number << _Repository);

			Threads.f_Insert(fs_ParseReviewThreads(*pReviewThreads));

			auto *pPageInfo = pReviewThreads->f_GetMember("pageInfo", EJsonType_Object);
			auto *pHasNextPage = pPageInfo ? pPageInfo->f_GetMember("hasNextPage", EJsonType_Boolean) : nullptr;
			auto *pEndCursor = pPageInfo ? pPageInfo->f_GetMember("endCursor", EJsonType_String) : nullptr;
			if (!pHasNextPage || !pHasNextPage->f_Boolean() || !pEndCursor)
				break;

			After = pEndCursor->f_String();
		}

		co_return fg_Move(Threads);
	}

	/// From the GraphQL node of the comment, review or review comment: the REST API has no editor
	auto CGitHostingProvider_GitHub::f_GetCommentEdits(CStr _NodeID) -> TCFuture<CCommentEdits>
	{
		CJsonSorted Variables;
		Variables["ID"] = _NodeID;
		auto Result = co_await fp_GraphQlApi("query($ID: ID!) { node(id: $ID) { ... on Comment { author { login } editor { login } lastEditedAt } } }", fg_Move(Variables));

		auto Edits = fs_ParseCommentEdits(Result);
		if (!Edits)
			co_return DMibErrorInstance("The comment {} is missing from the response"_f << _NodeID);

		co_return fg_Move(*Edits);
	}

	/// The comment's edits from the GraphQL response; empty when the response has no node
	auto CGitHostingProvider_GitHub::fs_ParseCommentEdits(CJsonSorted const &_Response) -> NStorage::TCOptional<CCommentEdits>
	{
		auto *pNode = _Response.f_GetMember("data", EJsonType_Object);
		if (pNode)
			pNode = pNode->f_GetMember("node", EJsonType_Object);
		if (!pNode)
			return {};

		auto fLogin = [&](ch8 const *_pName) -> CStr
			{
				auto *pActor = pNode->f_GetMember(_pName, EJsonType_Object);
				auto *pLogin = pActor ? pActor->f_GetMember("login", EJsonType_String) : nullptr;
				return pLogin ? pLogin->f_String() : CStr();
			}
		;

		CCommentEdits Edits;
		Edits.m_Author = fLogin("author");
		Edits.m_Editor = fLogin("editor");
		Edits.m_LastEditedAt = fsp_GetJsonTime(*pNode, "lastEditedAt");
		return Edits;
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_ResolveReviewThread(CStr _ThreadID)
	{
		CJsonSorted Variables;
		Variables["ThreadID"] = _ThreadID;
		co_await fp_GraphQlApi("mutation($ThreadID: ID!) { resolveReviewThread(input: {threadId: $ThreadID}) { thread { id isResolved } } }", fg_Move(Variables));
		co_return {};
	}

	/// A reply in the thread of an inline review comment; returns the reply's id
	TCFuture<uint64> CGitHostingProvider_GitHub::f_ReplyToReviewComment(CStr _Repository, uint64 _Number, uint64 _CommentID, CStr _Body)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Result = co_await fp_RestApiPost
			(
				"repos/{}/{}/pulls/{}/comments/{}/replies"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number << _CommentID
				, CJsonSorted{"body"_j= _Body}
				, "Failed to reply to review comment {} of pull request {} in repository '{}'"_f << _CommentID << _Number << _Repository
				, {}
				, 201
			)
		;

		if (auto *pID = Result.f_GetMember("id", EJsonType_Integer))
			co_return uint64(pID->f_Integer());

		co_return 0;
	}

	auto CGitHostingProvider_GitHub::f_GetPullRequestFiles(CStr _Repository, uint64 _Number) -> TCFuture<TCVector<CPullRequestFile>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/pulls/{}/files"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get the files of pull request {} in repository '{}'"_f << _Number << _Repository
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParsePullRequestFiles(Data);
	}

	auto CGitHostingProvider_GitHub::f_SearchIssues(CIssueSearchFilter _Filter) -> TCFuture<TCVector<CIssueSearchResult>>
	{
		auto Data = co_await fp_RestApi
			(
				"search/issues"
				, "Failed to search issues for '{}'"_f << _Filter.m_Query
				, TCMap<CStr, CStr>{{"q", _Filter.m_Query}, {"sort", "updated"}, {"order", "desc"}}
				, 0
				, 200
				, "items"
				, _Filter.m_MaxItems
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseIssueSearchResults(Data);
	}

	auto CGitHostingProvider_GitHub::f_GetIssueComments(CStr _Repository, uint64 _Number, NTime::CTime _Since) -> TCFuture<TCVector<CIssueComment>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		TCMap<CStr, CStr> QueryParams;
		if (_Since.f_IsValid())
			QueryParams["since"] = fs_FormatTime(_Since);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/issues/{}/comments"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get comments of issue {} in repository '{}'"_f << _Number << _Repository
				, fg_Move(QueryParams)
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseIssueComments(Data);
	}

	auto CGitHostingProvider_GitHub::f_CreateIssueComment(CStr _Repository, uint64 _Number, CStr _Body) -> TCFuture<CIssueComment>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApiPost
			(
				"repos/{}/{}/issues/{}/comments"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, CJsonSorted{"body"_j= _Body}
				, "Failed to comment on issue {} in repository '{}'"_f << _Number << _Repository
				, {}
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseIssueComment(Data);
	}

	auto CGitHostingProvider_GitHub::f_UpdateIssueComment(CStr _Repository, uint64 _CommentID, CStr _Body) -> TCFuture<CIssueComment>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApiPatch
			(
				"repos/{}/{}/issues/comments/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _CommentID
				, CJsonSorted{"body"_j= _Body}
				, "Failed to update comment {} in repository '{}'"_f << _CommentID << _Repository
				, {}
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseIssueComment(Data);
	}

	auto CGitHostingProvider_GitHub::f_GetIssueEvents(CStr _Repository, uint64 _Number) -> TCFuture<TCVector<CIssueEvent>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/issues/{}/events"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get the events of issue {} in repository '{}'"_f << _Number << _Repository
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseIssueEvents(Data);
	}

	CStr CGitHostingProvider_GitHub::fs_ReactionPath(CStr const &_Owner, CStr const &_Name, EReactionSubject _Subject, uint64 _ID)
	{
		switch (_Subject)
		{
		case EReactionSubject::mc_IssueComment: return "repos/{}/{}/issues/comments/{}/reactions"_f << _Owner << _Name << _ID;
		case EReactionSubject::mc_ReviewComment: return "repos/{}/{}/pulls/comments/{}/reactions"_f << _Owner << _Name << _ID;
		case EReactionSubject::mc_Issue: return "repos/{}/{}/issues/{}/reactions"_f << _Owner << _Name << _ID;
		}

		return {};
	}

	/// Adds a reaction such as "eyes" or "+1". GitHub answers 200 instead of 201 when the reaction already exists, which is a success too.
	TCFuture<void> CGitHostingProvider_GitHub::f_CreateReaction(CStr _Repository, EReactionSubject _Subject, uint64 _ID, CStr _Content)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << fs_ReactionPath(RepositorySlug.m_Owner, RepositorySlug.m_Name, _Subject, _ID));
		auto Headers = co_await fp_GetRestHeaders();

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_Post, Url.f_Encode(), fg_Move(Headers), CJsonSorted{"content"_j= _Content});

		if (Result.m_StatusCode != 201 && Result.m_StatusCode != 200)
			co_return fp_GetRestError("Failed to add a reaction in repository '{}': GitHub post request"_f << _Repository, Result, {});

		co_return {};
	}

	auto CGitHostingProvider_GitHub::f_GetTeamMembership(CStr _Organization, CStr _TeamSlug, CStr _User) -> TCFuture<ETeamMembershipState>
	{
		auto Data = co_await fp_RestApi
			(
				"orgs/{}/teams/{}/memberships/{}"_f << _Organization << _TeamSlug << _User
				, "Failed to get the membership of '{}' in team '{}/{}'"_f << _User << _Organization << _TeamSlug
				, {}
				, 404
			)
		;

		co_return fs_ParseTeamMembership(Data);
	}

	/// The resources of GET rate_limit, which doesn't count against any limit
	auto CGitHostingProvider_GitHub::fs_ParseApiRateLimits(CJsonSorted const &_Json) -> TCVector<CApiRateLimitStatus>
	{
		TCVector<CApiRateLimitStatus> Limits;
		auto *pResources = _Json.f_GetMember("resources", EJsonType_Object);
		if (!pResources)
			return Limits;

		for (auto &Resource : pResources->f_Object())
		{
			auto &Value = Resource.f_Value();
			if (!Value.f_IsObject())
				continue;

			CApiRateLimitStatus Status;
			Status.m_Resource = Resource.f_Name();
			if (auto *pLimit = Value.f_GetMember("limit", EJsonType_Integer))
				Status.m_Limit = uint64(pLimit->f_Integer());
			if (auto *pRemaining = Value.f_GetMember("remaining", EJsonType_Integer))
				Status.m_Remaining = uint64(pRemaining->f_Integer());
			if (auto *pReset = Value.f_GetMember("reset", EJsonType_Integer))
				Status.m_ResetTime = NTime::CTimeConvert::fs_FromUnixSeconds(pReset->f_Integer());
			Limits.f_Insert(fg_Move(Status));
		}

		return Limits;
	}

	auto CGitHostingProvider_GitHub::f_GetApiRateLimits() -> TCFuture<TCVector<CApiRateLimitStatus>>
	{
		auto Data = co_await fp_RestApi("rate_limit", "Failed to get the API rate limits", {});
		co_return fs_ParseApiRateLimits(Data);
	}

	/// What GitHub enforces on pushes to _Branch now, from its branch protection (404 when it has none) and the rulesets that apply to it
	auto CGitHostingProvider_GitHub::f_GetBranchRequirements(CStr _Repository, CStr _Branch) -> TCFuture<CBranchRequirements>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		// A branch name can hold characters such as # and ? that would end the path
		CStr BranchEncoded = NWeb::NHTTP::CURL::fs_PercentEncode(_Branch);

		auto Protection = co_await fp_RestApi
			(
				"repos/{}/{}/branches/{}/protection"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << BranchEncoded
				, "Failed to get the protection of branch '{}' in repository '{}'"_f << _Branch << _Repository
				, {}
				, 404
			)
		;

		auto Rules = co_await fp_RestApi
			(
				"repos/{}/{}/rules/branches/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << BranchEncoded
				, "Failed to get the rules of branch '{}' in repository '{}'"_f << _Branch << _Repository
			)
		;

		CBranchRequirements Requirements;
		fs_ParseBranchProtection(Protection, Requirements);
		fs_ParseBranchRules(Rules, Requirements);

		co_return fg_Move(Requirements);
	}

	CJsonSorted CGitHostingProvider_GitHub::fs_CommitStatusJson(CCommitStatus const &_Status)
	{
		CJsonSorted Json =
			{
				"state"_j= _Status.m_State
				, "context"_j= _Status.m_Context
			}
		;

		// GitHub rejects descriptions over 140 characters, so they are counted, and cut, as code points: a cut inside one would send
		// malformed UTF-8
		if (!_Status.m_Description.f_IsEmpty())
		{
			CUStr Description = _Status.m_Description;
			Json["description"] = Description.f_GetLen() > 140 ? CStr(CStr(Description.f_Left(137)) + "...") : _Status.m_Description;
		}

		if (!_Status.m_TargetUrl.f_IsEmpty())
			Json["target_url"] = _Status.m_TargetUrl;

		return Json;
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_SetCommitStatus(CStr _Repository, CStr _Sha, CCommitStatus _Status)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		co_await fp_RestApiPost
			(
				"repos/{}/{}/statuses/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Sha
				, fs_CommitStatusJson(_Status)
				, "Failed to set the status of commit {} in repository '{}'"_f << _Sha << _Repository
				, {}
			)
		;

		co_return {};
	}

	auto CGitHostingProvider_GitHub::f_GetCombinedStatus(CStr _Repository, CStr _Ref) -> TCFuture<CCombinedStatus>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/commits/{}/status"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << NWeb::NHTTP::CURL::fs_PercentEncode(_Ref)
				, "Failed to get the combined status of '{}' in repository '{}'"_f << _Ref << _Repository
				, {}
				, 0
				, 200
				, "statuses"
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseCombinedStatus(Data);
	}

	auto CGitHostingProvider_GitHub::f_GetCheckRuns(CStr _Repository, CStr _Ref) -> TCFuture<TCVector<CCheckRun>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/commits/{}/check-runs"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << NWeb::NHTTP::CURL::fs_PercentEncode(_Ref)
				, "Failed to get the check runs of '{}' in repository '{}'"_f << _Ref << _Repository
				, {}
				, 0
				, 200
				, "check_runs"
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseCheckRuns(Data);
	}

	/// The REST path of _Path at _Ref. Both can hold characters such as #, ? and & that would end the path or the query value, and
	/// the slashes of _Path separate its directories.
	CStr CGitHostingProvider_GitHub::fs_FileContentsPath(CStr const &_Owner, CStr const &_Name, CStr const &_Path, CStr const &_Ref)
	{
		return "repos/{}/{}/contents/{}?ref={}"_f << _Owner << _Name << NWeb::NHTTP::CURL::fs_PercentEncode(_Path, ":?#[]@!$&'()*+,;=%") << NWeb::NHTTP::CURL::fs_PercentEncode(_Ref);
	}

	/// The raw contents of _Path at _Ref, a branch, tag or commit
	auto CGitHostingProvider_GitHub::f_GetFileContents(CStr _Repository, CStr _Path, CStr _Ref) -> TCFuture<CByteVector>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		TCSharedPointer<CByteVector> pContents = fg_Construct();
		co_await fp_RestApiDownloadFile
			(
				fs_FileContentsPath(RepositorySlug.m_Owner, RepositorySlug.m_Name, _Path, _Ref)
				, g_ActorFunctor / [pContents](CByteVector _Data) -> TCFuture<void>
				{
					pContents->f_Insert(fg_Move(_Data));
					co_return {};
				}
				, "Failed to get '{}' at '{}' in repository '{}'"_f << _Path << _Ref << _Repository
				, 200
				, "application/vnd.github.raw"
			)
		;

		co_return fg_Move(*pContents);
	}
}
