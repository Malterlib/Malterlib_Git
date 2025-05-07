// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		CGitHostingProvider::CGetRepository fg_ParseRepository(CJsonSorted const &_RepositoryJson)
		{
			using ESquashMergeCommitTitle = CGitHostingProvider::CRepository::ESquashMergeCommitTitle;
			using ESquashMergeCommitMessage = CGitHostingProvider::CRepository::ESquashMergeCommitMessage;
			using EMergeCommitTitle = CGitHostingProvider::CRepository::EMergeCommitTitle;
			using EMergeCommitMessage = CGitHostingProvider::CRepository::EMergeCommitMessage;

			CGitHostingProvider::CGetRepository NewRepo;
			NewRepo.m_Name = _RepositoryJson["full_name"].f_String();
			NewRepo.m_DefaultBranch = _RepositoryJson["default_branch"].f_String();
			NewRepo.m_IsPrivate = _RepositoryJson["visibility"].f_String() == "private";

			if (auto *pParent = _RepositoryJson.f_GetMember("parent"))
				NewRepo.m_ForkedFromRepository = (*pParent)["full_name"].f_String();

			if (_RepositoryJson["description"].f_IsString())
				NewRepo.m_Description = _RepositoryJson["description"].f_String();

			if (_RepositoryJson["homepage"].f_IsString())
				NewRepo.m_Homepage = _RepositoryJson["homepage"].f_String();

			if (_RepositoryJson["language"].f_IsString())
				NewRepo.m_Language = _RepositoryJson["language"].f_String();

			NewRepo.m_Stats.m_ForksCount = _RepositoryJson["forks_count"].f_Integer();
			NewRepo.m_Stats.m_StargazersCount = _RepositoryJson["stargazers_count"].f_Integer();
			NewRepo.m_Stats.m_WatchersCount = _RepositoryJson["watchers_count"].f_Integer();
			NewRepo.m_Stats.m_OpenIssuesCount = _RepositoryJson["open_issues_count"].f_Integer();

			if (auto *pValue = _RepositoryJson.f_GetMember("subscribers_count"))
				NewRepo.m_Stats.m_SubscribersCount = pValue->f_Integer();

			if (auto *pValue = _RepositoryJson.f_GetMember("network_count"))
				NewRepo.m_Stats.m_NetworkCount = pValue->f_Integer();

			NewRepo.m_Stats.m_SizeKiloBytes = _RepositoryJson["size"].f_Integer();

			if (auto *pValue = _RepositoryJson.f_GetMember("is_template"))
				NewRepo.m_IsTemplate = pValue->f_Boolean();

			NewRepo.m_HasIssues = _RepositoryJson["has_issues"].f_Boolean();
			NewRepo.m_HasProjects = _RepositoryJson["has_projects"].f_Boolean();
			NewRepo.m_HasWiki = _RepositoryJson["has_wiki"].f_Boolean();
			NewRepo.m_bHasPages = _RepositoryJson["has_pages"].f_Boolean();
			
			if (auto *pValue = _RepositoryJson.f_GetMember("has_downloads"))
				NewRepo.m_HasDownloads = pValue->f_Boolean();

			NewRepo.m_HasDiscussions = _RepositoryJson["has_discussions"].f_Boolean();
			NewRepo.m_Archived = _RepositoryJson["archived"].f_Boolean();
			NewRepo.m_bDisabled = _RepositoryJson["disabled"].f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("allow_rebase_merge"))
				NewRepo.m_AllowRebaseMerge = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("allow_squash_merge"))
				NewRepo.m_AllowSquashMerge = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("allow_auto_merge"))
				NewRepo.m_AllowAutoMerge = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("delete_branch_on_merge"))
				NewRepo.m_DeleteBranchOnMerge = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("allow_merge_commit"))
				NewRepo.m_AllowMergeCommit = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("allow_update_branch"))
				NewRepo.m_AllowUpdateBranch = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("use_squash_pr_title_as_default"))
				NewRepo.m_UseSquashPrTitleAsDefault = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("allow_forking"))
				NewRepo.m_AllowForking = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("web_commit_signoff_required"))
				NewRepo.m_WebCommitSignoffRequired = pValue->f_Boolean();

			if (auto *pValue = _RepositoryJson.f_GetMember("custom_properties"))
			{
				TCMap<CStr, TCOptional<CStr>> OutProperties;

				for (auto &Value : pValue->f_Object())
				{
					if (Value.f_Value().f_IsNull())
						OutProperties[Value.f_Name()];
					else if (Value.f_Value().f_IsString())
						OutProperties[Value.f_Name()] = Value.f_Value().f_String();
				}

				NewRepo.m_CustomProperties = fg_Move(OutProperties);
			}

			if (auto *pValue = _RepositoryJson.f_GetMember("squash_merge_commit_title"))
			{
				auto &Value = pValue->f_String();
				if (Value == "PR_TITLE")
					NewRepo.m_SquashMergeCommitTitle = ESquashMergeCommitTitle::mc_PrTitle;
				if (Value == "COMMIT_OR_PR_TITLE")
					NewRepo.m_SquashMergeCommitTitle = ESquashMergeCommitTitle::mc_CommitOrPrTitle;
			}

			if (auto *pValue = _RepositoryJson.f_GetMember("squash_merge_commit_message"))
			{
				auto &Value = pValue->f_String();
				if (Value == "PR_BODY")
					NewRepo.m_SquashMergeCommitMessage = ESquashMergeCommitMessage::mc_PrBody;
				if (Value == "COMMIT_MESSAGES")
					NewRepo.m_SquashMergeCommitMessage = ESquashMergeCommitMessage::mc_CommitMessages;
				if (Value == "BLANK")
					NewRepo.m_SquashMergeCommitMessage = ESquashMergeCommitMessage::mc_Blank;
			}

			if (auto *pValue = _RepositoryJson.f_GetMember("merge_commit_title"))
			{
				auto &Value = pValue->f_String();
				if (Value == "PR_TITLE")
					NewRepo.m_MergeCommitTitle = EMergeCommitTitle::mc_PrTitle;
				if (Value == "MERGE_MESSAGE")
					NewRepo.m_MergeCommitTitle = EMergeCommitTitle::mc_MergeMessage;
			}

			if (auto *pValue = _RepositoryJson.f_GetMember("merge_commit_message"))
			{
				auto &Value = pValue->f_String();
				if (Value == "PR_BODY")
					NewRepo.m_MergeCommitMessage = EMergeCommitMessage::mc_PrBody;
				if (Value == "PR_TITLE")
					NewRepo.m_MergeCommitMessage = EMergeCommitMessage::mc_PrTitle;
				if (Value == "BLANK")
					NewRepo.m_MergeCommitMessage = EMergeCommitMessage::mc_Blank;
			}

			if (auto *pValue = _RepositoryJson.f_GetMember("security_and_analysis", EJsonType_Object))
			{
				auto &Value = pValue->f_Object();
				auto fGetStatusValue = [&](ch8 const *_pName) -> NStorage::TCOptional<bool>
					{
						NStorage::TCOptional<bool> Return;
						if (auto *pValue = Value.f_GetMember(_pName, EJsonType_Object))
						{
							auto &Value = pValue->f_Object();
							if (auto *pValue = Value.f_GetMember("status"))
								Return = pValue->f_String() == "enabled";
						}

						return Return;
					}
				;
				NewRepo.m_Security_AdvancedEnable = fGetStatusValue("advanced_security");
				NewRepo.m_Security_SecretScanning = fGetStatusValue("secret_scanning");
				NewRepo.m_Security_SecretScanningPushProtection = fGetStatusValue("secret_scanning_push_protection");
			}

			return NewRepo;
		}

		void fg_RepositoryToJson(CJsonSorted &o_Json, CGitHostingProvider::CRepository const &_Repository, bool _bUpdate)
		{
			using ESquashMergeCommitTitle = CGitHostingProvider::CRepository::ESquashMergeCommitTitle;
			using ESquashMergeCommitMessage = CGitHostingProvider::CRepository::ESquashMergeCommitMessage;
			using EMergeCommitTitle = CGitHostingProvider::CRepository::EMergeCommitTitle;
			using EMergeCommitMessage = CGitHostingProvider::CRepository::EMergeCommitMessage;
			
			if (_Repository.m_Name)
				o_Json["name"] = CFile::fs_GetFile(*_Repository.m_Name);
			if (_Repository.m_Description)
				o_Json["description"] = *_Repository.m_Description;
			if (_Repository.m_Homepage)
				o_Json["homepage"] = *_Repository.m_Homepage;

			if (_Repository.m_IsPrivate)
				o_Json["private"] = *_Repository.m_IsPrivate;
			if (_Repository.m_IsTemplate)
				o_Json["is_template"] = *_Repository.m_IsTemplate;
			if (_Repository.m_HasIssues)
				o_Json["has_issues"] = *_Repository.m_HasIssues;
			if (_Repository.m_HasProjects)
				o_Json["has_projects"] = *_Repository.m_HasProjects;
			if (_Repository.m_HasWiki)
				o_Json["has_wiki"] = *_Repository.m_HasWiki;
			if (_Repository.m_AllowSquashMerge)
				o_Json["allow_squash_merge"] = *_Repository.m_AllowSquashMerge;
 			if (_Repository.m_AllowMergeCommit)
				o_Json["allow_merge_commit"] = *_Repository.m_AllowMergeCommit;
			if (_Repository.m_AllowRebaseMerge)
				o_Json["allow_rebase_merge"] = *_Repository.m_AllowRebaseMerge;
			if (_Repository.m_AllowAutoMerge)
				o_Json["allow_auto_merge"] = *_Repository.m_AllowAutoMerge;
			if (_Repository.m_DeleteBranchOnMerge)
				o_Json["delete_branch_on_merge"] = *_Repository.m_DeleteBranchOnMerge;
			if (_Repository.m_UseSquashPrTitleAsDefault)
				o_Json["use_squash_pr_title_as_default"] = *_Repository.m_UseSquashPrTitleAsDefault;
			if (_Repository.m_HasDownloads)
				o_Json["has_downloads"] = *_Repository.m_HasDownloads;

			if (_Repository.m_SquashMergeCommitTitle)
			{
				o_Json["squash_merge_commit_title"] = [](auto _Enum)
					{
						switch (_Enum)
						{
						case ESquashMergeCommitTitle::mc_PrTitle: return "PR_TITLE";
						case ESquashMergeCommitTitle::mc_CommitOrPrTitle: return "COMMIT_OR_PR_TITLE";
						}
						DMibError("Invalid squash merge commit title");
					}
					(*_Repository.m_SquashMergeCommitTitle)
				;
			}

			if (_Repository.m_SquashMergeCommitMessage)
			{
				o_Json["squash_merge_commit_message"] = [](auto _Enum)
					{
						switch (_Enum)
						{
						case ESquashMergeCommitMessage::mc_PrBody: return "PR_BODY";
						case ESquashMergeCommitMessage::mc_CommitMessages: return "COMMIT_MESSAGES";
						case ESquashMergeCommitMessage::mc_Blank: return "BLANK";
						}
						DMibError("Invalid squash merge commit message");
					}
					(*_Repository.m_SquashMergeCommitMessage)
				;
			}

			if (_Repository.m_MergeCommitTitle)
			{
				o_Json["merge_commit_title"] = [](auto _Enum)
					{
						switch (_Enum)
						{
						case EMergeCommitTitle::mc_PrTitle: return "PR_TITLE";
						case EMergeCommitTitle::mc_MergeMessage: return "MERGE_MESSAGE";
						}
						DMibError("Invalid merge commit title");
					}
					(*_Repository.m_MergeCommitTitle)
				;
			}

			if (_Repository.m_MergeCommitMessage)
			{
				o_Json["merge_commit_message"] = [](auto _Enum)
					{
						switch (_Enum)
						{
						case EMergeCommitMessage::mc_PrTitle: return "PR_TITLE";
						case EMergeCommitMessage::mc_PrBody: return "PR_BODY";
						case EMergeCommitMessage::mc_Blank: return "BLANK";
						}
						DMibError("Invalid squash merge commit message");
					}
					(*_Repository.m_MergeCommitMessage)
				;
			}

			if (_Repository.m_CustomProperties)
			{
				auto &OutObject = o_Json["custom_properties"].f_Object();
				for (auto &Value : _Repository.m_CustomProperties->f_Entries())
				{
					if (Value.f_Value())
						OutObject[Value.f_Key()] = *Value.f_Value();
					else
						OutObject[Value.f_Key()] = nullptr;
				}
			}

			if (!_bUpdate)
				return;

			auto fToStatus = [&](bool _bEnabled)
				{
					return _bEnabled ? "enabled" : "disabled";
				}
			;

			if (_Repository.m_Security_AdvancedEnable)
				o_Json["security_and_analysis"]["advanced_security"]["status"] = fToStatus(*_Repository.m_Security_AdvancedEnable);
			if (_Repository.m_Security_SecretScanning)
				o_Json["security_and_analysis"]["secret_scanning"]["status"] = fToStatus(*_Repository.m_Security_SecretScanning);
			if (_Repository.m_Security_SecretScanningPushProtection)
				o_Json["security_and_analysis"]["secret_scanning_push_protection"]["status"] = fToStatus(*_Repository.m_Security_SecretScanningPushProtection);

			if (_Repository.m_DefaultBranch)
				o_Json["default_branch"] = *_Repository.m_DefaultBranch;
			if (_Repository.m_AllowUpdateBranch)
				o_Json["allow_update_branch"] = *_Repository.m_AllowUpdateBranch;
			if (_Repository.m_Archived)
				o_Json["archived"] = *_Repository.m_Archived;
			if (_Repository.m_AllowForking)
				o_Json["allow_forking"] = *_Repository.m_AllowForking;
			if (_Repository.m_WebCommitSignoffRequired)
				o_Json["web_commit_signoff_required"] = *_Repository.m_WebCommitSignoffRequired;

			if (_Repository.m_HasDiscussions)
				o_Json["has_discussions"] = *_Repository.m_HasDiscussions;
		}

		void fg_RepositoryToJson(CJsonSorted &o_Json, CGitHostingProvider::CCreateRepository const &_Repository)
		{
			if (_Repository.m_AutoInit)
				o_Json["auto_init"] = *_Repository.m_AutoInit;
			if (_Repository.m_GitIgnoreTemplate)
				o_Json["gitignore_template"] = *_Repository.m_GitIgnoreTemplate;
			if (_Repository.m_LicenseTemplate)
				o_Json["license_template"] = *_Repository.m_LicenseTemplate;

			fg_RepositoryToJson(o_Json, _Repository, false);
		}

		static constexpr CGitHostingProvider_GitHub::CFieldTranslationPair gc_FieldTranslations_Repository[] =
			{
				{gc_Str<"advanced_security">, gc_Str<"Security_AdvancedEnable">}
				, {gc_Str<"allow_auto_merge">, gc_Str<"AllowAutoMerge">}
				, {gc_Str<"allow_forking">, gc_Str<"AllowForking">}
				, {gc_Str<"allow_merge_commit">, gc_Str<"AllowMergeCommit">}
				, {gc_Str<"allow_rebase_merge">, gc_Str<"AllowRebaseMerge">}
				, {gc_Str<"allow_squash_merge">, gc_Str<"AllowSquashMerge">}
				, {gc_Str<"allow_update_branch">, gc_Str<"AllowUpdateBranch">}
				, {gc_Str<"archived">, gc_Str<"Archived">}
				, {gc_Str<"auto_init">, gc_Str<"AutoInit">}
				, {gc_Str<"custom_properties">, gc_Str<"CustomProperties">}
				, {gc_Str<"default_branch">, gc_Str<"DefaultBranch">}
				, {gc_Str<"delete_branch_on_merge">, gc_Str<"DeleteBranchOnMerge">}
				, {gc_Str<"description">, gc_Str<"Description">}
				, {gc_Str<"gitignore_template">, gc_Str<"GitIgnoreTemplate">}
				, {gc_Str<"has_discussions">, gc_Str<"HasDiscussions">}
				, {gc_Str<"has_downloads">, gc_Str<"HasDownloads">}
				, {gc_Str<"has_issues">, gc_Str<"HasIssues">}
				, {gc_Str<"has_projects">, gc_Str<"HasProjects">}
				, {gc_Str<"has_wiki">, gc_Str<"HasWiki">}
				, {gc_Str<"homepage">, gc_Str<"Homepage">}
				, {gc_Str<"is_template">, gc_Str<"IsTemplate">}
				, {gc_Str<"license_template">, gc_Str<"LicenseTemplate">}
				, {gc_Str<"merge_commit_message">, gc_Str<"MergeCommitMessage">}
				, {gc_Str<"merge_commit_title">, gc_Str<"MergeCommitTitle">}
				, {gc_Str<"name">, gc_Str<"Name">}
				, {gc_Str<"private">, gc_Str<"IsPrivate">}
				, {gc_Str<"secret_scanning">, gc_Str<"Security_SecretScanning">}
				, {gc_Str<"secret_scanning_push_protection">, gc_Str<"Security_SecretScanningPushProtection">}
				, {gc_Str<"squash_merge_commit_message">, gc_Str<"SquashMergeCommitMessage">}
				, {gc_Str<"squash_merge_commit_title">, gc_Str<"SquashMergeCommitTitle">}
				, {gc_Str<"use_squash_pr_title_as_default">, gc_Str<"UseSquashPrTitleAsDefault">}
				, {gc_Str<"web_commit_signoff_required">, gc_Str<"WebCommitSignoffRequired">}
			}
		;
	}

	auto CGitHostingProvider_GitHub::f_CreateRepository(CCreateRepository _CreateRepository) -> TCFuture<CGetRepository>
	{
		auto CaptureExceptions = co_await g_CaptureExceptions;

		CStr PostUrl;

		if (_CreateRepository.m_Organization)
			PostUrl = "orgs/{}/repos"_f << *_CreateRepository.m_Organization;
		else
			PostUrl = "user/repos";

		CJsonSorted PostData;

		fg_RepositoryToJson(PostData, _CreateRepository);

		auto Data = co_await fp_RestApiPost
			(
				PostUrl
				, PostData
				, "Failed to create repository"
#ifdef DCompiler_MSVC_Workaround
				, fsp_FieldTranslations(gc_FieldTranslations_Repository)
#else
				, fsp_FieldTranslations<gc_FieldTranslations_Repository>()
#endif
			)
		;

		co_return fg_ParseRepository(Data);
	}

	auto CGitHostingProvider_GitHub::f_ForkRepository(CStr _Repository, CForkRepository _ForkRepository) -> TCFuture<CGetRepository>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		CJsonSorted PostData =
			{
				"name"_j= CFile::fs_GetFile(_ForkRepository.m_Name)
			}
		;

		if (_ForkRepository.m_Organization)
			PostData["organization"] = *_ForkRepository.m_Organization;

		PostData["default_branch_only"] = _ForkRepository.m_bDefaultBranchOnly;

		static constexpr CFieldTranslationPair c_FieldTranslations[] =
			{
				{gc_Str<"default_branch_only">, gc_Str<"DefaultBranchOnly">}
				, {gc_Str<"organization">, gc_Str<"Organization">}
			}
		;

		auto Data = co_await fp_RestApiPost
			(
				"repos/{}/{}/forks"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
				, PostData
				, "Failed to fork release"
#ifdef DCompiler_MSVC_Workaround
				, fsp_FieldTranslations(c_FieldTranslations)
#else
				, fsp_FieldTranslations<c_FieldTranslations>()
#endif
			)
		;

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fg_ParseRepository(Data);
	}

	auto CGitHostingProvider_GitHub::f_UpdateRepository(NStr::CStr _Repository, CRepository _RepositorySettings) -> TCFuture<CGetRepository>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto CaptureExceptions = co_await g_CaptureExceptions;

		CJsonSorted PatchData;

		fg_RepositoryToJson(PatchData, _RepositorySettings, true);

		if (_RepositorySettings.m_CustomProperties)
		{
			CJsonSorted CustomPropertiesPatchData;
			auto &OutArray = CustomPropertiesPatchData["properties"].f_Array();
			for (auto &Property : _RepositorySettings.m_CustomProperties->f_Entries())
			{
				OutArray.f_Insert
					(
						CJsonSorted
						{
							"property_name"_j= Property.f_Key()
							, "value"_j= Property.f_Value() ? CJsonSorted(*Property.f_Value()) : CJsonSorted(nullptr)
						}
					)
				;
			}

			static constexpr CFieldTranslationPair c_FieldTranslations[] =
				{
					{gc_Str<"properties">, gc_Str<"CustomProperties">}
				}
			;

			co_await
				(
					fp_RestApiPatch
					(
						"repos/{}/{}/properties/values"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
						, CustomPropertiesPatchData
						, "Failed to set custom properties"
#ifdef DCompiler_MSVC_Workaround
						, fsp_FieldTranslations(c_FieldTranslations)
#else
						, fsp_FieldTranslations<c_FieldTranslations>()
#endif
						, 204
					)
				)
			;
		}

		auto Data = co_await
			(
				fp_RestApiPatch
				(
					"repos/{}/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
					, PatchData
					, "Failed to update release"
#ifdef DCompiler_MSVC_Workaround
					, fsp_FieldTranslations(gc_FieldTranslations_Repository)
#else
					, fsp_FieldTranslations<gc_FieldTranslations_Repository>()
#endif
				)
			)
		;

		co_return fg_ParseRepository(Data);
	}
	
	auto CGitHostingProvider_GitHub::f_GetRepositories(TCVector<CStr> _Organizations, bool _bPersonal) -> TCFuture<TCVector<CGetRepository>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;
		
		TCVector<CGetRepository> OutRepositories;
		auto fAddRepository = [&](CJsonSorted const &_RepositoryJson)
			{
				OutRepositories.f_Insert(fg_ParseRepository(_RepositoryJson));
			}
		;

		if (_bPersonal)
		{
			auto const User = co_await fp_RestApi("user", "Failed to get user information for logged in GitHub user");

			auto UserName = User["login"].f_String();
			CStr UserURL = "users/{}"_f << UserName;

			auto const UserRepositories = co_await fp_RestApi(UserURL / "repos", "Failed to get repositories of user '{}'"_f << UserName);
			for (auto &Repo : UserRepositories.f_Array())
				fAddRepository(Repo);
		}

		for (auto &Organization : _Organizations)
		{
			auto const OrganizationRepositories = co_await fp_RestApi("orgs/{}/repos"_f << Organization, "Failed to get repositories of organization '{}'"_f << Organization);

			for (auto &Repo : OrganizationRepositories.f_Array())
				fAddRepository(Repo);
		}

		co_return fg_Move(OutRepositories);
	}

	auto CGitHostingProvider_GitHub::f_GetRepository(CStr _Repository) -> TCFuture<CGetRepository>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto const Repository = co_await fp_RestApi("repos/{}/{}"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name, "Failed to get repository '{}'"_f << _Repository);

		co_return fg_ParseRepository(Repository);
	}
}
