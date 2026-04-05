// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Web/HTTP/URL>

namespace NMib::NGit
{
	TCFuture<CStr> fg_GetGitCredentials(NWeb::NHTTP::CURL _Url, CStr _WorkingDirectory);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif
