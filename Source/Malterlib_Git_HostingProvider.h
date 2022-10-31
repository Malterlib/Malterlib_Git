// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib


#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/Core/RuntimeType>

namespace NMib::NGit
{
	struct CGitHostingProvider;

	struct ICGitHostingProviderFactory
	{
		virtual ~ICGitHostingProviderFactory() = default;
		virtual TCActor<CGitHostingProvider> operator () () = 0;
	};

	struct CGitHostingProvider : public NConcurrency::CActor
	{
		struct CHostingProviderDescription
		{
			NStr::CStr m_Name;
			NStr::CStr m_ClassName;
		};

		struct CUser
		{
			COrdering_Partial operator <=> (CUser const &_Right) const;
			bool operator == (CUser const &_Right) const;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Login;
			NStr::CStr m_ID;
			NStr::CStr m_Name;
		};

		struct CApp
		{
			COrdering_Partial operator <=> (CApp const &_Right) const;
			bool operator == (CApp const &_Right) const;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Slug;
			NStr::CStr m_ID;
			NStr::CStr m_Name;
		};

		struct CTeam
		{
			COrdering_Partial operator <=> (CTeam const &_Right) const;
			bool operator == (CTeam const &_Right) const;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Slug;
			NStr::CStr m_ID;
			NStr::CStr m_Name;
		};

		struct CRequiredStatusCheck
		{
			auto operator <=> (CRequiredStatusCheck const &_Right) const = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Context;
			NStorage::TCOptional<CApp> m_App;
		};

		using CGitActor = NStorage::TCVariant<CUser, CTeam, CApp>;

		struct CBranchProtectionRule
		{
			NStorage::TCOptional<NStr::CStr> m_Pattern;
			NStorage::TCOptional<NStr::CStr> m_Creator;

			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_PushAllowances;
			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_ReviewDismissalAllowances;
			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_BypassForcePushAllowances;
			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_BypassPullRequestAllowances;

			NStorage::TCOptional<NContainer::TCVector<NStr::CStr>> m_RequiredStatusCheckContexts;
			NStorage::TCOptional<NContainer::TCVector<CRequiredStatusCheck>> m_RequiredStatusChecks;

			NStorage::TCOptional<uint32> m_RequiredApprovingReviewCount;

			NStorage::TCOptional<bool> m_AllowsForcePushes;
			NStorage::TCOptional<bool> m_AllowsDeletions;
			NStorage::TCOptional<bool> m_BlocksCreations;
			NStorage::TCOptional<bool> m_DismissesStaleReviews;
			NStorage::TCOptional<bool> m_IsAdminEnforced;
			NStorage::TCOptional<bool> m_RequiresApprovingReviews;
			NStorage::TCOptional<bool> m_RequiresCodeOwnerReviews;
			NStorage::TCOptional<bool> m_RequiresCommitSignatures;
			NStorage::TCOptional<bool> m_RequiresConversationResolution;
			NStorage::TCOptional<bool> m_RequiresLinearHistory;
			NStorage::TCOptional<bool> m_RequiresStatusChecks;
			NStorage::TCOptional<bool> m_RequiresStrictStatusChecks;
			NStorage::TCOptional<bool> m_RestrictsPushes;
			NStorage::TCOptional<bool> m_RestrictsReviewDismissals;

			bool f_IsUpdated(CBranchProtectionRule const &_Wanted, CStr &o_UpdateValues) const;
		};

		struct CRepositoryPermissions
		{
			NContainer::TCMap<NStr::CStr, NStr::CStr> m_TeamPermissions;
			NContainer::TCMap<NStr::CStr, NStr::CStr> m_UserPermissions;
		};

		struct CRepository
		{
			NStr::CStr m_Name;
			NStorage::TCOptional<NStr::CStr> m_DefaultBranch;
			NStorage::TCOptional<bool> m_IsPrivate;
		};

		CGitHostingProvider();

		virtual NConcurrency::TCFuture<void> f_Login(CEJSON const &_LoginDetails) = 0;
		virtual NConcurrency::TCFuture<NContainer::TCVector<CRepository>> f_GetRepositories(NContainer::TCVector<NStr::CStr> const &_Organizations, bool _bPersonal) = 0;
		virtual NConcurrency::TCFuture<NContainer::TCMap<NStr::CStr, CBranchProtectionRule>> f_GetBranchProtectionRules(NStr::CStr const &_Repository) = 0;
		virtual NConcurrency::TCFuture<void> f_UpdateBranchProtectionRule(NStr::CStr const &_Repository, NStr::CStr const &_RuleID, CBranchProtectionRule const &_Rule) = 0;
		virtual NConcurrency::TCFuture<NStr::CStr> f_CreateBranchProtectionRule(NStr::CStr const &_Repository, CBranchProtectionRule const &_Rule) = 0;
		virtual NConcurrency::TCFuture<void> f_DeleteBranchProtectionRule(NStr::CStr const &_Repository, NStr::CStr const &_RuleID) = 0;
		virtual NConcurrency::TCFuture<CRepositoryPermissions> f_GetRepositoryPermissions(NStr::CStr const &_Repository) = 0;
		virtual NConcurrency::TCFuture<void> f_AddRepositoryPermissions(NStr::CStr const &_Repository, CRepositoryPermissions const &_Permissions) = 0;
		virtual NConcurrency::TCFuture<void> f_RemoveRepositoryPermissions
			(
				NStr::CStr const &_Repository
				, NContainer::TCSet<NStr::CStr> const &_Teams
				, NContainer::TCSet<NStr::CStr> const &_Users
			) = 0
		;

		static NContainer::TCMap<NStr::CStr, NStr::CStr> fs_EnumHostingProviders();
		static NConcurrency::TCActor<CGitHostingProvider> fs_CreateHostingProvider(NStr::CStr const &_ClassName);
	};
}

#define	DMibGitHostingProviderRegister(d_ClassName) \
	class CGitHostingProviderFactory_##d_ClassName : public NMib::NGit::ICGitHostingProviderFactory \
	{ \
		NConcurrency::TCActor<CGitHostingProvider> operator () () override; \
	}; \
	DMibRuntimeClassNamedCasted \
		( \
			NMib::NGit::ICGitHostingProviderFactory \
			, CGitHostingProviderFactory_##d_ClassName \
			, CGitHostingProviderFactory_##d_ClassName \
			, NMib::NGit::ICGitHostingProviderFactory \
		) \
	NConcurrency::TCActor<CGitHostingProvider> CGitHostingProviderFactory_##d_ClassName::operator ()() \
	{ \
		return fg_Construct<d_ClassName>(); \
	} \
	void fg_Malterlib_MakeActive_##d_ClassName() \
	{ \
		DMibRuntimeClassMakeActive(CGitHostingProviderFactory_##d_ClassName); \
	}

#define	DMibGitHostingProviderMakeActiveDefine(d_ClassName) extern void fg_Malterlib_MakeActive_##d_ClassName()
#define	DMibGitHostingProviderMakeActive(d_ClassName) fg_Malterlib_MakeActive_##d_ClassName()

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif

#include "Malterlib_Git_HostingProvider.hpp"
