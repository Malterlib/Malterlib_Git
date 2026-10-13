// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

namespace NMib::NGit
{
	auto CGitHostingProvider_GitHub::fs_ParsePullRequest(CJsonSorted const &_Json) -> CPullRequest
	{
		CPullRequest PullRequest;

		PullRequest.m_Number = fsp_GetJsonUnsigned(_Json, "number");
		PullRequest.m_Title = fsp_GetJsonString(_Json, "title");
		PullRequest.m_Body = fsp_GetJsonString(_Json, "body");
		PullRequest.m_State = fsp_GetJsonString(_Json, "state");
		PullRequest.m_bDraft = fsp_GetJsonBool(_Json, "draft");

		// Only the single pull request endpoint returns merged
		if (_Json.f_GetMember("merged", EJsonType_Boolean))
			PullRequest.m_bMerged = fsp_GetJsonBool(_Json, "merged");
		else
			PullRequest.m_bMerged = !!_Json.f_GetMember("merged_at", EJsonType_String);

		if (auto *pHead = _Json.f_GetMember("head", EJsonType_Object))
		{
			PullRequest.m_HeadRef = fsp_GetJsonString(*pHead, "ref");
			PullRequest.m_HeadSha = fsp_GetJsonString(*pHead, "sha");
		}

		if (auto *pBase = _Json.f_GetMember("base", EJsonType_Object))
			PullRequest.m_BaseRef = fsp_GetJsonString(*pBase, "ref");

		PullRequest.m_User = fsp_GetJsonUserLogin(_Json);
		PullRequest.m_HtmlUrl = fsp_GetJsonString(_Json, "html_url");

		if (auto *pLabels = _Json.f_GetMember("labels", EJsonType_Array))
		{
			for (auto &LabelJson : pLabels->f_Array())
			{
				if (auto Name = fsp_GetJsonString(LabelJson, "name"))
					PullRequest.m_Labels.f_Insert(Name);
			}
		}

		PullRequest.m_CreatedAt = fsp_GetJsonTime(_Json, "created_at");

		return PullRequest;
	}

	auto CGitHostingProvider_GitHub::fs_ParsePullRequests(CJsonSorted const &_Json) -> TCVector<CPullRequest>
	{
		if (!_Json.f_IsArray())
			DMibError("GitHub pull request list is not an array");

		TCVector<CPullRequest> PullRequests;

		for (auto &PullRequestJson : _Json.f_Array())
			PullRequests.f_Insert(fs_ParsePullRequest(PullRequestJson));

		return PullRequests;
	}

	auto CGitHostingProvider_GitHub::fsp_ParsePullRequestFeedbackItem(CJsonSorted const &_Json, EPullRequestFeedbackKind _Kind) -> CPullRequestFeedback
	{
		CPullRequestFeedback Feedback;

		Feedback.m_Kind = _Kind;
		Feedback.m_ID = fsp_GetJsonUnsigned(_Json, "id");
		Feedback.m_User = fsp_GetJsonUserLogin(_Json);
		Feedback.m_AuthorAssociation = fsp_GetJsonString(_Json, "author_association");
		Feedback.m_Body = fsp_GetJsonString(_Json, "body");
		Feedback.m_HtmlUrl = fsp_GetJsonString(_Json, "html_url");

		switch (_Kind)
		{
		case EPullRequestFeedbackKind::mc_Review:
			Feedback.m_ReviewID = Feedback.m_ID;
			Feedback.m_State = fsp_GetJsonString(_Json, "state");
			Feedback.m_CreatedAt = fsp_GetJsonTime(_Json, "submitted_at");
			Feedback.m_UpdatedAt = Feedback.m_CreatedAt;

			break;
		case EPullRequestFeedbackKind::mc_ReviewComment:
			Feedback.m_ReviewID = fsp_GetJsonUnsigned(_Json, "pull_request_review_id");
			Feedback.m_InReplyToID = fsp_GetJsonUnsigned(_Json, "in_reply_to_id");
			Feedback.m_Path = fsp_GetJsonString(_Json, "path");

			// line is null once the commented code is outdated
			Feedback.m_Line = fsp_GetJsonUnsigned(_Json, "line");
			if (!Feedback.m_Line)
				Feedback.m_Line = fsp_GetJsonUnsigned(_Json, "original_line");

			Feedback.m_CreatedAt = fsp_GetJsonTime(_Json, "created_at");
			Feedback.m_UpdatedAt = fsp_GetJsonTime(_Json, "updated_at");

			break;
		case EPullRequestFeedbackKind::mc_IssueComment:
			Feedback.m_CreatedAt = fsp_GetJsonTime(_Json, "created_at");
			Feedback.m_UpdatedAt = fsp_GetJsonTime(_Json, "updated_at");

			break;
		}

		return Feedback;
	}

	// Reviews have no since parameter, so they are filtered here by submission time. Pending reviews are drafts only their
	// author can see and are left out. An invalid _Since keeps everything.
	auto CGitHostingProvider_GitHub::fs_ParsePullRequestFeedback
		(
			CJsonSorted const &_Reviews
			, CJsonSorted const &_ReviewComments
			, CJsonSorted const &_IssueComments
			, NTime::CTime const &_Since
		)
		-> TCVector<CPullRequestFeedback>
	{
		TCVector<CPullRequestFeedback> AllFeedback;

		auto fAddFeedback = [&](CJsonSorted const &_Items, EPullRequestFeedbackKind _Kind)
			{
				if (!_Items.f_IsArray())
					DMibError("GitHub pull request feedback list is not an array");

				for (auto &ItemJson : _Items.f_Array())
				{
					auto Feedback = fsp_ParsePullRequestFeedbackItem(ItemJson, _Kind);

					if (_Kind == EPullRequestFeedbackKind::mc_Review)
					{
						if (Feedback.m_State == "PENDING")
							continue;

						if (_Since.f_IsValid() && Feedback.m_CreatedAt < _Since)
							continue;
					}

					AllFeedback.f_Insert(fg_Move(Feedback));
				}
			}
		;

		fAddFeedback(_Reviews, EPullRequestFeedbackKind::mc_Review);
		fAddFeedback(_ReviewComments, EPullRequestFeedbackKind::mc_ReviewComment);
		fAddFeedback(_IssueComments, EPullRequestFeedbackKind::mc_IssueComment);

		AllFeedback.f_Sort
			(
				[](CPullRequestFeedback const &_Left, CPullRequestFeedback const &_Right)
				{
					if (auto Result = _Left.m_CreatedAt <=> _Right.m_CreatedAt; Result != 0)
						return Result;

					if (auto Result = _Left.m_Kind <=> _Right.m_Kind; Result != 0)
						return Result;

					return _Left.m_ID <=> _Right.m_ID;
				}
			)
		;

		return AllFeedback;
	}

	auto CGitHostingProvider_GitHub::f_GetPullRequests(CStr _Repository, CPullRequestFilter _Filter) -> TCFuture<TCVector<CPullRequest>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		TCMap<CStr, CStr> QueryParams;

		if (_Filter.m_State)
			QueryParams["state"] = _Filter.m_State;

		if (_Filter.m_Head)
			QueryParams["head"] = _Filter.m_Head;

		if (_Filter.m_Base)
			QueryParams["base"] = _Filter.m_Base;

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/pulls"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
				, "Failed to get pull requests in repository '{}'"_f << _Repository
				, fg_Move(QueryParams)
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParsePullRequests(Data);
	}

	auto CGitHostingProvider_GitHub::f_GetPullRequest(CStr _Repository, uint64 _Number) -> TCFuture<CPullRequest>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/pulls/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get pull request {} in repository '{}'"_f << _Number << _Repository
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParsePullRequest(Data);
	}

	auto CGitHostingProvider_GitHub::f_GetPullRequestFeedback(CStr _Repository, uint64 _Number, NTime::CTime _Since) -> TCFuture<TCVector<CPullRequestFeedback>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		TCMap<CStr, CStr> SinceQueryParams;
		if (_Since.f_IsValid())
			SinceQueryParams["since"] = fs_FormatTime(_Since);

		auto Reviews = co_await fp_RestApi
			(
				"repos/{}/{}/pulls/{}/reviews"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get reviews of pull request {} in repository '{}'"_f << _Number << _Repository
			)
		;

		auto ReviewComments = co_await fp_RestApi
			(
				"repos/{}/{}/pulls/{}/comments"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get review comments of pull request {} in repository '{}'"_f << _Number << _Repository
				, SinceQueryParams
			)
		;

		auto IssueComments = co_await fp_RestApi
			(
				"repos/{}/{}/issues/{}/comments"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Number
				, "Failed to get comments of pull request {} in repository '{}'"_f << _Number << _Repository
				, SinceQueryParams
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParsePullRequestFeedback(Reviews, ReviewComments, IssueComments, _Since);
	}
}
