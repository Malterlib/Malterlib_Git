// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_Policy.h"

namespace NMib::NGit
{
	namespace
	{
		TCMap<CStr, CStr> fg_GetChangedPermissions
			(
				TCMap<CStr, CStr> &_CurrentPermissions
				, TCMap<CStr, CStr> &_WantedPermissions
				, TCSet<CStr> &o_Removed
				, CStr &o_Added
				, CStr &o_Updated
			)
		{
			TCMap<CStr, CStr> AddedOrUpdatedPermissions;

			for (auto &Permission : _WantedPermissions)
			{
				auto &Actor = _WantedPermissions.fs_GetKey(Permission);
				if (!_CurrentPermissions.f_FindEqual(Actor))
				{
					AddedOrUpdatedPermissions[Actor] = Permission;
					fg_AddStrSep(o_Added, "{} ({})"_f << Actor << Permission, ", ");
				}
				else if (_CurrentPermissions[Actor] != Permission)
				{
					AddedOrUpdatedPermissions[Actor] = Permission;
					fg_AddStrSep(o_Updated, "{} ({} -> {})"_f << Actor << _CurrentPermissions[Actor] << Permission, ", ");
				}
			}

			for (auto &Permission : _CurrentPermissions)
			{
				auto &Actor = _CurrentPermissions.fs_GetKey(Permission);
				if (!_WantedPermissions.f_FindEqual(Actor))
					o_Removed[Actor];
			}

			return AddedOrUpdatedPermissions;
		}
	}

	TCFuture<void> CGitPolicyActor::f_ApplyPolicy_Permissions(CApplyPolicyContext _Context, CEJsonSorted _Permissions)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		bool bRemoveOther = false;
		if (auto pValue = _Permissions.f_GetMember("RemoveOther"))
			bRemoveOther = pValue->f_Boolean();

		CGitHostingProvider::CRepositoryPermissions WantedPermissions;

		if (auto pTeams = _Permissions.f_GetMember("Teams"))
		{
			for (auto &Team : pTeams->f_Object())
				WantedPermissions.m_TeamPermissions[Team.f_Name()] = Team.f_Value().f_String();
		}

		if (auto pUsers = _Permissions.f_GetMember("Users"))
		{
			for (auto &User : pUsers->f_Object())
				WantedPermissions.m_UserPermissions[User.f_Name()] = User.f_Value().f_String();
		}

		auto CurrentPermissions = co_await _Context.m_HostingProvider(&CGitHostingProvider::f_GetRepositoryPermissions, _Context.m_Repository);

		CGitHostingProvider::CRepositoryPermissions AddedOrUpdatedPermissions;

		TCSet<CStr> ExtraTeam;
		CStr AddedTeamPermissions;
		CStr UpdatedTeamPermissions;
		AddedOrUpdatedPermissions.m_TeamPermissions = fg_GetChangedPermissions
			(
				CurrentPermissions.m_TeamPermissions
				, WantedPermissions.m_TeamPermissions
				, ExtraTeam
				, AddedTeamPermissions
				, UpdatedTeamPermissions
			)
		;

		TCSet<CStr> ExtraUser;
		CStr AddedUserPermissions;
		CStr UpdatedUserPermissions;
		AddedOrUpdatedPermissions.m_UserPermissions = fg_GetChangedPermissions
			(
				CurrentPermissions.m_UserPermissions
				, WantedPermissions.m_UserPermissions
				, ExtraUser
				, AddedUserPermissions
				, UpdatedUserPermissions
			)
		;

		if (!AddedOrUpdatedPermissions.m_TeamPermissions.f_IsEmpty() || !AddedOrUpdatedPermissions.m_UserPermissions.f_IsEmpty())
		{
			if (AddedTeamPermissions || AddedUserPermissions)
			{
				if (_Context.m_fOnCreate)
					co_await _Context.m_fOnCreate(CStr(), "Team: {} User: {}"_f << AddedTeamPermissions << AddedUserPermissions);
			}

			if (UpdatedTeamPermissions || UpdatedUserPermissions)
			{
				if (_Context.m_fOnUpdate)
					co_await _Context.m_fOnUpdate(CStr(), "Team: {} User: {}"_f << UpdatedTeamPermissions << UpdatedUserPermissions);
			}

			if (!_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_AddRepositoryPermissions, _Context.m_Repository, AddedOrUpdatedPermissions);
		}

		if (bRemoveOther && (!ExtraTeam.f_IsEmpty() || !ExtraUser.f_IsEmpty()))
		{
			if (_Context.m_fOnDelete)
				co_await _Context.m_fOnDelete(CStr(), "Team: {vs} User: {vs}"_f << ExtraTeam << ExtraUser);

			if (!_Context.m_bPretend)
				co_await _Context.m_HostingProvider(&CGitHostingProvider::f_RemoveRepositoryPermissions, _Context.m_Repository, ExtraTeam, ExtraUser);
		}

		co_return {};
	}
}
