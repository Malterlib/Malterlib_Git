// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	TCFuture<void> CGitHostingProvider_GitHub::f_Login(CEJsonSorted _LoginDetails)
	{
		mp_Token = _LoginDetails.f_GetMemberValue("Token", "").f_String();
		co_return {};
	}

	DMibGitHostingProviderRegister(CGitHostingProvider_GitHub);
}
