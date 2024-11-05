// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Git/HostingProvider>
#include <Mib/Git/Policy>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	TCFuture<void> CGitPolicyManagerActor::fp_ApplyPolicies_Permissions(CEJSONSorted _Permissions, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto Auditor = mp_State.f_Auditor();

		TCActor<CGitPolicyActor> PolicyActor = fg_Construct();
		auto DestroyPolicyActor = co_await fg_AsyncDestroy(PolicyActor);

		CGitPolicyActor::CApplyPolicyContext Context
			{
				.m_Repository = _Repository
				, .m_HostingProvider = _HostingProvider
				, .m_fOnCreate = g_ActorFunctor / [=, this](CStr _Name, CStr _CreatedValues) -> TCFuture<void>
				{
					Auditor.f_Info
						(
							"{}Appying policy '{}' resulted in add permissions to repository '{}': {}"_f
							<< fp_PretendDescription()
							<< _PolicyName
							<< _Repository
							<< _CreatedValues
						)
					;

					co_return {};
				}
				, .m_fOnUpdate = g_ActorFunctor / [=, this](CStr _Name, CStr _UpdatedValues) -> TCFuture<void>
				{
					Auditor.f_Warning
						(
							"{}Appying policy '{}' resulted in updated permissions on repository '{}': {}"_f
							<< fp_PretendDescription()
							<< _PolicyName
							<< _Repository
							<< _UpdatedValues
						)
					;

					co_return {};
				}
				, .m_fOnDelete = g_ActorFunctor / [=, this](CStr _Name, CStr _DeletedValues) -> TCFuture<void>
				{
					Auditor.f_Warning
						(
							"{}Appying policy '{}' resulted in removed permissions on repository '{}': {}"_f
							<< fp_PretendDescription()
							<< _PolicyName
							<< _Repository
							<< _DeletedValues
						)
					;

					co_return {};
				}
				, .m_bPretend = mp_bPretend
			}
		;

		co_await PolicyActor(&CGitPolicyActor::f_ApplyPolicy_Permissions, fg_Move(Context), fg_Move(_Permissions));

		co_return {};
	}
}
