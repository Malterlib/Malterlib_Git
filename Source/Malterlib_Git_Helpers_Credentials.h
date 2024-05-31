// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
