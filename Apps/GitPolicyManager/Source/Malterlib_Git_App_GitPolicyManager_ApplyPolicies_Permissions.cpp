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

	TCFuture<void> CGitPolicyManagerActor::fp_ApplyPolicies_Permissions(CEJSONSorted _Permissions, CStr _Repository, NConcurrency::TCActor<CGitHostingProvider> _HostingProvider, CStr _PolicyName)
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto Auditor = mp_State.f_Auditor();

		bool bRemoveOther = _Permissions["RemoveOther"].f_Boolean();

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

		auto CurrentPermissions = co_await _HostingProvider(&CGitHostingProvider::f_GetRepositoryPermissions, _Repository);

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
				Auditor.f_Info
					(
						"{}Appying policy '{}' resulted in add permissions to repository '{}': Team: {} User: {}"_f
						<< fp_PretendDescription()
						<< _PolicyName
						<< _Repository
						<< AddedTeamPermissions
						<< AddedUserPermissions
					)
				;
			}

			if (UpdatedTeamPermissions || UpdatedUserPermissions)
			{
				Auditor.f_Warning
					(
						"{}Appying policy '{}' resulted in updated permissions on repository '{}': Team: {} User: {}"_f
						<< fp_PretendDescription()
						<< _PolicyName
						<< _Repository
						<< UpdatedTeamPermissions
						<< UpdatedUserPermissions
					)
				;
			}

			if (!mp_bPretend)
				co_await _HostingProvider(&CGitHostingProvider::f_AddRepositoryPermissions, _Repository, AddedOrUpdatedPermissions);
		}

		if (bRemoveOther && (!ExtraTeam.f_IsEmpty() || !ExtraUser.f_IsEmpty()))
		{
			Auditor.f_Warning
				(
					"{}Appying policy '{}' resulted in removed permissions on repository '{}': Team: {vs} User: {vs}"_f
					<< fp_PretendDescription()
					<< _PolicyName
					<< _Repository
					<< ExtraTeam
					<< ExtraUser
				)
			;

			if (!mp_bPretend)
				co_await _HostingProvider(&CGitHostingProvider::f_RemoveRepositoryPermissions, _Repository, ExtraTeam, ExtraUser);
		}

		co_return {};
	}
}
