// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_LfsReleaseStore.h"

namespace NMib::NGit
{
	CLfsReleaseStoreService::CLfsReleaseStoreService(NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine, CStr const &_WorkingDirectory)
		: mp_pCommandLine(_pCommandLine)
		, mp_WorkingDirectory(_WorkingDirectory)
	{
	}

	TCFuture<void> CLfsReleaseStoreService::f_WaitForExit()
	{
		co_return co_await mp_ExitPromise.f_Future();
	}

	TCFuture<void> CLfsReleaseStoreService::fp_Destroy()
	{
		if (mp_CancelSubscription)
			co_await fg_Exchange(mp_CancelSubscription, nullptr)->f_Destroy();

		if (mp_StdSubscription)
			co_await fg_Exchange(mp_StdSubscription, nullptr)->f_Destroy();

		if (!mp_ExitPromise.f_IsSet())
			mp_ExitPromise.f_SetResult();

		if (mp_HostingProvider)
			co_await fg_Move(mp_HostingProvider).f_Destroy();

		co_return {};
	};
}
