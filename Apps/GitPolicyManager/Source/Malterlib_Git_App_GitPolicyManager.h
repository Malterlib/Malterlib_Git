// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedDaemon>
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/SecretsManager>
#include <Mib/Cryptography/Certificate>

namespace NMib::NGit
{
	struct CGitHostingProvider;
}

namespace NMib::NGit::NGitPolicyManager
{
	struct CGitPolicyManagerActor : public CDistributedAppActor
	{
		CGitPolicyManagerActor();
		~CGitPolicyManagerActor();

	private:
		using EStatusSeverity = CDistributedAppSensorReporter::EStatusSeverity;

		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override;

		TCFuture<void> fp_StartApp(NEncoding::CEJsonSorted const _Params) override;
		TCFuture<void> fp_StopApp() override;
		TCFuture<void> fp_RegisterSensors();

		TCFuture<void> fp_PeriodicUpdate();
		TCFuture<umint> fp_ApplyPolicies();
		TCFuture<void> fp_ApplyPolicies_Repository(CEJsonSorted _Policy, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<void> fp_ApplyPolicies_Permissions(CEJsonSorted _Permissions, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<void> fp_ApplyPolicies_BranchProtection(CEJsonSorted _BranchProtection, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<void> fp_ApplyPolicies_GenericRules(CEJsonSorted _Rules, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<void> fp_ApplyPolicies_RepositorySettings(CEJsonSorted _Properties, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<void> fp_ApplyPolicies_ActionsSettings(CEJsonSorted _Settings, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);

		CStr fp_PretendDescription() const;

		CActorSubscription mp_PeriodicUpdateTimerSubscription;
		CDistributedAppSensorReporter::CSensorReporter mp_SensorReporter_Status;
		bool mp_bPretend = false;
	};
}
