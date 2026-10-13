// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	CGitHostingProvider_GitHub::CCustomRepositoryRoleCache::CCustomRepositoryRoleCache()
	{
		m_NameToID[gc_Str<"Maintain">.m_Str] = gc_Str<"2">;
		m_NameToID[gc_Str<"Write">.m_Str] = gc_Str<"4">;
		m_NameToID[gc_Str<"Repository admin">.m_Str] = gc_Str<"5">;
	}

	auto CGitHostingProvider_GitHub::fp_SplitRepositorySlug(CStr _Repository) -> TCFuture<CRepositorySlug>
	{
		auto RepoSplit = _Repository.f_Split("/");
		if (RepoSplit.f_GetLen() != 2)
			co_return DMibErrorInstance("Repository '{}' has wrong format"_f << _Repository);

		co_return CRepositorySlug{.m_Owner = RepoSplit[0], .m_Name = RepoSplit[1]};
	}

	// Parses the UTC timestamps GitHub returns, such as 2026-10-12T07:43:04Z. Returns an invalid time for anything else.
	NTime::CTime CGitHostingProvider_GitHub::fs_ParseTime(CStr const &_Time)
	{
		int64 Year = 0;
		int32 Month = 0;
		int32 Day = 0;
		int32 Hour = 0;
		int32 Minute = 0;
		int32 Second = 0;
		aint nParsed = 0;

		(CStr::CParse("{}-{}-{}T{}:{}:{}Z") >> Year >> Month >> Day >> Hour >> Minute >> Second).f_Parse(_Time, nParsed);

		if (nParsed != 6)
			return {};

		if (Month < 1 || Month > 12 || Day < 1 || Day > NTime::CTimeConvert::fs_GetDaysInMonth(Year, Month - 1))
			return {};

		if (Hour < 0 || Hour > 23 || Minute < 0 || Minute > 59 || Second < 0 || Second > 59)
			return {};

		return NTime::CTimeConvert::fs_CreateTime(Year, Month, Day, Hour, Minute, Second);
	}

	CStr CGitHostingProvider_GitHub::fs_FormatTime(NTime::CTime const &_Time)
	{
		auto DateTime = NTime::CTimeConvert{_Time}.f_ExtractDateTime();

		return "{}-{sj2,sf0}-{sj2,sf0}T{sj2,sf0}:{sj2,sf0}:{sj2,sf0}Z"_f
			<< DateTime.m_Year
			<< DateTime.m_Month
			<< DateTime.m_DayOfMonth
			<< DateTime.m_Hour
			<< DateTime.m_Minute
			<< DateTime.m_Second
		;
	}

	CStr CGitHostingProvider_GitHub::fsp_GetJsonString(CJsonSorted const &_Json, CStr const &_Member)
	{
		if (auto *pValue = _Json.f_GetMember(_Member, EJsonType_String))
			return pValue->f_String();

		return {};
	}

	uint64 CGitHostingProvider_GitHub::fsp_GetJsonUnsigned(CJsonSorted const &_Json, CStr const &_Member)
	{
		if (auto *pValue = _Json.f_GetMember(_Member, EJsonType_Integer); pValue && pValue->f_Integer() >= 0)
			return pValue->f_Integer();

		return 0;
	}

	bool CGitHostingProvider_GitHub::fsp_GetJsonBool(CJsonSorted const &_Json, CStr const &_Member)
	{
		if (auto *pValue = _Json.f_GetMember(_Member, EJsonType_Boolean))
			return pValue->f_Boolean();

		return false;
	}

	NTime::CTime CGitHostingProvider_GitHub::fsp_GetJsonTime(CJsonSorted const &_Json, CStr const &_Member)
	{
		if (auto *pValue = _Json.f_GetMember(_Member, EJsonType_String))
			return fs_ParseTime(pValue->f_String());

		return {};
	}

	CStr CGitHostingProvider_GitHub::fsp_GetJsonUserLogin(CJsonSorted const &_Json)
	{
		if (auto *pUser = _Json.f_GetMember("user", EJsonType_Object))
			return fsp_GetJsonString(*pUser, "login");

		return {};
	}

	CJsonSorted::CArray const &CGitHostingProvider_GitHub::fsp_GetJsonArray(CJsonSorted const &_Json, CStr const &_Member)
	{
		if (auto *pValue = _Json.f_GetMember(_Member, EJsonType_Array))
			return pValue->f_Array();

		DMibError("GitHub response has no '{}' array"_f << _Member);
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetAppID(CApp _App, bool _bGraphQL)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if (_App.m_ID)
			co_return _App.m_ID;
		else if (_App.m_Slug)
		{
			auto const App = co_await fp_RestApi("apps/{}"_f << _App.m_Slug, "Failed to get app '{}' by slug"_f << _App.m_Slug);
			if (_bGraphQL)
				co_return App["node_id"].f_String();
			else
				co_return App["id"].f_AsString();
		}
		else
			co_return DMibErrorInstance("App has neither ID nor Slug");
	}

	template <typename tf_CActor>
	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetActorIDGeneric(CStr _Organization, tf_CActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if constexpr
			(
				requires ()
				{
					_Actor.template f_GetAsType<CUser>();
				}
			)
		{
			if (_Actor.template f_IsOfType<CUser>())
			{
				auto &User = _Actor.template f_GetAsType<CUser>();
				if (User.m_ID)
					co_return User.m_ID;
				else if (User.m_Login)
				{
					auto const Data = co_await
						(
							fp_GraphQlApi
							(
								R"-----(
								query GetUserId($login: String!)
								{
									user(login: $login)
									{
										id
										, databaseId
									}
								}
								)-----"
								,
								{
									"login"_j= User.m_Login
								}
							)
							% ("Failed to get user node ID from user '{}'"_f << User.m_Login)
						)
					;
					auto &UserJson = Data["data"]["user"];
					if (UserJson.f_IsNull())
						co_return DMibErrorInstance("User with login '{}' not found"_f << User.m_Login);
					if (_bGraphQL)
						co_return UserJson["id"].f_String();
					else
						co_return UserJson["databaseId"].f_AsString();
				}
				else
					co_return DMibErrorInstance("User has neither ID nor Login");
			}
		}

		if (_Actor.template f_IsOfType<COrganizationAdmin>())
			co_return "1";

		if constexpr
			(
				requires ()
				{
					_Actor.template f_GetAsType<CRepositoryRole>();
				}
			)
		{
			if (_Actor.template f_IsOfType<CRepositoryRole>())
			{
				if (!_pRoleCache)
					co_return DMibErrorInstance("Internal error, no role cache");

				auto &Role = _Actor.template f_GetAsType<CRepositoryRole>();
				if (auto *pID = _pRoleCache->m_NameToID.f_FindEqual(Role.m_Name))
					co_return *pID;

				if (!_pRoleCache->m_bCustomGotten)
				{
					auto const Roles = co_await fp_RestApi("orgs/{}/custom-repository-roles"_f << _Organization, "Failed to get organization repository roles");

					for (auto &CustomRole : Roles["custom_roles"].f_Array())
						_pRoleCache->m_NameToID[CustomRole["name"].f_String()] = CustomRole["id"].f_AsString();

					_pRoleCache->m_bCustomGotten = true;

					if (auto *pID = _pRoleCache->m_NameToID.f_FindEqual(Role.m_Name))
						co_return *pID;
				}

				co_return DMibErrorInstance("Repository role '{}' not found"_f << Role.m_Name);
			}
		}

		if (_Actor.template f_IsOfType<CTeam>())
		{
			auto &Team = _Actor.template f_GetAsType<CTeam>();
			if (Team.m_ID)
				co_return Team.m_ID;
			else if (Team.m_Slug)
			{
				auto const Data = co_await
					(
						fp_GraphQlApi
						(
							R"-----(
							query GetTeamId($login: String!, $slug: String!)
							{
								organization(login: $login)
								{
									team(slug: $slug)
									{
										id
										, databaseId
									}
								}
							}
							)-----"
							,
							{
								"login"_j= _Organization
								, "slug"_j= Team.m_Slug
							}
						)
						% ("Failed to get team node ID for team '{}' in organization '{}'"_f << Team.m_Slug << _Organization)
					)
				;

				auto &TeamJson = Data["data"]["organization"]["team"];
				if (TeamJson.f_IsNull())
					co_return DMibErrorInstance("Team with slug '{}' not found"_f << Team.m_Slug);

				if (_bGraphQL)
					co_return TeamJson["id"].f_String();
				else
					co_return TeamJson["databaseId"].f_AsString();
			}
			else
				co_return DMibErrorInstance("Team has neither ID nor Slug");
		}
		else if (_Actor.template f_IsOfType<CApp>())
		{
			auto &App = _Actor.template f_GetAsType<CApp>();
			co_return co_await fp_GetAppID(App, _bGraphQL);
		}
		else
			co_return DMibErrorInstance("Unknown actor type");
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetActorID(CStr _Organization, CGitActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache)
	{
		co_return co_await fp_GetActorIDGeneric(_Organization, _Actor, _bGraphQL, _pRoleCache);
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetActorID(CStr _Organization, CGenericRuleGitActor _Actor, bool _bGraphQL, TCSharedPointer<CCustomRepositoryRoleCache> _pRoleCache)
	{
		co_return co_await fp_GetActorIDGeneric(_Organization, _Actor, _bGraphQL, _pRoleCache);
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetRepositoryID(CStr _Repository, bool _bGraphQL)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto const RepositoryData = co_await
			(
				fp_GraphQlApi
				(
					R"-----(
					query GetRepositoryId($owner: String!, $name: String!)
					{
						repository(owner: $owner, name: $name)
						{
							id
							, databaseId
						}
					}
					)-----"
					,
					{
						"owner"_j= RepositorySlug.m_Owner
						, "name"_j= RepositorySlug.m_Name
					}
				)
				% ("Failed to get repository node ID for repository '{}'"_f << _Repository)
			)
		;

		auto &RepositoryJson = RepositoryData["data"]["repository"];
		if (RepositoryJson.f_IsNull())
			co_return DMibErrorInstance("Repository with slug '{}' not found"_f << _Repository);

		if (_bGraphQL)
			co_return RepositoryJson["id"].f_String();
		else
			co_return RepositoryJson["databaseId"].f_AsString();
	}
}
