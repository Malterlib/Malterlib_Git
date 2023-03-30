// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Web/Curl>

namespace NMib::NGit
{
	TCFuture<CJSON> CGitHostingProvider_GitHub::fp_RestApi(CStr _Path, TCMap<CStr, CStr> _QueryParams, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		CJSON ReturnJson;

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		TCVector<NWeb::NHTTP::CURL::CQueryEntry> QueryEntries;

		if (!_QueryParams.f_FindEqual("per_page"))
			QueryEntries.f_Insert({"per_page", "100"});

		for (auto &Value : _QueryParams)
			QueryEntries.f_Insert({_QueryParams.fs_GetKey(Value), Value});

		Url.f_SetQuery(QueryEntries);

		CStr PageUrl = Url.f_Encode();

		while (PageUrl)
		{
			auto Result = co_await mp_CurlActor
				(
					&CCurlActor::f_Request
					, CCurlActor::EMethod_GET
					, PageUrl
					, TCMap<CStr, CStr>
					{
						{"Accept", "application/vnd.github+json"}
						, {"User-Agent", "MalterlibGitHostingProvider"}
						, {"Authorization", "Bearer {}"_f << mp_Token}
					}
					, CByteVector{}
					, TCMap<CStr, CStr>{}
				)
			;

			PageUrl.f_Clear();

			if (Result.m_StatusCode != _ExpectedStatus)
			{
				CStr Error = Result.m_Body;
				try
				{
					CJSON ErrorJson = CJSON::fs_FromString(Error);
					if (auto pMember = ErrorJson.f_GetMember("message", EJSONType_String))
						Error = pMember->f_String();
				}
				catch (...)
				{
				}

				co_return DMibErrorInstance("GitHub request failed with status {} ({}): {}"_f << Result.m_StatusCode << Result.m_StatusMessage << Error);
			}

			{
				auto CaptureScope = co_await g_CaptureExceptions;

				auto PageJson = CJSON::fs_FromString(Result.m_Body);
				if (!ReturnJson.f_IsValid())
					ReturnJson = fg_Move(PageJson);
				else
				{
					if (ReturnJson.f_IsArray() && PageJson.f_IsArray())
						ReturnJson.f_Array().f_Insert(fg_Move(PageJson.f_Array()));
					else
					{
						co_return DMibErrorInstance
							(
								"Unexpected paged GitHub request where previous or new result wasn't an array. Previous type: {}. New type: {}."_f
								<< fg_EJSONTypeToString((EEJSONType)ReturnJson.f_Type()) // EJSONType is a subset of EEJSONType
								<< fg_EJSONTypeToString((EEJSONType)PageJson.f_Type())
							)
						;
					}
				}

				if (auto *pLink = Result.m_Headers.f_FindEqual("link"))
				{
					CStr Rel;
					aint nParsed = 0;
					ch8 const *pParse = *pLink;
					while (*pParse)
					{
						CStr NextPage;

						pParse = (CStr::CParse("<{}>; rel=\"{}\"") >> NextPage >> Rel).f_ByPointer().f_Parse(pParse, nParsed);

						if (nParsed != 2)
							break;

						if (nParsed == 2 && NextPage.f_StartsWith("https://api.github.com/") && Rel == "next")
						{
							PageUrl = NextPage;
							break;
						}

						if (*pParse == ',')
							++pParse;
						fg_ParseWhiteSpace(pParse);

						if (*pParse != '<')
							break;
					}
				}
			}
		}

		co_return fg_Move(ReturnJson);
	}

	TCFuture<void> CGitHostingProvider_GitHub::fp_RestApiPut(CStr _Path, CJSON _Value, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		CStr PageUrl = Url.f_Encode();

		auto PutData = _Value.f_ToString(nullptr);

		auto Result = co_await mp_CurlActor
			(
				&CCurlActor::f_Request
				, CCurlActor::EMethod_PUT
				, PageUrl
				, TCMap<CStr, CStr>
				{
					{"Accept", "application/vnd.github+json"}
					, {"User-Agent", "MalterlibGitHostingProvider"}
					, {"Authorization", "Bearer {}"_f << mp_Token}
				}
				, CByteVector::fs_FromString(PutData)
				, TCMap<CStr, CStr>{}
			)
		;

		if (Result.m_StatusCode != _ExpectedStatus)
		{
			CStr Error = Result.m_Body;
			try
			{
				CJSON ErrorJson = CJSON::fs_FromString(Error);
				if (auto pMember = ErrorJson.f_GetMember("message", EJSONType_String))
					Error = pMember->f_String();
			}
			catch (...)
			{
			}

			co_return DMibErrorInstance("GitHub put request failed with status {} ({}): {}"_f << Result.m_StatusCode << Result.m_StatusMessage << Error);
		}

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::fp_RestApiDelete(CStr _Path, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		CStr PageUrl = Url.f_Encode();

		auto Result = co_await mp_CurlActor
			(
				&CCurlActor::f_Request
				, CCurlActor::EMethod_DELETE
				, PageUrl
				, TCMap<CStr, CStr>
				{
					{"Accept", "application/vnd.github+json"}
					, {"User-Agent", "MalterlibGitHostingProvider"}
					, {"Authorization", "Bearer {}"_f << mp_Token}
				}
				, CByteVector{}
				, TCMap<CStr, CStr>{}
			)
		;

		if (Result.m_StatusCode != _ExpectedStatus)
		{
			CStr Error = Result.m_Body;
			try
			{
				CJSON ErrorJson = CJSON::fs_FromString(Error);
				if (auto pMember = ErrorJson.f_GetMember("message", EJSONType_String))
					Error = pMember->f_String();
			}
			catch (...)
			{
			}

			co_return DMibErrorInstance("GitHub delete request failed with status {} ({}): {}"_f << Result.m_StatusCode << Result.m_StatusMessage << Error);
		}

		co_return {};
	}
}
