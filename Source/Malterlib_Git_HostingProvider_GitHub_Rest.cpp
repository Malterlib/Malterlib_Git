// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Web/HttpClient>

namespace NMib::NGit
{
	TCMap<CStr, CStr> CGitHostingProvider_GitHub::fsp_GetPublicRestHeaders()
	{
		return
			{
				{"Accept", "application/vnd.github+json"}
				, {"User-Agent", "MalterlibGitHostingProvider"}
				, {"X-GitHub-Api-Version", "2022-11-28"}
			}
		;
	}

	TCFuture<TCMap<CStr, CStr>> CGitHostingProvider_GitHub::fp_GetRestHeaders()
	{
		co_await fp_EnsureAccessToken();

		TCMap<CStr, CStr> Headers = fsp_GetPublicRestHeaders();
		if (mp_Token)
			Headers["Authorization"] = "Bearer {}"_f << mp_Token;

		co_return fg_Move(Headers);
	}

	CStr const *CGitHostingProvider_GitHub::CFieldTranslations::f_GetMalterlibField(CStr const &_GitHubField) const
	{
		auto iField = fg_BinarySearch(m_pFields, m_nFields, _GitHubField);
		if (iField >= 0)
			return &m_pFields[iField].m_MalterlibField;

		return nullptr;
	}

	CExceptionPointer CGitHostingProvider_GitHub::fp_GetRestError(CStr const &_Description, CHttpClientActor::CResult const &_Result, CFieldTranslations const &_FieldTranslation)
	{
		CStr Error = _Result.m_Body;

		CGitHostingProviderExceptionData ExceptionData;
		static_cast<CHttpClientRequestExceptionData &>(ExceptionData) = CHttpClientRequestExceptionData::fs_FromResult(_Result);
		ExceptionData.m_GitRawError = _Result.m_Body;

		try
		{
			CJsonSorted ErrorJson = CJsonSorted::fs_FromString(Error);
			if (auto pMember = ErrorJson.f_GetMember("message", EJsonType_String))
				Error = pMember->f_String();

			if (auto pErrors = ErrorJson.f_GetMember("errors", EJsonType_Array))
			{
				for (auto &ErrorJson : pErrors->f_Array())
				{
					if (ErrorJson.f_IsString())
					{
						fg_AddStrSep(Error, ErrorJson.f_String(), ". ");
						continue;
					}

					auto &GitError = ExceptionData.m_GitErrors.f_Insert();

					if (auto Resource = ErrorJson.f_GetMemberValue("resource", CStr()).f_String())
					{
						fg_AddStrSep(Error, "Resource: {}"_f << Resource, ". ");
						GitError.m_Resource = Resource;
					}

					if (auto Field = ErrorJson.f_GetMemberValue("field", CStr()).f_String())
					{
						fg_AddStrSep(Error, "Field: {}"_f << Field, ". ");
						if (auto *pField = _FieldTranslation.f_GetMalterlibField(Field))
							GitError.m_Field = *pField;
						else
							GitError.m_Field = Field;
					}

					if (auto Code = ErrorJson.f_GetMemberValue("code", CStr()).f_String())
					{
						fg_AddStrSep(Error, "Code: {}"_f << Code, ". ");
						if (Code == "missing")
							GitError.m_Code = EGitHostingProviderErrorCode::mc_ResourceDoesNotExist;
						else if (Code == "already_exists")
							GitError.m_Code = EGitHostingProviderErrorCode::mc_ResourceAlreadyExists;
						else if (Code == "missing_field")
							GitError.m_Code = EGitHostingProviderErrorCode::mc_MissingRequiredParameter;
						else if (Code == "invalid")
							GitError.m_Code = EGitHostingProviderErrorCode::mc_InvalidParameter;
						else if (Code == "unprocessable")
							GitError.m_Code = EGitHostingProviderErrorCode::mc_ParametersInvalid;
						else if (Code == "custom")
							GitError.m_Code = EGitHostingProviderErrorCode::mc_Custom;
					}

					if (auto Message = ErrorJson.f_GetMemberValue("message", CStr()).f_String())
					{
						fg_AddStrSep(Error, "Message: {}"_f << Message, ". ");
						GitError.m_Message = Message;
					}
				}
			}
		}
		catch (...)
		{
		}

		if (auto RateLimit = fs_GetRateLimit(_Result.m_StatusCode, _Result.m_Headers, _Result.m_Body, NTime::CTime::fs_NowUTC()))
		{
			if (RateLimit->m_ResetTime.f_IsValid())
				fg_AddStrSep(Error, "Rate limited until {} UTC"_f << RateLimit->m_ResetTime, ". ");
			else
				fg_AddStrSep(Error, "Rate limited", ". ");

			ExceptionData.m_RateLimit = fg_Move(*RateLimit);
		}

		return DMibErrorInstanceGitHostingProvider("{} failed with status {} ({}): {}"_f << _Description << _Result.m_StatusCode << _Result.m_StatusMessage << Error, fg_Move(ExceptionData));
	}

	// Primary limits answer with X-RateLimit-Remaining 0 and the reset as Unix seconds in X-RateLimit-Reset. Secondary limits
	// answer with Retry-After in seconds, which takes precedence, or with neither and a message naming the secondary rate limit,
	// after which GitHub says to wait at least a minute. Any other 403 is a permission error, not a rate limit.
	auto CGitHostingProvider_GitHub::fs_GetRateLimit(uint32 _StatusCode, TCMap<CStr, CStr, CCompareNoCase> const &_Headers, CStr const &_Body, NTime::CTime const &_Now)
		-> TCOptional<CGitHostingProviderRateLimit>
	{
		if (_StatusCode != 403 && _StatusCode != 429)
			return {};

		auto fGetHeader = [&](CStr const &_Name) -> TCOptional<uint64>
			{
				auto *pValue = _Headers.f_FindEqual(_Name);
				if (!pValue)
					return {};

				int64 Value = pValue->f_Trim().f_ToInt(int64(-1));
				if (Value < 0)
					return {};

				return uint64(Value);
			}
		;

		CGitHostingProviderRateLimit RateLimit;
		RateLimit.m_Remaining = fGetHeader("X-RateLimit-Remaining");
		RateLimit.m_RetryAfterSeconds = fGetHeader("Retry-After");
		auto ResetUnixSeconds = fGetHeader("X-RateLimit-Reset");

		bool bPrimaryLimitExceeded = RateLimit.m_Remaining && *RateLimit.m_Remaining == 0;
		bool bSecondaryLimitMessage = _Body.f_FindNoCase("secondary rate limit") >= 0;

		if (_StatusCode != 429 && !RateLimit.m_RetryAfterSeconds && !bPrimaryLimitExceeded && !bSecondaryLimitMessage)
			return {};

		if (!RateLimit.m_RetryAfterSeconds && !bPrimaryLimitExceeded && bSecondaryLimitMessage)
			RateLimit.m_RetryAfterSeconds = 60;

		if (RateLimit.m_RetryAfterSeconds)
			RateLimit.m_ResetTime = _Now + NTime::CTimeSpanConvert::fs_CreateSecondSpan(int64(*RateLimit.m_RetryAfterSeconds));
		else if (ResetUnixSeconds && bPrimaryLimitExceeded)
			RateLimit.m_ResetTime = NTime::CTimeConvert::fs_FromUnixSeconds(*ResetUnixSeconds);

		return RateLimit;
	}

	CStr CGitHostingProvider_GitHub::fs_GetNextPageUrl(CStr const &_LinkHeader)
	{
		CStr Rel;
		aint nParsed = 0;
		ch8 const *pParse = _LinkHeader;
		while (*pParse)
		{
			CStr NextPage;

			pParse = (CStr::CParse("<{}>; rel=\"{}\"") >> NextPage >> Rel).f_ByPointer().f_Parse(pParse, nParsed);

			if (nParsed != 2)
				break;

			if (NextPage.f_StartsWith("https://api.github.com/") && Rel == "next")
				return NextPage;

			if (*pParse == ',')
				++pParse;
			fg_ParseWhiteSpace(pParse);

			if (*pParse != '<')
				break;
		}

		return {};
	}

	// Appends the items of _Page to o_Result. Without _PagedArrayMember both are arrays; with it both are objects whose
	// _PagedArrayMember holds the items, and the other members of the first page are kept. Returns true once o_Result holds
	// _MaxItems items, truncating it to that count; 0 means no limit.
	bool CGitHostingProvider_GitHub::fs_MergeRestPage(CJsonSorted &o_Result, CJsonSorted &&_Page, CStr const &_PagedArrayMember, umint _MaxItems)
	{
		auto fGetItems = [&](CJsonSorted &_Json, ch8 const *_pWhich) -> CJsonSorted::CArray &
			{
				if (!_PagedArrayMember)
				{
					if (!_Json.f_IsArray())
					{
						DMibError
							(
								"Unexpected paged GitHub request where {} result wasn't an array. Type: {}."_f
								<< _pWhich
								<< fg_EJsonTypeToString((EEJsonType)_Json.f_Type()) // EJsonType is a subset of EEJsonType
							)
						;
					}

					return _Json.f_Array();
				}

				if (auto *pItems = _Json.f_GetMember(_PagedArrayMember, EJsonType_Array))
					return pItems->f_Array();

				DMibError("Unexpected paged GitHub request where {} result has no '{}' array"_f << _pWhich << _PagedArrayMember);
			}
		;

		if (!o_Result.f_IsValid())
			o_Result = fg_Move(_Page);
		else
			fGetItems(o_Result, "previous").f_Insert(fg_Move(fGetItems(_Page, "new")));

		if (!_MaxItems)
			return false;

		auto &Items = fGetItems(o_Result, "merged");
		if (Items.f_GetLen() < _MaxItems)
			return false;

		Items.f_SetLen(_MaxItems);

		return true;
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApi
		(
			CStr _Path
			, CStr _ErrorDescription
			, TCMap<CStr, CStr> _QueryParams
			, uint32 _EmptyStatus
			, uint32 _ExpectedStatus
			, CStr _PagedArrayMember
			, umint _MaxItems
		)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		CJsonSorted ReturnJson;

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		TCVector<NWeb::NHTTP::CURL::CQueryEntry> QueryEntries;

		if (!_QueryParams.f_FindEqual("per_page"))
		{
			umint PerPage = 100;
			if (_MaxItems && _MaxItems < PerPage)
				PerPage = _MaxItems;

			QueryEntries.f_Insert({"per_page", CStr::fs_ToStr(PerPage)});
		}

		for (auto &Value : _QueryParams)
			QueryEntries.f_Insert({_QueryParams.fs_GetKey(Value), Value});

		Url.f_SetQuery(QueryEntries);

		CStr PageUrl = Url.f_Encode();

		while (PageUrl)
		{
			// Per page, since an App's installation token can expire during a long enumeration
			auto Headers = co_await fp_GetRestHeaders();

			auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_Get, PageUrl, fg_Move(Headers));

			PageUrl.f_Clear();

			if (Result.m_StatusCode == _EmptyStatus)
				co_return {};

			if (Result.m_StatusCode != _ExpectedStatus)
				co_return fp_GetRestError("{}: GitHub request"_f << _ErrorDescription, Result, {});

			{
				auto CaptureScope = co_await g_CaptureExceptions;

				if (fs_MergeRestPage(ReturnJson, CJsonSorted::fs_FromString(Result.m_Body), _PagedArrayMember, _MaxItems))
					break;

				if (auto *pLink = Result.m_Headers.f_FindEqual("link"))
					PageUrl = fs_GetNextPageUrl(*pLink);
			}
		}

		co_return fg_Move(ReturnJson);
	}

	TCFuture<void> CGitHostingProvider_GitHub::fp_RestApiPut(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		auto Headers = co_await fp_GetRestHeaders();

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_Put, Url.f_Encode(), fg_Move(Headers), fg_Move(_Value));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub put request"_f << _ErrorDescription, Result, _FieldTranslation);

		co_return {};
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApiPost(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		auto Headers = co_await fp_GetRestHeaders();

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_Post, Url.f_Encode(), fg_Move(Headers), fg_Move(_Value));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub post request"_f << _ErrorDescription, Result, _FieldTranslation);

		auto CaptureScope = co_await g_CaptureExceptions;

		co_return CJsonSorted::fs_FromString(Result.m_Body);
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApiPatch(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		auto Headers = co_await fp_GetRestHeaders();

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_Patch, Url.f_Encode(), fg_Move(Headers), fg_Move(_Value));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub patch request"_f << _ErrorDescription, Result, _FieldTranslation);

		auto CaptureScope = co_await g_CaptureExceptions;

		co_return CJsonSorted::fs_FromString(Result.m_Body);
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApiUploadFile
		(
			CStr _Path
			, TCActorFunctor<TCFuture<CByteVector> (umint _nBytes)> _fReadData
			, uint64 _DataSize
			, CStr _ErrorDescription
			, uint32 _ExpectedStatus
		)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://uploads.github.com/{}"_f << _Path);

		CHttpClientActor::CRequest Request;

		Request.m_URL = Url.f_Encode();
		Request.m_Method = CHttpClientActor::EMethod_POST;
		Request.m_Headers = co_await fp_GetRestHeaders();
		Request.m_Headers["Content-Type"] = "application/octet-stream";

		auto &SendData = Request.f_AsyncSend();
		SendData.m_Size = _DataSize;
		SendData.m_fRead = fg_Move(_fReadData);

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_SendRequest, fg_Move(Request));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub upload request"_f << _ErrorDescription, Result, {});

		auto CaptureScope = co_await g_CaptureExceptions;

		co_return CJsonSorted::fs_FromString(Result.m_Body);
	}

	auto CGitHostingProvider_GitHub::fp_PublicDownloadFile(CStr _Url, TCActorFunctor<TCFuture<void> (CByteVector _Data)> _fWriteData, CStr _ErrorDescription, uint32 _ExpectedStatus)
		-> TCFuture<void>
	{
		CHttpClientActor::CRequest Request;

		Request.m_URL = _Url;
		Request.m_Method = CHttpClientActor::EMethod_GET;
		Request.m_Headers = fsp_GetPublicRestHeaders();
		Request.m_Headers["Accept"] = "application/octet-stream";

		auto &ReceiveData = Request.f_AsyncReceive();
		ReceiveData.m_fWrite = fg_Move(_fWriteData);

		Request.m_bFollowRedirects = true;

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_SendRequest, fg_Move(Request));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub public download request"_f << _ErrorDescription, Result, {});

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::fp_RestApiDownloadFile
		(
			CStr _Path
			, TCActorFunctor<TCFuture<void> (CByteVector _Data)> _fWriteData
			, CStr _ErrorDescription
			, uint32 _ExpectedStatus
			, CStr _Accept
		)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		CHttpClientActor::CRequest Request;

		Request.m_URL = Url.f_Encode();
		Request.m_Method = CHttpClientActor::EMethod_GET;
		Request.m_Headers = co_await fp_GetRestHeaders();
		Request.m_Headers["Accept"] = _Accept;

		auto &ReceiveData = Request.f_AsyncReceive();
		ReceiveData.m_fWrite = fg_Move(_fWriteData);

		Request.m_bFollowRedirects = true;

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_SendRequest, fg_Move(Request));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub download request"_f << _ErrorDescription, Result, {});

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::fp_RestApiDelete(CStr _Path, CStr _ErrorDescription, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		CStr PageUrl = Url.f_Encode();

		auto Headers = co_await fp_GetRestHeaders();

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_Delete, PageUrl, fg_Move(Headers));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub delete request"_f << _ErrorDescription, Result, {});

		co_return {};
	}
}
