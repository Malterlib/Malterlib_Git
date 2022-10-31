// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider.h"

#include <Mib/Web/Curl>

namespace NMib::NGit
{
	struct CGitHostingProvider_GitHub : public CGitHostingProvider
	{
		TCFuture<void> f_Login(CEJSON const &_LoginDetails) override;
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

		TCFuture<CJSON> fp_GraphQlApi(CStr _Query, CJSON _Variables);
		TCFuture<CJSON> fp_RestApi(CStr _Path, TCMap<CStr, CStr> _QueryParams = {}, uint32 _ExpectedStatus = 200);
		TCFuture<void> fp_RestApiPut(CStr _Path, CJSON _Value, uint32 _ExpectedStatus = 204);
		TCFuture<void> fp_RestApiDelete(CStr _Path, uint32 _ExpectedStatus = 204);
		TCFuture<CJSON> fp_PopulateGraphQl_BranchProtectionRule(CStr _Organization, CBranchProtectionRule _Rule);
		TCFuture<CStr> fp_GetActorID(CStr _Organization, CGitHostingProvider::CGitActor _Actor);
		TCFuture<CStr> fp_GetAppID(CGitHostingProvider::CApp _App);
		TCFuture<CStr> fp_GetRepositoryID(CStr _Repository);
		TCFuture<CRepositorySlug> fp_SplitRepositorySlug(CStr _Repository);

		TCActor<CCurlActor> mp_CurlActor{fg_Construct(), "Curl Actor"};
		CStr mp_Token;
	};
}
