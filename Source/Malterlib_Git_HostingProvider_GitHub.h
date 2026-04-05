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
				return fg_StrCmpConstExpr(m_GitHubField.m_pStr, _Right.m_GitHubField.m_pStr) <=> 0;
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

		TCFuture<void> f_Login(CEJsonSorted _LoginDetails) override;
		TCFuture<CGetRepository> f_CreateRepository(CCreateRepository _CreateRepository) override;
		TCFuture<CGetRepository> f_ForkRepository(CStr _Repository, CForkRepository _ForkRepository) override;
		TCFuture<CGetRepository> f_UpdateRepository(CStr _Repository, CRepository _RepositorySettings) override;
		TCFuture<TCVector<CGetRepository>> f_GetRepositories(TCVector<CStr> _Organizations, bool _bPersonal) override;
		TCFuture<CGetRepository> f_GetRepository(CStr _Repository) override;

		TCFuture<TCMap<CStr, CBranchProtectionRule>> f_GetBranchProtectionRules(CStr _Repository) override;
		TCFuture<void> f_UpdateBranchProtectionRule(CStr _Repository, CStr _RuleID, CBranchProtectionRule _Rule) override;
		TCFuture<CStr> f_CreateBranchProtectionRule(CStr _Repository, CBranchProtectionRule _Rule) override;
		TCFuture<void> f_DeleteBranchProtectionRule(CStr _Repository, CStr _RuleID) override;

		TCFuture<CRepositoryPermissions> f_GetRepositoryPermissions(CStr _Repository) override;
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

		TCFuture<CJsonSorted> fp_RestApi(CStr _Path, CStr _ErrorDescription, TCMap<CStr, CStr> _QueryParams = {}, uint32 _EmptyStatus = 0, uint32 _ExpectedStatus = 200);
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
		TCFuture<void> fp_RestApiDownloadFile(CStr _Path, TCActorFunctor<TCFuture<void> (CByteVector _Data)> _fWriteData, CStr _ErrorDescription, uint32 _ExpectedStatus = 200);
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
		TCMap<CStr, CStr> fp_GetRestHeaders(bool _bAuthorize = true);
		CExceptionPointer fp_GetRestError(CStr const &_Description, CHttpClientActor::CResult const &_Result, CFieldTranslations const &_FieldTranslation);

		TCActor<CHttpClientActor> mp_HttpClientActor{fg_Construct(), "HTTP client Actor"};
		CStr mp_Token;
	};
}

#include "Malterlib_Git_HostingProvider_GitHub.hpp"
