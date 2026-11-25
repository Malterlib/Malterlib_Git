// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Cryptography/PublicCrypto>
#include <Mib/Encoding/JsonShortcuts>
#include <Mib/File/File>

namespace NMib::NGit
{
	/// _LoginDetails is {"Token": "..."} for a token GitHub issued to a user, or a GitHub App installation:
	/// {"App": {"AppID": "...", "PrivateKey": "<PEM>", "InstallationID": "..."}}, with "Repository": "owner/name" in place of
	/// "InstallationID" to look the installation up, and "PrivateKeyFile": "<path of the PEM file>" in place of "PrivateKey" to keep the
	/// key out of the login details. An App login may also pass the installation token it already holds as "Token" with its expiry as
	/// the date "TokenExpires", which saves creating a new one until it is about to expire. Only the latest login takes effect: one that
	/// fails logs the provider out, and one that a later login replaced while it read its key file changes nothing.
	TCFuture<void> CGitHostingProvider_GitHub::f_Login(CEJsonSorted _LoginDetails)
	{
		uint64 LoginAttempt = ++mp_LoginAttempts;

		// Calls made while the login reads its key file use the previous login, never one that is half set up
		auto Login = co_await fp_PrepareLogin(fg_Move(_LoginDetails)).f_Wrap();

		if (LoginAttempt != mp_LoginAttempts)
			co_return DMibErrorInstance("A later login replaced this one while it read the GitHub App private key file");

		++mp_LoginGeneration;
		mp_Token.f_Clear();
		mp_AppLogin.f_Clear();

		if (!Login)
			co_return Login.f_GetException();

		mp_Token = fg_Move(Login->m_Token);
		mp_AppLogin = fg_Move(Login->m_AppLogin);

		co_return {};
	}

	auto CGitHostingProvider_GitHub::fp_PrepareLogin(CEJsonSorted _LoginDetails) -> TCFuture<CPreparedLogin>
	{
		CPreparedLogin Login;
		Login.m_Token = _LoginDetails.f_GetMemberValue("Token", "").f_String();

		auto *pApp = _LoginDetails.f_GetMember("App", EEJsonType_Object);
		if (!pApp)
			co_return fg_Move(Login);

		CAppLogin AppLogin;
		AppLogin.m_AppID = pApp->f_GetMemberValue("AppID", "").f_String();
		AppLogin.m_InstallationID = pApp->f_GetMemberValue("InstallationID", "").f_String();
		AppLogin.m_Repository = pApp->f_GetMemberValue("Repository", "").f_String();

		if (AppLogin.m_AppID.f_IsEmpty())
			co_return DMibErrorInstance("The GitHub App login has no AppID");

		if (AppLogin.m_InstallationID.f_IsEmpty() && AppLogin.m_Repository.f_IsEmpty())
			co_return DMibErrorInstance("The GitHub App login needs an InstallationID, or a Repository to find the installation from");

		{
			CStr PrivateKey = pApp->f_GetMemberValue("PrivateKey", "").f_String();
			CStr PrivateKeyFile = pApp->f_GetMemberValue("PrivateKeyFile", "").f_String();

			if (!PrivateKey.f_IsEmpty() && !PrivateKeyFile.f_IsEmpty())
				co_return DMibErrorInstance("The GitHub App login has both PrivateKey and PrivateKeyFile");

			NContainer::CSecureByteVector PemPrivateKey;
			if (!PrivateKeyFile.f_IsEmpty())
				PemPrivateKey = co_await (fp_ReadAppPrivateKeyFile(PrivateKeyFile) % ("Could not read the GitHub App private key file '{}'"_f << PrivateKeyFile));
			else if (PrivateKey.f_IsEmpty())
				co_return DMibErrorInstance("The GitHub App login has no PrivateKey or PrivateKeyFile");
			else
				PemPrivateKey.f_Insert((uint8 const *)PrivateKey.f_GetStr(), PrivateKey.f_GetLen());

			auto CaptureScope = co_await (g_CaptureExceptions % "The GitHub App private key is not a valid unencrypted PEM private key");
			AppLogin.m_PrivateKey = NCryptography::CPublicCrypto::fs_ConvertToDer_PrivateKey(PemPrivateKey);
		}

		if (Login.m_Token)
		{
			if (auto *pExpires = _LoginDetails.f_GetMember("TokenExpires", EEJsonType_Date))
				AppLogin.m_TokenExpires = pExpires->f_Date();
			else
				Login.m_Token.f_Clear();
		}

		Login.m_AppLogin = fg_Move(AppLogin);

		co_return fg_Move(Login);
	}

	/// An override may suspend for as long as it needs: f_Login changes nothing until the key is read.
	TCFuture<CSecureByteVector> CGitHostingProvider_GitHub::fp_ReadAppPrivateKeyFile(CStr _Path)
	{
		auto BlockingActorCheckout = fg_BlockingActor();
		co_return co_await
			(
				g_Dispatch(BlockingActorCheckout) / [_Path]()
				{
					return NFile::CFile::fs_ReadFileSecure(_Path);
				}
			)
		;
	}

	DMibGitHostingProviderRegister(CGitHostingProvider_GitHub);
}
