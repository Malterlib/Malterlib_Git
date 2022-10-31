// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Concurrency/DistributedApp>

namespace NMib::NGit
{
	NConcurrency::TCActor<NConcurrency::CDistributedAppActor> fg_ConstructApp_GitPolicyManager();
}
