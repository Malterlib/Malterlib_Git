// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Git_HostingProvider.h"

#include <Mib/Web/HttpClient>

namespace NMib::NGit
{
	struct CGitHostingProvider_GitHub : public CGitHostingProvider
	{
		struct CFieldTranslationPair
		{
#ifndef DCompiler_MSVC_Workaround
			constexpr
#endif
			COrdering_Strong operator <=> (CFieldTranslationPair const &_Right) const noexcept
			{
				return fg_StrCmp(m_GitHubField.m_pStr, _Right.m_GitHubField.m_pStr) <=> 0;
			}

#ifndef DCompiler_MSVC_Workaround
			constexpr
#endif
			COrdering_Strong operator <=> (CStr const &_Right) const noexcept
			{
				return m_GitHubField.m_Str <=> _Right;
			}

			TCStrConst<NStr::CStr> m_GitHubField;
			NStr::CStr m_MalterlibField;
		};

		struct CAppLogin
		{
			CStr m_AppID;
			CSecureByteVector m_PrivateKey; // DER. Secret
			CStr m_InstallationID; // Empty: looked up from m_Repository, or else m_Owner
			CStr m_Repository;
			CStr m_Owner;
			NTime::CTime m_TokenExpires;
		};

		TCFuture<void> f_Login(CEJsonSorted _LoginDetails) override;
		TCFuture<CAccessToken> f_GetAccessToken() override;
		TCFuture<CAccessToken> f_CreateAccessToken(CCreateAccessToken _Request) override;
		TCFuture<TCVector<CInstallationRepository>> f_ListInstallationRepositories() override;
		TCFuture<CGetRepository> f_CreateRepository(CCreateRepository _CreateRepository) override;
		TCFuture<CGetRepository> f_ForkRepository(CStr _Repository, CForkRepository _ForkRepository) override;
		TCFuture<CGetRepository> f_UpdateRepository(CStr _Repository, CRepository _RepositorySettings) override;
		TCFuture<TCVector<CGetRepository>> f_GetRepositories(TCVector<CStr> _Organizations, bool _bPersonal) override;
		TCFuture<CGetRepository> f_GetRepository(CStr _Repository) override;

		TCFuture<bool> f_IsOrganization(CStr _Owner) override;

		TCFuture<CUser> f_GetAuthenticatedUser() override;

		TCFuture<void> f_RenameBranch(CStr _Repository, CStr _OldName, CStr _NewName) override;

		TCFuture<TCMap<CStr, CBranchProtectionRule>> f_GetBranchProtectionRules(CStr _Repository) override;
		TCFuture<void> f_UpdateBranchProtectionRule(CStr _Repository, CStr _RuleID, CBranchProtectionRule _Rule) override;
		TCFuture<CStr> f_CreateBranchProtectionRule(CStr _Repository, CBranchProtectionRule _Rule) override;
		TCFuture<void> f_DeleteBranchProtectionRule(CStr _Repository, CStr _RuleID) override;

		TCFuture<CRepositoryPermissions> f_GetRepositoryPermissions(CStr _Repository) override;
		TCFuture<CStr> f_GetCollaboratorPermission(CStr _Repository, CStr _User) override;
		TCFuture<void> f_AddRepositoryPermissions(CStr _Repository, CRepositoryPermissions _Permissions) override;
		TCFuture<void> f_RemoveRepositoryPermissions(CStr _Repository, TCSet<CStr> _Teams, TCSet<CStr> _Users) override;

		TCFuture<CRelease> f_CreateRelease(CStr _Repository, CCreateRelease _CreateRelease) override;
		TCFuture<TCOptional<CRelease>> f_GetRelease(CStr _Repository, CStr _ReleaseTag) override;
		TCFuture<void> f_DeleteRelease(NStr::CStr _Repository, NStr::CStr _ReleaseID) override;
		TCFuture<TCVector<CRelease>> f_GetReleases(CStr _Repository) override;

		TCFuture<CReleaseAsset> f_UploadReleaseAsset(CStr _Repository, CStr _ReleaseIdentifier, CUploadReleaseAsset _UploadRelease) override;
		TCFuture<void> f_DownloadReleaseAsset(CStr _Repository, CDownloadReleaseAsset _DownloadRelease) override;
		TCFuture<void> f_DownloadPublicReleaseAsset(CStr _Repository, CDownloadPublicReleaseAsset _DownloadRelease) override;
		TCFuture<void> f_DeleteReleaseAsset(CStr _Repository, NStr::CStr _Identifier) override;
		TCFuture<CStr> f_GetPublicReleaseAssetUrl(CStr _Repository, CStr _TagName, CStr _AssetName) override;

		TCFuture<TCMap<CStr, CGenericRuleset>> f_GetGenericRulesets(CStr _Repository) override;
		TCFuture<CGenericRuleset> f_PopulateGenericRulesetIDs(CStr _Repository, CGenericRuleset _Ruleset) override;
		TCFuture<void> f_UpdateGenericRuleset(CStr _Repository, CStr _ID, CGenericRuleset _Ruleset) override;
		TCFuture<CStr> f_CreateGenericRuleset(CStr _Repository, CGenericRuleset _Ruleset) override;
		TCFuture<void> f_DeleteGenericRuleset(CStr _Repository, CStr _ID) override;

		TCFuture<CActionsSettings> f_GetActionsSettings(NStr::CStr _Repository) override;
		TCFuture<void> f_UpdateActionsSettings(NStr::CStr _Repository, CActionsSettings _ActionsSettings) override;

		TCFuture<TCVector<CWorkflowRun>> f_GetWorkflowRuns(CStr _Repository, CStr _Workflow, CWorkflowRunFilter _Filter) override;
		TCFuture<TCVector<CWorkflowJob>> f_GetWorkflowRunJobs(CStr _Repository, uint64 _RunID, bool _bAllAttempts) override;
		TCFuture<CStr> f_GetWorkflowJobLog(CStr _Repository, uint64 _JobID) override;

		TCFuture<TCVector<CPullRequest>> f_GetPullRequests(CStr _Repository, CPullRequestFilter _Filter) override;
		TCFuture<CPullRequest> f_GetPullRequest(CStr _Repository, uint64 _Number) override;
		TCFuture<TCVector<CPullRequestFeedback>> f_GetPullRequestFeedback(CStr _Repository, uint64 _Number, NTime::CTime _Since) override;

		static TCVector<CWorkflowRun> fs_ParseWorkflowRuns(CJsonSorted const &_Json);
		static TCVector<CWorkflowJob> fs_ParseWorkflowJobs(CJsonSorted const &_Json);
		static CPullRequest fs_ParsePullRequest(CJsonSorted const &_Json);
		static TCVector<CPullRequest> fs_ParsePullRequests(CJsonSorted const &_Json);
		static CStr fs_ParseCollaboratorPermission(CJsonSorted const &_Json);
		static TCVector<CPullRequestFeedback> fs_ParsePullRequestFeedback
			(
				CJsonSorted const &_Reviews
				, CJsonSorted const &_ReviewComments
				, CJsonSorted const &_IssueComments
				, NTime::CTime const &_Since
			)
		;

		static NTime::CTime fs_ParseTime(CStr const &_Time);
		static CStr fs_CreateAppJwt(CStr const &_AppID, CSecureByteVector const &_PrivateKey, NTime::CTime const &_Now);
		static CStr fs_FormatTime(NTime::CTime const &_Time);

		static bool fs_MergeRestPage(CJsonSorted &o_Result, CJsonSorted &&_Page, CStr const &_PagedArrayMember, umint _MaxItems);
		static CStr fs_GetNextPageUrl(CStr const &_LinkHeader);
		static TCOptional<CGitHostingProviderRateLimit> fs_GetRateLimit
			(
				uint32 _StatusCode
				, TCMap<CStr, CStr, CCompareNoCase> const &_Headers
				, CStr const &_Body
				, NTime::CTime const &_Now
			)
		;

	private:
		struct CRepositorySlug
		{
			CStr m_Owner;
			CStr m_Name;
		};

		struct CCustomRepositoryRoleCache
		{
			CCustomRepositoryRoleCache();

			TCMap<CStr, CStr> m_NameToID;
			bool m_bCustomGotten = false;
		};

		struct CFieldTranslations
		{
			CStr const *f_GetMalterlibField(CStr const &_GitHubField) const;

			CFieldTranslationPair const *m_pFields = nullptr;
			umint m_nFields = 0;
		};

		template <umint t_nFields>
		struct TCFieldTranslations : public CFieldTranslations
		{
			constexpr TCFieldTranslations(CFieldTranslationPair const (&_Pairs)[t_nFields])
				: CFieldTranslations
				{
					.m_pFields = _Pairs
					, .m_nFields = t_nFields
				}
			{
			}
		};

#ifdef DCompiler_MSVC_Workaround
		template <umint t_nFields>
		static CFieldTranslations fsp_FieldTranslations(CFieldTranslationPair const (&_Pairs)[t_nFields]);
#else
		template <TCFieldTranslations t_Fields>
		static CFieldTranslations fsp_FieldTranslations();
#endif

		TCFuture<CJsonSorted> fp_GraphQlApi(CStr _Query, CJsonSorted _Variables);

		TCFuture<CJsonSorted> fp_RestApi
			(
				CStr _Path
				, CStr _ErrorDescription
				, TCMap<CStr, CStr> _QueryParams = {}
				, uint32 _EmptyStatus = 0
				, uint32 _ExpectedStatus = 200
				, CStr _PagedArrayMember = {}
				, umint _MaxItems = 0
			)
		;
		TCFuture<CJsonSorted> fp_RestApiPost(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus = 201);
		TCFuture<CJsonSorted> fp_RestApiPatch(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription,CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus = 200);
		TCFuture<void> fp_RestApiPut(CStr _Path, CJsonSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus = 204);
		TCFuture<void> fp_RestApiDelete(CStr _Path, CStr _ErrorDescription, uint32 _ExpectedStatus = 204);

		TCFuture<CJsonSorted> fp_RestApiUploadFile
			(
				CStr _Path
				, TCActorFunctor<TCFuture<CByteVector> (umint _nBytes)> _fReadData
				, uint64 _DataSize
				, CStr _ErrorDescription
				, uint32 _ExpectedStatus = 201
			)
		;
		TCFuture<void> fp_RestApiDownloadFile
			(
				CStr _Path
				, TCActorFunctor<TCFuture<void> (CByteVector _Data)> _fWriteData
				, CStr _ErrorDescription
				, uint32 _ExpectedStatus = 200
				, CStr _Accept = "application/octet-stream"
			)
		;
		TCFuture<void> fp_PublicDownloadFile(CStr _Url, TCActorFunctor<TCFuture<void> (CByteVector _Data)> _fWriteData, CStr _ErrorDescription, uint32 _ExpectedStatus = 200);

		TCFuture<CJsonSorted> fp_PopulateGraphQl_BranchProtectionRule(CStr _Organization, CBranchProtectionRule _Rule);
		TCFuture<CJsonSorted> fp_PopulateRest_GenericRuleset(CStr _Organization, CGenericRuleset _Ruleset);

		template <typename tf_CActor>
		TCFuture<CStr> fp_GetActorIDGeneric(CStr _Organization, tf_CActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache);
		TCFuture<CStr> fp_GetActorID(CStr _Organization, CGitActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache);
		TCFuture<CStr> fp_GetActorID(CStr _Organization, CGenericRuleGitActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache);
		TCFuture<CStr> fp_GetAppID(CApp _App, bool _bGraphQL);
		TCFuture<CStr> fp_GetRepositoryID(CStr _Repository, bool _bGraphQL);

		TCFuture<CRepositorySlug> fp_SplitRepositorySlug(CStr _Repository);

		static CStr fsp_GetJsonString(CJsonSorted const &_Json, CStr const &_Member);
		static uint64 fsp_GetJsonUnsigned(CJsonSorted const &_Json, CStr const &_Member);
		static bool fsp_GetJsonBool(CJsonSorted const &_Json, CStr const &_Member);
		static NTime::CTime fsp_GetJsonTime(CJsonSorted const &_Json, CStr const &_Member);
		static CStr fsp_GetJsonUserLogin(CJsonSorted const &_Json);
		static CJsonSorted::CArray const &fsp_GetJsonArray(CJsonSorted const &_Json, CStr const &_Member);

		static CWorkflowRun fsp_ParseWorkflowRun(CJsonSorted const &_Json);
		static CWorkflowJob fsp_ParseWorkflowJob(CJsonSorted const &_Json);
		static CPullRequestFeedback fsp_ParsePullRequestFeedbackItem(CJsonSorted const &_Json, EPullRequestFeedbackKind _Kind);
		TCFuture<void> fp_EnsureAccessToken();
		TCFuture<void> fp_RenewAccessToken();
		TCFuture<CStr> fp_GetInstallationID(CStr _Owner, CStr _Repository);
		TCFuture<CJsonSorted> fp_AppApi
			(
				CHttpClientActor::EMethod _Method
				, CStr _Path
				, CStr _ErrorDescription
				, uint32 _ExpectedStatus
				, CJsonSorted _Body = CJsonSorted(EJsonType_Object)
			)
		;
		TCFuture<CUser> fp_GetAppUser();
		TCFuture<TCMap<CStr, CStr>> fp_GetRestHeaders();
		static TCMap<CStr, CStr> fsp_GetPublicRestHeaders();
		CExceptionPointer fp_GetRestError(CStr const &_Description, CHttpClientActor::CResult const &_Result, CFieldTranslations const &_FieldTranslation);

		// GitHub's API answers within seconds, so a request that gets almost nothing for this long has stalled and would otherwise wait forever
		static constexpr fp64 mc_StallTimeout = 120.0;

		TCActor<CHttpClientActor> mp_HttpClientActor{fg_Construct(CHttpClientActor::CCertificateConfig{}, mc_StallTimeout)};
		CStr mp_Token;
		uint64 mp_LoginGeneration = 0; // Changes with every f_Login, so a token renewal that a new login overlapped is discarded
		TCOptional<CAppLogin> mp_AppLogin; // Set when logged in as a GitHub App, which renews mp_Token for its installation
		TCOptional<TCVector<TCPromise<void>>> mp_AccessTokenRenewalWaiters; // Set while a renewal runs, with the calls waiting for it
	};
}

#include "Malterlib_Git_HostingProvider_GitHub.hpp"
