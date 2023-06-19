// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Git/HostingProvider>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	namespace
	{
		struct CNameFilter
		{
			CNameFilter(CEJSONSorted const &_Config, CStr const &_ConfigName)
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

			bool f_IsActive() const
			{
				return !m_Include.f_IsEmpty() || !m_IncludeWildcards.f_IsEmpty();
			}

			bool f_Matches(CStr const &_Name) const
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

				if
					(
						m_Exclude.f_Contains(_Name) >= 0
						|| NStr::fg_StrMatchesAnyWildcardInContainer<EMatchWildcardResult_WholeStringMatchedAndPatternExhausted>(_Name, m_ExcludeWildcards)
					)
				{
					return false;
				}

				return true;
			}

			TCVector<CStr> m_Exclude;
			TCVector<CStr> m_Include;
			TCVector<CStr> m_ExcludeWildcards;
			TCVector<CStr> m_IncludeWildcards;
		};

		TCVector<CGitHostingProvider::CRepository> fg_FilterRepositories(TCVector<CGitHostingProvider::CRepository> const &_Array, CEJSONSorted const &_Policy)
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
				if (FilterPrivate)
				{
					if (!Value.m_IsPrivate)
						continue;

					if (*Value.m_IsPrivate != *FilterPrivate)
						continue;
				}

				if (!RepositoryNameFilter.f_Matches(Value.m_Name))
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

	TCFuture<void> CGitPolicyManagerActor::fp_ApplyPolicies()
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

		for (auto &Config : pConfigs->f_Array())
		{
			auto &Provider = Config["Provider"].f_String();

			auto *pProvider = Providers.f_FindEqual(Provider);
			if (!pProvider)
				co_return DMibErrorInstance("Could not find '{}' hosting provider"_f << Provider);

			auto HostingProvider = CGitHostingProvider::fs_CreateHostingProvider(*pProvider);

			co_await HostingProvider
				(
					&CGitHostingProvider::f_Login
					, Config["Authentication"]
				)
			;

			auto Repositories = co_await HostingProvider(&CGitHostingProvider::f_GetRepositories, Config["Organizations"].f_StringArray(), false);

			TCSet<CStr> MatchedRepositories;

			for (auto &Policy : Config["Policies"].f_Array())
			{
				auto &PolicyName = Policy["Name"].f_String();
				for (auto &Repository : fg_FilterRepositories(Repositories, Policy))
				{
					if (!MatchedRepositories(Repository.m_Name).f_WasCreated())
						continue;

					co_await fp_ApplyPolicies_Repository(Policy, Repository.m_Name, HostingProvider, PolicyName);
				}
			}

			if (bWarnWithoutPolicies)
			{
				CStr WithoutPolicy;
				for (auto &Repository : Repositories)
				{
					if (MatchedRepositories.f_FindEqual(Repository.m_Name))
						continue;

					fg_AddStrSep
						(
							WithoutPolicy
							, "{}\n"
							"    private       : {}\n"
							"    default branch: {}"_f
							<< Repository.m_Name
							<< (Repository.m_IsPrivate && *Repository.m_IsPrivate ? "true" : "false")
							<< (Repository.m_DefaultBranch ? *Repository.m_DefaultBranch : CStr("-"))
							, "\n"
						)
					;
				}

				if (!WithoutPolicy.f_IsEmpty())
					Auditor.f_Warning("The following repositories have no policy:\n{}"_f << WithoutPolicy);
			}
		}

		co_return {};
	}

	TCFuture<void> CGitPolicyManagerActor::fp_ApplyPolicies_Repository(CEJSONSorted _Policy, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		if (auto pPermissions = _Policy.f_GetMember("Permissions"))
			co_await fp_ApplyPolicies_Permissions(*pPermissions, _Repository, _HostingProvider, _PolicyName);

		if (auto pBranchProtection = _Policy.f_GetMember("BranchProtection"))
			co_await fp_ApplyPolicies_BranchProtection(*pBranchProtection, _Repository, _HostingProvider, _PolicyName);

		co_return {};
	}
}
