// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Cryptography/PublicCrypto>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	/// _LoginDetails is {"Token": "..."} for a token GitHub issued to a user, or a GitHub App installation:
	/// {"App": {"AppID": "...", "PrivateKey": "<PEM>", "InstallationID": "..."}}. In place of "InstallationID", the App can set
	/// "Repository": "owner/name" or the organization "Owner" to look the installation up; "InstallationID" comes first, then "Repository",
	/// then "Owner". An App login without any of them can't renew its token. An App login can also pass the installation token it already
	/// holds as "Token" beside "App", with its expiry as the date "TokenExpires", and the token is used until it expires within
	/// gc_AccessTokenRenewMargin seconds.
	TCFuture<void> CGitHostingProvider_GitHub::f_Login(CEJsonSorted _LoginDetails)
	{
		++mp_LoginGeneration;
		mp_Token = _LoginDetails.f_GetMemberValue("Token", "").f_String();
		mp_AppLogin.f_Clear();

		auto *pApp = _LoginDetails.f_GetMember("App", EEJsonType_Object);
		if (!pApp)
			co_return {};

		CAppLogin AppLogin;
		AppLogin.m_AppID = pApp->f_GetMemberValue("AppID", "").f_String();
		AppLogin.m_InstallationID = pApp->f_GetMemberValue("InstallationID", "").f_String();
		AppLogin.m_Repository = pApp->f_GetMemberValue("Repository", "").f_String();
		AppLogin.m_Owner = pApp->f_GetMemberValue("Owner", "").f_String();

		if (AppLogin.m_AppID.f_IsEmpty())
			co_return DMibErrorInstance("The GitHub App login has no AppID");

		// A Repository beside an InstallationID selects nothing, so it isn't checked
		if (AppLogin.m_Repository && AppLogin.m_InstallationID.f_IsEmpty())
		{
			auto iSlash = AppLogin.m_Repository.f_FindChar('/');
			if (iSlash <= 0 || iSlash == AppLogin.m_Repository.f_GetLen() - 1)
				co_return DMibErrorInstance("The GitHub App login's Repository '{}' isn't of the form owner/name"_f << AppLogin.m_Repository);
		}

		{
			CStr PrivateKey = pApp->f_GetMemberValue("PrivateKey", "").f_String();
			if (PrivateKey.f_IsEmpty())
				co_return DMibErrorInstance("The GitHub App login has no PrivateKey");

			NContainer::CSecureByteVector PemPrivateKey;
			PemPrivateKey.f_Insert((uint8 const *)PrivateKey.f_GetStr(), PrivateKey.f_GetLen());

			auto CaptureScope = co_await (g_CaptureExceptions % "The GitHub App private key is not a valid unencrypted PEM private key");
			AppLogin.m_PrivateKey = NCryptography::CPublicCrypto::fs_ConvertToDer_PrivateKey(PemPrivateKey);
		}

		if (mp_Token)
		{
			if (auto *pExpires = _LoginDetails.f_GetMember("TokenExpires", EEJsonType_Date))
				AppLogin.m_TokenExpires = pExpires->f_Date();
			else
				mp_Token.f_Clear();
		}

		mp_AppLogin = fg_Move(AppLogin);

		co_return {};
	}

	DMibGitHostingProviderRegister(CGitHostingProvider_GitHub);
}
