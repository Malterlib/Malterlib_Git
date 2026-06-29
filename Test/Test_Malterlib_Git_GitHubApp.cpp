// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Git/GitHubApp>

#include <Mib/Cryptography/PublicCrypto>
#include <Mib/Encoding/Base64>
#include <Mib/Encoding/EJson>

namespace
{
	using namespace NMib;
	using namespace NMib::NStr;
	using namespace NMib::NGit;
	using namespace NMib::NEncoding;
	using namespace NMib::NCryptography;

	NContainer::CByteVector fg_Base64UrlDecode(CStr _Input)
	{
		CStr Standard = _Input.f_Replace("-", "+").f_Replace("_", "/");
		while (Standard.f_GetLen() % 4 != 0)
			Standard += "=";

		NContainer::CByteVector Decoded;
		NEncoding::fg_Base64Decode(Standard, Decoded);
		return Decoded;
	}

	CStr fg_BytesToString(NContainer::CByteVector const &_Bytes)
	{
		return CStr((ch8 const *)_Bytes.f_GetArray(), _Bytes.f_GetLen());
	}

	// Verifies a header.payload.signature JWT against an RSA public key (DER) using RS256.
	bool fg_VerifyJwt(CStr const &_Jwt, NContainer::CSecureByteVector const &_PublicKeyDer)
	{
		auto Parts = _Jwt.f_Split(".");
		if (Parts.f_GetLen() != 3)
			return false;

		CStr SigningInput = "{}.{}"_f << Parts[0] << Parts[1];
		NContainer::CSecureByteVector Message((uint8 const *)SigningInput.f_GetStr(), SigningInput.f_GetLen());
		NContainer::CByteVector SignatureBytes = fg_Base64UrlDecode(Parts[2]);
		NContainer::CSecureByteVector Signature(SignatureBytes.f_GetArray(), SignatureBytes.f_GetLen());

		return CPublicCrypto::fs_VerifySignature(Message, _PublicKeyDer, Signature, EDigestType_SHA256);
	}

	class CGitHubApp_Tests : public NTest::CTest
	{
	public:
		void f_DoTests()
		{
			DMibTestSuite("JwtSignAndVerify")
			{
				NContainer::CSecureByteVector PrivateDer;
				NContainer::CSecureByteVector PublicDer;
				CPublicCrypto::fs_GenerateKeys(PrivateDer, PublicDer, CPublicKeySettings_RSA{2048});

				CStr Jwt = fg_BuildGitHubAppJwt("123456", PrivateDer, 1000, 1540);

				auto Parts = Jwt.f_Split(".");
				DMibExpect(Parts.f_GetLen(), ==, umint(3));

				// Header decodes to the fixed RS256/JWT envelope.
				CEJsonSorted Header = CEJsonSorted::fs_FromString(fg_BytesToString(fg_Base64UrlDecode(Parts[0])));
				DMibExpect(Header["alg"].f_AsString({}), ==, "RS256");
				DMibExpect(Header["typ"].f_AsString({}), ==, "JWT");

				// Payload carries the issuer and the supplied iat/exp.
				CEJsonSorted Payload = CEJsonSorted::fs_FromString(fg_BytesToString(fg_Base64UrlDecode(Parts[1])));
				DMibExpect(Payload["iss"].f_AsString({}), ==, "123456");
				DMibExpect(Payload["iat"].f_AsInteger(0), ==, int64(1000));
				DMibExpect(Payload["exp"].f_AsInteger(0), ==, int64(1540));

				// The signature verifies against the matching public key.
				DMibExpectTrue(fg_VerifyJwt(Jwt, PublicDer));

				// A signature over a different payload must not verify against this key.
				CStr OtherJwt = fg_BuildGitHubAppJwt("999", PrivateDer, 1000, 1540);
				DMibExpectTrue(OtherJwt != Jwt);
			};

			DMibTestSuite("JwtFromPem")
			{
				// A throwaway PKCS#8 RSA test key (not used anywhere else).
				CStr Pem = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDZU29/p2BuqDxg
C26+uSVOsjY7mW8NL7S6GkiOGECi854UUE1ltNpwohYpnC3Gb/QsaGxlDm2oQ4AA
DOMNqfUtndyfj2cE/Fo9sqqZE+e2mIN9s354wXBnhV/4F/rwJBmO85lpTO1UTynQ
Zc9KnOJlelfXM+1QFvwbzCke6wgr5fmDoRpTcPn+K1nK+9qJLSiVQxkRpgCdX2bk
rJW7f1GT6wReS+MS8UbYimqjB7Wcgf+pd4NyRG7ZnM+rc7R9jrSE8iO3J5Z/5VhV
MIEuwcydi2T5DmrpJvAdq70l8RtqKBGiOcd/floffwj3eVDNHltHWkc86bYJtu6M
2AWfEpmHAgMBAAECggEAMHgv12gklTl3UbJrDiVr4SvU9aDoSCgaJiGDihF1pQIx
gPVR9buKtnEoATrAaNACRL87YaSAp3T6gMhfAWak9HoPQRTyFIYVuEn3S3HJjYth
VDEqpVL7N1NjTolGobVjb+L7laUfD10ihcSXIVxxD9Hj8m614FVn/+keSLOBnIxi
GtfjH9CO4awJo4Le56zo8PXeBq0Kge5FPoI7Sqv2DpzcbpaZhV+wa3m2dvvQWjjf
rIQcSr0vh7h0eVAfn6Z/keGFDnCE2RR0n6wkROGA7lwGsAh+Fsm//W0B7QDtJLG/
8OS26/ujDc8AD9K3VcVyV2+RVeEh8HINfGlssHIjIQKBgQD61P7W90bT801w6t09
bs5+hXaZ1NimQILLsKdfXkisb+72HL9FE+iWPynk/Fnc6ta76kNVn166074rjFs1
jK7CV96y9el8m6YTCL2wpi2yJ6le26f0d5YK2EKoMUzgzGd66Wk72GWqAb3KB72I
K3MrfdIB/ScfeCYy/gupoQwI5wKBgQDdzbafzngyxXwYQoz3B7hakN29gClcqFpZ
JH6FXWAbRjWShq2jhcKb/87YvrZ5prWyQZpKp733ivQaBimDI7wQVE6GzClZqPSV
456uOOjFgk9mnBRBpm+qch28vm/XNfjnc8mWNeHJyxpsRpU0ksSJb+CeDpj8WjzC
w///unm2YQKBgGnt27Wy9jF6dcDzHv3bts8N1BmBHwGPCu20q+qFqdFQ8Cz11Pz6
PGZ/RFUVEWpPruHrPAaD6ICj/ZLsknRZ9k/SxhTz72gVX5x4O4vHklLDly6dOx/u
BOqNjBD1yQ7CpAzvV+bTK5QRajJQ9IT2PIwodbErVQNgPVmZmhlIDwoPAoGBAJwp
wBDXz6z/ehWilZk/qD6rjFNlrrl8FtB4b1P6oDXTwtg9VnexL4miG8Ji1BrmkzrE
EZvKamelP6Qq/oNEX56nnPovOFXWLQ5zSj+j9c9Jphm6flCSnEBHRESlWB0P9QUQ
crf9i9EF3L6rG1X+l72kNWNTJ8dUyT7fvJgSdKtBAoGBAIP1JWsGqz/FPQMJW0S3
gk0RTypQ7GoCsELuZ4bzJWr6vFmnfUwprO2ivkpIyqjc3+21rRK0WOMDk/LaXch0
dX8RX+DV/py0AxQlDqudsduKaAPCxJzK+PeXexSkN1PAt8k5Lto04eZY+sUJfkAP
InMbAb+CGjeTYUinSpNB2TqK
-----END PRIVATE KEY-----
)PEM";

				NContainer::CSecureByteVector PemBytes((uint8 const *)Pem.f_GetStr(), Pem.f_GetLen());

				CStr Jwt = fg_BuildGitHubAppJwtFromPem("Iv1.testclientid", PemBytes, 2000, 2300);

				auto Parts = Jwt.f_Split(".");
				DMibExpect(Parts.f_GetLen(), ==, umint(3));

				CEJsonSorted Payload = CEJsonSorted::fs_FromString(fg_BytesToString(fg_Base64UrlDecode(Parts[1])));
				DMibExpect(Payload["iss"].f_AsString({}), ==, "Iv1.testclientid");

				// PEM -> DER -> public key, then verify the JWT signature, exercising fs_ConvertPrivateKeyPemToDer.
				NContainer::CSecureByteVector Der = CPublicCrypto::fs_ConvertPrivateKeyPemToDer(PemBytes);
				NContainer::CSecureByteVector PublicDer = CPublicCrypto::fs_GetPublicKeyFromPrivateKey(Der);
				DMibExpectTrue(fg_VerifyJwt(Jwt, PublicDer));
			};
		}
	};

	DMibTestRegister(CGitHubApp_Tests, Malterlib::Git);
}
