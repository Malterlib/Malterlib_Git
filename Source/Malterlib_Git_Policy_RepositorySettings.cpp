// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_Policy.h"

#include "Malterlib_Git_Policy_RuleParsing.hpp"

#include <Mib/Web/HttpClient>

namespace NMib::NGit
{
	namespace
	{
		TCUnsafeFuture<CGitHostingProvider::CRepository> fg_ParseRepositorySettings(CEJsonSorted const &_Properties)
		{
			CGitHostingProvider::CRepository OutRepository;

			co_await fg_ParseRuleSetting(_Properties, "Name", OutRepository.m_Name);
			co_await fg_ParseRuleSetting(_Properties, "Description", OutRepository.m_Description);
			co_await fg_ParseRuleSetting(_Properties, "Homepage", OutRepository.m_Homepage);
			co_await fg_ParseRuleSetting(_Properties, "DefaultBranch", OutRepository.m_DefaultBranch);
			co_await fg_ParseRuleSetting(_Properties, "IsPrivate", OutRepository.m_IsPrivate);
			co_await fg_ParseRuleSetting(_Properties, "IsTemplate", OutRepository.m_IsTemplate);
			co_await fg_ParseRuleSetting(_Properties, "HasIssues", OutRepository.m_HasIssues);
			co_await fg_ParseRuleSetting(_Properties, "HasProjects", OutRepository.m_HasProjects);
			co_await fg_ParseRuleSetting(_Properties, "HasWiki", OutRepository.m_HasWiki);
			co_await fg_ParseRuleSetting(_Properties, "HasDiscussions", OutRepository.m_HasDiscussions);
			co_await fg_ParseRuleSetting(_Properties, "HasDownloads", OutRepository.m_HasDownloads);
			co_await fg_ParseRuleSetting(_Properties, "AllowForking", OutRepository.m_AllowForking);
			co_await fg_ParseRuleSetting(_Properties, "AllowSquashMerge", OutRepository.m_AllowSquashMerge);
			co_await fg_ParseRuleSetting(_Properties, "AllowMergeCommit", OutRepository.m_AllowMergeCommit);
			co_await fg_ParseRuleSetting(_Properties, "AllowRebaseMerge", OutRepository.m_AllowRebaseMerge);
			co_await fg_ParseRuleSetting(_Properties, "AllowAutoMerge", OutRepository.m_AllowAutoMerge);
			co_await fg_ParseRuleSetting(_Properties, "AllowUpdateBranch", OutRepository.m_AllowUpdateBranch);
			co_await fg_ParseRuleSetting(_Properties, "WebCommitSignoffRequired", OutRepository.m_WebCommitSignoffRequired);
			co_await fg_ParseRuleSetting(_Properties, "DeleteBranchOnMerge", OutRepository.m_DeleteBranchOnMerge);
			co_await fg_ParseRuleSetting(_Properties, "UseSquashPrTitleAsDefault", OutRepository.m_UseSquashPrTitleAsDefault);
			co_await fg_ParseRuleSetting(_Properties, "Archived", OutRepository.m_Archived);
			co_await fg_ParseRuleSetting(_Properties, "SquashMergeCommitTitle", OutRepository.m_SquashMergeCommitTitle);
			co_await fg_ParseRuleSetting(_Properties, "SquashMergeCommitMessage", OutRepository.m_SquashMergeCommitMessage);
			co_await fg_ParseRuleSetting(_Properties, "MergeCommitTitle", OutRepository.m_MergeCommitTitle);
			co_await fg_ParseRuleSetting(_Properties, "MergeCommitMessage", OutRepository.m_MergeCommitMessage);
			co_await fg_ParseRuleSetting(_Properties, "Security_AdvancedEnable", OutRepository.m_Security_AdvancedEnable);
			co_await fg_ParseRuleSetting(_Properties, "Security_SecretScanning", OutRepository.m_Security_SecretScanning);
			co_await fg_ParseRuleSetting(_Properties, "Security_SecretScanningPushProtection", OutRepository.m_Security_SecretScanningPushProtection);
			co_await fg_ParseRuleSetting(_Properties, "CustomProperties", OutRepository.m_CustomProperties);

			co_return fg_Move(OutRepository);
		};
	}

	TCFuture<bool> CGitPolicyActor::f_ApplyPolicy_Repository(CApplyPolicyContext _Context, NEncoding::CEJsonSorted _RepositorySettings)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto WantedProperties = co_await fg_ParseRepositorySettings(_RepositorySettings);
		auto CurrentPropertiesWrapped = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetRepository, _Context.m_Repository).f_Wrap();

		if (!CurrentPropertiesWrapped)
		{
			if (!_Context.m_bCreateMissing)
				co_return CurrentPropertiesWrapped.f_GetException();

			bool bIsMissing = false;
			NException::fg_VisitException<CGitHostingProviderException>
				(
					CurrentPropertiesWrapped.f_GetException()
					, [&](CGitHostingProviderException const &_Exception)
					{
						if (_Exception.f_GetSpecific().m_StatusCode == 404)
							bIsMissing = true;
					}
				)
			;

			if (!bIsMissing)
				co_return CurrentPropertiesWrapped.f_GetException();

			if (!_Context.m_bPretend)
			{
				CGitHostingProvider::CCreateRepository CreateRepository;
				static_cast<CGitHostingProvider::CRepository &>(CreateRepository) = WantedProperties;
				CreateRepository.m_Organization = CFile::fs_GetPath(_Context.m_Repository);

				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_CreateRepository, CreateRepository);
				CurrentPropertiesWrapped = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetRepository, _Context.m_Repository).f_Wrap();
				if (!CurrentPropertiesWrapped)
					co_return CurrentPropertiesWrapped.f_GetException();
			}

			if (_Context.m_fOnCreate)
				co_await _Context.m_fOnCreate(CStr(), _Context.m_Repository);

			co_return true;
		}

		auto &CurrentProperties = *CurrentPropertiesWrapped;

		CStr UpdatedValues;
		if (CurrentProperties.f_IsUpdated(WantedProperties, UpdatedValues))
		{
			if (!_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_UpdateRepository, _Context.m_Repository, WantedProperties);

			if (_Context.m_fOnUpdate)
				co_await _Context.m_fOnUpdate(CStr(), UpdatedValues);
		}

		co_return false;
	}
}
