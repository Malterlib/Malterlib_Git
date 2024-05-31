// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Git/Helpers/Launch>

namespace NMib::NGit
{
	TCFuture<CStr> fg_GetGitCredentials(NWeb::NHTTP::CURL _Url, CStr _WorkingDirectory)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;

		CStr CredentialFillQuery =
			"protocol={}\n"
			"host={}\n"
			"path={}\n"_f
			<< _Url.f_GetScheme()
			<< _Url.f_GetHost()
			<< _Url.f_GetFullPath()
		;

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
