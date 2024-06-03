// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include "Malterlib_Git_HostingProvider.h"

#include <Mib/Web/Curl>

namespace NMib::NGit
{
	struct CGitHostingProvider_GitHub : public CGitHostingProvider
	{
		struct CFieldTranslationPair
		{
#ifndef DCompiler_MSVC_Workaround
			constexpr 
#endif
			COrdering_Strong operator <=> (CFieldTranslationPair const &_Right) const
			{
				return fg_StrCmpConstExpr(m_GitHubField.m_pStr, _Right.m_GitHubField.m_pStr) <=> 0;
			}

#ifndef DCompiler_MSVC_Workaround
			constexpr 
#endif
			COrdering_Strong operator <=> (CStr const &_Right) const
			{
				return m_GitHubField.m_Str <=> _Right;
			}

			TCStrConst<NStr::CStr> m_GitHubField;
			NStr::CStr m_MalterlibField;
		};
		
		TCFuture<void> f_Login(CEJSONSorted const &_LoginDetails) override;
		TCFuture<CGetRepository> f_CreateRepository(CCreateRepository &&_CreateRepository) override;
		TCFuture<CGetRepository> f_ForkRepository(CStr const &_Repository, CForkRepository &&_ForkRepository) override;
		TCFuture<CGetRepository> f_UpdateRepository(CStr const &_Repository, CRepository &&_RepositorySettings) override;
		TCFuture<TCVector<CGetRepository>> f_GetRepositories(TCVector<CStr> const &_Organizations, bool _bPersonal) override;
		TCFuture<CGetRepository> f_GetRepository(CStr const &_Repository) override;

		TCFuture<TCMap<CStr, CBranchProtectionRule>> f_GetBranchProtectionRules(CStr const &_Repository) override;
		TCFuture<void> f_UpdateBranchProtectionRule(CStr const &_Repository, CStr const &_RuleID, CBranchProtectionRule const &_Rule) override;
		TCFuture<CStr> f_CreateBranchProtectionRule(CStr const &_Repository, CBranchProtectionRule const &_Rule) override;
		TCFuture<void> f_DeleteBranchProtectionRule(CStr const &_Repository, CStr const &_RuleID) override;

		TCFuture<CRepositoryPermissions> f_GetRepositoryPermissions(CStr const &_Repository) override;
		TCFuture<void> f_AddRepositoryPermissions(CStr const &_Repository, CRepositoryPermissions const &_Permissions) override;
		TCFuture<void> f_RemoveRepositoryPermissions(CStr const &_Repository, TCSet<CStr> const &_Teams, TCSet<CStr> const &_Users) override;

		TCFuture<CRelease> f_CreateRelease(CStr const &_Repository, CCreateRelease const &_CreateRelease) override;
		TCFuture<TCOptional<CRelease>> f_GetRelease(CStr const &_Repository, CStr const &_ReleaseTag) override;
		TCFuture<void> f_DeleteRelease(NStr::CStr const &_Repository, NStr::CStr const &_ReleaseID) override;
		TCFuture<TCVector<CRelease>> f_GetReleases(CStr const &_Repository) override;

		TCFuture<CReleaseAsset> f_UploadReleaseAsset(CStr const &_Repository, CStr const &_ReleaseIdentifier, CUploadReleaseAsset &&_UploadRelease) override;
		TCFuture<void> f_DownloadReleaseAsset(CStr const &_Repository, CDownloadReleaseAsset &&_DownloadRelease) override;
		TCFuture<void> f_DownloadPublicReleaseAsset(CStr const &_Repository, CDownloadPublicReleaseAsset &&_DownloadRelease) override;
		TCFuture<void> f_DeleteReleaseAsset(CStr const &_Repository, NStr::CStr const &_Identifier) override;
		TCFuture<CStr> f_GetPublicReleaseAssetUrl(CStr const &_Repository, CStr const &_TagName, CStr const &_AssetName) override;

		TCFuture<TCMap<CStr, CGenericRuleset>> f_GetGenericRulesets(CStr const &_Repository) override;
		TCFuture<CGenericRuleset> f_PopulateGenericRulesetIDs(CStr const &_Repository, CGenericRuleset &&_Ruleset) override;
		TCFuture<void> f_UpdateGenericRuleset(CStr const &_Repository, CStr const &_ID, CGenericRuleset const &_Ruleset) override;
		TCFuture<CStr> f_CreateGenericRuleset(CStr const &_Repository, CGenericRuleset const &_Ruleset) override;
		TCFuture<void> f_DeleteGenericRuleset(CStr const &_Repository, CStr const &_ID) override;

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
			mint m_nFields = 0;
		};

		template <mint t_nFields>
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
		template <mint t_nFields>
		static CFieldTranslations fsp_FieldTranslations(CFieldTranslationPair const (&_Pairs)[t_nFields]);
#else
		template <TCFieldTranslations t_Fields>
		static CFieldTranslations fsp_FieldTranslations();
#endif

		TCFuture<CJSONSorted> fp_GraphQlApi(CStr _Query, CJSONSorted _Variables);

		TCFuture<CJSONSorted> fp_RestApi(CStr _Path, CStr _ErrorDescription, TCMap<CStr, CStr> _QueryParams = {}, uint32 _EmptyStatus = 0, uint32 _ExpectedStatus = 200);
		TCFuture<CJSONSorted> fp_RestApiPost(CStr _Path, CJSONSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus = 201);
		TCFuture<CJSONSorted> fp_RestApiPatch(CStr _Path, CJSONSorted _Value, CStr _ErrorDescription,CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus = 200);
		TCFuture<void> fp_RestApiPut(CStr _Path, CJSONSorted _Value, CStr _ErrorDescription, CFieldTranslations _FieldTranslation, uint32 _ExpectedStatus = 204);
		TCFuture<void> fp_RestApiDelete(CStr _Path, CStr _ErrorDescription, uint32 _ExpectedStatus = 204);

		TCFuture<CJSONSorted> fp_RestApiUploadFile
			(
				CStr _Path
				, TCActorFunctor<TCFuture<CByteVector> (mint _nBytes)> _fReadData
				, uint64 _DataSize
				, CStr _ErrorDescription
				, uint32 _ExpectedStatus = 201
			)
		;
		TCFuture<void> fp_RestApiDownloadFile(CStr _Path, TCActorFunctor<TCFuture<void> (CByteVector &&_Data)> _fWriteData, CStr _ErrorDescription, uint32 _ExpectedStatus = 200);
		TCFuture<void> fp_PublicDownloadFile(CStr _Url, TCActorFunctor<TCFuture<void> (CByteVector &&_Data)> _fWriteData, CStr _ErrorDescription, uint32 _ExpectedStatus = 200);

		TCFuture<CJSONSorted> fp_PopulateGraphQl_BranchProtectionRule(CStr _Organization, CBranchProtectionRule _Rule);
		TCFuture<CJSONSorted> fp_PopulateRest_GenericRuleset(CStr _Organization, CGenericRuleset _Ruleset);

		template <typename tf_CActor>
		TCFuture<CStr> fp_GetActorIDGeneric(CStr _Organization, tf_CActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache);
		TCFuture<CStr> fp_GetActorID(CStr _Organization, CGitActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache);
		TCFuture<CStr> fp_GetActorID(CStr _Organization, CGenericRuleGitActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache);
		TCFuture<CStr> fp_GetAppID(CApp _App, bool _bGraphQL);
		TCFuture<CStr> fp_GetRepositoryID(CStr _Repository, bool _bGraphQL);

		TCFuture<CRepositorySlug> fp_SplitRepositorySlug(CStr _Repository);
		TCMap<CStr, CStr> fp_GetRestHeaders(bool _bAuthorize = true);
		CExceptionPointer fp_GetRestError(CStr const &_Description, CCurlActor::CResult const &_Result, CFieldTranslations const &_FieldTranslation);

		TCActor<CCurlActor> mp_CurlActor{fg_Construct(), "Curl Actor"};
		CStr mp_Token;
	};
}

#include "Malterlib_Git_HostingProvider_GitHub.hpp"
