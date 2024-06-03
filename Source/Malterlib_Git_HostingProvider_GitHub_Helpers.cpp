// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NGit
{
	auto CGitHostingProvider_GitHub::fp_SplitRepositorySlug(CStr _Repository) -> TCFuture<CRepositorySlug>
	{
		auto RepoSplit = _Repository.f_Split("/");
		if (RepoSplit.f_GetLen() != 2)
			co_return DMibErrorInstance("Repository '{}' has wrong format"_f << _Repository);

		co_return CRepositorySlug{.m_Owner = RepoSplit[0], .m_Name = RepoSplit[1]};
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetAppID(CGitHostingProvider::CApp _App)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if (_App.m_ID)
			co_return _App.m_ID;
		else if (_App.m_Slug)
		{
			auto const App = co_await fp_RestApi("apps/{}"_f << _App.m_Slug, "Failed to get app '{}' by slug"_f << _App.m_Slug);
			co_return App["node_id"].f_String();
		}
		else
			co_return DMibErrorInstance("App has neither ID nor Slug");
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetActorID(CStr _Organization, CGitHostingProvider::CGitActor _Actor)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if (_Actor.f_IsOfType<CGitHostingProvider::CUser>())
		{
			auto &User = _Actor.f_GetAsType<CGitHostingProvider::CUser>();
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
				co_return UserJson["id"].f_String();
			}
			else
				co_return DMibErrorInstance("User has neither ID nor Login");
		}
		else if (_Actor.f_IsOfType<CGitHostingProvider::CTeam>())
		{
			auto &Team = _Actor.f_GetAsType<CGitHostingProvider::CTeam>();
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
				co_return TeamJson["id"].f_String();
			}
			else
				co_return DMibErrorInstance("Team has neither ID nor Slug");
		}
		else if (_Actor.f_IsOfType<CGitHostingProvider::CApp>())
		{
			auto &App = _Actor.f_GetAsType<CGitHostingProvider::CApp>();
			co_return co_await fp_GetAppID(App);
		}
		else
			co_return DMibErrorInstance("Unknown actor type");
	}

	TCFuture<CStr> CGitHostingProvider_GitHub::fp_GetRepositoryID(CStr _Repository)
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

		co_return RepositoryJson["id"].f_String();
	}
}
