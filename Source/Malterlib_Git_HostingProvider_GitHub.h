// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
		TCFuture<TCVector<CRepository>> f_GetRepositories(TCVector<CStr> const &_Organizations, bool _bPersonal) override;
		TCFuture<TCMap<CStr, CBranchProtectionRule>> f_GetBranchProtectionRules(CStr const &_Repository) override;
		TCFuture<void> f_UpdateBranchProtectionRule(CStr const &_Repository, CStr const &_RuleID, CBranchProtectionRule const &_Rule) override;
		TCFuture<CStr> f_CreateBranchProtectionRule(CStr const &_Repository, CBranchProtectionRule const &_Rule) override;
		TCFuture<void> f_DeleteBranchProtectionRule(CStr const &_Repository, CStr const &_RuleID) override;
		TCFuture<CRepositoryPermissions> f_GetRepositoryPermissions(CStr const &_Repository) override;
		TCFuture<void> f_AddRepositoryPermissions(NStr::CStr const &_Repository, CRepositoryPermissions const &_Permissions) override;
		TCFuture<void> f_RemoveRepositoryPermissions(CStr const &_Repository, TCSet<CStr> const &_Teams, TCSet<CStr> const &_Users) override;

	private:
		struct CRepositorySlug
		{
			CStr m_Owner;
			CStr m_Name;
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
		TCFuture<CStr> fp_GetActorID(CStr _Organization, CGitHostingProvider::CGitActor _Actor);
		TCFuture<CStr> fp_GetAppID(CGitHostingProvider::CApp _App);
		TCFuture<CStr> fp_GetRepositoryID(CStr _Repository);
		TCFuture<CRepositorySlug> fp_SplitRepositorySlug(CStr _Repository);
		TCMap<CStr, CStr> fp_GetRestHeaders(bool _bAuthorize = true);
		CExceptionPointer fp_GetRestError(CStr const &_Description, CCurlActor::CResult const &_Result, CFieldTranslations const &_FieldTranslation);

		TCActor<CCurlActor> mp_CurlActor{fg_Construct(), "Curl Actor"};
		CStr mp_Token;
	};
}

#include "Malterlib_Git_HostingProvider_GitHub.hpp"
