// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		CStr fg_PermissionToRole(CStr const &_Permission)
		{
			if (_Permission == "pull")
				return "read";
			else if (_Permission == "push")
				return "write";
			return _Permission;
		}

		CStr fg_RoleToPermission(CStr const &_Role)
		{
			if (_Role == "read")
				return "pull";
			else if (_Role == "write")
				return "push";
			return _Role;
		}
	}

	auto CGitHostingProvider_GitHub::f_GetRepositoryPermissions(CStr _Repository) -> TCFuture<CRepositoryPermissions>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		CRepositoryPermissions OutPermissions;

		auto const Teams = co_await fp_RestApi("repos/{}/teams"_f << _Repository, "Failed to get teams from repository '{}'"_f << _Repository);

		for (auto &Team : Teams.f_Array())
			OutPermissions.m_TeamPermissions[Team["slug"].f_String()] = fg_PermissionToRole(Team["permission"].f_String());

		auto const Users = co_await (fp_RestApi("repos/{}/collaborators"_f << _Repository, "Failed to get collaborators from repository '{}'"_f << _Repository, {{"affiliation", "direct"}}));

		for (auto &User : Users.f_Array())
			OutPermissions.m_UserPermissions[User["login"].f_String()] = User["role_name"].f_String();

		co_return fg_Move(OutPermissions);
	}

	/// The legacy permission of GET repos/{o}/{r}/collaborators/{username}/permission: "admin", "write", "read" or "none". Unlike
	/// role_name it folds maintain into write and triage into read, so callers compare against four values only.
	CStr CGitHostingProvider_GitHub::fs_ParseCollaboratorPermission(CJsonSorted const &_Json)
	{
		if (!_Json.f_IsObject())
			DMibError("GitHub collaborator permission is not an object");

		CStr Permission = fsp_GetJsonString(_Json, "permission");
		if (Permission != "admin" && Permission != "write" && Permission != "read" && Permission != "none")
			DMibError("GitHub returned an unknown collaborator permission '{}'"_f << Permission);

		return Permission;
	}

	/// The user's effective permission on the repository, including access through teams and the organization, unlike
	/// f_GetRepositoryPermissions, which lists direct collaborators only. A user GitHub doesn't know has "none".
	auto CGitHostingProvider_GitHub::f_GetCollaboratorPermission(CStr _Repository, CStr _User) -> TCFuture<CStr>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto Data = co_await fp_RestApi
			(
				"repos/{}/{}/collaborators/{}/permission"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name << _User
				, "Failed to get the permission of '{}' in repository '{}'"_f << _User << _Repository
				, {}
				, 404
			)
		;

		if (!Data.f_IsValid())
			co_return CStr("none");

		auto CaptureExceptions = co_await g_CaptureExceptions;

		co_return fs_ParseCollaboratorPermission(Data);
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_AddRepositoryPermissions(NStr::CStr _Repository, CRepositoryPermissions _Permissions)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		for (auto &Permission : _Permissions.m_TeamPermissions)
		{
			auto &Team = _Permissions.m_TeamPermissions.fs_GetKey(Permission);
			co_await
				(
					fp_RestApiPut
					(
						"orgs/{}/teams/{}/repos/{}"_f << RepositorySlug.m_Owner << Team << _Repository
						, {"permission"_j= fg_RoleToPermission(Permission)}
						, "Failed to grant team '{}' access to repository '{}'"_f << Team << _Repository
						, {}
					)
				)
			;
		}

		for (auto &Permission : _Permissions.m_UserPermissions)
		{
			auto &User = _Permissions.m_UserPermissions.fs_GetKey(Permission);
			co_await
				(
					fp_RestApiPut
					(
						"repos/{}/collaborators/{}"_f << _Repository << User
						, {"permission"_j= fg_RoleToPermission(Permission)}
						, "Failed to grant user '{}' acccess to repository '{}'"_f << User << _Repository
						, {}
					)
				)
			;
		}

		co_return {};
	}

	TCFuture<void> CGitHostingProvider_GitHub::f_RemoveRepositoryPermissions(CStr _Repository, TCSet<CStr> _Teams, TCSet<CStr> _Users)
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		for (auto &Team : _Teams)
		{
			co_await
				(
					fp_RestApiDelete
					(
						"orgs/{}/teams/{}/repos/{}"_f << RepositorySlug.m_Owner << Team << _Repository
						, "Failed to revoke team '{}' from repository '{}'"_f << Team << _Repository
					)
				)
			;
		}

		for (auto &User : _Users)
			co_await fp_RestApiDelete("repos/{}/collaborators/{}"_f << _Repository << User, "Failed to revoke user '{}' from repository '{}'"_f << User << _Repository);

		co_return {};
	}
}
