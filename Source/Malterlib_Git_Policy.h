// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
			NConcurrency::TCActorFunctor<TCFuture<void> (CStr &&_Name, CStr &&_CreatedValues)> m_fOnCreate;
			NConcurrency::TCActorFunctor<TCFuture<void> (CStr &&_Name, CStr &&_UpdatedValues)> m_fOnUpdate;
			NConcurrency::TCActorFunctor<TCFuture<void> (CStr &&_Name, CStr &&_DeletedValues)> m_fOnDelete;
			bool m_bPretend = true;
			bool m_bCreateMissing = false;
		};

		NConcurrency::TCFuture<bool> f_ApplyPolicy_Repository(CApplyPolicyContext &&_Context, NEncoding::CEJSONSorted &&_RepositorySettings);
		NConcurrency::TCFuture<void> f_ApplyPolicy_Permissions(CApplyPolicyContext &&_Context, NEncoding::CEJSONSorted &&_Permissions);
		NConcurrency::TCFuture<void> f_ApplyPolicy_BranchProtection(CApplyPolicyContext &&_Context, NEncoding::CEJSONSorted &&_BranchProtection);
		NConcurrency::TCFuture<void> f_ApplyPolicy_GenericRules(CApplyPolicyContext &&_Context, NEncoding::CEJSONSorted &&_Rules);
		NConcurrency::TCFuture<void> f_ApplyPolicy_ActionsSettings(CApplyPolicyContext &&_Context, NEncoding::CEJSONSorted &&_RepositorySettings);
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif
