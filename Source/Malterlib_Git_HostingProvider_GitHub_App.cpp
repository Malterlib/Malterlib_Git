// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Cryptography/PublicCrypto>
#include <Mib/Encoding/Base64>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		// GitHub rejects an App JWT that expires more than 10 minutes out, or that is issued ahead of its own clock
		constexpr uint64 gc_AppJwtBackdateSeconds = 60;
		constexpr uint64 gc_AppJwtLifetimeSeconds = 540;

		// An installation token lives for an hour. Renewing it this early means a request that starts with it finishes before it expires.
		constexpr fp64 gc_AccessTokenRenewMargin = 300.0;

		template <typename tf_CSource>
		CStr fg_Base64UrlEncode(tf_CSource const &_Source)
		{
			return fg_Base64Encode(_Source).f_TrimRight("=").f_ReplaceChar('+', '-').f_ReplaceChar('/', '_');
		}
	}

	/// The JSON Web Token a GitHub App authenticates its own API calls with: RS256 over the App ID with the DER private key, issued a
	/// minute before _Now to allow for clock drift
	CStr CGitHostingProvider_GitHub::fs_CreateAppJwt(CStr const &_AppID, CSecureByteVector const &_PrivateKey, NTime::CTime const &_Now)
	{
		uint64 Now = NTime::CTimeConvert(_Now).f_UnixSeconds();

		CJsonSorted Header =
			{
				"alg"_j= "RS256"
				, "typ"_j= "JWT"
			}
		;

		CJsonSorted Payload =
			{
				"iat"_j= int64(Now - gc_AppJwtBackdateSeconds)
				, "exp"_j= int64(Now + gc_AppJwtLifetimeSeconds)
				, "iss"_j= _AppID
			}
		;

		CStr SigningInput = "{}.{}"_f << fg_Base64UrlEncode(Header.f_ToString(nullptr)) << fg_Base64UrlEncode(Payload.f_ToString(nullptr));

		CSecureByteVector ToSign;
		ToSign.f_Insert((uint8 const *)SigningInput.f_GetStr(), SigningInput.f_GetLen());

		auto Signature = NCryptography::CPublicCrypto::fs_SignMessageRawSignature(ToSign, _PrivateKey, NCryptography::EDigestType_SHA256);

		return "{}.{}"_f << SigningInput << fg_Base64UrlEncode(Signature);
	}

	/// An API call authenticated as the GitHub App itself rather than as one of its installations. Only GET and POST are needed;
	/// _Body is only sent with POST.
	TCFuture<CJsonSorted> CGitHostingProvider_GitHub::fp_AppApi
		(
			CHttpClientActor::EMethod _Method
			, CStr _Path
			, CStr _ErrorDescription
			, uint32 _ExpectedStatus
			, CJsonSorted _Body
		)
	{
		DMibRequire(!_Path.f_StartsWith("/"));
		DMibRequire(_Method == CHttpClientActor::EMethod_GET || _Method == CHttpClientActor::EMethod_POST);

		if (!mp_AppLogin)
			co_return DMibErrorInstance("{}: not logged in as a GitHub App"_f << _ErrorDescription);

		TCMap<CStr, CStr> Headers = fsp_GetPublicRestHeaders();
		{
			auto CaptureScope = co_await (g_CaptureExceptions % CStr("{}: could not sign the GitHub App token"_f << _ErrorDescription));
			Headers["Authorization"] = "Bearer {}"_f << fs_CreateAppJwt(mp_AppLogin->m_AppID, mp_AppLogin->m_PrivateKey, NTime::CTime::fs_NowUTC());
		}

		CStr Url = NWeb::NHTTP::CURL("https://api.github.com/{}"_f << _Path).f_Encode();

		auto Result = _Method == CHttpClientActor::EMethod_POST
			? co_await mp_HttpClientActor(&CHttpClientActor::f_Post, Url, fg_Move(Headers), fg_Move(_Body))
			: co_await mp_HttpClientActor(&CHttpClientActor::f_Get, Url, fg_Move(Headers))
		;

		if (Result.m_StatusCode != _ExpectedStatus)
			co_return fp_GetRestError("{}: GitHub App request"_f << _ErrorDescription, Result, {});

		auto CaptureScope = co_await g_CaptureExceptions;

		co_return CJsonSorted::fs_FromString(Result.m_Body);
	}

	/// Creates a new installation token when logged in as a GitHub App and the current one is missing or about to expire
	TCFuture<void> CGitHostingProvider_GitHub::fp_EnsureAccessToken()
	{
		if (!mp_AppLogin)
			co_return {};

		auto RenewTime = NTime::CTime::fs_NowUTC() + NTime::CTimeSpanConvert::fs_CreateSpanFromSeconds(gc_AccessTokenRenewMargin);
		if (mp_Token && mp_AppLogin->m_TokenExpires.f_IsValid() && RenewTime < mp_AppLogin->m_TokenExpires)
			co_return {};

		// Calls that need a token while one is being created wait for that one instead of each creating another
		if (mp_AccessTokenRenewalWaiters)
		{
			TCPromiseFuturePair<void> Pair;
			auto Future = fg_Move(Pair.m_Future);
			mp_AccessTokenRenewalWaiters->f_Insert(fg_Move(Pair.m_Promise));
			co_await fg_Move(Future);
			co_return {};
		}

		mp_AccessTokenRenewalWaiters.f_CreateNew();
		auto Result = co_await fp_RenewAccessToken().f_Wrap();

		auto Waiters = fg_Move(*mp_AccessTokenRenewalWaiters);
		mp_AccessTokenRenewalWaiters.f_Clear();
		for (auto &Promise : Waiters)
		{
			if (Result)
				Promise.f_SetResult();
			else
				Promise.f_SetException(Result.f_GetException());
		}

		if (!Result)
			co_return Result.f_GetException();

		co_return {};
	}

	/// The installation the GitHub App login names, or else the one of _Owner, or of the repository _Owner/_Repository. An installation
	/// found from the login is kept with the login.
	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetInstallationID(CStr _Owner, CStr _Repository)
	{
		if (!mp_AppLogin)
			co_return DMibErrorInstance("Not logged in as a GitHub App");

		if (mp_AppLogin->m_InstallationID)
			co_return mp_AppLogin->m_InstallationID;

		uint64 LoginGeneration = mp_LoginGeneration;
		bool bFromLogin = true;

		CStr Path;
		CStr Target;
		if (mp_AppLogin->m_Repository)
		{
			Target = mp_AppLogin->m_Repository;
			Path = "repos/{}/installation"_f << Target;
		}
		else if (mp_AppLogin->m_Owner)
		{
			Target = mp_AppLogin->m_Owner;
			Path = "orgs/{}/installation"_f << Target;
		}
		else if (_Owner)
		{
			bFromLogin = false;
			if (_Repository)
			{
				Target = "{}/{}"_f << _Owner << _Repository;
				Path = "repos/{}/installation"_f << Target;
			}
			else
			{
				Target = _Owner;
				Path = "orgs/{}/installation"_f << Target;
			}
		}
		else
			co_return DMibErrorInstance("The GitHub App login has no InstallationID, Repository or Owner to find the installation from");

		auto Installation = co_await fp_AppApi(CHttpClientActor::EMethod_GET, Path, "Failed to find the GitHub App installation for {}"_f << Target, 200);

		uint64 ID = fsp_GetJsonUnsigned(Installation, "id");
		if (!ID)
			co_return DMibErrorInstance("GitHub returned no installation id for the GitHub App on {}"_f << Target);

		CStr InstallationID = "{}"_f << ID;

		if (bFromLogin && mp_AppLogin && mp_LoginGeneration == LoginGeneration)
			mp_AppLogin->m_InstallationID = InstallationID;

		co_return InstallationID;
	}

	/// Creates a new installation token for the GitHub App login
	TCFuture<void> CGitHostingProvider_GitHub::fp_RenewAccessToken()
	{
		uint64 LoginGeneration = mp_LoginGeneration;
		auto fLoginChanged = [&]
			{
				return mp_LoginGeneration != LoginGeneration || !mp_AppLogin;
			}
		;

		CStr InstallationID = co_await fp_GetInstallationID({}, {});

		if (fLoginChanged())
			co_return DMibErrorInstance("The GitHub login changed while the GitHub App installation was being looked up");

		auto Token = co_await fp_AppApi
			(
				CHttpClientActor::EMethod_POST
				, "app/installations/{}/access_tokens"_f << InstallationID
				, "Failed to create a GitHub App installation token"
				, 201
			)
		;

		CStr NewToken = fsp_GetJsonString(Token, "token");
		NTime::CTime Expires = fsp_GetJsonTime(Token, "expires_at");
		if (NewToken.f_IsEmpty() || !Expires.f_IsValid())
			co_return DMibErrorInstance("GitHub returned no installation token or expiry for the GitHub App");

		// A login while the token was being created replaced this App or installation, so the token belongs to no one
		if (fLoginChanged())
			co_return DMibErrorInstance("The GitHub login changed while a GitHub App installation token was being created");

		mp_AppLogin->m_InstallationID = InstallationID;
		mp_AppLogin->m_TokenExpires = Expires;
		mp_Token = NewToken;

		co_return {};
	}

	auto CGitHostingProvider_GitHub::f_GetAccessToken() -> TCFuture<CAccessToken>
	{
		co_await fp_EnsureAccessToken();

		CAccessToken AccessToken;
		AccessToken.m_Token = mp_Token;
		if (mp_AppLogin)
			AccessToken.m_Expires = mp_AppLogin->m_TokenExpires;

		co_return fg_Move(AccessToken);
	}

	auto CGitHostingProvider_GitHub::f_CreateAccessToken(CCreateAccessToken _Request) -> TCFuture<CAccessToken>
	{
		if (!mp_AppLogin)
			co_return DMibErrorInstance("Creating a GitHub access token needs a GitHub App login");

		CStr FirstRepository = _Request.m_Repositories.f_IsEmpty() ? CStr() : _Request.m_Repositories[0];
		CStr InstallationID = co_await fp_GetInstallationID(_Request.m_Owner, FirstRepository);

		CJsonSorted Body(EJsonType_Object);
		if (!_Request.m_Repositories.f_IsEmpty())
		{
			auto &Repositories = Body["repositories"];
			for (auto &Repository : _Request.m_Repositories)
				Repositories.f_Insert(Repository);
		}

		if (_Request.m_Permissions.f_IsObject())
			Body["permissions"] = fg_Move(_Request.m_Permissions);

		auto Token = co_await fp_AppApi
			(
				CHttpClientActor::EMethod_POST
				, "app/installations/{}/access_tokens"_f << InstallationID
				, "Failed to create a GitHub App installation token"
				, 201
				, fg_Move(Body)
			)
		;

		CAccessToken AccessToken;
		AccessToken.m_Token = fsp_GetJsonString(Token, "token");
		AccessToken.m_Expires = fsp_GetJsonTime(Token, "expires_at");
		if (AccessToken.m_Token.f_IsEmpty() || !AccessToken.m_Expires.f_IsValid())
			co_return DMibErrorInstance("GitHub returned no installation token or expiry for the GitHub App");

		co_return fg_Move(AccessToken);
	}

	auto CGitHostingProvider_GitHub::f_ListInstallationRepositories() -> TCFuture<TCVector<CInstallationRepository>>
	{
		auto Result = co_await fp_RestApi("installation/repositories", "Failed to list the repositories of the GitHub App installation", {}, 0, 200, "repositories");

		auto CaptureScope = co_await g_CaptureExceptions;

		TCVector<CInstallationRepository> Repositories;
		for (auto &Repository : fsp_GetJsonArray(Result, "repositories"))
		{
			CStr Name = fsp_GetJsonString(Repository, "name");
			if (Name.f_IsEmpty())
				continue;

			auto &Entry = Repositories.f_Insert();
			Entry.m_Name = fg_Move(Name);
			Entry.m_CloneUrl = fsp_GetJsonString(Repository, "clone_url");
			Entry.m_WebUrl = fsp_GetJsonString(Repository, "html_url");
			if (auto *pOwner = Repository.f_GetMember("owner", EJsonType_Object))
				Entry.m_Owner = fsp_GetJsonString(*pOwner, "login");
		}

		co_return fg_Move(Repositories);
	}

	/// The bot user GitHub acts as for the App's installations, `<app-slug>[bot]`, which is the author of what the App creates
	auto CGitHostingProvider_GitHub::fp_GetAppUser() -> TCFuture<CUser>
	{
		auto App = co_await fp_AppApi(CHttpClientActor::EMethod_GET, "app", "Failed to get the authenticated GitHub App", 200);

		CStr Slug = fsp_GetJsonString(App, "slug");
		if (Slug.f_IsEmpty())
			co_return DMibErrorInstance("GitHub returned no slug for the authenticated GitHub App");

		auto BotUser = co_await fp_RestApi("users/{}[bot]"_f << Slug, "Failed to get the GitHub user of the GitHub App {}"_f << Slug);

		CUser User;
		User.m_Login = fsp_GetJsonString(BotUser, "login");
		User.m_ID = "{}"_f << fsp_GetJsonUnsigned(BotUser, "id");
		User.m_Name = fsp_GetJsonString(App, "name");

		co_return fg_Move(User);
	}
}
