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

	/// An API call authenticated as the GitHub App itself rather than as one of its installations. Only GET and POST are needed; a POST
	/// sends _PostBody, or an empty object when it is unset.
	auto CGitHostingProvider_GitHub::fp_AppApi(CHttpClientActor::EMethod _Method, CStr _Path, CStr _ErrorDescription, uint32 _ExpectedStatus, CJsonSorted _PostBody) -> TCFuture<CJsonSorted>
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

		auto Result
			= _Method == CHttpClientActor::EMethod_POST
			? co_await mp_HttpClientActor(&CHttpClientActor::f_Post, Url, fg_Move(Headers), _PostBody.f_IsValid() ? fg_Move(_PostBody) : CJsonSorted(EJsonType_Object))
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

	/// The installation the GitHub App login names or finds from its Repository or Owner. A login that selects none uses the repository
	/// _FallbackOwner/_FallbackRepository, or the organization _FallbackOwner when _FallbackRepository is empty, and fails without
	/// _FallbackOwner or when the installation GitHub finds isn't on _FallbackOwner's account. Fallback installations are not kept: the next
	/// request can target another installation.
	TCFuture<CStr> CGitHostingProvider_GitHub::fp_InstallationID(CStr _FallbackOwner, CStr _FallbackRepository)
	{
		if (!mp_AppLogin)
			co_return DMibErrorInstance("Not logged in as a GitHub App");

		if (!mp_AppLogin->m_InstallationID.f_IsEmpty())
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
		else if (_FallbackOwner)
		{
			bFromLogin = false;
			Target = _FallbackRepository ? CStr("{}/{}"_f << _FallbackOwner << _FallbackRepository) : _FallbackOwner;

			auto CaptureScope = co_await g_CaptureExceptions;
			Path = fs_FallbackInstallationPath(_FallbackOwner, _FallbackRepository);
		}
		else
			co_return DMibErrorInstance("The GitHub App login has no InstallationID, Repository or Owner to find the installation from");

		auto Installation = co_await fp_AppApi(CHttpClientActor::EMethod_GET, Path, "Failed to find the GitHub App installation for {}"_f << Target, 200);

		if (mp_LoginGeneration != LoginGeneration || !mp_AppLogin)
			co_return DMibErrorInstance("The GitHub login changed while the GitHub App installation was being looked up");

		uint64 ID = fsp_GetJsonUnsigned(Installation, "id");
		if (!ID)
			co_return DMibErrorInstance("GitHub returned no installation id for the GitHub App on {}"_f << Target);

		CStr InstallationID = "{}"_f << ID;

		// GitHub answers the lookup of a repository or organization with the installation on its account
		if (!bFromLogin)
		{
			CStr Account;
			if (auto *pAccount = Installation.f_GetMember("account", EJsonType_Object))
				Account = fsp_GetJsonString(*pAccount, "login");

			if (Account.f_IsEmpty())
				co_return DMibErrorInstance("GitHub returned no account login for the GitHub App installation on {}"_f << Target);

			if (auto pError = fs_CheckAccessTokenOwner(_FallbackOwner, Account))
				co_return fg_Move(pError);
		}
		else
		{
			mp_AppLogin->m_InstallationID = InstallationID;
			mp_AppLogin->m_InstallationTargetType = fsp_GetJsonString(Installation, "target_type");
			if (auto *pAccount = Installation.f_GetMember("account", EJsonType_Object))
				mp_AppLogin->m_InstallationOwner = fsp_GetJsonString(*pAccount, "login");
		}

		co_return InstallationID;
	}

	/// The path that looks up the GitHub App installation of the repository _Owner/_Repository, or of the organization _Owner when _Repository is
	/// empty. Throws for a name that isn't one path component of the characters GitHub allows in owner and repository names, which could make it
	/// another endpoint.
	CStr CGitHostingProvider_GitHub::fs_FallbackInstallationPath(CStr const &_Owner, CStr const &_Repository)
	{
		auto fCheckName = [](CStr const &_Name, ch8 const *_pKind)
			{
				bool bValid = !_Name.f_IsEmpty() && _Name != "." && _Name != "..";
				for (aint iChar = 0; bValid && iChar < _Name.f_GetLen(); ++iChar)
				{
					ch8 Char = _Name[iChar];
					bValid = (Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z') || (Char >= '0' && Char <= '9') || Char == '-' || Char == '_' || Char == '.';
				}

				if (!bValid)
					DMibError("'{}' isn't a valid GitHub {} name"_f << _Name << _pKind);
			}
		;

		fCheckName(_Owner, "owner");
		if (_Repository.f_IsEmpty())
			return "orgs/{}/installation"_f << _Owner;

		fCheckName(_Repository, "repository");
		return "repos/{}/{}/installation"_f << _Owner << _Repository;
	}

	/// The owner of the installation a GitHub App login selects, when it is known without asking GitHub: the account found for
	/// m_InstallationID, or for a login without an installation ID the owner of the Repository or Owner, in the order fp_InstallationID
	/// looks them up. Empty for a login whose installation ID has no account found yet, since the ID can name another account's
	/// installation than Repository and Owner do, or for a login that selects none.
	CStr CGitHostingProvider_GitHub::fs_LoginInstallationOwner(CAppLogin const &_Login)
	{
		if (_Login.m_InstallationOwner)
			return _Login.m_InstallationOwner;

		if (_Login.m_InstallationID)
			return {};

		if (_Login.m_Repository)
		{
			auto iSlash = _Login.m_Repository.f_FindChar('/');
			if (iSlash >= 0)
				return _Login.m_Repository.f_Left(iSlash);

			return _Login.m_Repository;
		}

		return _Login.m_Owner;
	}

	/// An error when a request names an owner other than the owner of the login's installation. An empty _RequestOwner takes the login's
	/// installation, and an empty _InstallationOwner, for a login that selects none, takes the request's owner. GitHub compares owners
	/// ignoring case.
	CExceptionPointer CGitHostingProvider_GitHub::fs_CheckAccessTokenOwner(CStr const &_RequestOwner, CStr const &_InstallationOwner)
	{
		if (_RequestOwner.f_IsEmpty() || _InstallationOwner.f_IsEmpty() || _RequestOwner.f_CmpNoCase(_InstallationOwner) == 0)
			return nullptr;

		return DMibErrorInstance("The access token request is for the owner '{}', but the GitHub App login's installation belongs to '{}'"_f << _RequestOwner << _InstallationOwner);
	}

	/// An error when a request limits the token's repositories or permissions for an installation whose target type, GitHub's target_type,
	/// is Enterprise: GitHub gives the token of an enterprise installation all of the installation's permissions whatever the request asks
	/// for. An empty _InstallationTargetType, for an installation found by its repository or organization, is never an enterprise one.
	CExceptionPointer CGitHostingProvider_GitHub::fs_CheckAccessTokenScope(bool _bScoped, CStr const &_InstallationTargetType)
	{
		if (!_bScoped || _InstallationTargetType.f_CmpNoCase("Enterprise") != 0)
			return nullptr;

		return DMibErrorInstance
			(
				"The access token request limits repositories or permissions, but the GitHub App login's installation is an enterprise installation, "
				"whose tokens GitHub can't limit"
			)
		;
	}

	/// Records the account and target type of the installation m_InstallationID names, looking it up by ID. Callers use the returned values
	/// instead of rereading mp_AppLogin, which a login that overlaps their await can replace or clear.
	auto CGitHostingProvider_GitHub::fp_LookUpLoginInstallation() -> TCFuture<CLoginInstallation>
	{
		if (!mp_AppLogin)
			co_return DMibErrorInstance("Not logged in as a GitHub App");

		uint64 LoginGeneration = mp_LoginGeneration;
		CStr InstallationID = mp_AppLogin->m_InstallationID;

		auto Installation = co_await fp_AppApi
			(
				CHttpClientActor::EMethod_GET
				, "app/installations/{}"_f << InstallationID
				, "Failed to get the GitHub App installation {}"_f << InstallationID
				, 200
			)
		;

		if (mp_LoginGeneration != LoginGeneration || !mp_AppLogin)
			co_return DMibErrorInstance("The GitHub login changed while the GitHub App installation was being looked up");

		CStr TargetType = fsp_GetJsonString(Installation, "target_type");
		if (TargetType.f_IsEmpty())
			co_return DMibErrorInstance("GitHub returned no target type for the GitHub App installation {}"_f << InstallationID);

		mp_AppLogin->m_InstallationTargetType = TargetType;

		CLoginInstallation LoginInstallation;
		LoginInstallation.m_TargetType = TargetType;

		// The account of an enterprise installation is an enterprise, which has no login
		if (auto *pAccount = Installation.f_GetMember("account", EJsonType_Object))
		{
			if (CStr Account = fsp_GetJsonString(*pAccount, "login"))
			{
				mp_AppLogin->m_InstallationOwner = Account;
				LoginInstallation.m_Owner = Account;
			}
		}

		co_return fg_Move(LoginInstallation);
	}

	/// The account that owns the installation the GitHub App login selects, empty for a login that selects none. An installation ID
	/// whose account isn't known yet is looked up.
	TCFuture<CStr> CGitHostingProvider_GitHub::fp_InstallationOwner()
	{
		if (!mp_AppLogin)
			co_return DMibErrorInstance("Not logged in as a GitHub App");

		// The installation ID decides what the token covers, so an owner found earlier is kept also after the account is renamed: a request
		// naming the old name gets a token for the renamed account, never for an account that claims the old name. f_CreateAccessToken looks
		// the account up again when a request names another owner
		CStr Owner = fs_LoginInstallationOwner(*mp_AppLogin);
		if (Owner || mp_AppLogin->m_InstallationID.f_IsEmpty())
			co_return Owner;

		CStr InstallationID = mp_AppLogin->m_InstallationID;

		auto LoginInstallation = co_await fp_LookUpLoginInstallation();

		if (LoginInstallation.m_Owner.f_IsEmpty())
			co_return DMibErrorInstance("GitHub returned no account login for the GitHub App installation {} of type {}"_f << InstallationID << LoginInstallation.m_TargetType);

		co_return fg_Move(LoginInstallation.m_Owner);
	}

	/// Fails a request that limits the token for the login's own installation when that is an enterprise installation. A token for a fallback
	/// installation, which a request finds by its repository or organization, is never for an enterprise installation.
	TCFuture<void> CGitHostingProvider_GitHub::fp_CheckLoginInstallationScope(bool _bScoped)
	{
		if (!_bScoped || !mp_AppLogin || mp_AppLogin->m_InstallationID.f_IsEmpty())
			co_return {};

		CStr TargetType = mp_AppLogin->m_InstallationTargetType;
		if (TargetType.f_IsEmpty())
			TargetType = (co_await fp_LookUpLoginInstallation()).m_TargetType;

		if (auto pError = fs_CheckAccessTokenScope(_bScoped, TargetType))
			co_return fg_Move(pError);

		co_return {};
	}

	/// An error when a permission of _Permissions has a level that isn't a string, such as an unset value, which can't be sent to GitHub. A
	/// non-object keeps the installation's permissions.
	CExceptionPointer CGitHostingProvider_GitHub::fs_CheckAccessTokenPermissions(CJsonSorted const &_Permissions)
	{
		if (!_Permissions.f_IsObject())
			return nullptr;

		for (auto &Permission : _Permissions.f_Object())
		{
			if (!Permission.f_Value().f_IsString())
				return DMibErrorInstance("The access token request's permission '{}' needs a level such as \"read\""_f << Permission.f_Name());
		}

		return nullptr;
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

		CStr InstallationID = co_await fp_InstallationID({}, {});

		auto Token = co_await fp_AppApi(CHttpClientActor::EMethod_POST, "app/installations/{}/access_tokens"_f << InstallationID, "Failed to create a GitHub App installation token", 201);

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

	CJsonSorted CGitHostingProvider_GitHub::fs_RestrictedAccessTokenJson(CRestrictedAccessTokenRequest const &_Request)
	{
		CJsonSorted Json(EJsonType_Object);

		if (!_Request.m_Permissions.f_IsEmpty())
		{
			auto &Permissions = Json["permissions"];
			Permissions = CJsonSorted(EJsonType_Object);
			for (auto &Level : _Request.m_Permissions)
				Permissions[_Request.m_Permissions.fs_GetKey(Level)] = Level;
		}

		if (!_Request.m_Repositories.f_IsEmpty())
		{
			auto &Repositories = Json["repositories"];
			Repositories = CJsonSorted(EJsonType_Array);
			for (auto &Repository : _Request.m_Repositories)
				Repositories.f_Insert(Repository);
		}

		return Json;
	}

	/// A new installation token with at most the permissions and repositories of _Request. GitHub refuses a permission the installation
	/// doesn't have, and can't limit the token of an enterprise installation, for which a limited request fails. The provider keeps using
	/// its own token.
	auto CGitHostingProvider_GitHub::f_CreateRestrictedAccessToken(CRestrictedAccessTokenRequest _Request) -> TCFuture<CAccessToken>
	{
		if (!mp_AppLogin)
			co_return DMibErrorInstance("A restricted access token needs a GitHub App login");

		uint64 LoginGeneration = mp_LoginGeneration;

		CStr InstallationID = co_await fp_InstallationID({}, {});

		co_await fp_CheckLoginInstallationScope(!_Request.m_Permissions.f_IsEmpty() || !_Request.m_Repositories.f_IsEmpty());

		if (mp_LoginGeneration != LoginGeneration || !mp_AppLogin)
			co_return DMibErrorInstance("The GitHub login changed while the GitHub App installation was being looked up");

		auto Token = co_await fp_AppApi
			(
				CHttpClientActor::EMethod_POST
				, "app/installations/{}/access_tokens"_f << InstallationID
				, "Failed to create a restricted GitHub App installation token"
				, 201
				, fs_RestrictedAccessTokenJson(_Request)
			)
		;

		CAccessToken AccessToken;
		AccessToken.m_Token = fsp_GetJsonString(Token, "token");
		AccessToken.m_Expires = fsp_GetJsonTime(Token, "expires_at");
		if (AccessToken.m_Token.f_IsEmpty() || !AccessToken.m_Expires.f_IsValid())
			co_return DMibErrorInstance("GitHub returned no installation token or expiry for the restricted GitHub App token");

		co_return fg_Move(AccessToken);
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

	/// A new installation token, separate from the one the provider authenticates with, optionally limited to some repositories and permissions.
	/// GitHub can't limit the token of an enterprise installation, so a limited request for a login whose installation is one fails.
	/// If the login selects no installation, _Request.m_Owner must name the installation's owner. If it selects one, a non-empty
	/// _Request.m_Owner must be that installation's owner.
	auto CGitHostingProvider_GitHub::f_CreateAccessToken(CCreateAccessToken _Request) -> TCFuture<CAccessToken>
	{
		if (!mp_AppLogin)
			co_return DMibErrorInstance("Creating a GitHub access token needs a GitHub App login");

		// The HTTP client generates the body on its actor, where a value that can't be generated would throw
		if (auto pError = fs_CheckAccessTokenPermissions(_Request.m_Permissions))
			co_return fg_Move(pError);

		uint64 LoginGeneration = mp_LoginGeneration;

		// The token is for the installation the login selects, whatever the account is called now. A request naming the account's name from
		// when it was looked up is that installation's owner also after a rename, and when another account claims the old name the token still
		// only covers the installation's repositories, never that account's: a URL with the old name then names that account's repository
		// whatever token it is used with, and this token adds nothing to what that URL reaches. Only a request naming another owner needs the current name.
		if (_Request.m_Owner)
		{
			CStr InstallationOwner = co_await fp_InstallationOwner();
			if (fs_CheckAccessTokenOwner(_Request.m_Owner, InstallationOwner))
			{
				// The owner found earlier is the account's name at the time, so an account renamed since then is looked up again by the
				// installation ID
				co_await fp_InstallationID({}, {});
				auto LoginInstallation = co_await fp_LookUpLoginInstallation();
				if (LoginInstallation.m_Owner.f_IsEmpty())
				{
					co_return DMibErrorInstance
						(
							"The access token request is for the owner '{}', but the GitHub App login's installation of type {} has no account login"_f
							<< _Request.m_Owner
							<< LoginInstallation.m_TargetType
						)
					;
				}

				if (auto pError = fs_CheckAccessTokenOwner(_Request.m_Owner, LoginInstallation.m_Owner))
					co_return fg_Move(pError);
			}
		}

		CStr FirstRepository = _Request.m_Repositories.f_IsEmpty() ? CStr() : _Request.m_Repositories[0];
		CStr InstallationID = co_await fp_InstallationID(_Request.m_Owner, FirstRepository);

		co_await fp_CheckLoginInstallationScope(!_Request.m_Repositories.f_IsEmpty() || _Request.m_Permissions.f_IsObject());

		// The installation ID and the JWT must come from the same login
		if (mp_LoginGeneration != LoginGeneration || !mp_AppLogin)
			co_return DMibErrorInstance("The GitHub login changed while the GitHub App installation was being looked up");

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
		// The response comes from api.github.com, whose RFC 3339 timestamps have four-digit years, inside the range of CTime, so fs_ParseTime,
		// which throws for a year outside it, doesn't throw here
		AccessToken.m_Expires = fsp_GetJsonTime(Token, "expires_at");
		if (AccessToken.m_Token.f_IsEmpty() || !AccessToken.m_Expires.f_IsValid())
			co_return DMibErrorInstance("GitHub returned no installation token or expiry for the GitHub App");

		co_return fg_Move(AccessToken);
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
