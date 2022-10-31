// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Core/Application>

#include "Malterlib_Git_App_GitPolicyManager.h"

using namespace NMib;
using namespace NMib::NGit::NGitPolicyManager;

class CGitPolicyManager : public CApplication
{
	aint f_Main()
	{
		NConcurrency::CDistributedDaemon Daemon
			{
				"MalterlibGitPolicyManager"
				, "Malterlib Git Policy Manager"
				, "Manages policy setup on Git hosting providers"
				, []
				{
					return fg_ConstructActor<CGitPolicyManagerActor>();
				}
			}
		;

		return Daemon.f_Run();
	}
};

DAppImplement(CGitPolicyManager);
