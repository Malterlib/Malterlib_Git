// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Platform>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Git/HostingProvider>
#include <Mib/Git/Policy>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	auto CGitPolicyManagerActor::fp_GenericRulesContext(CStr const &_Repository, NConcurrency::TCActor<CGitHostingProvider> const &_HostingProvider, CStr const &_PolicyName)
		-> CGitPolicyActor::CApplyPolicyContext
	{
		auto Auditor = mp_State.f_Auditor();

		return CGitPolicyActor::CApplyPolicyContext
			{
				.m_Repository = _Repository
				, .m_HostingProvider = _HostingProvider
				, .m_fOnCreate = g_ActorFunctor / [=, this](CStr _Name, CStr _CreatedValues) -> TCFuture<void>
				{
					Auditor.f_Info
						(
							"{}Appying policy '{}' resulted in created generic ruleset for branch pattern '{}' on '{}'"_f << fp_PretendDescription() << _PolicyName << _Name << _Repository
						)
					;

					co_return {};
				}
				, .m_fOnUpdate = g_ActorFunctor / [=, this](CStr _Name, CStr _UpdatedValues) -> TCFuture<void>
				{
					Auditor.f_Warning
						(
							"{}Appying policy '{}' resulted in updated generic ruleset for branch pattern '{}' on '{}'. Updated values:\n{}"_f
							<< fp_PretendDescription()
							<< _PolicyName
							<< _Name
							<< _Repository
							<< _UpdatedValues.f_Indent("    ", true)
						)
					;

					co_return {};
				}
				, .m_fOnDelete = g_ActorFunctor / [=, this](CStr _Name, CStr _DeletedValues) -> TCFuture<void>
				{
					Auditor.f_Warning("{}Applying policy '{}' resulted in deleted generic ruleset '{}' on '{}'"_f << fp_PretendDescription() << _PolicyName << _Name << _Repository);

					co_return {};
				}
				, .m_fOnUnmanageable = g_ActorFunctor / [=](CStr _Name, CStr _Reasons) -> TCFuture<void>
				{
					Auditor.f_Warning
						(
							"Applying policy '{}' skipped generic ruleset '{}' on '{}', since updating it would lose what the policy can't express:\n{}"_f
							<< _PolicyName
							<< _Name
							<< _Repository
							<< _Reasons.f_Indent("    ", true)
						)
					;

					co_return {};
				}
				, .m_bPretend = mp_bPretend
			}
		;
	}

	auto CGitPolicyManagerActor::fp_ApplyPolicies_Rules(CEJsonSorted _Policy, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName)
		-> TCFuture<bool>
	{
		TCActor<CGitPolicyActor> PolicyActor = fg_Construct();
		auto DestroyPolicyActor = co_await fg_AsyncDestroy(PolicyActor);

		co_return co_await PolicyActor
			(
				&CGitPolicyActor::f_ApplyPolicy_Rules
				, fp_BranchProtectionContext(_Repository, _HostingProvider, _PolicyName)
				, fp_GenericRulesContext(_Repository, _HostingProvider, _PolicyName)
				, fg_Move(_Policy)
			)
		;
	}
}
