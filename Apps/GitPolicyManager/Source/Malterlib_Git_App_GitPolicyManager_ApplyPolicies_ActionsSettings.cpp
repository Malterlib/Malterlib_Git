// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Git/HostingProvider>
#include <Mib/Git/Policy>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	TCFuture<void> CGitPolicyManagerActor::fp_ApplyPolicies_ActionsSettings
		(
			CEJSONSorted _Settings
			, CStr _Repository
			, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider
			, CStr _PolicyName
		)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto Auditor = mp_State.f_Auditor();

		TCActor<CGitPolicyActor> PolicyActor = fg_Construct();
		auto DestroyPolicyActor = co_await fg_AsyncDestroy(PolicyActor);

		CGitPolicyActor::CApplyPolicyContext Context
			{
				.m_Repository = _Repository
				, .m_HostingProvider = _HostingProvider
				, .m_fOnUpdate = g_ActorFunctor / [=, this](CStr &&_Name, CStr &&_UpdatedValues) -> TCFuture<void>
				{
					Auditor.f_Warning
						(
							"{}Appying policy '{}' resulted in updated actions settings on repository '{}'. Updated values:\n{}"_f
							<< fp_PretendDescription()
							<< _PolicyName
							<< _Repository
							<< _UpdatedValues.f_Indent("    ", true)
						)
					;

					co_return {};
				}
				, .m_bPretend = mp_bPretend
			}
		;

		co_await PolicyActor(&CGitPolicyActor::f_ApplyPolicy_ActionsSettings, fg_Move(Context), fg_Move(_Settings));

		co_return {};
	}
}
