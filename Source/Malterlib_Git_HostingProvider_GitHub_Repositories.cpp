// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

namespace NMib::NGit
{
	auto CGitHostingProvider_GitHub::f_GetRepositories(TCVector<CStr> const &_Organizations, bool _bPersonal) -> TCFuture<TCVector<CRepository>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;
		
		TCVector<CRepository> OutRepositories;
		auto fAddRepository = [&](CJSONSorted const &_RepositoryJson)
			{
				auto &NewRepo = OutRepositories.f_Insert();
				NewRepo.m_Name = _RepositoryJson["full_name"].f_String();
				NewRepo.m_DefaultBranch = _RepositoryJson["default_branch"].f_String();
				NewRepo.m_IsPrivate = _RepositoryJson["visibility"].f_String() == "private";
			}
		;

		if (_bPersonal)
		{
			auto const User = co_await fp_RestApi("user", "Failed to get user information for logged in GitHub user");

			auto UserName = User["login"].f_String();
			CStr UserURL = "users/{}"_f << UserName;

			auto const UserRepositories = co_await fp_RestApi(UserURL / "repos", "Failed to get repositories of user '{}'"_f << UserName);
			for (auto &Repo : UserRepositories.f_Array())
				fAddRepository(Repo);
		}

		for (auto &Organization : _Organizations)
		{
			auto const OrganizationRepositories = co_await fp_RestApi("orgs/{}/repos"_f << Organization, "Failed to get repositories of organization '{}'"_f << Organization);

			for (auto &Repo : OrganizationRepositories.f_Array())
				fAddRepository(Repo);
		}

		co_return fg_Move(OutRepositories);
	}
}
