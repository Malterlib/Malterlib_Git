// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NGit
{
	template <typename tf_CStream>
	void CGitHostingProviderError::f_Stream(tf_CStream &_Stream)
	{
		_Stream % m_Resource;
		_Stream % m_Field;
		_Stream % m_Message;
		_Stream % m_Code;
	}

	template <typename tf_CStream>
	void CGitHostingProviderExceptionData::f_Stream(tf_CStream &_Stream)
	{
		CHttpClientRequestExceptionData::f_Stream(_Stream);
		_Stream % m_GitRawError;
		_Stream % m_GitErrors;
	}

	CStr fg_EnumToString(CGitHostingProvider::EGenericRuleTarget _Value);
	CStr fg_EnumToString(CGitHostingProvider::EGenericRuleEnforcement _Value);
	CStr fg_EnumToString(CGitHostingProvider::EGenericRuleBypassMode _Value);
	CStr fg_EnumToString(CGitHostingProvider::EStringMatchOperator _Value);

	template <typename tf_CStr>
	void CGitHostingProvider::CUser::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("User: {} ({})") << m_ID << m_Login;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CApp::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("App: {} ({})") << m_ID << m_Slug;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CTeam::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Team: {} ({})") << m_ID << m_Slug;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CRequiredStatusCheck::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Context: {} App: {}") << m_Context << m_App;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CRepositoryRole::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("RepositoryRole: {} ({})") << m_ID << m_Name;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::COrganizationAdmin::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "OrganizationAdmin";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CRepositoryReference::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Repository: {} ({})") << m_ID << m_Slug;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRuleBypassActor::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Actor: {} Mode: {}") << m_Actor << fg_EnumToString(m_BypassMode);
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_Creation::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "Creation";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_Update::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Update AllowFetchAndMerge: {}") << m_bAllowFetchAndMerge;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_Deletion::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "Deletion";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_LinearHistory::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "LinearHistory";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_Deployments::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "Deployments\n";
		o_Str += typename tf_CStr::CFormat("    RequiredEnvironments: {}")
			<< CStr::fs_ToStr(m_RequiredDeploymentEnvironments).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_Signatures::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "Signatures";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_PullRequest::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "PullRequest\n";
		o_Str += typename tf_CStr::CFormat
			(
				"    RequiredApprovingReviewCount: {}\n"
				"    DismissStaleReviewsOnPush: {}\n"
				"    RequireCodeOwnerReview: {}\n"
				"    RequireLastPushApproval: {}\n"
				"    RequireReviewThreadResolution: {}"
			)
			<< CStr::fs_ToStr(m_RequiredApprovingReviewCount).f_Indent("        ", false)
			<< CStr::fs_ToStr(m_bDismissStaleReviewsOnPush).f_Indent("        ", false)
			<< CStr::fs_ToStr(m_bRequireCodeOwnerReview).f_Indent("        ", false)
			<< CStr::fs_ToStr(m_bRequireLastPushApproval).f_Indent("        ", false)
			<< CStr::fs_ToStr(m_bRequireReviewThreadResolution).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_StatusChecks::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "StatusChecks";
		o_Str += typename tf_CStr::CFormat
			(
				"    RequiredStatusChecks: {}\n"
				"    PullRequestsMustBeTestedWithLatestCode: {}"
			)
			<< CStr::fs_ToStr(m_RequiredStatusChecks).f_Indent("        ", false)
			<< CStr::fs_ToStr(m_bPullRequestsMustBeTestedWithLatestCode).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_StatusChecks::CRequiredStatusCheck::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Context: {} App: {}") << m_Context << m_App;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_FastForwardOnly::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "FastForwardOnly";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CStringMatch::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Pattern: {} Operator: {} Name: {} Negate: {}") << m_Pattern << fg_EnumToString(m_Operator) << m_Name << m_Negate;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_CommitMessage::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "CommitMessage";
		o_Str += typename tf_CStr::CFormat
			(
				"    StringMatch: {}"
			)
			<< CStr::fs_ToStr(m_StringMatch).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_CommitAuthorEmail::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "CommitAuthorEmail";
		o_Str += typename tf_CStr::CFormat
			(
				"    StringMatch: {}"
			)
			<< CStr::fs_ToStr(m_StringMatch).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_CommitterEmail::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "CommitterEmail";
		o_Str += typename tf_CStr::CFormat
			(
				"    StringMatch: {}"
			)
			<< CStr::fs_ToStr(m_StringMatch).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_BranchName::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "BranchName";
		o_Str += typename tf_CStr::CFormat
			(
				"    StringMatch: {}"
			)
			<< CStr::fs_ToStr(m_StringMatch).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_TagName::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "TagName";
		o_Str += typename tf_CStr::CFormat
			(
				"    StringMatch: {}"
			)
			<< CStr::fs_ToStr(m_StringMatch).f_Indent("        ", false)
		;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_Workflow::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "Workflow";
		o_Str += typename tf_CStr::CFormat
			(
				"    Workflows: {}"
			)
			<< CStr::fs_ToStr(m_Workflows).f_Indent("        ", false)
		;
		o_Str += typename tf_CStr::CFormat("{}");
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CGenericRule_Workflow::CRequiredWorkflow::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Path: {} WorkflowRepository: {} Ref: {} Sha: {}") << m_Path << m_WorkflowRepository << m_Ref << m_Sha;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CAllowedAction_All::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "All";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CAllowedAction_LocalOnly::f_Format(tf_CStr &o_Str) const
	{
		o_Str += "LocalOnly";
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CAllowedAction_Selected::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Selected: GithubOwnedAllowed: {} VerifiedAllowed: {} PatternsAllowed: {vs}") << m_bGithubOwnedAllowed << m_bVerifiedAllowed << m_PatternsAllowed;
	}
}
