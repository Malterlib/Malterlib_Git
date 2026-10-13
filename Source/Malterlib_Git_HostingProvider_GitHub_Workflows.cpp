// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

namespace NMib::NGit
{
	auto CGitHostingProvider_GitHub::fsp_ParseWorkflowRun(CJsonSorted const &_Json) -> CWorkflowRun
	{
		CWorkflowRun Run;

		Run.m_ID = fsp_GetJsonUnsigned(_Json, "id");
		Run.m_RunAttempt = fsp_GetJsonUnsigned(_Json, "run_attempt");
		Run.m_Name = fsp_GetJsonString(_Json, "name");
		Run.m_Event = fsp_GetJsonString(_Json, "event");
		Run.m_HeadBranch = fsp_GetJsonString(_Json, "head_branch");
		Run.m_HeadSha = fsp_GetJsonString(_Json, "head_sha");
		Run.m_Status = fsp_GetJsonString(_Json, "status");
		Run.m_Conclusion = fsp_GetJsonString(_Json, "conclusion");
		Run.m_HtmlUrl = fsp_GetJsonString(_Json, "html_url");
		Run.m_CreatedAt = fsp_GetJsonTime(_Json, "created_at");
		Run.m_UpdatedAt = fsp_GetJsonTime(_Json, "updated_at");

		return Run;
	}

	auto CGitHostingProvider_GitHub::fsp_ParseWorkflowJob(CJsonSorted const &_Json) -> CWorkflowJob
	{
		CWorkflowJob Job;

		Job.m_ID = fsp_GetJsonUnsigned(_Json, "id");
		Job.m_RunID = fsp_GetJsonUnsigned(_Json, "run_id");
		Job.m_RunAttempt = fsp_GetJsonUnsigned(_Json, "run_attempt");
		Job.m_Name = fsp_GetJsonString(_Json, "name");
		Job.m_Status = fsp_GetJsonString(_Json, "status");
		Job.m_Conclusion = fsp_GetJsonString(_Json, "conclusion");
		Job.m_HtmlUrl = fsp_GetJsonString(_Json, "html_url");
		Job.m_HeadSha = fsp_GetJsonString(_Json, "head_sha");
		Job.m_StartedAt = fsp_GetJsonTime(_Json, "started_at");
		Job.m_CompletedAt = fsp_GetJsonTime(_Json, "completed_at");

		if (auto *pSteps = _Json.f_GetMember("steps", EJsonType_Array))
		{
			for (auto &StepJson : pSteps->f_Array())
			{
				auto &Step = Job.m_Steps.f_Insert();
				Step.m_Number = fsp_GetJsonUnsigned(StepJson, "number");
				Step.m_Name = fsp_GetJsonString(StepJson, "name");
				Step.m_Status = fsp_GetJsonString(StepJson, "status");
				Step.m_Conclusion = fsp_GetJsonString(StepJson, "conclusion");
			}
		}

		return Job;
	}

	auto CGitHostingProvider_GitHub::fs_ParseWorkflowRuns(CJsonSorted const &_Json) -> TCVector<CWorkflowRun>
	{
		TCVector<CWorkflowRun> Runs;

		for (auto &RunJson : fsp_GetJsonArray(_Json, "workflow_runs"))
			Runs.f_Insert(fsp_ParseWorkflowRun(RunJson));

		return Runs;
	}

	auto CGitHostingProvider_GitHub::fs_ParseWorkflowJobs(CJsonSorted const &_Json) -> TCVector<CWorkflowJob>
	{
		TCVector<CWorkflowJob> Jobs;

		for (auto &JobJson : fsp_GetJsonArray(_Json, "jobs"))
			Jobs.f_Insert(fsp_ParseWorkflowJob(JobJson));

		return Jobs;
	}

	auto CGitHostingProvider_GitHub::f_GetWorkflowRuns(CStr _Repository, CStr _Workflow, CWorkflowRunFilter _Filter) -> TCFuture<TCVector<CWorkflowRun>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		TCMap<CStr, CStr> QueryParams;

		if (_Filter.m_Branch)
			QueryParams["branch"] = _Filter.m_Branch;

		if (_Filter.m_Event)
			QueryParams["event"] = _Filter.m_Event;

		if (_Filter.m_Status)
			QueryParams["status"] = _Filter.m_Status;

		if (_Filter.m_HeadSha)
			QueryParams["head_sha"] = _Filter.m_HeadSha;

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/actions/workflows/{}/runs"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _Workflow
				, "Failed to get runs of workflow '{}' in repository '{}'"_f << _Workflow << _Repository
				, fg_Move(QueryParams)
				, 0
				, 200
				, "workflow_runs"
				, _Filter.m_MaxRuns
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseWorkflowRuns(Data);
	}

	auto CGitHostingProvider_GitHub::f_GetWorkflowRunJobs(CStr _Repository, uint64 _RunID, bool _bAllAttempts) -> TCFuture<TCVector<CWorkflowJob>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/actions/runs/{}/jobs"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _RunID
				, "Failed to get jobs of workflow run {} in repository '{}'"_f << _RunID << _Repository
				, {{"filter", _bAllAttempts ? "all" : "latest"}}
				, 0
				, 200
				, "jobs"
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseWorkflowJobs(Data);
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::f_GetWorkflowJobLog(CStr _Repository, uint64 _JobID)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CHttpClientActor::CRequest Request;
		Request.m_URL = NWeb::NHTTP::CURL("https://api.github.com/repos/{}/{}/actions/jobs/{}/logs"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _JobID).f_Encode();
		Request.m_Method = CHttpClientActor::EMethod_GET;
		Request.m_Headers = co_await fp_GetRestHeaders();
		// The endpoint redirects to blob storage and rejects application/octet-stream with 415
		Request.m_Headers["Accept"] = "application/vnd.github+json";
		Request.m_bFollowRedirects = true;

		// Not streamed, so an error keeps its body, which tells a secondary rate limit from other refusals
		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_SendRequest, fg_Move(Request));
		if (Result.m_StatusCode != 200)
			co_return fp_GetRestError("Failed to get log of workflow job {} in repository '{}'"_f << _JobID << _Repository, Result, {});

		co_return fg_Move(Result.m_Body);
	}
}
