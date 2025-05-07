// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Cryptography/RandomID>
#include <Mib/CommandLine/TableRenderer>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	void CGitPolicyManagerActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				"Malterlib Git Policy Manager"
				, "Manages policy setup on Git hosting providers."
			)
		;
	}
}
