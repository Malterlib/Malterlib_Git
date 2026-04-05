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

		auto fWriteData = g_ActorFunctor / [this, Size, CompressionRatio, FilePath, pFileReadState, BytesSoFar = uint64(0), BytesLastTime = uint64(0), Stopwatch = CStopwatch{true}]
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

		if (_bPublic)
		{
			co_await mp_HostingProvider
				(
					&CGitHostingProvider::f_DownloadPublicReleaseAsset
					, _AssetInfo.m_Repository
					, CGitHostingProvider::CDownloadPublicReleaseAsset
					{
						.m_Url = _AssetInfo.m_PublicDownloadUrl
						, .m_fWriteData = fg_Move(fWriteData)
					}
				)
			;
		}
		else
		{
			co_await mp_HostingProvider
				(
					&CGitHostingProvider::f_DownloadReleaseAsset
					, _AssetInfo.m_Repository
					, CGitHostingProvider::CDownloadReleaseAsset
					{
						.m_Identifier = _AssetInfo.m_ID
						, .m_fWriteData = fg_Move(fWriteData)
					}
				)
			;
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
