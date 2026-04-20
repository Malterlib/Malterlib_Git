// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/Core/RuntimeType>
#include <Mib/Web/HttpClient>

namespace NMib::NGit
{
	enum class EGitHostingProviderErrorCode : uint32
	{
		mc_None
		, mc_ResourceDoesNotExist
		, mc_ResourceAlreadyExists
		, mc_MissingRequiredParameter
		, mc_InvalidParameter
		, mc_ParametersInvalid
		, mc_Custom

	};

	struct CGitHostingProviderError
	{
		template <typename tf_CStream>
		void f_Stream(tf_CStream &_Stream);

		template <typename tf_CStr>
		void f_Format(tf_CStr &o_String) const
		{
			o_String += typename tf_CStr::CFormat("Code: {} Resource: {} Field: {} Message: {}") << m_Code << m_Resource << m_Field << m_Message;
		}

		CStr m_Resource;
		CStr m_Field;
		CStr m_Message;
		EGitHostingProviderErrorCode m_Code = EGitHostingProviderErrorCode::mc_None;
	};

	struct CGitHostingProviderExceptionData : public CHttpClientRequestExceptionData
	{
		template <typename tf_CStream>
		void f_Stream(tf_CStream &_Stream);

		bool f_HasError(CStr const &_Field, EGitHostingProviderErrorCode _ErrorCode, CStr const &_Resource = {}, CStr const &_Message = {}) const;

		CStr m_GitRawError;
		TCVector<CGitHostingProviderError> m_GitErrors;
	};

	DMibImpErrorSpecificClassDefine(CGitHostingProviderException, NMib::NException::CException, CGitHostingProviderExceptionData);

#	define DMibErrorGitHostingProvider(d_Description, d_Specific) DMibImpErrorSpecific(NMib::NGit::CGitHostingProviderException, d_Description, d_Specific)
#	define DMibErrorInstanceGitHostingProvider(d_Description, d_Specific) DMibImpExceptionInstanceSpecific(NMib::NGit::CGitHostingProviderException, d_Description, d_Specific)

	struct CGitHostingProvider;

	struct ICGitHostingProviderFactory
	{
		virtual ~ICGitHostingProviderFactory() = default;
		virtual TCActor<CGitHostingProvider> operator () () = 0;
	};

	struct CGitHostingProvider : public NConcurrency::CActor
	{
		struct CHostingProviderDescription
		{
			NStr::CStr m_Name;
			NStr::CStr m_ClassName;
		};

		struct CUser
		{
			COrdering_Partial operator <=> (CUser const &_Right) const noexcept;
			bool operator == (CUser const &_Right) const noexcept;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Login;
			NStr::CStr m_ID;
			NStr::CStr m_Name;
		};

		struct CApp
		{
			COrdering_Partial operator <=> (CApp const &_Right) const noexcept;
			bool operator == (CApp const &_Right) const noexcept;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Slug;
			NStr::CStr m_ID;
			NStr::CStr m_Name;
		};

		struct CTeam
		{
			COrdering_Partial operator <=> (CTeam const &_Right) const noexcept;
			bool operator == (CTeam const &_Right) const noexcept;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Slug;
			NStr::CStr m_ID;
			NStr::CStr m_Name;
		};

		struct CRepositoryReference
		{
			COrdering_Partial operator <=> (CRepositoryReference const &_Right) const noexcept;
			bool operator == (CRepositoryReference const &_Right) const noexcept;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Slug;
			NStr::CStr m_ID;
		};

		struct CRepositoryRole
		{
			COrdering_Partial operator <=> (CRepositoryRole const &_Right) const noexcept;
			bool operator == (CRepositoryRole const &_Right) const noexcept;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_ID;
			NStr::CStr m_Name;
		};

		struct COrganizationAdmin
		{
			auto operator <=> (COrganizationAdmin const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CRequiredStatusCheck
		{
			auto operator <=> (CRequiredStatusCheck const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStr::CStr m_Context;
			NStorage::TCOptional<CApp> m_App;
		};

		using CGitActor = NStorage::TCVariant<CUser, CTeam, CApp>;
		using CGenericRuleGitActor = NStorage::TCVariant<CTeam, CApp, CRepositoryRole, COrganizationAdmin>;

		struct CBranchProtectionRule
		{
			bool f_IsUpdated(CBranchProtectionRule const &_Wanted, NStr::CStr &o_UpdateValues) const;

			NStorage::TCOptional<NStr::CStr> m_Pattern;
			NStorage::TCOptional<NStr::CStr> m_Creator;

			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_PushAllowances;
			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_ReviewDismissalAllowances;
			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_BypassForcePushAllowances;
			NStorage::TCOptional<NContainer::TCVector<CGitActor>> m_BypassPullRequestAllowances;

			NStorage::TCOptional<NContainer::TCVector<NStr::CStr>> m_RequiredStatusCheckContexts;
			NStorage::TCOptional<NContainer::TCVector<CRequiredStatusCheck>> m_RequiredStatusChecks;

			NStorage::TCOptional<uint32> m_RequiredApprovingReviewCount;

			NStorage::TCOptional<bool> m_AllowsForcePushes;
			NStorage::TCOptional<bool> m_AllowsDeletions;
			NStorage::TCOptional<bool> m_BlocksCreations;
			NStorage::TCOptional<bool> m_DismissesStaleReviews;
			NStorage::TCOptional<bool> m_IsAdminEnforced;
			NStorage::TCOptional<bool> m_RequiresApprovingReviews;
			NStorage::TCOptional<bool> m_RequiresCodeOwnerReviews;
			NStorage::TCOptional<bool> m_RequiresCommitSignatures;
			NStorage::TCOptional<bool> m_RequiresConversationResolution;
			NStorage::TCOptional<bool> m_RequiresLinearHistory;
			NStorage::TCOptional<bool> m_RequiresStatusChecks;
			NStorage::TCOptional<bool> m_RequiresStrictStatusChecks;
			NStorage::TCOptional<bool> m_RestrictsPushes;
			NStorage::TCOptional<bool> m_RestrictsReviewDismissals;
		};

		enum class EGenericRuleTarget : uint8
		{
			mc_Branch
			, mc_Tag
		};

		enum class EGenericRuleEnforcement : uint8
		{
			mc_Disabled
			, mc_Active
			, mc_Evaluate
		};

		enum class EGenericRuleBypassMode : uint8
		{
			mc_Always
			, mc_PullRequest
		};

		enum class EStringMatchOperator : uint8
		{
			mc_StartsWith
			, mc_EndsWith
			, mc_Contains
			, mc_Regex
		};

		enum class EAllowedActions : uint8
		{
			mc_All
			, mc_LocalOnly
			, mc_Selected
		};

		enum class EActionsAccessOutsideOfRepository : uint8
		{
			mc_None
			, mc_User
			, mc_Organization
		};

		enum class EActionsWorkflowPermissions : uint8
		{
			mc_Read
			, mc_Write
		};

		struct CGenericRuleBypassActor
		{
			auto operator <=> (CGenericRuleBypassActor const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			CGenericRuleGitActor m_Actor;
			EGenericRuleBypassMode m_BypassMode = EGenericRuleBypassMode::mc_Always;
		};

		struct CGenericRule_Creation
		{
			auto operator <=> (CGenericRule_Creation const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CGenericRule_Update
		{
			auto operator <=> (CGenericRule_Update const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			bool m_bAllowFetchAndMerge = false;
		};

		struct CGenericRule_Deletion
		{
			auto operator <=> (CGenericRule_Deletion const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CGenericRule_LinearHistory
		{
			auto operator <=> (CGenericRule_LinearHistory const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CGenericRule_Deployments
		{
			auto operator <=> (CGenericRule_Deployments const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NContainer::TCVector<NStr::CStr> m_RequiredDeploymentEnvironments;
		};

		struct CGenericRule_Signatures
		{
			auto operator <=> (CGenericRule_Signatures const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CGenericRule_PullRequest
		{
			auto operator <=> (CGenericRule_PullRequest const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			uint8 m_RequiredApprovingReviewCount = 0;
			bool m_bDismissStaleReviewsOnPush = false;
			bool m_bRequireCodeOwnerReview = false;
			bool m_bRequireLastPushApproval = false;
			bool m_bRequireReviewThreadResolution = false;
		};

		struct CGenericRule_StatusChecks
		{
			auto operator <=> (CGenericRule_StatusChecks const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			struct CRequiredStatusCheck
			{
				auto operator <=> (CRequiredStatusCheck const &_Right) const noexcept = default;
				template <typename tf_CStr>
				void f_Format(tf_CStr &o_Str) const;

				NStr::CStr m_Context;
				NStorage::TCOptional<CApp> m_App;
			};

			NContainer::TCVector<CRequiredStatusCheck> m_RequiredStatusChecks;
			bool m_bPullRequestsMustBeTestedWithLatestCode = false;
		};

		struct CGenericRule_FastForwardOnly
		{
			auto operator <=> (CGenericRule_FastForwardOnly const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CStringMatch
		{
			auto operator <=> (CStringMatch const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			NStorage::TCOptional<NStr::CStr> m_Name;
			NStorage::TCOptional<bool> m_Negate;
			NStr::CStr m_Pattern;
			EStringMatchOperator m_Operator = EStringMatchOperator::mc_Contains;
		};

		struct CGenericRule_CommitMessage
		{
			auto operator <=> (CGenericRule_CommitMessage const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			CStringMatch m_StringMatch;
		};

		struct CGenericRule_CommitAuthorEmail
		{
 			auto operator <=> (CGenericRule_CommitAuthorEmail const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			CStringMatch m_StringMatch;
		};

		struct CGenericRule_CommitterEmail
		{
 			auto operator <=> (CGenericRule_CommitterEmail const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			CStringMatch m_StringMatch;
		};

		struct CGenericRule_BranchName
		{
 			auto operator <=> (CGenericRule_BranchName const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			CStringMatch m_StringMatch;
		};

		struct CGenericRule_TagName
		{
 			auto operator <=> (CGenericRule_TagName const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			CStringMatch m_StringMatch;
		};

		struct CGenericRule_Workflow
		{
 			auto operator <=> (CGenericRule_Workflow const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			struct CRequiredWorkflow
			{
				auto operator <=> (CRequiredWorkflow const &_Right) const noexcept = default;

				template <typename tf_CStr>
				void f_Format(tf_CStr &o_Str) const;

				NStr::CStr m_Path;
				CRepositoryReference m_WorkflowRepository;
				NStorage::TCOptional<NStr::CStr> m_Ref;
				NStorage::TCOptional<NStr::CStr> m_Sha;
			};

			NContainer::TCVector<CRequiredWorkflow> m_Workflows;
		};

		using CGenericRule = TCVariant
			<
				CGenericRule_Creation
				, CGenericRule_Update
				, CGenericRule_Deletion
				, CGenericRule_LinearHistory
				, CGenericRule_Deployments
				, CGenericRule_Signatures
				, CGenericRule_PullRequest
				, CGenericRule_StatusChecks
				, CGenericRule_FastForwardOnly
				, CGenericRule_CommitMessage
				, CGenericRule_CommitAuthorEmail
				, CGenericRule_CommitterEmail
				, CGenericRule_BranchName
				, CGenericRule_TagName
				, CGenericRule_Workflow
			>
		;

		struct CGenericRuleset
		{
 			auto operator <=> (CGenericRuleset const &_Right) const noexcept = default;
			bool f_IsUpdated(CGenericRuleset const &_Wanted, NStr::CStr &o_UpdateValues) const;

			NStorage::TCOptional<NStr::CStr> m_Name;
			NStorage::TCOptional<TCVector<CGenericRuleBypassActor>> m_BypassActors;
			NStorage::TCOptional<TCVector<NStr::CStr>> m_IncludeRefNames;
			NStorage::TCOptional<TCVector<NStr::CStr>> m_ExcludeRefNames;
			NStorage::TCOptional<TCVector<CGenericRule>> m_Rules;
			NStorage::TCOptional<EGenericRuleTarget> m_Target;
			NStorage::TCOptional<EGenericRuleEnforcement> m_Enforcement;
		};

		struct CRepositoryPermissions
		{
			NContainer::TCMap<NStr::CStr, NStr::CStr> m_TeamPermissions;
			NContainer::TCMap<NStr::CStr, NStr::CStr> m_UserPermissions;
		};

		struct CRepository
		{
			enum class ESquashMergeCommitTitle : uint32
			{
				mc_PrTitle
				, mc_CommitOrPrTitle
			};

			enum class ESquashMergeCommitMessage : uint32
			{
				mc_PrBody
				, mc_CommitMessages
				, mc_Blank
			};

			enum class EMergeCommitTitle : uint32
			{
				mc_PrTitle
				, mc_MergeMessage
			};

			enum class EMergeCommitMessage : uint32
			{
				mc_PrBody
				, mc_PrTitle
				, mc_Blank
			};

			bool f_IsUpdated(CRepository const &_Wanted, NStr::CStr &o_UpdateValues) const;

			NStorage::TCOptional<NStr::CStr> m_Name;
			NStorage::TCOptional<NStr::CStr> m_Description;
			NStorage::TCOptional<NStr::CStr> m_Homepage;
			NStorage::TCOptional<NStr::CStr> m_DefaultBranch;
			NStorage::TCOptional<bool> m_IsPrivate;
			NStorage::TCOptional<bool> m_IsTemplate;
			NStorage::TCOptional<bool> m_HasIssues;
			NStorage::TCOptional<bool> m_HasProjects;
			NStorage::TCOptional<bool> m_HasWiki;
			NStorage::TCOptional<bool> m_HasDiscussions;
			NStorage::TCOptional<bool> m_HasDownloads;
			NStorage::TCOptional<bool> m_AllowForking;
			NStorage::TCOptional<bool> m_AllowSquashMerge;
 			NStorage::TCOptional<bool> m_AllowMergeCommit;
			NStorage::TCOptional<bool> m_AllowRebaseMerge;
			NStorage::TCOptional<bool> m_AllowAutoMerge;
			NStorage::TCOptional<bool> m_AllowUpdateBranch;
			NStorage::TCOptional<bool> m_WebCommitSignoffRequired;
			NStorage::TCOptional<bool> m_DeleteBranchOnMerge;
			NStorage::TCOptional<bool> m_UseSquashPrTitleAsDefault;
			NStorage::TCOptional<bool> m_Archived;

			NStorage::TCOptional<ESquashMergeCommitTitle> m_SquashMergeCommitTitle;
			NStorage::TCOptional<ESquashMergeCommitMessage> m_SquashMergeCommitMessage;
			NStorage::TCOptional<EMergeCommitTitle> m_MergeCommitTitle;
			NStorage::TCOptional<EMergeCommitMessage> m_MergeCommitMessage;

			NStorage::TCOptional<bool> m_Security_AdvancedEnable;
			NStorage::TCOptional<bool> m_Security_SecretScanning;
			NStorage::TCOptional<bool> m_Security_SecretScanningPushProtection;

			NStorage::TCOptional<TCMap<CStr, TCOptional<CStr>>> m_CustomProperties;
		};

		struct CRepositoryStats
		{
			uint32 m_ForksCount = 0;
			uint32 m_StargazersCount = 0;
			uint32 m_WatchersCount = 0;
			uint32 m_OpenIssuesCount = 0;
			uint32 m_SubscribersCount = 0;
			uint32 m_NetworkCount = 0;
			uint32 m_SizeKiloBytes = 0;
		};

		struct CGetRepository : public CRepository
		{
			NStorage::TCOptional<NStr::CStr> m_ForkedFromRepository;
			NStorage::TCOptional<NStr::CStr> m_Language;
			CRepositoryStats m_Stats;
			bool m_bHasPages = false;
			bool m_bDisabled = false;
		};

		struct CCreateRepository : public CRepository
		{
			NStorage::TCOptional<NStr::CStr> m_Organization;
			NStorage::TCOptional<NStr::CStr> m_LicenseTemplate;
			NStorage::TCOptional<NStr::CStr> m_GitIgnoreTemplate;
			NStorage::TCOptional<bool> m_AutoInit;
		};

		struct CForkRepository
		{
			NStorage::TCOptional<NStr::CStr> m_Organization;
			NStr::CStr m_Name;
			bool m_bDefaultBranchOnly = true;
		};

		struct CReleaseAsset
		{
			NStr::CStr m_Identifier;
			NStr::CStr m_ContentType;
			NStr::CStr m_Name;
			NStr::CStr m_Label;
			NStr::CStr m_State;
			NStr::CStr m_DownloadUrl;
			uint64 m_Size = 0;
			uint64 m_DownloadCount = 0;
		};

		struct CRelease
		{
 			NStr::CStr m_Identifier;
			NStr::CStr m_TagName;
			NContainer::TCVector<CReleaseAsset> m_Assets;
			NStr::CStr m_Name;
			NStorage::TCOptional<NStr::CStr> m_Description;
			NStr::CStr m_TargetReference;
			bool m_bPublished = false;
			bool m_bPreRelease = false;
		};

		struct CCreateRelease
		{
			NStr::CStr m_TagName;
			NStorage::TCOptional<NStr::CStr> m_Name;
			NStorage::TCOptional<NStr::CStr> m_Description;
			NStorage::TCOptional<NStr::CStr> m_TargetReference;
			NStorage::TCOptional<bool> m_Published;
			NStorage::TCOptional<bool> m_PreRelease;
			NStorage::TCOptional<bool> m_GenerateReleaseNotes;
			NStorage::TCOptional<bool> m_MakeLatest;
		};

		struct CUploadReleaseAsset
		{
			NStr::CStr m_Name;
			NStorage::TCOptional<NStr::CStr> m_Label;
			uint64 m_AssetSize = 0;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<NContainer::CByteVector> (umint _nBytes)> m_fReadData;
		};

		struct CDownloadPublicReleaseAsset
		{
			NStr::CStr m_Url;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NContainer::CByteVector _Data)> m_fWriteData;
		};

		struct CDownloadReleaseAsset
		{
			NStr::CStr m_Identifier;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NContainer::CByteVector _Data)> m_fWriteData;
		};

		struct CAllowedAction_All
		{
			auto operator <=> (CAllowedAction_All const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CAllowedAction_LocalOnly
		{
			auto operator <=> (CAllowedAction_LocalOnly const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;
		};

		struct CAllowedAction_Selected
		{
			auto operator <=> (CAllowedAction_Selected const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			bool m_bGithubOwnedAllowed = true;
			bool m_bVerifiedAllowed = true;
			TCVector<CStr> m_PatternsAllowed;
		};

		using CAllowedActions = NStorage::TCStreamableVariant
			<
				EAllowedActions
				, NStorage::TCMember<CAllowedAction_All, EAllowedActions::mc_All>
				, NStorage::TCMember<CAllowedAction_LocalOnly, EAllowedActions::mc_LocalOnly>
				, NStorage::TCMember<CAllowedAction_Selected, EAllowedActions::mc_Selected>
			>
		;

		struct CActionsSettings
		{
			bool f_IsUpdated(CActionsSettings const &_Wanted, NStr::CStr &o_UpdateValues) const;

			NStorage::TCOptional<bool> m_ActionsEnabled;
			NStorage::TCOptional<CAllowedActions> m_AllowedActions;
			NStorage::TCOptional<EActionsAccessOutsideOfRepository> m_AccessOutsideOfRepository;
			NStorage::TCOptional<EActionsWorkflowPermissions> m_DefaultPermissions;
			NStorage::TCOptional<bool> m_CanApprovePullRequestReviews;
		};

		CGitHostingProvider();

		virtual NConcurrency::TCFuture<void> f_Login(CEJsonSorted _LoginDetails) = 0;

		virtual NConcurrency::TCFuture<CGetRepository> f_CreateRepository(CCreateRepository _CreateRepository) = 0;
		virtual NConcurrency::TCFuture<CGetRepository> f_ForkRepository(NStr::CStr _Repository, CForkRepository _ForkRepository) = 0;
		virtual NConcurrency::TCFuture<CGetRepository> f_UpdateRepository(NStr::CStr _Repository, CRepository _RepositorySettings) = 0;
		virtual NConcurrency::TCFuture<NContainer::TCVector<CGetRepository>> f_GetRepositories(NContainer::TCVector<NStr::CStr> _Organizations, bool _bPersonal) = 0;
		virtual NConcurrency::TCFuture<CGetRepository> f_GetRepository(NStr::CStr _Repository) = 0;

		// Returns true if the named owner (user / org name) is an organization on the provider,
		// false if it's a personal user account. Used to decide whether to pass `organization`
		// when forking — GitHub's fork API rejects `organization=<username>` with 422.
		virtual NConcurrency::TCFuture<bool> f_IsOrganization(NStr::CStr _Owner) = 0;

		// Returns the currently-authenticated user's identity (login, id, name) as known to
		// the provider. Used when deciding fork destination: personal forks can only be made
		// into the authenticated user's own namespace, while org forks require an organization
		// target. Must be called after a successful f_Login.
		virtual NConcurrency::TCFuture<CUser> f_GetAuthenticatedUser() = 0;

		virtual NConcurrency::TCFuture<void> f_RenameBranch(NStr::CStr _Repository, NStr::CStr _OldName, NStr::CStr _NewName) = 0;

		virtual NConcurrency::TCFuture<NContainer::TCMap<NStr::CStr, CBranchProtectionRule>> f_GetBranchProtectionRules(NStr::CStr _Repository) = 0;
		virtual NConcurrency::TCFuture<void> f_UpdateBranchProtectionRule(NStr::CStr _Repository, NStr::CStr _RuleID, CBranchProtectionRule _Rule) = 0;
		virtual NConcurrency::TCFuture<NStr::CStr> f_CreateBranchProtectionRule(NStr::CStr _Repository, CBranchProtectionRule _Rule) = 0;
		virtual NConcurrency::TCFuture<void> f_DeleteBranchProtectionRule(NStr::CStr _Repository, NStr::CStr _RuleID) = 0;

		virtual NConcurrency::TCFuture<CRepositoryPermissions> f_GetRepositoryPermissions(NStr::CStr _Repository) = 0;
		virtual NConcurrency::TCFuture<void> f_AddRepositoryPermissions(NStr::CStr _Repository, CRepositoryPermissions _Permissions) = 0;
		virtual NConcurrency::TCFuture<void> f_RemoveRepositoryPermissions
			(
				NStr::CStr _Repository
				, NContainer::TCSet<NStr::CStr> _Teams
				, NContainer::TCSet<NStr::CStr> _Users
			) = 0
		;

		virtual NConcurrency::TCFuture<CRelease> f_CreateRelease(NStr::CStr _Repository, CCreateRelease _CreateRelease) = 0;
		virtual NConcurrency::TCFuture<NStorage::TCOptional<CRelease>> f_GetRelease(NStr::CStr _Repository, NStr::CStr _ReleaseTag) = 0;
		virtual NConcurrency::TCFuture<void> f_DeleteRelease(NStr::CStr _Repository, NStr::CStr _ReleaseID) = 0;
		virtual NConcurrency::TCFuture<NContainer::TCVector<CRelease>> f_GetReleases(NStr::CStr _Repository) = 0;

		virtual NConcurrency::TCFuture<CReleaseAsset> f_UploadReleaseAsset(NStr::CStr _Repository, NStr::CStr _ReleaseIdentifier, CUploadReleaseAsset _UploadRelease) = 0;
		virtual NConcurrency::TCFuture<void> f_DownloadReleaseAsset(NStr::CStr _Repository, CDownloadReleaseAsset _DownloadRelease) = 0;
		virtual NConcurrency::TCFuture<void> f_DownloadPublicReleaseAsset(NStr::CStr _Repository, CDownloadPublicReleaseAsset _DownloadRelease) = 0;
		virtual NConcurrency::TCFuture<void> f_DeleteReleaseAsset(NStr::CStr _Repository, NStr::CStr _Identifier) = 0;
		virtual NConcurrency::TCFuture<NStr::CStr> f_GetPublicReleaseAssetUrl(NStr::CStr _Repository, NStr::CStr _TagName, NStr::CStr _AssetName) = 0;

		virtual NConcurrency::TCFuture<NContainer::TCMap<NStr::CStr, CGenericRuleset>> f_GetGenericRulesets(NStr::CStr _Repository) = 0;
		virtual NConcurrency::TCFuture<CGenericRuleset> f_PopulateGenericRulesetIDs(NStr::CStr _Repository, CGenericRuleset _Ruleset) = 0;
		virtual NConcurrency::TCFuture<void> f_UpdateGenericRuleset(NStr::CStr _Repository, NStr::CStr _ID, CGenericRuleset _Ruleset) = 0;
		virtual NConcurrency::TCFuture<NStr::CStr> f_CreateGenericRuleset(NStr::CStr _Repository, CGenericRuleset _Ruleset) = 0;
		virtual NConcurrency::TCFuture<void> f_DeleteGenericRuleset(NStr::CStr _Repository, NStr::CStr _ID) = 0;

		virtual NConcurrency::TCFuture<CActionsSettings> f_GetActionsSettings(NStr::CStr _Repository) = 0;
		virtual NConcurrency::TCFuture<void> f_UpdateActionsSettings(NStr::CStr _Repository, CActionsSettings _ActionsSettings) = 0;

		static NContainer::TCMap<NStr::CStr, NStr::CStr> fs_EnumHostingProviders();
		static NConcurrency::TCActor<CGitHostingProvider> fs_CreateHostingProvider(NStr::CStr const &_ClassName);
	};
}

#define	DMibGitHostingProviderRegister(d_ClassName) \
	class CGitHostingProviderFactory_##d_ClassName : public NMib::NGit::ICGitHostingProviderFactory \
	{ \
		NConcurrency::TCActor<CGitHostingProvider> operator () () override; \
	}; \
	DMibRuntimeClassNamedCasted \
		( \
			NMib::NGit::ICGitHostingProviderFactory \
			, CGitHostingProviderFactory_##d_ClassName \
			, CGitHostingProviderFactory_##d_ClassName \
			, NMib::NGit::ICGitHostingProviderFactory \
		) \
	NConcurrency::TCActor<CGitHostingProvider> CGitHostingProviderFactory_##d_ClassName::operator ()() \
	{ \
		return fg_Construct<d_ClassName>(); \
	} \
	void fg_Malterlib_MakeActive_##d_ClassName() \
	{ \
		DMibRuntimeClassMakeActive(CGitHostingProviderFactory_##d_ClassName); \
	}

#define	DMibGitHostingProviderMakeActiveDefine(d_ClassName) extern void fg_Malterlib_MakeActive_##d_ClassName()
#define	DMibGitHostingProviderMakeActive(d_ClassName) fg_Malterlib_MakeActive_##d_ClassName()

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif

#include "Malterlib_Git_HostingProvider.hpp"
