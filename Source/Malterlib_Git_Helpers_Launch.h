// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Process/ProcessLaunchActor>

namespace NMib::NGit
{
	NConcurrency::TCFuture<NStr::CStr> fg_LaunchGit(NContainer::TCVector<NStr::CStr> _Params, NStr::CStr _WorkingDirectory, CSystemEnvironment _Environment = {});
	auto fg_LaunchGitWithResult(NContainer::TCVector<NStr::CStr> _Params, NStr::CStr _WorkingDirectory, CSystemEnvironment _Environment = {})
		-> NConcurrency::TCFuture<NProcess::CProcessLaunchActor::CSimpleLaunchResult>
	;
	NConcurrency::TCFuture<NStr::CStr> fg_LaunchGitSendStdIn(NContainer::TCVector<NStr::CStr> _Params, NStr::CStr _StdIn, NStr::CStr _WorkingDirectory, CSystemEnvironment _Environment = {});
}

#ifndef DMibPNoShortCuts
using namespace NMib::NGit;
#endif
