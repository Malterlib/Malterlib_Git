// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Git/Helpers/Launch>

namespace NMib::NGit
{
	TCFuture<CStr> fg_GetGitCredentials(NWeb::NHTTP::CURL _Url, CStr _WorkingDirectory)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;

		CStr CredentialFillQuery ="url={}\n"_f << _Url.f_Encode();

		if (_Url.f_HasUsername())
			CredentialFillQuery += "username={}\n"_f << _Url.f_GetUsername();

		auto Credentials = (co_await fg_LaunchGitSendStdIn({"credential", "fill"}, CredentialFillQuery, _WorkingDirectory)).f_SplitLine();

		for (auto &Line : Credentials)
		{
			if (Line.f_StartsWith("password="))
				co_return Line.f_RemovePrefix("password=");
		}

		co_return {};
	}
}
