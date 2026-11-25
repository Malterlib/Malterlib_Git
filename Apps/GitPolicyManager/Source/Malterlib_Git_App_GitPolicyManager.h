// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedDaemon>
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/SecretsManager>
#include <Mib/Cryptography/Certificate>
#include <Mib/Git/Policy>

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

		struct CApplyPoliciesResult
		{
			umint m_nRepositoriesWithoutPolicy = 0;
			umint m_nRepositoriesPartlyApplied = 0; // A generic ruleset the policy lists was left unchanged, since updating it would lose what the policy can't express
		};

		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override;

		TCFuture<void> fp_StartApp(NEncoding::CEJsonSorted const _Params) override;
		TCFuture<void> fp_StopApp() override;
		TCFuture<void> fp_RegisterSensors();

		TCFuture<void> fp_PeriodicUpdate();
		TCFuture<CApplyPoliciesResult> fp_ApplyPolicies();
		TCFuture<bool> fp_ApplyPolicies_Repository(CEJsonSorted _Policy, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<void> fp_ApplyPolicies_Permissions(CEJsonSorted _Permissions, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<bool> fp_ApplyPolicies_Rules(CEJsonSorted _Policy, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		auto fp_BranchProtectionContext(CStr const &_Repository, NConcurrency::TCActor<CGitHostingProvider> const &_HostingProvider, CStr const &_PolicyName)
			-> CGitPolicyActor::CApplyPolicyContext
		;
		auto fp_GenericRulesContext(CStr const &_Repository, NConcurrency::TCActor<CGitHostingProvider> const &_HostingProvider, CStr const &_PolicyName)
			-> CGitPolicyActor::CApplyPolicyContext
		;
		TCFuture<void> fp_ApplyPolicies_RepositorySettings(CEJsonSorted _Properties, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);
		TCFuture<void> fp_ApplyPolicies_ActionsSettings(CEJsonSorted _Settings, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName);

		CStr fp_PretendDescription() const;

		CActorSubscription mp_PeriodicUpdateTimerSubscription;
		CDistributedAppSensorReporter::CSensorReporter mp_SensorReporter_Status;
		bool mp_bPretend = false;
	};
}
