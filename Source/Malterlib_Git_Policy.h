// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Git/HostingProvider>

namespace NMib::NGit
{
	struct CGitPolicyActor : public NConcurrency::CActor
	{
		struct CApplyPolicyContext
		{
			NStr::CStr m_Repository;
			NConcurrency::TCActor<CGitHostingProvider> m_HostingProvider;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NStr::CStr _Name, NStr::CStr _CreatedValues)> m_fOnCreate;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NStr::CStr _Name, NStr::CStr _UpdatedValues)> m_fOnUpdate;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NStr::CStr _Name, NStr::CStr _DeletedValues)> m_fOnDelete;
			// A listed ruleset left alone: an update would lose _Reasons
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NStr::CStr _Name, NStr::CStr _Reasons)> m_fOnUnmanageable;
			bool m_bPretend = true;
			bool m_bCreateMissing = false;
		};

		CGitPolicyActor();
		~CGitPolicyActor();

		NConcurrency::TCFuture<bool> f_ApplyPolicy_Repository(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _RepositorySettings);
		NConcurrency::TCFuture<void> f_ApplyPolicy_Permissions(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _Permissions);
		NConcurrency::TCFuture<void> f_ApplyPolicy_BranchProtection(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _BranchProtection);
		NConcurrency::TCFuture<bool> f_ApplyPolicy_GenericRules(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _Rules);
		NConcurrency::TCFuture<void> f_ApplyPolicy_ActionsSettings(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _RepositorySettings);
		auto f_ApplyPolicy_Rules(CApplyPolicyContext _BranchProtectionContext, CApplyPolicyContext _GenericRulesContext, NEncoding::CEJsonSorted _Policy) -> NConcurrency::TCFuture<bool>;

	private:
		NConcurrency::TCUnsafeFuture<void> fp_ApplyPolicy_BranchProtection(CApplyPolicyContext const &_Context, NEncoding::CEJsonSorted const &_BranchProtection);
		NConcurrency::TCUnsafeFuture<bool> fp_ApplyPolicy_GenericRules(CApplyPolicyContext const &_Context, NEncoding::CEJsonSorted const &_Rules);
		NConcurrency::TCUnsafeFuture<void> fp_RemoveOther_BranchProtection(CApplyPolicyContext const &_Context, NEncoding::CEJsonSorted const &_BranchProtection);
		NConcurrency::TCUnsafeFuture<void> fp_RemoveOther_GenericRules(CApplyPolicyContext const &_Context, NEncoding::CEJsonSorted const &_Rules);
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NGit;
#endif
