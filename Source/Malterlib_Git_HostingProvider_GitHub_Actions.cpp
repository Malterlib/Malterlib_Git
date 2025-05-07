// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider_GitHub.h"

#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		CGitHostingProvider::CActionsSettings fg_ParseActionsSettings
			(
				CJsonSorted const &_Permissions
				, CJsonSorted const &_SelectedActions
				, CJsonSorted const &_LevelOfAccess
				, CJsonSorted const &_WorkflowPermissions
			)
		{
			CGitHostingProvider::CActionsSettings ActionsSettings;

			ActionsSettings.m_ActionsEnabled = _Permissions["enabled"].f_Boolean();

			if (auto *pValue = _Permissions.f_GetMember("allowed_actions"))
			{
				auto &Value = pValue->f_String();
				if (Value == "all")
					ActionsSettings.m_AllowedActions = CGitHostingProvider::CAllowedAction_All();
				else if (Value == "local_only")
					ActionsSettings.m_AllowedActions = CGitHostingProvider::CAllowedAction_LocalOnly();
				else if (Value == "selected")
				{
					ActionsSettings.m_AllowedActions = CGitHostingProvider::CAllowedAction_Selected();
					auto &Selected = ActionsSettings.m_AllowedActions->f_GetAsType<CGitHostingProvider::CAllowedAction_Selected>();

					if (auto *pValue = _SelectedActions.f_GetMember("github_owned_allowed"))
						Selected.m_bGithubOwnedAllowed = pValue->f_Boolean();

					if (auto *pValue = _SelectedActions.f_GetMember("verified_allowed"))
						Selected.m_bVerifiedAllowed = pValue->f_Boolean();

					if (auto *pValue = _SelectedActions.f_GetMember("patterns_allowed"))
						Selected.m_PatternsAllowed = pValue->f_StringArray();
				}
			}

			if (auto *pValue = _LevelOfAccess.f_GetMember("access_level"))
			{
				auto &Value = pValue->f_String();

				if (Value == "none")
					ActionsSettings.m_AccessOutsideOfRepository = CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_None;
				else if (Value == "user")
					ActionsSettings.m_AccessOutsideOfRepository = CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_User;
				else if (Value == "organization")
					ActionsSettings.m_AccessOutsideOfRepository = CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_Organization;
			}

			if (auto *pValue = _WorkflowPermissions.f_GetMember("can_approve_pull_request_reviews"))
				ActionsSettings.m_CanApprovePullRequestReviews = pValue->f_Boolean();

			if (auto *pValue = _WorkflowPermissions.f_GetMember("default_workflow_permissions"))
			{
				auto &Value = pValue->f_String();

				if (Value == "read")
					ActionsSettings.m_DefaultPermissions = CGitHostingProvider::EActionsWorkflowPermissions::mc_Read;
				else if (Value == "write")
					ActionsSettings.m_DefaultPermissions = CGitHostingProvider::EActionsWorkflowPermissions::mc_Write;
			}

			return ActionsSettings;
		}

		void fg_ActionsSettingsToJson
			(
				CGitHostingProvider::CActionsSettings const &_ActionsSettings
				, CJsonSorted &o_Permissions
				, CJsonSorted &o_SelectedActions
				, CJsonSorted &o_LevelOfAccess
				, CJsonSorted &o_WorkflowPermissions
			)
		{
			if (_ActionsSettings.m_ActionsEnabled)
				o_Permissions["enabled"] = *_ActionsSettings.m_ActionsEnabled;

			if (_ActionsSettings.m_AllowedActions)
			{
				o_Permissions["allowed_actions"] = [&](auto _Enum)
					{
						switch (_Enum)
						{
						case CGitHostingProvider::EAllowedActions::mc_All: return "all";
						case CGitHostingProvider::EAllowedActions::mc_LocalOnly: return "local_only";
						case CGitHostingProvider::EAllowedActions::mc_Selected:
							{
								auto &Selected = _ActionsSettings.m_AllowedActions->f_Get<CGitHostingProvider::EAllowedActions::mc_Selected>();
								o_SelectedActions["github_owned_allowed"] = Selected.m_bGithubOwnedAllowed;
								o_SelectedActions["verified_allowed"] = Selected.m_bVerifiedAllowed;
								o_SelectedActions["patterns_allowed"] = Selected.m_PatternsAllowed;

								return "selected";
							}
						}
						DMibError("Invalid access level");
					}
					(_ActionsSettings.m_AllowedActions->f_GetTypeID())
				;
			}

			if (_ActionsSettings.m_AccessOutsideOfRepository)
			{
				o_LevelOfAccess["access_level"] = [](auto _Enum)
					{
						switch (_Enum)
						{
						case CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_None: return "none";
						case CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_User: return "user";
						case CGitHostingProvider::EActionsAccessOutsideOfRepository::mc_Organization: return "organization";
						}
						DMibError("Invalid access level");
					}
					(*_ActionsSettings.m_AccessOutsideOfRepository)
				;
			}

			if (_ActionsSettings.m_DefaultPermissions)
			{
				o_WorkflowPermissions["default_workflow_permissions"] = [](auto _Enum)
					{
						switch (_Enum)
						{
						case CGitHostingProvider::EActionsWorkflowPermissions::mc_Read: return "read";
						case CGitHostingProvider::EActionsWorkflowPermissions::mc_Write: return "write";
						}
						DMibError("Invalid default permissions");
					}
					(*_ActionsSettings.m_DefaultPermissions)
				;
			}

			if (_ActionsSettings.m_CanApprovePullRequestReviews)
				o_WorkflowPermissions["can_approve_pull_request_reviews"] = *_ActionsSettings.m_CanApprovePullRequestReviews;
		}
	}

	auto CGitHostingProvider_GitHub::f_GetActionsSettings(NStr::CStr _Repository) -> TCFuture<CActionsSettings>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto const Permissions = co_await 
			(
				fp_RestApi("repos/{}/{}/actions/permissions"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name, "Failed to get repository actions permissions '{}'"_f << _Repository)
			)
		;

		bool bIsSelected = false;
		if (auto *pValue = Permissions.f_GetMember("allowed_actions"))
			bIsSelected = pValue->f_String() == "selected";

		CJsonSorted SelectedActions;
		if (bIsSelected)
		{
			SelectedActions = co_await
				(
					fp_RestApi
					(
						"repos/{}/{}/actions/permissions/selected-actions"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
						, "Failed to get repository actions permissions (selected actions) '{}'"_f << _Repository
					)
				)
			;
		}

		auto const LevelOfAccess = co_await
			(
				fp_RestApi
				(
					"repos/{}/{}/actions/permissions/access"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
					, "Failed to get repository actions permissions (level of access) '{}'"_f << _Repository
				)
			).f_Wrap()
		;

		if (!LevelOfAccess && LevelOfAccess.f_GetExceptionStr().f_Find("Access policy only applies to internal and private repositories.") < 0)
			co_return LevelOfAccess.f_GetException();

		auto const WorkflowPermissions = co_await
			(
				fp_RestApi
				(
					"repos/{}/{}/actions/permissions/workflow"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
					, "Failed to get repository actions workflow permissions '{}'"_f << _Repository
				)
			)
		;

		co_return fg_ParseActionsSettings(Permissions, SelectedActions, LevelOfAccess ? *LevelOfAccess : CJsonSorted{}, WorkflowPermissions);
	}

	auto CGitHostingProvider_GitHub::f_UpdateActionsSettings(NStr::CStr _Repository, CActionsSettings _ActionsSettings) -> TCFuture<void>
	{
		auto RepositorySlug = co_await fp_SplitRepositorySlug(_Repository);

		auto CaptureExceptions = co_await g_CaptureExceptions;

		CJsonSorted Permissions;
		CJsonSorted SelectedActions;
		CJsonSorted LevelOfAccess;
		CJsonSorted WorkflowPermissions;

		fg_ActionsSettingsToJson
			(
				_ActionsSettings
				, Permissions
				, SelectedActions
				, LevelOfAccess
				, WorkflowPermissions
			)
		;

		if (Permissions.f_IsValid())
		{
			static constexpr CFieldTranslationPair c_FieldTranslations[] =
				{
					{gc_Str<"allowed_actions">, gc_Str<"AllowedActions">}
					, {gc_Str<"enabled">, gc_Str<"ActionsEnabled">}
				}
			;

			co_await
				(
					fp_RestApiPut
					(
						"repos/{}/{}/actions/permissions"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
						, Permissions
						, "Failed to set actions permissions"
#ifdef DCompiler_MSVC_Workaround
						, fsp_FieldTranslations(c_FieldTranslations)
#else
						, fsp_FieldTranslations<c_FieldTranslations>()
#endif
					)
				)
			;
		}

		if (SelectedActions.f_IsValid())
		{
			static constexpr CFieldTranslationPair c_FieldTranslations[] =
				{
					{gc_Str<"github_owned_allowed">, gc_Str<"GithubOwnedAllowed">}
					, {gc_Str<"patterns_allowed">, gc_Str<"PatternsAllowed">}
					, {gc_Str<"verified_allowed">, gc_Str<"VerifiedAllowed">}
				}
			;

			co_await
				(
					fp_RestApiPut
					(
						"repos/{}/{}/actions/permissions/selected-actions"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
						, SelectedActions
						, "Failed to set actions permissions"
#ifdef DCompiler_MSVC_Workaround
						, fsp_FieldTranslations(c_FieldTranslations)
#else
						, fsp_FieldTranslations<c_FieldTranslations>()
#endif
					)
				)
			;
		}

		if (LevelOfAccess.f_IsValid())
		{
			static constexpr CFieldTranslationPair c_FieldTranslations[] =
				{
					{gc_Str<"access_level">, gc_Str<"AccessOutsideOfRepository">}
				}
			;

			co_await
				(
					fp_RestApiPut
					(
						"repos/{}/{}/actions/permissions/access"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
						, LevelOfAccess
						, "Failed to set actions permissions"
#ifdef DCompiler_MSVC_Workaround
						, fsp_FieldTranslations(c_FieldTranslations)
#else
						, fsp_FieldTranslations<c_FieldTranslations>()
#endif
					)
				)
			;
		}

		if (WorkflowPermissions.f_IsValid())
		{
			static constexpr CFieldTranslationPair c_FieldTranslations[] =
				{
					{gc_Str<"can_approve_pull_request_reviews">, gc_Str<"CanApprovePullRequestReviews">}
					, {gc_Str<"default_workflow_permissions">, gc_Str<"DefaultPermissions">}
				}
			;

			co_await
				(
					fp_RestApiPut
					(
						"repos/{}/{}/actions/permissions/workflow"_f << RepositorySlug.m_Owner << RepositorySlug.m_Name
						, WorkflowPermissions
						, "Failed to set actions permissions"
#ifdef DCompiler_MSVC_Workaround
						, fsp_FieldTranslations(c_FieldTranslations)
#else
						, fsp_FieldTranslations<c_FieldTranslations>()
#endif
					)
				)
			;
		}

		co_return {};
	}
}
