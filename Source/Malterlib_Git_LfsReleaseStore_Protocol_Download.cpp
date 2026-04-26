// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Process/ProcessLaunchActor>

namespace NMib::NGit
{
	TCFuture<CStr> CLfsReleaseStoreService::fp_DownloadReleaseAsset(CReleaseAssetInfo _AssetInfo, bool _bPublic)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		bool bCompressed = false;
		fp64 CompressionRatio = 1.0;
		umint Size = _AssetInfo.m_Size;
		if (_AssetInfo.m_CompressedSize)
		{
			CompressionRatio = fp64(_AssetInfo.m_Size) / fp64(_AssetInfo.m_CompressedSize);
			bCompressed = true;
			Size = _AssetInfo.m_CompressedSize;
		}

		struct CFileReadState
		{
			CFile m_File;
			bool m_bFinished = false;
		};

		TCSharedPointer<CFileReadState> pFileReadState = fg_Construct();

		CStr TempDir = fp_GetTempDir();

		CStr FilePath;
		if (bCompressed)
			FilePath = TempDir / ("{}.tar.zst"_f << fg_FastRandomID());
		else
			FilePath = TempDir / ("{}.bin"_f << fg_FastRandomID());

		auto CleanupFileState = g_BlockingActorSubscription / [pFileReadState, FilePath]
			{
				pFileReadState->m_File.f_Close();

				if (!pFileReadState->m_bFinished)
					CFile::fs_DeleteFile(FilePath);
			}
		;

		// The write callback is move-only and consumed by each request. Wrap its
		// construction in a factory so the retry loop below can hand a fresh
		// callback (with reset byte counters) to each attempt without duplicating
		// the lambda body.
		auto fGetWriteData = [this, Size, CompressionRatio, FilePath, pFileReadState]
			() -> TCActorFunctor<TCFuture<void> (CByteVector)>
			{
				return g_ActorFunctor / [this, Size, CompressionRatio, FilePath, pFileReadState, BytesSoFar = uint64(0), BytesLastTime = uint64(0), Stopwatch = CStopwatch{true}]
					(CByteVector _Data) mutable -> TCFuture<void>
					{
						umint nBytes = _Data.f_GetLen();
						{
							auto BlockingActorCheckout = fg_BlockingActor();
							co_await
								(
									g_Dispatch(BlockingActorCheckout) / [FilePath, pFileReadState, Data = fg_Move(_Data)]
									{
										if (!pFileReadState->m_File.f_IsValid())
										{
											CFile::fs_CreateDirectoryForFile(FilePath);
											pFileReadState->m_File.f_Open(FilePath, EFileOpen_Write | EFileOpen_ShareAll);
										}

										pFileReadState->m_File.f_Write(Data.f_GetArray(), Data.f_GetLen());
									}
								)
							;
						}
						BytesSoFar += nBytes;

						if (BytesLastTime == 0 || BytesSoFar == Size || Stopwatch.f_GetTime() > 1.0)
						{
							if (BytesLastTime != 0)
								Stopwatch.f_AddOffset(1.0);

							uint64 BytesSoFarCorrected = (fp64(BytesSoFar) * CompressionRatio).f_ToInt();
							uint64 BytesThisTime = BytesSoFarCorrected - BytesLastTime;
							BytesLastTime = BytesSoFarCorrected;
							fp_SendProgress(mp_CurrentObjectID, BytesSoFarCorrected, BytesThisTime).f_DiscardResult();
						}

						co_return {};
					}
				;
			}
		;

		if (!_bPublic)
			co_await fp_EnsureLogin(true);

		// Spurious 5xx responses from the GitHub release-asset CDN happen often
		// enough during clones from public repositories to break CI. Retry with
		// exponential backoff on transient HTTP statuses; bounded so a real
		// outage still surfaces.
		constexpr umint c_MaxAttempts = 6;
		TCVector<uint32> AttemptStatusCodes;
		CStr PrivateLoginFailure;
		bool bPrivateRetry = false;
		for (umint iAttempt = 0; ; ++iAttempt)
		{
			if (iAttempt > 0)
			{
				// Discard partial bytes from the failed attempt before the next one
				// starts writing.
				auto BlockingActorCheckout = fg_BlockingActor();
				co_await
					(
						g_Dispatch(BlockingActorCheckout) / [pFileReadState, FilePath]
						{
							pFileReadState->m_File.f_Close();
							if (CFile::fs_FileExists(FilePath))
								CFile::fs_DeleteFile(FilePath);
						}
					)
				;
			}

			TCAsyncResult<void> Result;
			if (_bPublic)
			{
				Result = co_await mp_HostingProvider
					(
						&CGitHostingProvider::f_DownloadPublicReleaseAsset
						, _AssetInfo.m_Repository
						, CGitHostingProvider::CDownloadPublicReleaseAsset
						{
							.m_Url = _AssetInfo.m_PublicDownloadUrl
							, .m_fWriteData = fGetWriteData()
						}
					).f_Wrap()
				;
			}
			else
			{
				Result = co_await mp_HostingProvider
					(
						&CGitHostingProvider::f_DownloadReleaseAsset
						, _AssetInfo.m_Repository
						, CGitHostingProvider::CDownloadReleaseAsset
						{
							.m_Identifier = _AssetInfo.m_ID
							, .m_fWriteData = fGetWriteData()
						}
					).f_Wrap()
				;
			}

			if (Result)
				break;

			bool bShouldRetry = false;
			uint32 AttemptStatusCode = 0;
			NException::fg_VisitException<CGitHostingProviderException>
				(
					Result.f_GetException()
					, [&](CGitHostingProviderException const &_Exception)
					{
						uint32 Status = _Exception.f_GetSpecific().m_StatusCode;
						AttemptStatusCode = Status;
						if (Status == 408 || Status == 429 || (Status >= 500 && Status <= 599))
							bShouldRetry = true;
					}
				)
			;
			AttemptStatusCodes.f_Insert(AttemptStatusCode);

			if (!bShouldRetry || iAttempt + 1 >= c_MaxAttempts)
			{
				if (iAttempt == 0 && !bShouldRetry)
					co_return Result.f_GetException();

				CStr PrivateLoginMessage;
				if (PrivateLoginFailure)
					PrivateLoginMessage = "\nPrivate fallback login failure: {}"_f << PrivateLoginFailure;
				else if (bPrivateRetry)
					PrivateLoginMessage = "\nLast attempt switched to authenticated download.";

				auto pException = Result.f_GetException();
				co_return DMibErrorInstanceWrapped
					(
						"Release asset download failed after {} attempts ({} retries, final error retryable: {}, status codes: {vs}): {}{}"_f
						<< (iAttempt + 1)
						<< iAttempt
						<< bShouldRetry
						<< AttemptStatusCodes
						<< Result.f_GetExceptionStr()
						<< PrivateLoginMessage
						, fg_Move(pException)
						, false
					).f_ExceptionPointer()
				;
			}

			// After five failed public CDN attempts, immediately try the authenticated release
			// asset API as a last resort when credentials are available. A stale token
			// can make this final attempt fail with an auth error, but by this point
			// another public URL retry is likely less useful than switching paths.
			if (iAttempt == 4 && _bPublic)
			{
				auto fLogPrivateRetry = [&]
					{
						DMibLog
							(
								Info
								, "Release asset download failed (attempt {}/{}); retrying with authenticated download: {}"
								, iAttempt + 1
								, c_MaxAttempts
								, Result.f_GetExceptionStr()
							)
						;
					}
				;
				if (mp_bLoggedIn)
				{
					_bPublic = false;
					bPrivateRetry = true;
					fLogPrivateRetry();
					continue;
				}
				else
				{
					auto LoginResult = co_await fp_EnsureLogin(false).f_Wrap();

					if (LoginResult && mp_bLoggedIn)
					{
						_bPublic = false;
						bPrivateRetry = true;
						fLogPrivateRetry();
						continue;
					}
					else if (!LoginResult)
					{
						PrivateLoginFailure = LoginResult.f_GetExceptionStr();
						DMibLog
							(
								Info
								, "Release asset private-download fallback login failed; continuing with public URL: {}"
								, PrivateLoginFailure
							)
						;
					}
				}
			}

			fp64 BackoffSeconds = fg_Min(fp64(60), fp64(uint64(1) << iAttempt));
			DMibLog
				(
					Info
					, "Release asset download failed (attempt {}/{}); retrying in {fe1}s: {}"
					, iAttempt + 1
					, c_MaxAttempts
					, BackoffSeconds
					, Result.f_GetExceptionStr()
				)
			;
			co_await fg_Timeout(BackoffSeconds);
		}

		TCActor<CProcessLaunchActor> CompressLaunch;
		CompressLaunch = fg_Construct();
		auto DestroyLaunch = co_await fg_AsyncDestroy(CompressLaunch);

		pFileReadState->m_bFinished = true;

		co_await CleanupFileState->f_Destroy();

		if (bCompressed)
		{
			auto CleanupFileStateCompressed = g_BlockingActorSubscription / [pFileReadState, FilePath]
				{
					CFile::fs_DeleteFile(FilePath);
				}
			;

			CProcessLaunchActor::CSimpleLaunch Launch
				(
					CFile::fs_GetProgramDirectory() / ("bsdtar" + CFile::mc_ExecutableExtension)
					, {"-xvf", FilePath}
					, TempDir
					, CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode
				)
			;

			CStr ExtractedFiles = (co_await CompressLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Launch))).f_GetStdErr().f_Trim();

			TCVector<CStr> Files;
			for (auto &Line : ExtractedFiles.f_SplitLine())
			{
				auto File = Line.f_RemovePrefix("x ");
				if (File.f_StartsWith("._"))
				{
					auto BlockingActorCheckout = fg_BlockingActor();
					co_await
						(
							g_Dispatch(BlockingActorCheckout) / [ToDelete = TempDir / File]
							{
								CFile::fs_DeleteFile(ToDelete);
							}
						)
					;

					continue;
				}
				Files.f_Insert(fg_Move(File));
			}

			if (Files.f_GetLen() != 1)
				co_return DMibErrorInstance("Expected only one file in tar archive. Got: {vs}"_f << Files);

			co_await CleanupFileStateCompressed->f_Destroy();

			co_return TempDir / Files[0];
		}

		co_return FilePath;
	}

	auto CLfsReleaseStoreService::fp_GetReleaseAssetInfo(TCSharedPointer<CReleaseIndexCache> _pIndexCache, CStr _Repository, CStr _ObjectID, uint64 _Size) -> TCFuture<CReleaseAssetInfo>
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		auto *pCachedInfo = _pIndexCache->m_Assets.f_FindEqual(mp_CurrentObjectID);
		if (pCachedInfo)
			co_return *pCachedInfo;

		co_await fp_EnsureLogin(true);

		auto Repository = _Repository;

		CStr TagName = fsp_GetTagName(mp_CurrentObjectID);

		TCOptional<CGitHostingProvider::CReleaseAsset> ReleaseAsset;

		uint64 CompressedSize = 0;

		while (true)
		{
			auto ExistingRelease = co_await mp_HostingProvider(&CGitHostingProvider::f_GetRelease, Repository, TagName);
			if (ExistingRelease)
			{
				auto &Release = *ExistingRelease;

				CStr CompressedAssetNameStart = "lfs-{}-{}-"_f << mp_CurrentObjectID << _Size;
				CStr UncompressedAssetName = "lfs-{}.bin"_f << mp_CurrentObjectID;

				for (auto &Asset : Release.m_Assets)
				{
					if (Asset.m_Name.f_StartsWith(CompressedAssetNameStart) && Asset.m_Name.f_EndsWith(".tar.zst"))
					{
						CStr ObjectID;
						uint64 AssetSize = 0;
						aint nParsed = 0;
						(CStr::CParse("lfs-{}-{}-{}.tar.zst") >> ObjectID >> AssetSize >> CompressedSize).f_Parse(Asset.m_Name, nParsed);
						if (nParsed != 3)
							continue;

						ReleaseAsset = Asset;
						break;
					}
					else if (Asset.m_Name == UncompressedAssetName)
					{
						ReleaseAsset = Asset;
						break;
					}
				}

				if (ReleaseAsset)
					break;
			}

			auto pRepository = co_await fp_GetCachedRepository(Repository);

			if (!pRepository->m_ForkedFromRepository)
			{
				co_return DMibErrorInstance
					(
						"Could not find release asset for object ID '{}' with tag '{}' on any remote in the chain of forked repositories"_f
						<< mp_CurrentObjectID
						<< TagName
					)
				;
			}

			Repository = *pRepository->m_ForkedFromRepository;
		}

		if (!ReleaseAsset)
			co_return DMibErrorInstance("Could not find release asset on remote");

		co_return CReleaseAssetInfo
			{
				.m_ID = ReleaseAsset->m_Identifier
				, .m_Repository = Repository
				, .m_PublicDownloadUrl = ReleaseAsset->m_DownloadUrl
				, .m_Size = _Size
				, .m_CompressedSize = CompressedSize
			}
		;
	}

	TCFuture<void> CLfsReleaseStoreService::fp_Protocol_Download(CEJsonSorted const _Packet)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;

		mp_CurrentObjectID = _Packet["oid"].f_String();

		auto Repository = mp_HostingProviderPath.f_RemoveSuffix(".git").f_RemovePrefix("/");
		uint64 Size = _Packet["size"].f_Integer();

		auto pCache = co_await fp_GetReleaseIndexCache(Repository);
		auto ReleaseAssetInfo = co_await fp_GetReleaseAssetInfo(pCache, Repository, mp_CurrentObjectID, Size);

		CStr FilePath = co_await fp_DownloadReleaseAsset(ReleaseAssetInfo, pCache->m_bPublicRepository);

		CEJsonSorted Message =
			{
				"event"_= "complete"
				, "oid"_= mp_CurrentObjectID
				, "path"_= FilePath
			}
		;

		co_await mp_pCommandLine->f_StdOut(CStr("{jp}\n"_f << Message));

		co_return {};
	}
}
