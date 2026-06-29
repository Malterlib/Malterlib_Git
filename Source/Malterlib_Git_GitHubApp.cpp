// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_GitHubApp.h"

#include <Mib/Encoding/Base64>

namespace NMib::NGit
{
	namespace
	{
		NStr::CStr fg_Base64UrlNoPadding(NContainer::CByteVector const &_Data)
		{
			return NEncoding::fg_Base64Encode(_Data)
				.f_Replace("+", "-")
				.f_Replace("/", "_")
				.f_Replace("=", "")
			;
		}

		NStr::CStr fg_Base64UrlNoPadding(NStr::CStr const &_String)
		{
			return fg_Base64UrlNoPadding(NContainer::CByteVector((uint8 const *)_String.f_GetStr(), _String.f_GetLen()));
		}
	}

	NStr::CStr fg_BuildGitHubAppJwt
		(
			NStr::CStr const &_Issuer
			, NContainer::CSecureByteVector const &_PrivateKeyDer
			, int64 _IssuedAtUnix
			, int64 _ExpiresAtUnix
		)
	{
		using namespace NMib::NStr;

		// Hand-built so the encoded JSON is canonical and compact. A literal '{' is escaped as "{{" while a literal
		// '}' is written as-is. Issuer is an app id / client id (digits and dots), so it needs no JSON escaping.
		CStr Header = R"({"alg":"RS256","typ":"JWT"})";
		CStr Payload = "{{\"iat\":{},\"exp\":{},\"iss\":\"{}\"}"_f << _IssuedAtUnix << _ExpiresAtUnix << _Issuer;

		CStr SigningInput = "{}.{}"_f << fg_Base64UrlNoPadding(Header) << fg_Base64UrlNoPadding(Payload);

		NContainer::CSecureByteVector Message((uint8 const *)SigningInput.f_GetStr(), SigningInput.f_GetLen());
		NContainer::CSecureByteVector Signature = NCryptography::CPublicCrypto::fs_SignMessage(Message, _PrivateKeyDer, NCryptography::EDigestType_SHA256);

		// The signature is part of the public token, so a plain byte vector is fine for encoding.
		NContainer::CByteVector SignatureBytes(Signature.f_GetArray(), Signature.f_GetLen());

		return "{}.{}"_f << SigningInput << fg_Base64UrlNoPadding(SignatureBytes);
	}

	NStr::CStr fg_BuildGitHubAppJwtFromPem
		(
			NStr::CStr const &_Issuer
			, NContainer::CSecureByteVector const &_PrivateKeyPem
			, int64 _IssuedAtUnix
			, int64 _ExpiresAtUnix
		)
	{
		NContainer::CSecureByteVector PrivateKeyDer = NCryptography::CPublicCrypto::fs_ConvertPrivateKeyPemToDer(_PrivateKeyPem);
		return fg_BuildGitHubAppJwt(_Issuer, PrivateKeyDer, _IssuedAtUnix, _ExpiresAtUnix);
	}
}
