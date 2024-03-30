// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_HostingProvider.h"

namespace NMib::NGit
{
	DMibImpErrorClassImplement(CGitHostingProviderException);

	DMibGitHostingProviderMakeActiveDefine(CGitHostingProvider_GitHub);

	CGitHostingProvider::CGitHostingProvider()
	{
		DMibGitHostingProviderMakeActive(CGitHostingProvider_GitHub);
	}

	bool CGitHostingProviderExceptionData::f_HasError(CStr const &_Field, EGitHostingProviderErrorCode _ErrorCode, CStr const &_Resource, CStr const &_Message) const
	{
		for (auto &Error : m_GitErrors)
		{
			if (Error.m_Code != _ErrorCode)
				continue;

			if (_Field && Error.m_Field != _Field)
				continue;

			if (_Resource && Error.m_Resource != _Resource)
				continue;

			if (_Message && Error.m_Message != _Message)
				continue;

			return true;
		}

		return false;
	}

	namespace
	{
		COrdering_Partial fg_CompareNameOrID(auto const &_LeftName, auto const &_RightName, auto const &_LeftID, auto const &_RightID)
		{
			bool bHasNameLeft = !!_LeftName;
			bool bHasNameRight = !!_RightName;

			if (auto Compare = bHasNameLeft <=> bHasNameRight; Compare != 0)
				return Compare;

			if (bHasNameLeft)
			{
				if (auto Compare = _LeftName <=> _RightName; Compare != 0)
					return Compare;

				return _LeftID <=> _RightID;
			}
			else
			{
				if (auto Compare = !!_LeftID <=> !!_RightID; Compare != 0)
					return Compare;

				if (auto Compare = _LeftID <=> _RightID; Compare != 0)
					return Compare;

				return _LeftName <=> _RightName;
			}
		}
	}

	COrdering_Partial CGitHostingProvider::CUser::operator <=> (CUser const &_Right) const
	{
		return fg_CompareNameOrID(m_Login, _Right.m_Login, m_ID, _Right.m_ID);
	}

	bool CGitHostingProvider::CUser::operator == (CUser const &_Right) const
	{
		return (m_Login && m_Login == _Right.m_Login) || (m_ID && m_ID == _Right.m_ID);
	}

	COrdering_Partial CGitHostingProvider::CApp::operator <=> (CApp const &_Right) const
	{
		return fg_CompareNameOrID(m_Slug, _Right.m_Slug, m_ID, _Right.m_ID);
	}

	bool CGitHostingProvider::CApp::operator == (CApp const &_Right) const
	{
		return (m_Slug && m_Slug == _Right.m_Slug) || (m_ID && m_ID == _Right.m_ID);
	}

	COrdering_Partial CGitHostingProvider::CTeam::operator <=> (CTeam const &_Right) const
	{
		return fg_CompareNameOrID(m_Slug, _Right.m_Slug, m_ID, _Right.m_ID);
	}

	bool CGitHostingProvider::CTeam::operator == (CTeam const &_Right) const
	{
		return (m_Slug && m_Slug == _Right.m_Slug) || (m_ID && m_ID == _Right.m_ID);
	}

	COrdering_Partial CGitHostingProvider::CRepositoryReference::operator <=> (CRepositoryReference const &_Right) const
	{
		return fg_CompareNameOrID(m_Slug, _Right.m_Slug, m_ID, _Right.m_ID);
	}

	bool CGitHostingProvider::CRepositoryReference::operator == (CRepositoryReference const &_Right) const
	{
		return (m_Slug && m_Slug == _Right.m_Slug) || (m_ID && m_ID == _Right.m_ID);
	}

	COrdering_Partial CGitHostingProvider::CRepositoryRole::operator <=> (CRepositoryRole const &_Right) const
	{
		return fg_CompareNameOrID(m_Name, _Right.m_Name, m_ID, _Right.m_ID);
	}

	bool CGitHostingProvider::CRepositoryRole::operator == (CRepositoryRole const &_Right) const
	{
		return (m_Name && m_Name == _Right.m_Name) || (m_ID && m_ID == _Right.m_ID);
	}

	NContainer::TCMap<NStr::CStr, NStr::CStr> CGitHostingProvider::fs_EnumHostingProviders()
	{
		NMib::CRunTimeObjectInfo * const pHostingProviders = fg_GetRuntimeTypeInfo("NMib::NGit::ICGitHostingProviderFactory");
		NContainer::TCMap<NStr::CStr, NStr::CStr> Result;

		if (!pHostingProviders)
			return {};

		for (auto &RunTimeObjectInfo : pHostingProviders->m_Children)
		{
			NStr::CStr Name = RunTimeObjectInfo.f_GetName();
			if (!Name.f_StartsWith("CGitHostingProviderFactory_CGitHostingProvider_"))
				continue;

			Name = Name.f_Extract(fg_StrLen("CGitHostingProviderFactory_CGitHostingProvider_"));
			Result[Name] = RunTimeObjectInfo.f_GetName();
		}

		return Result;
	}

	NConcurrency::TCActor<CGitHostingProvider> CGitHostingProvider::fs_CreateHostingProvider(NStr::CStr const &_ProviderName)
	{
		NMib::CRunTimeObjectInfo * const pHostingProvider = fg_GetRuntimeTypeInfo(_ProviderName);

		if (!pHostingProvider)
			return {};

		if (auto *pFactory = (ICGitHostingProviderFactory *)pHostingProvider->f_CreateObject())
		{
			auto Cleanup = g_OnScopeExit / [&]
				{
					fg_DeleteObject(NMemory::CDefaultAllocator(), pFactory);
				}
			;
			return (*pFactory)();
		}

		return {};
	}

	namespace
	{
		template <typename tf_CClass>
		bool fg_IsUpdated(auto &_Wanted, auto &_Current, auto (tf_CClass::*_pMember), CStr const &_Name, CStr &o_UpdateValues)
		{
			auto &Wanted = _Wanted.*_pMember;
			auto &Current = _Current.*_pMember;

			if (!Wanted)
				return false;

			if (Wanted == Current)
				return false;

			CStr NewValue;
			if constexpr
				(
					requires ()
					{
						fg_EnumToString(*Wanted);
					}
				)
			{
				NewValue = fg_EnumToString(*Wanted);
			}
			else
				NewValue = "{}"_f << *Wanted;

			if (Current)
			{
				CStr OldValue;
				if constexpr
					(
						requires ()
						{
							fg_EnumToString(*Current);
						}
					)
				{
					OldValue = fg_EnumToString(*Current);
				}
				else
					OldValue = "{}"_f << *Current;

				if (OldValue.f_FindChar('\n') >= 0 || NewValue.f_FindChar('\n') >= 0)
				{
					fg_AddStrSep
						(
							o_UpdateValues
							, "{}\n"
							"    Old value:\n"
							"{}\n"
							"    New value:\n"
							"{}"_f
							<< _Name
							<< OldValue.f_Indent("        ")
							<< NewValue.f_Indent("        ")
							, "\n"
						)
					;
				}
				else
					fg_AddStrSep(o_UpdateValues, "{} ({} -> {})"_f << _Name << OldValue << NewValue, "\n");
			}
			else
			{
				if (NewValue.f_FindChar('\n') >= 0)
				{
					fg_AddStrSep
						(
							o_UpdateValues
							, "{}\n"
							"{}"_f
							<< _Name
							<< NewValue.f_Indent("        ")
							, "\n"
						)
					;
				}
				else
					fg_AddStrSep(o_UpdateValues, "{} ({})"_f << _Name << NewValue, "\n");
			}

			return true;
		}
	}

	bool CGitHostingProvider::CBranchProtectionRule::f_IsUpdated(CGitHostingProvider::CBranchProtectionRule const &_Wanted, CStr &o_UpdateValues) const
	{
		bool bRet = false;

		auto fCheckValue = [&](auto &_Wanted, auto &_Current, auto (CBranchProtectionRule::*_pMember), CStr const &_Name)
			{
				if (fg_IsUpdated(_Wanted, _Current, _pMember, _Name, o_UpdateValues))
					bRet = true;
			}
		;

		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_Pattern, "Pattern");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_Creator, "Creator");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_PushAllowances, "PushAllowances");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_ReviewDismissalAllowances, "ReviewDismissalAllowances");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_BypassForcePushAllowances, "BypassForcePushAllowances");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_BypassPullRequestAllowances, "BypassPullRequestAllowances");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiredStatusCheckContexts, "RequiredStatusCheckContexts");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiredStatusChecks, "RequiredStatusChecks");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiredApprovingReviewCount, "RequiredApprovingReviewCount");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_AllowsForcePushes, "AllowsForcePushes");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_AllowsDeletions, "AllowsDeletions");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_BlocksCreations, "BlocksCreations");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_DismissesStaleReviews, "DismissesStaleReviews");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_IsAdminEnforced, "IsAdminEnforced");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiresApprovingReviews, "RequiresApprovingReviews");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiresCodeOwnerReviews, "RequiresCodeOwnerReviews");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiresCommitSignatures, "RequiresCommitSignatures");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiresConversationResolution, "RequiresConversationResolution");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiresLinearHistory, "RequiresLinearHistory");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiresStatusChecks, "RequiresStatusChecks");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RequiresStrictStatusChecks, "RequiresStrictStatusChecks");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RestrictsPushes, "RestrictsPushes");
		fCheckValue(_Wanted, *this, &CBranchProtectionRule::m_RestrictsReviewDismissals, "RestrictsReviewDismissals");

		return bRet;
	}

	bool CGitHostingProvider::CGenericRuleset::f_IsUpdated(CGenericRuleset const &_Wanted, NStr::CStr &o_UpdateValues) const
	{
		bool bRet = false;

		auto fCheckValue = [&](auto &_Wanted, auto &_Current, auto (CGenericRuleset::*_pMember), CStr const &_Name)
			{
				if (fg_IsUpdated(_Wanted, _Current, _pMember, _Name, o_UpdateValues))
					bRet = true;
			}
		;

		fCheckValue(_Wanted, *this, &CGenericRuleset::m_Name, "Name");
		fCheckValue(_Wanted, *this, &CGenericRuleset::m_BypassActors, "BypassActors");
		fCheckValue(_Wanted, *this, &CGenericRuleset::m_IncludeRefNames, "IncludeRefNames");
		fCheckValue(_Wanted, *this, &CGenericRuleset::m_ExcludeRefNames, "ExcludeRefNames");
		fCheckValue(_Wanted, *this, &CGenericRuleset::m_Rules, "Rules");
		fCheckValue(_Wanted, *this, &CGenericRuleset::m_Target, "Target");
		fCheckValue(_Wanted, *this, &CGenericRuleset::m_Enforcement, "Enforcement");

		return bRet;
	}

	CStr fg_EnumToString(CGitHostingProvider::EGenericRuleTarget _Value)
	{
		switch (_Value)
		{
		case CGitHostingProvider::EGenericRuleTarget::mc_Branch: return gc_Str<"Branch">;
		case CGitHostingProvider::EGenericRuleTarget::mc_Tag: return gc_Str<"Tag">;
		}

		return {};
	}

	CStr fg_EnumToString(CGitHostingProvider::EGenericRuleEnforcement _Value)
	{
		switch (_Value)
		{
		case CGitHostingProvider::EGenericRuleEnforcement::mc_Disabled: return gc_Str<"Disabled">;
		case CGitHostingProvider::EGenericRuleEnforcement::mc_Active: return gc_Str<"Active">;
		case CGitHostingProvider::EGenericRuleEnforcement::mc_Evaluate: return gc_Str<"Evaluate">;
		}

		return {};
	}

	CStr fg_EnumToString(CGitHostingProvider::EGenericRuleBypassMode _Value)
	{
		switch (_Value)
		{
		case CGitHostingProvider::EGenericRuleBypassMode::mc_Always: return gc_Str<"Always">;
		case CGitHostingProvider::EGenericRuleBypassMode::mc_PullRequest: return gc_Str<"PullRequest">;
		}

		return {};
	}

	CStr fg_EnumToString(CGitHostingProvider::EStringMatchOperator _Value)
	{
		switch (_Value)
		{
		case CGitHostingProvider::EStringMatchOperator::mc_StartsWith: return gc_Str<"StartsWith">;
		case CGitHostingProvider::EStringMatchOperator::mc_EndsWith: return gc_Str<"EndsWith">;
		case CGitHostingProvider::EStringMatchOperator::mc_Contains: return gc_Str<"Contains">;
		case CGitHostingProvider::EStringMatchOperator::mc_Regex: return gc_Str<"Regex">;
		}

		return {};
	}
}
