// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Git/HostingProvider>

namespace NMib::NGit
{
	struct CGitPolicyActor : public CActor
	{
		CGitPolicyActor();
		~CGitPolicyActor();

		struct CApplyPolicyContext
		{
			NStr::CStr m_Repository;
			NConcurrency::TCActor<CGitHostingProvider> m_HostingProvider;
			NConcurrency::TCActorFunctor<TCFuture<void> (CStr _Name, CStr _CreatedValues)> m_fOnCreate;
			NConcurrency::TCActorFunctor<TCFuture<void> (CStr _Name, CStr _UpdatedValues)> m_fOnUpdate;
			NConcurrency::TCActorFunctor<TCFuture<void> (CStr _Name, CStr _DeletedValues)> m_fOnDelete;
			bool m_bPretend = true;
			bool m_bCreateMissing = false;
		};

		NConcurrency::TCFuture<bool> f_ApplyPolicy_Repository(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _RepositorySettings);
		NConcurrency::TCFuture<void> f_ApplyPolicy_Permissions(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _Permissions);
		NConcurrency::TCFuture<void> f_ApplyPolicy_BranchProtection(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _BranchProtection);
		NConcurrency::TCFuture<void> f_ApplyPolicy_GenericRules(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _Rules);
		NConcurrency::TCFuture<void> f_ApplyPolicy_ActionsSettings(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _RepositorySettings);
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif
