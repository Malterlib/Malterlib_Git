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

	COrdering_Partial CGitHostingProvider::CUser::operator <=> (CUser const &_Right) const
	{
		return m_Login <=> _Right.m_Login;
	}

	bool CGitHostingProvider::CUser::operator == (CUser const &_Right) const
	{
		return m_Login == _Right.m_Login;
	}

	COrdering_Partial CGitHostingProvider::CApp::operator <=> (CApp const &_Right) const
	{
		return m_Slug <=> _Right.m_Slug;
	}

	bool CGitHostingProvider::CApp::operator == (CApp const &_Right) const
	{
		return m_Slug == _Right.m_Slug;
	}

	COrdering_Partial CGitHostingProvider::CTeam::operator <=> (CTeam const &_Right) const
	{
		return m_Slug <=> _Right.m_Slug;
	}

	bool CGitHostingProvider::CTeam::operator == (CTeam const &_Right) const
	{
		return m_Slug == _Right.m_Slug;
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

	bool CGitHostingProvider::CBranchProtectionRule::f_IsUpdated(CGitHostingProvider::CBranchProtectionRule const &_Wanted, CStr &o_UpdateValues) const
	{
		bool bRet = false;

		auto fCheckValue = [&](auto &_Wanted, auto &_Current, auto (CBranchProtectionRule::*_pMember), CStr const &_Name)
			{
				auto &Wanted = _Wanted.*_pMember;
				auto &Current = _Current.*_pMember;
				
				if (!Wanted)
					return;

				if (Wanted != Current)
				{
					bRet = true;
					CStr NewValue = "{}"_f << *Wanted;
					if (Current)
					{
						CStr OldValue = "{}"_f << *Current;
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
				}
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
}
