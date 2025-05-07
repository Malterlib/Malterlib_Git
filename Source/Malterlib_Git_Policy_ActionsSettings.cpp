// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_Policy.h"

#include "Malterlib_Git_Policy_RuleParsing.hpp"

namespace NMib::NGit
{
	namespace
	{
		TCUnsafeFuture<CGitHostingProvider::CActionsSettings> fg_ParseActionsSettings(CEJsonSorted const &_Properties)
		{
			CGitHostingProvider::CActionsSettings OutSettings;

			co_await fg_ParseRuleSetting(_Properties, "ActionsEnabled", OutSettings.m_ActionsEnabled);
			co_await fg_ParseRuleSetting(_Properties, "AllowedActions", OutSettings.m_AllowedActions);
			co_await fg_ParseRuleSetting(_Properties, "AccessOutsideOfRepository", OutSettings.m_AccessOutsideOfRepository);
			co_await fg_ParseRuleSetting(_Properties, "DefaultPermissions", OutSettings.m_DefaultPermissions);
			co_await fg_ParseRuleSetting(_Properties, "CanApprovePullRequestReviews", OutSettings.m_CanApprovePullRequestReviews);
			
			co_return fg_Move(OutSettings);
		};
	}

	TCFuture<void> CGitPolicyActor::f_ApplyPolicy_ActionsSettings(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _Policy)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto WantedProperties = co_await fg_ParseActionsSettings(_Policy);
		auto CurrentProperties = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetActionsSettings, _Context.m_Repository);

		CStr UpdatedValues;
		if (CurrentProperties.f_IsUpdated(WantedProperties, UpdatedValues))
		{
			if (!_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_UpdateActionsSettings, _Context.m_Repository, WantedProperties);

			if (_Context.m_fOnUpdate)
				co_await _Context.m_fOnUpdate(CStr(), UpdatedValues);
		}

		co_return {};
	}
}
