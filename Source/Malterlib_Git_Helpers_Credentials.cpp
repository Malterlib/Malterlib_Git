// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Core/System>
#include <Mib/Git/Helpers/Launch>

namespace NMib::NGit
{
	TCFuture<CStr> fg_GetGitCredentials(NWeb::NHTTP::CURL _Url, CStr _WorkingDirectory, bool _bAllowInteractive)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;

		CStr CredentialFillQuery ="url={}\n"_f << _Url.f_Encode();

		if (_Url.f_HasUsername())
			CredentialFillQuery += "username={}\n"_f << _Url.f_GetUsername();

		// Force `git credential fill` to be non-interactive. GIT_TERMINAL_PROMPT=0
		// disables terminal prompts. We additionally point GIT_ASKPASS and
		// SSH_ASKPASS at `true`, a no-op program that exits 0 with empty
		// stdout, but only when the parent environment supplies *neither*
		// variable. This suppresses GUI prompts configured via `core.askpass`
		// (the common case on macOS where an IDE installs a graphical askpass
		// script into the system git config) by giving git a silent answer
		// rather than relying on a failing fallback chain — empirically, a
		// failing askpass program (`false`) does not reliably suppress
		// downstream prompts on macOS, while `true` does.
		//
		// CI systems (GitHub Actions, GitLab CI, etc.) wire one or both env
		// vars to non-interactive scripts that hand back tokens; because git
		// consults GIT_ASKPASS before SSH_ASKPASS, overriding either when only
		// the other is inherited would short-circuit the working fallback —
		// hence the "neither inherited" guard.
		//
		// Tradeoff: with `true` as askpass, `git credential fill` exits 0 with
		// an empty password whenever no creds are available, so a broken
		// credential helper is indistinguishable from a missing one — both
		// produce an empty token. Callers that proceed unauthenticated on an
		// empty token (e.g. BuildSystem probes, LFS public-repo download) work
		// correctly; callers that need auth see the eventual provider 401/404
		// rather than the original helper error. A setup with a non-interactive
		// `core.askpass` script *and* no askpass env vars will be masked here;
		// such users should either configure `credential.helper` (the
		// conventional non-interactive path) or export GIT_ASKPASS pointing at
		// their script.
		CSystemEnvironment Env;
		Env["GIT_TERMINAL_PROMPT"] = "0";
		auto *pSystem = fg_GetSys();
		bool bHasGitAskPass = false;
		bool bHasSshAskPass = false;
		pSystem->f_GetEnvironmentVariable("GIT_ASKPASS", {}, &bHasGitAskPass);
		pSystem->f_GetEnvironmentVariable("SSH_ASKPASS", {}, &bHasSshAskPass);

		auto fEnvVarIsTruthy = [&](CStr const &_Name)
			{
				auto Value = pSystem->f_GetEnvironmentVariable(_Name);
				return Value == "1" || Value.f_CmpNoCase("true") == 0;
			}
		;
		auto fEnvVarIsSet = [&](CStr const &_Name)
			{
				bool bExists = false;
				auto Value = pSystem->f_GetEnvironmentVariable(_Name, {}, &bExists);
				return bExists && !Value.f_IsEmpty();
			}
		;

		auto fIsAutomatedBuild = [&]
			{
				return fEnvVarIsTruthy("RunningCI")
					|| fEnvVarIsTruthy("MalterlibBuildServerBuild")
					|| fEnvVarIsTruthy("BUILDSERVER")
					|| fEnvVarIsTruthy("CI")
					|| fEnvVarIsTruthy("GITHUB_ACTIONS")
					|| fEnvVarIsTruthy("GITLAB_CI")
					|| fEnvVarIsTruthy("TF_BUILD")
					|| fEnvVarIsTruthy("BUILDKITE")
					|| fEnvVarIsTruthy("CIRCLECI")
					|| fEnvVarIsTruthy("TRAVIS")
					|| fEnvVarIsTruthy("APPVEYOR")
					|| fEnvVarIsSet("TEAMCITY_VERSION")
					|| fEnvVarIsSet("JENKINS_URL")
				;
			}
		;

		if ((!bHasGitAskPass && !bHasSshAskPass) || (!_bAllowInteractive && !fIsAutomatedBuild()))
		{
			Env["GIT_ASKPASS"] = "true";
			Env["SSH_ASKPASS"] = "true";
		}

		auto Credentials = (co_await fg_LaunchGitSendStdIn({"credential", "fill"}, CredentialFillQuery, _WorkingDirectory, fg_Move(Env))).f_SplitLine();

		for (auto &Line : Credentials)
		{
			if (Line.f_StartsWith("password="))
				co_return Line.f_RemovePrefix("password=");
		}

		co_return {};
	}
}
