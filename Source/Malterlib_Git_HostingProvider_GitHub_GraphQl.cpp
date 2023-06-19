// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Web/Curl>

namespace NMib::NGit
{
	TCFuture<CJSONSorted> CGitHostingProvider_GitHub::fp_GraphQlApi(CStr _Query, CJSONSorted _Variables)
	{
		CJSONSorted QueryJson;
		QueryJson["query"] = fg_Move(_Query);
		QueryJson["variables"] = fg_Move(_Variables);

		auto Result = co_await mp_CurlActor
			(
				&CCurlActor::f_Request
				, CCurlActor::EMethod_POST
				, "https://api.github.com/graphql"
				, TCMap<CStr, CStr>
				{
					{"User-Agent", "MalterlibGitHostingProvider"}
					, {"Authorization", "Bearer {}"_f << mp_Token}
				}
				, CByteVector::fs_FromString(QueryJson.f_ToString())
				, TCMap<CStr, CStr>{}
			)
		;

		if (Result.m_StatusCode != 200)
		{
			CStr Error = Result.m_Body;

			co_return DMibErrorInstance("GitHub request failed with status {} ({}): {}"_f << Result.m_StatusCode << Result.m_StatusMessage << Result.m_Body);
		}

		auto CaptureScope = co_await g_CaptureExceptions;

		auto ResultJson = CJSONSorted::fs_FromString(Result.m_Body);

		if (auto pErrors = ResultJson.f_GetMember("errors", EJSONType_Array))
		{
			for (auto &Error : fg_Const(pErrors->f_Array()))
			{
				if (auto *pMessage = Error.f_GetMember("message", EJSONType_String))
					co_return DMibErrorInstance("GitHub GraphQL request failed: {}"_f << pMessage->f_String());
				else
					co_return DMibErrorInstance("GitHub GraphQL request failed: {}"_f << Error);
			}
		}

		co_return fg_Move(ResultJson);
	}
}
