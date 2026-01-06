// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Web/Curl>

namespace NMib::NGit
{
	TCMap<CStr, CStr> CGitHostingProvider_GitHub::fp_GetRestHeaders(bool _bAuthorize)
	{
		TCMap<CStr, CStr> Headers =
			{
				{"Accept", "application/vnd.github+json"}
				, {"User-Agent", "MalterlibGitHostingProvider"}
				, {"X-GitHub-Api-Version", "2022-11-28"}
			}
		;

		if (_bAuthorize && mp_Token)
			Headers["Authorization"] = "Bearer {}"_f << mp_Token;

		return Headers;
	}

	CStr const *CGitHostingProvider_GitHub::CFieldTranslations::f_GetMalterlibField(CStr const &_GitHubField) const
	{
		auto iField = fg_BinarySearch(m_pFields, m_nFields, _GitHubField);
		if (iField >= 0)
			return &m_pFields[iField].m_MalterlibField;

		return nullptr;
	}

	CExceptionPointer CGitHostingProvider_GitHub::fp_GetRestError(CStr const &_Description, CCurlActor::CResult const &_Result, CFieldTranslations const &_FieldTranslation)
	{
		CStr Error = _Result.m_Body;

		CGitHostingProviderExceptionData ExceptionData;
		static_cast<CWebRequestExceptionData &>(ExceptionData) = CWebRequestExceptionData::fs_FromResult(_Result);
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

		return DMibErrorInstanceGitHostingProvider
			(
				"{} failed with status {} ({}): {}"_f << _Description << _Result.m_StatusCode << _Result.m_StatusMessage << Error
				, fg_Move(ExceptionData)
			)
		;
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApi(CStr _Path, CStr _ErrorDescription, TCMap<CStr, CStr> _QueryParams, uint32 _EmptyStatus, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		CJsonSorted ReturnJson;

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		TCVector<NWeb::NHTTP::CURL::CQueryEntry> QueryEntries;

		if (!_QueryParams.f_FindEqual("per_page"))
			QueryEntries.f_Insert({"per_page", "100"});

		for (auto &Value : _QueryParams)
			QueryEntries.f_Insert({_QueryParams.fs_GetKey(Value), Value});

		Url.f_SetQuery(QueryEntries);

		CStr PageUrl = Url.f_Encode();

		TCMap<CStr, CStr> Headers = fp_GetRestHeaders();

		while (PageUrl)
		{
			auto Result = co_await mp_CurlActor
				(
					&CCurlActor::f_Request
					, CCurlActor::EMethod_GET
					, PageUrl
					, Headers
					, CByteVector{}
					, TCMap<CStr, CStr>{}
				)
			;

			PageUrl.f_Clear();

			if (Result.m_StatusCode == _EmptyStatus)
				co_return {};

			if (Result.m_StatusCode != _ExpectedStatus)
				co_return fp_GetRestError("{}: GitHub request"_f << _ErrorDescription, Result, {});

			{
				auto CaptureScope = co_await g_CaptureExceptions;

				auto PageJson = CJsonSorted::fs_FromString(Result.m_Body);
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
								<< fg_EJsonTypeToString((EEJsonType)ReturnJson.f_Type()) // EJsonType is a subset of EEJsonType
								<< fg_EJsonTypeToString((EEJsonType)PageJson.f_Type())
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

	TCFuture<void> CGitHostingProvider_GitHub::fp_RestApiPut(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		auto PutData = _Value.f_ToString(nullptr);

		auto Result = co_await mp_CurlActor
			(
				&CCurlActor::f_Request
				, CCurlActor::EMethod_PUT
				, Url.f_Encode()
				, fp_GetRestHeaders()
				, CByteVector::fs_FromString(PutData)
				, TCMap<CStr, CStr>{}
			)
		;

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub put request"_f << _ErrorDescription, Result, _FieldTranslation);

		co_return {};
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApiPost(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		auto PostData = _Value.f_ToString(nullptr);

		auto Result = co_await mp_CurlActor
			(
				&CCurlActor::f_Request
				, CCurlActor::EMethod_POST
				, Url.f_Encode()
				, fp_GetRestHeaders()
				, CByteVector::fs_FromString(PostData)
				, TCMap<CStr, CStr>{}
			)
		;

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub post request"_f << _ErrorDescription, Result, _FieldTranslation);

		auto CaptureScope = co_await g_CaptureExceptions;

		co_return CJsonSorted::fs_FromString(Result.m_Body);
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApiPatch(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		auto PostData = _Value.f_ToString(nullptr);

		auto Result = co_await mp_CurlActor
			(
				&CCurlActor::f_Request
				, CCurlActor::EMethod_PATCH
				, Url.f_Encode()
				, fp_GetRestHeaders()
				, CByteVector::fs_FromString(PostData)
				, TCMap<CStr, CStr>{}
			)
		;

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub patch request"_f << _ErrorDescription, Result, _FieldTranslation);

		auto CaptureScope = co_await g_CaptureExceptions;

		co_return CJsonSorted::fs_FromString(Result.m_Body);
	}

	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_RestApiUploadFile
		(
			CStr _Path
			, TCActorFunctor<TCFuture<CByteVector> (mint _nBytes)> _fReadData
			, uint64 _DataSize
			, CStr _ErrorDescription
			, uint32 _ExpectedStatus
		)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://uploads.github.com/{}"_f << _Path);

		CCurlActor::CRequest Request;

		Request.m_URL = Url.f_Encode();
		Request.m_Method = CCurlActor::EMethod_POST;
		Request.m_Headers = fp_GetRestHeaders();
		Request.m_ReadDataSize = _DataSize;
		Request.m_fReadData = fg_Move(_fReadData);
		Request.m_Headers["Content-Type"] = "application/octet-stream";

		auto Result = co_await mp_CurlActor(&CCurlActor::f_ExecuteRequest, fg_Move(Request));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub upload request"_f << _ErrorDescription, Result, {});

		auto CaptureScope = co_await g_CaptureExceptions;

		co_return CJsonSorted::fs_FromString(Result.m_Body);
	}

	TCFuture<void> CGitHostingProvider_GitHub::fp_PublicDownloadFile
		(
			CStr _Url
			, TCActorFunctor<TCFuture<void> (CByteVector _Data)> _fWriteData
			, CStr _ErrorDescription
			, uint32 _ExpectedStatus
		)
	{
		CCurlActor::CRequest Request;

		Request.m_URL = _Url;
		Request.m_Method = CCurlActor::EMethod_GET;
		Request.m_Headers = fp_GetRestHeaders(false);
		Request.m_fWriteData = fg_Move(_fWriteData);
		Request.m_Headers["Accept"] = "application/octet-stream";
		Request.m_bFollowRedirects = true;

		auto Result = co_await mp_CurlActor(&CCurlActor::f_ExecuteRequest, fg_Move(Request));

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
		)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		CCurlActor::CRequest Request;

		Request.m_URL = Url.f_Encode();
		Request.m_Method = CCurlActor::EMethod_GET;
		Request.m_Headers = fp_GetRestHeaders();
		Request.m_fWriteData = fg_Move(_fWriteData);
		Request.m_Headers["Accept"] = "application/octet-stream";
		Request.m_bFollowRedirects = true;

		auto Result = co_await mp_CurlActor(&CCurlActor::f_ExecuteRequest, fg_Move(Request));

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub download request"_f << _ErrorDescription, Result, {});

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::fp_RestApiDelete(CStr _Path, CStr _ErrorDescription, uint32 _ExpectedStatus)
	{
		DMibRequire(!_Path.f_StartsWith("/"));

		NWeb::NHTTP::CURL Url("https://api.github.com/{}"_f << _Path);

		CStr PageUrl = Url.f_Encode();

		auto Result = co_await mp_CurlActor
			(
				&CCurlActor::f_Request
				, CCurlActor::EMethod_DELETE
				, PageUrl
				, fp_GetRestHeaders()
				, CByteVector{}
				, TCMap<CStr, CStr>{}
			)
		;

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub delete request"_f << _ErrorDescription, Result, {});

		co_return {};
	}
}
