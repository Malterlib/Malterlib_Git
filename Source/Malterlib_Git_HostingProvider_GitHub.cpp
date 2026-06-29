// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Cryptography/PublicCrypto>
#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Time/Time>

#include "Malterlib_Git_GitHubApp.h"

namespace NMib::NGit
{
	namespace
	{
		int64 fg_NowUnixSeconds()
		{
			return int64(NTime::CTimeConvert(NTime::CTime::fs_NowUTC()).f_UnixSeconds());
		}
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_Login(CEJsonSorted _LoginDetails)
	{
		// A plain token (a personal access token or a pre-minted installation token).
		mp_Token = _LoginDetails.f_GetMemberValue("Token", "").f_String();

		// GitHub App credentials: an app/client id plus the app private key (PEM). This enables f_CreateAccessToken,
		// and — when an installation id is configured — REST calls authenticated as that installation.
		CStr AppID = _LoginDetails.f_GetMemberValue("AppID", "").f_String();
		if (!AppID)
			AppID = _LoginDetails.f_GetMemberValue("ClientID", "").f_String();

		CStr PrivateKeyPem = _LoginDetails.f_GetMemberValue("PrivateKey", "").f_String();

		if (AppID && PrivateKeyPem)
		{
			{
				auto CaptureScope = co_await (g_CaptureExceptions % "Failed to parse GitHub App private key");
				mp_AppPrivateKeyDer = NCryptography::CPublicCrypto::fs_ConvertPrivateKeyPemToDer(NContainer::CSecureByteVector((uint8 const *)PrivateKeyPem.f_GetStr(), PrivateKeyPem.f_GetLen()));
			}

			mp_AppID = AppID;
			mp_InstallationID = _LoginDetails.f_GetMemberValue("InstallationID", "").f_String();
			if (auto ApiBaseUrl = _LoginDetails.f_GetMemberValue("ApiBaseUrl", "").f_String())
				mp_ApiBaseUrl = ApiBaseUrl;

			// With a known installation and no explicit token, mint a full installation token so the REST methods can
			// operate as that installation.
			if (mp_InstallationID && !mp_Token)
			{
				CAccessToken Token = co_await f_CreateAccessToken({});
				mp_Token = fg_Move(Token.m_Token);
			}
		}

		co_return {};
	}

	bool CGitHostingProvider_GitHub::fp_HasAppCredentials() const
	{
		return mp_AppID && !mp_AppPrivateKeyDer.f_IsEmpty();
	}

	CStr CGitHostingProvider_GitHub::fp_BuildAppJwt()
	{
		// Back-date iat by 60s to tolerate clock skew between this host and GitHub, per GitHub's guidance.
		int64 Now = fg_NowUnixSeconds();
		return fg_BuildGitHubAppJwt(mp_AppID, mp_AppPrivateKeyDer, Now - 60, Now + gc_GitHubAppJwtLifetimeSeconds);
	}

	TCMap<CStr, CStr> CGitHostingProvider_GitHub::fp_GetAppJwtHeaders(CStr const &_Jwt)
	{
		TCMap<CStr, CStr> Headers = fp_GetRestHeaders(false);
		Headers["Authorization"] = "Bearer {}"_f << _Jwt;
		return Headers;
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_ResolveInstallationID(CStr _Jwt, CStr _Owner, CStr _Repository)
	{
		if (!_Owner)
			co_return CStr();

		// A repository pins the installation precisely; otherwise fall back to the owner's org installation.
		CStr Path = _Repository ? ("repos/{}/{}/installation"_f << _Owner << _Repository) : ("orgs/{}/installation"_f << _Owner);

		auto Result = co_await mp_HttpClientActor(&CHttpClientActor::f_Get, "{}/{}"_f << mp_ApiBaseUrl << Path, fp_GetAppJwtHeaders(_Jwt));
		if (Result.m_StatusCode != 200)
			co_return fp_GetRestError("Resolve GitHub App installation", Result, {});

		auto CaptureScope = co_await g_CaptureExceptions;

		CJsonSorted Json = CJsonSorted::fs_FromString(Result.m_Body);
		if (auto pId = Json.f_GetMember("id"))
		{
			if (pId->f_IsInteger())
				co_return "{}"_f << pId->f_Integer();

			co_return pId->f_AsString({});
		}

		co_return CStr();
	}

	TCFuture<CGitHostingProvider::CAccessToken> CGitHostingProvider_GitHub::f_CreateAccessToken(CCreateAccessToken _Request)
	{
		if (!fp_HasAppCredentials())
			co_return DMibErrorInstance("Cannot create a GitHub access token without GitHub App credentials (call f_Login with AppID and PrivateKey)");

		CStr Jwt;
		{
			auto CaptureScope = co_await (g_CaptureExceptions % "Failed to build GitHub App JWT");
			Jwt = fp_BuildAppJwt();
		}

		CStr InstallationID = mp_InstallationID;
		if (!InstallationID)
		{
			CStr FirstRepository = _Request.m_Repositories.f_IsEmpty() ? CStr() : _Request.m_Repositories[0];
			InstallationID = co_await fp_ResolveInstallationID(Jwt, _Request.m_Owner, FirstRepository);
		}

		if (!InstallationID)
			co_return DMibErrorInstance("Cannot resolve a GitHub App installation id; configure InstallationID or provide an owner");

		// Scope the token: an empty repository list yields a full installation token; otherwise the token is limited
		// to the named repositories, and the permission map narrows it further (e.g. write access in one repository).
		CJsonSorted Body(EJsonType_Object);
		if (!_Request.m_Repositories.f_IsEmpty())
		{
			auto &Repositories = Body["repositories"];
			for (auto &Repository : _Request.m_Repositories)
				Repositories.f_Insert(Repository);
		}
		if (_Request.m_Permissions.f_IsObject())
			Body["permissions"] = fg_Move(_Request.m_Permissions);

		auto Result = co_await mp_HttpClientActor
			(
				&CHttpClientActor::f_Post
				, "{}/app/installations/{}/access_tokens"_f << mp_ApiBaseUrl << InstallationID
				, fp_GetAppJwtHeaders(Jwt)
				, CJsonSorted(fg_Move(Body))
			)
		;

		if (Result.m_StatusCode != 201)
			co_return fp_GetRestError("Create GitHub installation access token", Result, {});

		CAccessToken Token;
		{
			auto CaptureScope = co_await g_CaptureExceptions;
			CJsonSorted Json = CJsonSorted::fs_FromString(Result.m_Body);
			Token.m_Token = Json.f_GetMemberValue("token", CStr()).f_String();
		}

		if (!Token.m_Token)
			co_return DMibErrorInstance("GitHub installation access token response did not contain a token");

		// GitHub installation tokens last one hour; report a slightly earlier expiry so callers refresh in time.
		Token.m_ExpiresUnixTime = fg_NowUnixSeconds() + gc_GitHubInstallationTokenLifetimeSeconds;

		co_return Token;
	}

	DMibGitHostingProviderRegister(CGitHostingProvider_GitHub);
}
