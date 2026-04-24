// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Git/Helpers/Credentials>
#include <Mib/Git/Helpers/Launch>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	namespace
	{
		constexpr CStr gc_LfsReleaseTargetTagName = gc_Str<"lfs/index">;

		CStr fg_ParseLsRemoteHash(CStr const &_Output, CStr const &_Ref)
		{
			CStr Hash;
			CStr PeeledHash;

			for (auto &Line : _Output.f_SplitLine())
			{
				CStr LineHash;
				CStr LineRef;
				aint nParsed = 0;
				(CStr::CParse("{}\t{}") >> LineHash >> LineRef).f_Parse(Line, nParsed);
				if (nParsed != 2 || LineHash.f_IsEmpty())
					continue;

				if (LineRef == _Ref)
					Hash = LineHash;
				else if (LineRef == _Ref + "^{}")
					PeeledHash = LineHash;
			}

			return PeeledHash ? PeeledHash : Hash;
		}
	}

	CStr CLfsReleaseStoreService::fsp_GetTagName(CStr const &_ObjectID)
	{
		CStr ObjectID64 = "0x" + _ObjectID.f_Left(16);
		uint64 ObjectIDPrefix = ObjectID64.f_ToInt(uint64(0));

		umint nBits = fg_GetHighestBitSet(mc_ReleaseBuckets) + 1;
		umint nChars = (nBits + 3) / 4;

		return "lfs/{nfh,sf0,sj*}"_f << (ObjectIDPrefix % mc_ReleaseBuckets) << nChars;
	}

	TCFuture<CStr> CLfsReleaseStoreService::fp_EnsureLfsReleaseTarget()
	{
		if (mp_bLfsReleaseTargetInitialized && mp_LfsReleaseTargetHash)
			co_return mp_LfsReleaseTargetHash;

		CStr TargetTagRef = "refs/tags/{}"_f << gc_LfsReleaseTargetTagName;
		mp_LfsReleaseTargetHash = fg_ParseLsRemoteHash(co_await fg_LaunchGit({"ls-remote", "--tags", mp_RemoteUrl, TargetTagRef, TargetTagRef + "^{}"}, mp_WorkingDirectory), TargetTagRef);
		if (mp_LfsReleaseTargetHash)
		{
			mp_bLfsReleaseTargetInitialized = true;
			co_return mp_LfsReleaseTargetHash;
		}

		CStr LegacyLfsBranchHash;
		{
			CStr LfsBranchRef = gc_Str<"refs/heads/lfs">.m_Str;
			CStr LfsBranch = (co_await fg_LaunchGit({"ls-remote", "--heads", mp_RemoteUrl, LfsBranchRef}, mp_WorkingDirectory)).f_Trim();
			if (LfsBranch)
			{
				LegacyLfsBranchHash = fg_ParseLsRemoteHash(LfsBranch, LfsBranchRef);
				if (LegacyLfsBranchHash.f_IsEmpty())
					co_return DMibErrorInstance("Failed to parse remote LFS branch hash: {}"_f << LfsBranch);
			}
		}

		CStr TempDir = fp_GetTempDir();
		CFile::fs_CreateDirectory(TempDir);

		CStr TempRepoPath = TempDir / ("{}.repo"_f << fg_FastRandomID());

		auto Cleanup = g_BlockingActorSubscription / [TempRepoPath]
			{
				if (CFile::fs_FileExists(TempRepoPath))
					CFile::fs_DeleteDirectoryRecursive(TempRepoPath);
			}
		;

		co_await fg_LaunchGit({"clone", "--no-checkout", mp_RemoteUrl, TempRepoPath}, TempDir);

		if (LegacyLfsBranchHash)
		{
			co_await fg_LaunchGit({"fetch", "origin", "refs/heads/lfs:refs/remotes/origin/lfs"}, TempRepoPath);

			auto PushResult = co_await fg_LaunchGitWithResult({"push", "--porcelain", "origin", "refs/remotes/origin/lfs:refs/tags/{}"_f << gc_LfsReleaseTargetTagName}, TempRepoPath);
			if (PushResult.m_ExitCode != 0)
			{
				mp_LfsReleaseTargetHash = fg_ParseLsRemoteHash
					(
						co_await fg_LaunchGit({"ls-remote", "--tags", mp_RemoteUrl, TargetTagRef, TargetTagRef + "^{}"}, mp_WorkingDirectory)
						, TargetTagRef
					)
				;

				if (mp_LfsReleaseTargetHash.f_IsEmpty())
					co_return DMibErrorInstance("Push LFS release target tag from legacy LFS branch failed with: {}"_f << PushResult.f_GetStdErr());
			}
			else
				mp_LfsReleaseTargetHash = LegacyLfsBranchHash;
		}
		else
		{
			co_await fg_LaunchGit({"switch", "--orphan", "lfs-release-target"}, TempRepoPath);

			CFile::fs_WriteStringToFile(TempRepoPath / ".gitattributes", "** export-ignore\n", false);

			co_await fg_LaunchGit({"add", ".gitattributes"}, TempRepoPath);
			co_await fg_LaunchGit({"commit", "-m", "LFS release target"}, TempRepoPath);
			mp_LfsReleaseTargetHash = (co_await fg_LaunchGit({"rev-parse", "HEAD"}, TempRepoPath)).f_Trim();

			auto PushResult = co_await fg_LaunchGitWithResult({"push", "--porcelain", "origin", "HEAD:refs/tags/{}"_f << gc_LfsReleaseTargetTagName}, TempRepoPath);
			if (PushResult.m_ExitCode != 0)
			{
				mp_LfsReleaseTargetHash = fg_ParseLsRemoteHash
					(
						co_await fg_LaunchGit({"ls-remote", "--tags", mp_RemoteUrl, TargetTagRef, TargetTagRef + "^{}"}, mp_WorkingDirectory)
						, TargetTagRef
					)
				;

				if (mp_LfsReleaseTargetHash.f_IsEmpty())
					co_return DMibErrorInstance("Push LFS release target tag failed with: {}"_f << PushResult.f_GetStdErr());
			}
		}

		mp_bLfsReleaseTargetInitialized = true;

		co_await Cleanup->f_Destroy();

		co_return mp_LfsReleaseTargetHash;
	}

	CStr CLfsReleaseStoreService::fp_GetTempDir()
	{
		if (mp_TempDir)
			return mp_TempDir;

		return CFile::fs_GetUserHomeDirectory() / ".Malterlib/lfs-temp";
	}

	TCFuture<bool> CLfsReleaseStoreService::fp_Init(CStr _Remote)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		auto RemoteFuture = fg_LaunchGit({"remote", "get-url", _Remote}, mp_WorkingDirectory);
		auto EnvironmentFuture = fg_LaunchGit({"lfs", "env"}, mp_WorkingDirectory);

		mp_RemoteUrl = (co_await fg_Move(RemoteFuture)).f_Trim();

		if (mp_RemoteUrl == mp_LastRemoteUrl)
			co_return false;

		auto Environment = (co_await fg_Move(EnvironmentFuture)).f_Trim();

		for (auto &Line : Environment.f_SplitLine())
		{
			if (Line.f_StartsWith("TempDir="))
			{
				mp_TempDir = CFile::fs_GetMalterlibPath(Line.f_RemovePrefix("TempDir="));
				break;
			}
		}

		NWeb::NHTTP::CURL Url(mp_RemoteUrl);
		if (!Url.f_IsValid())
			co_return DMibErrorInstance("Failed to parse remote url");

		mp_LastRemoteUrl.f_Clear();
		mp_LfsReleaseTargetHash.f_Clear();
		mp_bLfsReleaseTargetInitialized = false;

		mp_HostingProviderProtocol = Url.f_GetScheme();
		mp_HostingProviderHost = Url.f_GetHost();
		mp_HostingProviderPath = Url.f_GetFullPath();

		if (mp_HostingProviderProtocol != "https")
			co_return DMibErrorInstance("Only https hosting provider protocol is supported. Remote '{}' has protocol: {}"_f << _Remote << mp_HostingProviderProtocol);

		if (mp_HostingProviderHost != "github.com")
			co_return DMibErrorInstance("Only github.com hosting provider is supported. Remote '{}' has host: {}"_f << _Remote << mp_HostingProviderHost);

		mp_HostingProviderToken = co_await fg_GetGitCredentials(Url, mp_WorkingDirectory);

		mp_HostingProvider = CGitHostingProvider::fs_CreateHostingProvider("CGitHostingProviderFactory_CGitHostingProvider_GitHub");

		if (mp_HostingProviderToken)
			co_await mp_HostingProvider(&CGitHostingProvider::f_Login, CEJsonSorted{"Token"_= mp_HostingProviderToken});

		if (!mp_HostingProvider)
			co_return DMibErrorInstance("GitHub hosting provider not available");

		mp_LastRemoteUrl = mp_RemoteUrl;

		co_return true;
	}

	TCFuture<CGitHostingProvider::CRelease> CLfsReleaseStoreService::fp_GetOrCreateRelease(CStr _Repository, CStr _TagName, bool _bAllowCreate)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CGitHostingProvider::CCreateRelease CreateRelease;

		CreateRelease.m_TagName = _TagName;
		CreateRelease.m_Name = CreateRelease.m_TagName;
		CreateRelease.m_Description = "LFS Storage. Do not delete or edit.";
		CreateRelease.m_GenerateReleaseNotes = false;
		CreateRelease.m_Published = true;
		CreateRelease.m_PreRelease = false;
		CreateRelease.m_MakeLatest = false;

		CGitHostingProvider::CRelease Release;

		bool bTryGetRelease = true;
		for (umint iRetry = 0; ; ++iRetry)
		{
			if (bTryGetRelease)
			{
				if (auto ExistingRelease = co_await mp_HostingProvider(&CGitHostingProvider::f_GetRelease, _Repository, CreateRelease.m_TagName))
				{
					Release = fg_Move(*ExistingRelease);
					break;
				}
				else if (!_bAllowCreate)
					co_return {};
			}
			else
				bTryGetRelease = true;

			if (_bAllowCreate)
				CreateRelease.m_TargetReference = co_await fp_EnsureLfsReleaseTarget();

			auto CreateReleaseResult = co_await mp_HostingProvider(&CGitHostingProvider::f_CreateRelease, _Repository, CreateRelease).f_Wrap();
			if (CreateReleaseResult)
			{
				Release = *CreateReleaseResult;
				break;
			}

			bool bIsMissingTarget = false;
			bool bAlreadyExists = false;
			NException::fg_VisitException<CGitHostingProviderException>
				(
					CreateReleaseResult.f_GetException()
					, [&](CGitHostingProviderException const &_Exception)
					{
						auto &Specific = _Exception.f_GetSpecific();
						if (Specific.f_HasError("TagName", EGitHostingProviderErrorCode::mc_ResourceAlreadyExists))
							bAlreadyExists = true;
						else if (Specific.f_HasError({}, EGitHostingProviderErrorCode::mc_Custom, "Release", "Published releases must have a valid tag"))
							bAlreadyExists = true;
						else if (Specific.f_HasError("TargetReference", EGitHostingProviderErrorCode::mc_InvalidParameter))
							bIsMissingTarget = true;
					}
				)
			;

			if (bAlreadyExists && iRetry < 5)
				;
			else if (bIsMissingTarget && iRetry < 5)
			{
				mp_bLfsReleaseTargetInitialized = false;
				co_await fp_EnsureLfsReleaseTarget();
				bTryGetRelease = false;
			}
			else
				co_return CreateReleaseResult.f_GetException();
		}

		co_return fg_Move(Release);
	}

	auto CLfsReleaseStoreService::fp_GetCachedReleases(CStr _Repository) -> TCFuture<TCSharedPointer<CCachedReleases>>
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		auto &pOutRepository = *mp_RepositoryCache(_Repository, fg_Construct());

		if (pOutRepository->m_bInitedReleases)
			co_return pOutRepository;

		auto &OutReleases = pOutRepository->m_Releases;

		for (auto &Release : co_await mp_HostingProvider(&CGitHostingProvider::f_GetReleases, _Repository))
		{
			if (!Release.m_Name.f_StartsWith("lfs/") || Release.m_Name == gc_LfsReleaseTargetTagName)
				continue;

			auto &OutRelease = OutReleases[Release.m_Name];
			OutRelease.m_Release = fg_Move(Release);
			for (auto &Asset : OutRelease.m_Release.m_Assets)
			{
				if (!Asset.m_Name.f_StartsWith(gc_Str<"lfs-">.m_Str))
					continue;

				CStr ObjectID;
				uint64 AssetSize = 0;
				uint64 CompressedSize = 0;

				if (Asset.m_Name.f_EndsWith(".tar.zst"))
				{
					aint nParsed = 0;
					(CStr::CParse("lfs-{}-{}-{}.tar.zst") >> ObjectID >> AssetSize >> CompressedSize).f_Parse(Asset.m_Name, nParsed);
					if (nParsed != 3)
						continue;
				}
				else if (Asset.m_Name.f_EndsWith(".bin"))
				{
					aint nParsed = 0;
					(CStr::CParse("lfs-{}.bin") >> ObjectID).f_Parse(Asset.m_Name, nParsed);
					if (nParsed != 1)
						continue;

					AssetSize = Asset.m_Size;
				}
				else
					continue;

				auto &ReleaseAsset = OutRelease.m_ReleaseAssets[ObjectID];
				ReleaseAsset.m_pSource = &Asset;
				ReleaseAsset.m_ObjectID = fg_Move(ObjectID);
				ReleaseAsset.m_PublicDownloadUrl = Asset.m_DownloadUrl;
				ReleaseAsset.m_AssetSize = AssetSize;
				ReleaseAsset.m_CompressedSize = CompressedSize;
			}
		}

		pOutRepository->m_bInitedReleases = true;
		co_return pOutRepository;
	}

	TCFuture<TCSharedPointer<CGitHostingProvider::CGetRepository>> CLfsReleaseStoreService::fp_GetCachedRepository(CStr _Repository)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		auto &pOutRepository = *mp_RepositoryCache(_Repository, fg_Construct());
		if (pOutRepository->m_pRepository)
			co_return pOutRepository->m_pRepository;

		pOutRepository->m_pRepository = fg_Construct(co_await mp_HostingProvider(&CGitHostingProvider::f_GetRepository, _Repository));

		co_return pOutRepository->m_pRepository;
	}
}
