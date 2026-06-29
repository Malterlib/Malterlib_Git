// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Cryptography/PublicCrypto>
#include <Mib/String/String>

namespace NMib::NGit
{
	// GitHub App authentication primitives. A GitHub App authenticates by signing a short-lived JWT (RS256) with the
	// app's private key; that JWT is then exchanged at the REST API for an installation access token. These helpers
	// build the JWT; the exchange lives in the GitHub hosting provider (CGitHostingProvider::f_CreateAccessToken).
	//
	// They are pure (no actor / no network) so the signing path is unit testable offline.

	// GitHub caps a JWT's lifetime at 10 minutes; this is the default lifetime used by the hosting provider, kept
	// under the cap with margin for clock skew.
	inline constexpr int64 gc_GitHubAppJwtLifetimeSeconds = 540;
	// GitHub installation access tokens last one hour; the hosting provider treats them as expiring a little early so
	// callers refresh before the hard expiry.
	inline constexpr int64 gc_GitHubInstallationTokenLifetimeSeconds = 3300;

	// Builds a signed GitHub App JWT. _Issuer is the app id (numeric) or client id. _PrivateKeyDer is the app private
	// key in DER (use fg_BuildGitHubAppJwtFromPem when you have PEM). _IssuedAtUnix/_ExpiresAtUnix are unix seconds;
	// GitHub requires (_ExpiresAtUnix - _IssuedAtUnix) <= 600.
	NStr::CStr fg_BuildGitHubAppJwt
		(
			NStr::CStr const &_Issuer
			, NContainer::CSecureByteVector const &_PrivateKeyDer
			, int64 _IssuedAtUnix
			, int64 _ExpiresAtUnix
		)
	;

	// As fg_BuildGitHubAppJwt, but takes the private key in PEM form (the format GitHub hands out).
	NStr::CStr fg_BuildGitHubAppJwtFromPem
		(
			NStr::CStr const &_Issuer
			, NContainer::CSecureByteVector const &_PrivateKeyPem
			, int64 _IssuedAtUnix
			, int64 _ExpiresAtUnix
		)
	;
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif
