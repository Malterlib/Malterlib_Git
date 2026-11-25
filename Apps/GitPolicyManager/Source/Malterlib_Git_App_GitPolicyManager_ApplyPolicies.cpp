// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Platform>

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/File/File>
#include <Mib/Git/HostingProvider>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	namespace
	{
		struct CNameFilter
		{
			CNameFilter(CEJsonSorted const &_Config, CStr const &_ConfigName);

			bool f_IsActive() const;
			bool f_Matches(CStr const &_Name) const;

			TCVector<CStr> m_Exclude;
			TCVector<CStr> m_Include;
			TCVector<CStr> m_ExcludeWildcards;
			TCVector<CStr> m_IncludeWildcards;
		};

		CNameFilter::CNameFilter(CEJsonSorted const &_Config, CStr const &_ConfigName)
		{
			if (auto pValue = _Config.f_GetMember("Exclude{}"_f << _ConfigName))
				m_Exclude = pValue->f_StringArray();

			if (auto pValue = _Config.f_GetMember("Include{}"_f << _ConfigName))
				m_Include = pValue->f_StringArray();

			if (auto pValue = _Config.f_GetMember("Exclude{}Wildcards"_f << _ConfigName))
				m_ExcludeWildcards = pValue->f_StringArray();

			if (auto pValue = _Config.f_GetMember("Include{}Wildcards"_f << _ConfigName))
				m_IncludeWildcards = pValue->f_StringArray();
		}

		bool CNameFilter::f_IsActive() const
		{
			return !m_Include.f_IsEmpty() || !m_IncludeWildcards.f_IsEmpty();
		}

		bool CNameFilter::f_Matches(CStr const &_Name) const
		{
			if
			(
				f_IsActive()
				&& m_Include.f_Contains(_Name) < 0
				&& !NStr::fg_StrMatchesAnyWildcardInContainer<EMatchWildcardResult_WholeStringMatchedAndPatternExhausted>(_Name, m_IncludeWildcards)
			)
			{
				return false;
			}

			if (m_Exclude.f_Contains(_Name) >= 0 || NStr::fg_StrMatchesAnyWildcardInContainer<EMatchWildcardResult_WholeStringMatchedAndPatternExhausted>(_Name, m_ExcludeWildcards))
				return false;

			return true;
		}

		TCVector<CGitHostingProvider::CRepository> fg_FilterRepositories(TCVector<CGitHostingProvider::CRepository> const &_Array, CEJsonSorted const &_Policy)
		{
			CNameFilter RepositoryNameFilter(_Policy, "Repository");
			CNameFilter DefaultBranchFilter(_Policy, "DefaultBranch");

			TCOptional<bool> FilterPrivate;
			if (auto pValue = _Policy.f_GetMember("IncludeRepositoryType"))
			{
				if (pValue->f_String() == "Private")
					FilterPrivate = true;
				else if (pValue->f_String() == "Public")
					FilterPrivate = false;
				else
					DMibError("Unknown repository type: {}"_f << pValue->f_String());
			}

			TCVector<CGitHostingProvider::CRepository> Filtered;

			for (auto &Value : _Array)
			{
				if (!Value.m_Name)
					continue;

				if (FilterPrivate)
				{
					if (!Value.m_IsPrivate)
						continue;

					if (*Value.m_IsPrivate != *FilterPrivate)
						continue;
				}

				if (!RepositoryNameFilter.f_Matches(*Value.m_Name))
					continue;

				if (!Value.m_DefaultBranch)
				{
					if (DefaultBranchFilter.f_IsActive())
						continue;
				}
				else if (!DefaultBranchFilter.f_Matches(*Value.m_DefaultBranch))
					continue;

				Filtered.f_Insert(Value);
			}

			return Filtered;
		}
	}

	CStr CGitPolicyManagerActor::fp_PretendDescription() const
	{
		if (mp_bPretend)
			return "Would have: ";
		else
			return "";
	}

	auto CGitPolicyManagerActor::fp_ApplyPolicies() -> TCFuture<CApplyPoliciesResult>
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto Auditor = mp_State.f_Auditor();

		auto Providers = CGitHostingProvider::fs_EnumHostingProviders();

		auto const &ConfigJson = mp_State.m_ConfigDatabase.m_Data;

		mp_bPretend = ConfigJson.f_GetMemberValue("Pretend", false).f_Boolean();
		bool bWarnWithoutPolicies = ConfigJson.f_GetMemberValue("WarnWithoutPolicies", true).f_Boolean();

		auto pConfigs = ConfigJson.f_GetMember("Configs");
		if (!pConfigs)
			co_return {};

		CApplyPoliciesResult Result;

		TCVector<CGitHostingProvider::CRepository> AllRepositories;
		TCSet<CStr> AllMatchedRepositories;
		TCMap<CStr, TCSet<CStr>> PartlyAppliedRepositories; // By provider, since several configs can cover a repository

		// A copy: the config can be reloaded while the hosting providers are awaited
		auto Configs = pConfigs->f_Array();
		for (auto &Config : Configs)
		{
			auto &Provider = Config["Provider"].f_String();

			auto *pProvider = Providers.f_FindEqual(Provider);
			if (!pProvider)
				co_return DMibErrorInstance("Could not find '{}' hosting provider"_f << Provider);

			auto HostingProvider = CGitHostingProvider::fs_CreateHostingProvider(*pProvider);

			auto Authentication = CEJsonSorted::fs_FromCompatible(Config["Authentication"]);

			// A relative key file is next to the config file, wherever the daemon runs from
			if (auto *pApp = Authentication.f_GetMember("App", EEJsonType_Object))
			{
				if (auto *pPrivateKeyFile = pApp->f_GetMember("PrivateKeyFile", EEJsonType_String))
					*pPrivateKeyFile = CFile::fs_GetExpandedPath(pPrivateKeyFile->f_String(), mp_State.m_RootDirectory);
			}

			co_await HostingProvider(&CGitHostingProvider::f_Login, fg_Move(Authentication));

			auto Repositories = co_await HostingProvider(&CGitHostingProvider::f_GetRepositories, Config["Organizations"].f_StringArray(), false);

			TCSet<CStr> MatchedRepositories;

			for (auto &PolicyYaml : Config["Policies"].f_Array())
			{
				auto Policy = CEJsonSorted::fs_FromCompatible(PolicyYaml);
				auto &PolicyName = Policy["Name"].f_String();
				for (auto &Repository : fg_FilterRepositories(Repositories, Policy))
				{
					if (!MatchedRepositories(*Repository.m_Name).f_WasCreated())
						continue;

					if (!co_await fp_ApplyPolicies_Repository(Policy, *Repository.m_Name, HostingProvider, PolicyName))
						PartlyAppliedRepositories[Provider][*Repository.m_Name];
				}
			}

			AllMatchedRepositories += MatchedRepositories;
			AllRepositories.f_Insert(Repositories);
		}

		for (auto &Repositories : PartlyAppliedRepositories)
			Result.m_nRepositoriesPartlyApplied += Repositories.f_GetLen();

		if (bWarnWithoutPolicies)
		{
			TCSet<CStr> WarnedRepositories;
			CStr WithoutPolicy;
			for (auto &Repository : AllRepositories)
			{
				if (AllMatchedRepositories.f_FindEqual(*Repository.m_Name))
					continue;

				if (!WarnedRepositories(*Repository.m_Name).f_WasCreated())
					continue;

				++Result.m_nRepositoriesWithoutPolicy;

				fg_AddStrSep
					(
						WithoutPolicy
						, "{}\n"
						"    private       : {}\n"
						"    default branch: {}"_f
						<< *Repository.m_Name
						<< (Repository.m_IsPrivate && *Repository.m_IsPrivate ? "true" : "false")
						<< (Repository.m_DefaultBranch ? *Repository.m_DefaultBranch : CStr("-"))
						, "\n"
					)
				;
			}

			if (!WithoutPolicy.f_IsEmpty())
				Auditor.f_Warning("The following repositories have no policy:\n{}"_f << WithoutPolicy);
		}

		co_return Result;
	}

	/// Returns false when the policy left a generic ruleset it lists unchanged
	TCFuture<bool> CGitPolicyManagerActor::fp_ApplyPolicies_Repository(CEJsonSorted _Policy, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		bool bAllApplied = true;

		if (auto pRepositorySettings = _Policy.f_GetMember("RepositorySettings"))
			co_await fp_ApplyPolicies_RepositorySettings(*pRepositorySettings, _Repository, _HostingProvider, _PolicyName);

		if (auto pPermissions = _Policy.f_GetMember("Permissions"))
			co_await fp_ApplyPolicies_Permissions(*pPermissions, _Repository, _HostingProvider, _PolicyName);

		if (_Policy.f_GetMember("BranchProtection") || _Policy.f_GetMember("GenericRules") || _Policy.f_GetMember("RemoveOther"))
			bAllApplied = co_await fp_ApplyPolicies_Rules(_Policy, _Repository, _HostingProvider, _PolicyName);

		if (auto pGenericRules = _Policy.f_GetMember("ActionsSettings"))
			co_await fp_ApplyPolicies_ActionsSettings(*pGenericRules, _Repository, _HostingProvider, _PolicyName);

		co_return bAllApplied;
	}
}
