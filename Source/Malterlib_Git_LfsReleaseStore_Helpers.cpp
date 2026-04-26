// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Cryptography/Hashes/SHA>
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

		CStr fg_GetReleaseLockFileName(CStr const &_TempDir, CStr const &_Repository, CStr const &_TagName)
		{
			CStr LockKey = "{}\n{}"_f << _Repository << _TagName;
			CStr LockKeyHash = CHash_SHA256::fs_DigestFromData(LockKey.f_GetStr(), LockKey.f_GetLen()).f_GetString().f_Left(16);
			CStr LockName = CFile::fs_MakeNiceFilename("{}-{}"_f << _Repository << _TagName).f_Left(80);

			return _TempDir / ("release-{}-{}.lock"_f << LockKeyHash << LockName);
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
		mp_HostingProviderToken.f_Clear();
		mp_pLoginState.f_Clear();

		mp_HostingProviderProtocol = Url.f_GetScheme();
		mp_HostingProviderHost = Url.f_GetHost();
		mp_HostingProviderPath = Url.f_GetFullPath();

		if (mp_HostingProviderProtocol != "https")
			co_return DMibErrorInstance("Only https hosting provider protocol is supported. Remote '{}' has protocol: {}"_f << _Remote << mp_HostingProviderProtocol);

		if (mp_HostingProviderHost != "github.com")
			co_return DMibErrorInstance("Only github.com hosting provider is supported. Remote '{}' has host: {}"_f << _Remote << mp_HostingProviderHost);

		// Defer credential acquisition to the first GitHub API call that
		// actually needs it. fg_GetGitCredentials shells out to `git credential
		// fill`, which in environments like VS Code triggers the IDE's askpass
		// prompt. Public-repo LFS downloads complete entirely via the public
		// release-asset CDN (no API, no rate limits) and should never prompt
		// the user. fp_EnsureLogin runs the credential fetch + provider login
		// lazily, on demand, exactly once per remote.
		mp_HostingProvider = CGitHostingProvider::fs_CreateHostingProvider("CGitHostingProviderFactory_CGitHostingProvider_GitHub");

		if (!mp_HostingProvider)
			co_return DMibErrorInstance("GitHub hosting provider not available");

		mp_LastRemoteUrl = mp_RemoteUrl;

		co_return true;
	}

	TCFuture<void> CLfsReleaseStoreService::fp_EnsureLogin()
	{
		// Concurrent transfers (LFS may dispatch multiple object packets in
		// parallel within one adapter process) can all reach this method at
		// once. We funnel them through a shared waiter list: the first caller
		// drives the credential fetch + provider login; subsequent callers
		// arriving while the login is in flight register a per-caller promise
		// and await its future. When the driver finishes, it resolves every
		// queued promise with the same outcome. A one-shot promise won't do —
		// TCPromise::f_Future() can only be called once, so each waiter needs
		// its own promise/future pair.
		if (mp_pLoginState && mp_pLoginState->m_bCompleted)
		{
			if (mp_pLoginState->m_pException)
				co_return mp_pLoginState->m_pException;
			co_return {};
		}

		if (mp_pLoginState)
		{
			TCPromiseFuturePair<void> Pair;
			auto Future = fg_Move(Pair.m_Future);
			mp_pLoginState->m_Waiters.f_Insert(fg_Move(Pair.m_Promise));
			co_await fg_Move(Future);
			co_return {};
		}

		mp_pLoginState = fg_Construct<CLoginState>();
		auto pState = mp_pLoginState;

		auto Result = co_await fp_DoLogin().f_Wrap();

		pState->m_bCompleted = true;
		if (!Result)
			pState->m_pException = Result.f_GetException();

		// Resolve every waiter that queued up while we were doing the work.
		// The actor is single-threaded, so no waiter can be added between
		// setting m_bCompleted and draining the queue.
		auto Waiters = fg_Move(pState->m_Waiters);
		for (auto &Promise : Waiters)
		{
			if (pState->m_pException)
				Promise.f_SetException(pState->m_pException);
			else
				Promise.f_SetResult();
		}

		if (!Result)
			co_return Result.f_GetException();
		co_return {};
	}

	TCFuture<void> CLfsReleaseStoreService::fp_DoLogin()
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		// Try the credential helper. A failure (broken or missing helper) is
		// not fatal here — it just means subsequent API calls run
		// unauthenticated, hitting GitHub's much lower rate limit. Operations
		// that genuinely require auth (uploads, private downloads) will fail
		// at the API level with their own clearer error.
		NWeb::NHTTP::CURL Url(mp_RemoteUrl);
		auto TokenResult = co_await fg_GetGitCredentials(Url, mp_WorkingDirectory).f_Wrap();
		if (TokenResult)
			mp_HostingProviderToken = *TokenResult;

		if (mp_HostingProviderToken)
			co_await mp_HostingProvider(&CGitHostingProvider::f_Login, CEJsonSorted{"Token"_= mp_HostingProviderToken});

		co_return {};
	}

	TCFuture<CGitHostingProvider::CRelease> CLfsReleaseStoreService::fp_GetOrCreateRelease(CStr _Repository, CStr _TagName, bool _bAllowCreate)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		co_await fp_EnsureLogin();

		TCUniquePointer<CLockFile> pCreateReleaseLock;
		if (_bAllowCreate)
		{
			CStr TempDir = fp_GetTempDir();
			CStr LockFileName = fg_GetReleaseLockFileName(TempDir, _Repository, _TagName);

			// Git LFS may run several transfer adapter processes concurrently. GitHub
			// can race duplicate release creation for the same tag, so serialize the
			// get/create sequence across processes, not just within this actor.
			auto BlockingActorCheckout = fg_BlockingActor();
			pCreateReleaseLock = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [TempDir, LockFileName]() mutable
					{
						CFile::fs_CreateDirectory(TempDir);

						TCUniquePointer<CLockFile> pLockFile = fg_Construct(LockFileName);
						pLockFile->f_LockWithException(10.0 * 60.0);

						return fg_Move(pLockFile);
					}
				)
			;
		}

		auto CleanupCreateReleaseLock = g_BlockingActorSubscription / [pCreateReleaseLock = fg_Move(pCreateReleaseLock)]() mutable -> TCFuture<void>
			{
				pCreateReleaseLock.f_Clear();

				co_return {};
			}
		;

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

		co_await fp_EnsureLogin();

		auto &OutReleases = pOutRepository->m_Releases;

		for (auto &Release : co_await mp_HostingProvider(&CGitHostingProvider::f_GetReleases, _Repository))
		{
			if (!Release.m_TagName.f_StartsWith("lfs/") || Release.m_TagName == gc_LfsReleaseTargetTagName)
				continue;

			auto &OutRelease = OutReleases[Release.m_TagName];
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

		co_await fp_EnsureLogin();

		pOutRepository->m_pRepository = fg_Construct(co_await mp_HostingProvider(&CGitHostingProvider::f_GetRepository, _Repository));

		co_return pOutRepository->m_pRepository;
	}
}
