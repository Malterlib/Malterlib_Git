// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Web/HttpClient>

namespace NMib::NGit
{
	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_GraphQlApi(CStr _Query, CJsonSorted _Variables)
	{
		CJsonSorted QueryJson;
		QueryJson["query"] = fg_Move(_Query);
		QueryJson["variables"] = fg_Move(_Variables);

		TCMap<CStr, CStr> Headers = {{"User-Agent", "MalterlibGitHostingProvider"}};

		if (mp_Token)
			Headers["Authorization"] = "Bearer {}"_f << mp_Token;

		auto Result = co_await mp_HttpClientActor
			(
				&CHttpClientActor::f_Post
				, "https://api.github.com/graphql"
				, fg_Move(Headers)
				, fg_Move(QueryJson)
			)
		;

		if (Result.m_StatusCode != 200)
		{
			CStr Error = Result.m_Body;

			co_return DMibErrorInstance("GitHub request failed with status {} ({}): {}"_f << Result.m_StatusCode << Result.m_StatusMessage << Result.m_Body);
		}

		auto CaptureScope = co_await g_CaptureExceptions;

		auto ResultJson = CJsonSorted::fs_FromString(Result.m_Body);

		if (auto pErrors = ResultJson.f_GetMember("errors", EJsonType_Array))
		{
			for (auto &Error : fg_Const(pErrors->f_Array()))
			{
				if (auto *pMessage = Error.f_GetMember("message", EJsonType_String))
					co_return DMibErrorInstance("GitHub GraphQL request failed: {}"_f << pMessage->f_String());
				else
					co_return DMibErrorInstance("GitHub GraphQL request failed: {}"_f << Error);
			}
		}

		co_return fg_Move(ResultJson);
	}
}
