// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		CStr fg_ParseIdentifiers(CJsonSorted const &_Data)
		{
			return "Rest:{},GraphQL:{}"_f << _Data.f_GetMemberValue("id", 0).f_Integer() << _Data.f_GetMemberValue("node_id", CStr()).f_String();
		}

		CGitHostingProvider::CReleaseAsset fg_ParseReleaseAsset(CJsonSorted const &_Data)
		{
			CGitHostingProvider::CReleaseAsset Return;

			Return.m_Identifier = fg_ParseIdentifiers(_Data);
			Return.m_ContentType = _Data.f_GetMemberValue("content_type", CStr()).f_String();
			Return.m_Name = _Data.f_GetMemberValue("name", CStr()).f_String();
			Return.m_Label = _Data.f_GetMemberValue("label", CStr()).f_String();
			Return.m_Size = _Data.f_GetMemberValue("size", uint64(0)).f_Integer();
			Return.m_DownloadCount = _Data.f_GetMemberValue("download_count", uint64(0)).f_Integer();
			Return.m_State = _Data.f_GetMemberValue("state", CStr()).f_String();
			Return.m_DownloadUrl = _Data.f_GetMemberValue("browser_download_url", CStr()).f_String();

			return Return;
		}

		CGitHostingProvider::CRelease fg_ParseRelease(CJsonSorted const &_Data)
		{
			CGitHostingProvider::CRelease Return;

			Return.m_Identifier = fg_ParseIdentifiers(_Data);
			Return.m_TagName = _Data.f_GetMemberValue("tag_name", CStr()).f_String();
			Return.m_TargetReference = _Data.f_GetMemberValue("target_commitish", CStr()).f_String();
			Return.m_Name = _Data.f_GetMemberValue("name", CStr()).f_String();
			Return.m_bPublished = !_Data.f_GetMemberValue("draft", false).f_Boolean();
			Return.m_bPreRelease = _Data.f_GetMemberValue("prerelease", false).f_Boolean();

			if (auto pValue = _Data.f_GetMember("body", EJsonType_String))
				Return.m_Description = pValue->f_String();

			for (auto &AssetJson : _Data["assets"].f_Array())
				Return.m_Assets.f_Insert(fg_ParseReleaseAsset(AssetJson));

			return Return;
		}

		CStr fg_GetRestID(CStr const &_Identifier)
		{
			for (auto &SubID : _Identifier.f_Split(","))
			{
				if (SubID.f_StartsWith("Rest:"))
					return SubID.f_RemovePrefix("Rest:");
			}

			return {};
		}

	}

	auto CGitHostingProvider_GitHub::f_CreateRelease(CStr _Repository, CCreateRelease _CreateRelease) -> TCFuture<CRelease>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJsonSorted PostData =
			{
				"tag_name"_j= _CreateRelease.m_TagName
			}
		;

		if (_CreateRelease.m_Name)
			PostData["name"] = *_CreateRelease.m_Name;

		if (_CreateRelease.m_Description)
			PostData["body"] = *_CreateRelease.m_Description;

		if (_CreateRelease.m_TargetReference)
			PostData["target_commitish"] = *_CreateRelease.m_TargetReference;

		if (_CreateRelease.m_Published)
			PostData["draft"] = !*_CreateRelease.m_Published;

		if (_CreateRelease.m_PreRelease)
			PostData["prerelease"] = *_CreateRelease.m_PreRelease;

		if (_CreateRelease.m_GenerateReleaseNotes)
			PostData["generate_release_notes"] = *_CreateRelease.m_GenerateReleaseNotes;

		if (_CreateRelease.m_MakeLatest)
			PostData["make_latest"] = CStr::fs_ToStr(*_CreateRelease.m_MakeLatest);

		static constexpr CFieldTranslationPair c_FieldTranslations[] =
			{
				{gc_Str<"body">, gc_Str<"Description">}
				, {gc_Str<"generate_release_notes">, gc_Str<"GenerateReleaseNotes">}
				, {gc_Str<"make_latest">, gc_Str<"MakeLatest">}
				, {gc_Str<"name">, gc_Str<"Name">}
				, {gc_Str<"prerelease">, gc_Str<"PreRelease">}
				, {gc_Str<"tag_name">, gc_Str<"TagName">}
				, {gc_Str<"target_commitish">, gc_Str<"TargetReference">}
			}
		;

		auto Data = co_await
			(
				fp_RestApiPost
				(
					"repos/{}/{}/releases"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
					, PostData
					, "Failed to create release"
#ifdef DCompiler_MSVC_Workaround
					, fsp_FieldTranslations(c_FieldTranslations)
#else
					, fsp_FieldTranslations<c_FieldTranslations>()
#endif
				)
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fg_ParseRelease(Data);
	}

	auto CGitHostingProvider_GitHub::f_GetRelease(CStr _Repository, CStr _ReleaseTag) -> TCFuture<TCOptional<CRelease>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi("repos/{}/{}/releases/tags/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _ReleaseTag, "Failed to get release", {}, 404);
		if (!Data.f_IsValid())
			co_return {};

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fg_ParseRelease(Data);
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DeleteRelease(NStr::CStr _Repository, NStr::CStr _ReleaseID)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CStr URL = "repos/{}/{}/releases/{}"_f
			<< RepositorySlug.m_Owner
			<< RepositorySlug.m_Name
			<< fg_GetRestID(_ReleaseID)
		;

		co_await fp_RestApiDelete(URL, "Failed to delete release");

		co_return {};
	}

	auto CGitHostingProvider_GitHub::f_GetReleases(CStr _Repository) -> TCFuture<TCVector<CRelease>>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi("repos/{}/{}/releases"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name, "Failed to get releases", {});
		if (!Data.f_IsValid())
			co_return {};

		auto CaptureExceptions = co_await g_CaptureExceptions;

		TCVector<CRelease> OutReleases;

		for (auto &Release : Data.f_Array())
			OutReleases.f_Insert(fg_ParseRelease(Release));

		co_return fg_Move(OutReleases);
	}

	auto CGitHostingProvider_GitHub::f_UploadReleaseAsset(CStr _Repository, CStr _ReleaseIdentifier, CUploadReleaseAsset _UploadRelease) -> TCFuture<CReleaseAsset>
	{
		if (_ReleaseIdentifier.f_IsEmpty())
			co_return DMibErrorInstance("Release identifier cannot be empty");

		if (_UploadRelease.m_Name.f_IsEmpty())
			co_return DMibErrorInstance("Release name cannot be empty");

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CStr URL = "repos/{}/{}/releases/{}/assets?name={}"_f
			<< RepositorySlug.m_Owner
			<< RepositorySlug.m_Name
			<< fg_GetRestID(_ReleaseIdentifier)
			<< NWeb::NHTTP::CURL::fs_PercentEncode(_UploadRelease.m_Name)
		;

		if (_UploadRelease.m_Label)
			URL += "&label={}"_f << NWeb::NHTTP::CURL::fs_PercentEncode(*_UploadRelease.m_Label);

		auto Data = co_await fp_RestApiUploadFile
			(
				URL
				, fg_Move(_UploadRelease.m_fReadData)
				, _UploadRelease.m_AssetSize
				, "Failed to upload release asset"
			)
		;

		co_return fg_ParseReleaseAsset(Data);
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DownloadReleaseAsset(CStr _Repository, CDownloadReleaseAsset _DownloadRelease)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CStr URL = "repos/{}/{}/releases/assets/{}"_f
			<< RepositorySlug.m_Owner
			<< RepositorySlug.m_Name
			<< fg_GetRestID(_DownloadRelease.m_Identifier)
		;

		co_await fp_RestApiDownloadFile
			(
				URL
				, fg_Move(_DownloadRelease.m_fWriteData)
				, "Failed to download release asset"
			)
		;

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DownloadPublicReleaseAsset(CStr _Repository, CDownloadPublicReleaseAsset _DownloadRelease)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		co_await fp_PublicDownloadFile
			(
				_DownloadRelease.m_Url
				, fg_Move(_DownloadRelease.m_fWriteData)
				, "Failed to download release asset"
			)
		;

		co_return {};
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::f_GetPublicReleaseAssetUrl(CStr _Repository, CStr _TagName, NStr::CStr _AssetName)
	{
		co_return "https://github.com/{}/releases/download/{}/{}"_f << _Repository << _TagName << _AssetName;
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_DeleteReleaseAsset(CStr _Repository, NStr::CStr _Identifier)
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CStr URL = "repos/{}/{}/releases/assets/{}"_f
			<< RepositorySlug.m_Owner
			<< RepositorySlug.m_Name
			<< fg_GetRestID(_Identifier)
		;

		co_await fp_RestApiDelete(URL, "Failed to delete release asset");

		co_return {};
	}
}
