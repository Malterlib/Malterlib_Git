// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Process/ProcessLaunchActor>

namespace NMib::NGit
{
	TCFuture<CStr> fg_LaunchGit(TCVector<CStr> _Params, CStr _WorkingDirectory, CSystemEnvironment _Environment = {});
	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> fg_LaunchGitWithResult(TCVector<CStr> _Params, CStr _WorkingDirectory, CSystemEnvironment _Environment = {});
	TCFuture<CStr> fg_LaunchGitSendStdIn(TCVector<CStr> _Params, CStr _StdIn, CStr _WorkingDirectory, CSystemEnvironment _Environment = {});
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif
