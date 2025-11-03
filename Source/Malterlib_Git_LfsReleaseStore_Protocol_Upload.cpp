// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Process/ProcessLaunchActor>
#include <Mib/Web/Curl>

namespace NMib::NGit
{
	TCFuture<void> CLfsReleaseStoreService::fp_UploadReleaseAsset(CStr _Repository, CStr _ReleaseIdentifier, CStr _Path, uint64 _Size)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		TCActor<CProcessLaunchActor> CompressLaunch;
		CompressLaunch = fg_Construct();
		auto DestroyLaunch = co_await fg_AsyncDestroy(CompressLaunch);

		bool bShouldCompress = true;

		CStr TempDir = fp_GetTempDir();
		CStr CompressedFileName = TempDir / ("{}.tar.zst"_f << fg_RandomID());

		auto CleanupFile = g_BlockingActorSubscription / [CompressedFileName]
			{
				if (CFile::fs_FileExists(CompressedFileName))
					CFile::fs_DeleteFile(CompressedFileName);
			}
		;

		CStr AssetName = "lfs-{}.bin"_f << mp_CurrentObjectID;
		uint64 AssetSize = _Size;
		auto UploadPath = _Path;
		fp64 CompressionRatio = 1.0;

		if (bShouldCompress)
		{
			CProcessLaunchActor::CSimpleLaunch Launch
				(
					CFile::fs_GetProgramDirectory() / ("bsdtar" + CFile::mc_ExecutableExtension)
					, {"--options", "zstd:compression-level=8,zstd:threads=0", "-czaf", CompressedFileName, CFile::fs_GetFile(_Path)}
					, CFile::fs_GetPath(_Path)
					, CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode
				)
			;

			co_await CompressLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Launch));

			uint64 CompressedSize;
			{
				auto BlockingActorCheckout = fg_BlockingActor();
				CompressedSize = co_await BlockingActorCheckout.f_Actor().f_Bind<CFile::fs_GetFileSize>(CompressedFileName);
			}

			if (CompressedSize < AssetSize)
			{
				AssetName = "lfs-{}-{}-{}.tar.zst"_f << mp_CurrentObjectID << AssetSize << CompressedSize;
				CompressionRatio = fp64(AssetSize) / fp64(CompressedSize);
				AssetSize = CompressedSize;
				UploadPath = CompressedFileName;
			}

			DMibLog(Info, "Compression Ratio: {fe3}", CompressionRatio);
		}

		struct CFileReadState
		{
			CFile m_File;
		};

		TCSharedPointer<CFileReadState> pFileReadState = fg_Construct();

		auto CleanupFileState = g_BlockingActorSubscription / [pFileReadState]
			{
				pFileReadState->m_File.f_IsValid();
				pFileReadState->m_File.f_Close();
			}
		;

		co_await mp_HostingProvider
			(
				&CGitHostingProvider::f_UploadReleaseAsset
				, _Repository
				, _ReleaseIdentifier
				, CGitHostingProvider::CUploadReleaseAsset
				{
					.m_Name = AssetName
					, .m_AssetSize = AssetSize
					, .m_fReadData = g_ActorFunctor
					/ [this, AssetSize, CompressionRatio, UploadPath, pFileReadState, BytesSoFar = uint64(0), BytesLastTime = uint64(0), ReportClock = CClock{true}]
					(mint _nBytes) mutable -> TCFuture<CByteVector>
					{
						CByteVector Result;
						{
							auto BlockingActorCheckout = fg_BlockingActor();
							Result = co_await
								(
									g_Dispatch(BlockingActorCheckout) / [UploadPath, pFileReadState, _nBytes]
									{
										if (!pFileReadState->m_File.f_IsValid())
											pFileReadState->m_File.f_Open(UploadPath, EFileOpen_Read | EFileOpen_ShareAll);

										auto BytesLeftInFile = pFileReadState->m_File.f_GetLength() - pFileReadState->m_File.f_GetPosition();

										mint BytesToRead = fg_Min(mint(BytesLeftInFile), _nBytes);

										CByteVector Return;
										Return.f_SetLen(BytesToRead);

										pFileReadState->m_File.f_Read(Return.f_GetArray(), BytesToRead);

										return Return;
									}
								)
							;
						}

						if (Result.f_GetLen() != _nBytes)
							co_return DMibErrorInstance("Could not read all bytes from file. {} != {}"_f << Result.f_GetLen() << _nBytes);

						BytesSoFar += _nBytes;

						if (BytesLastTime == 0 || BytesSoFar == AssetSize || ReportClock.f_GetTime() > 1.0)
						{
							if (BytesLastTime != 0)
								ReportClock.f_AddOffset(1.0);

							uint64 BytesSoFarCorrected = (fp64(BytesSoFar) * CompressionRatio).f_ToInt();
							uint64 BytesThisTime = BytesSoFarCorrected - BytesLastTime;
							BytesLastTime = BytesSoFarCorrected;
							fp_SendProgress(mp_CurrentObjectID, BytesSoFarCorrected, BytesThisTime).f_DiscardResult();
						}

						co_return fg_Move(Result);
					}
				}
			)
		;

		co_await CleanupFileState->f_Destroy();

		co_return {};
	}

	TCFuture<void> CLfsReleaseStoreService::fp_Protocol_Upload(CEJsonSorted const _Packet)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;

		mp_CurrentObjectID = _Packet["oid"].f_String();
		auto Path = _Packet["path"].f_String();
		auto Repository = mp_HostingProviderPath.f_RemoveSuffix(".git").f_RemovePrefix("/");
		uint64 Size = _Packet["size"].f_Integer();

		auto Release = co_await fp_GetOrCreateRelease(Repository, fsp_GetTagName(mp_CurrentObjectID), true);

		CStr CompressedAssetNameStart = "lfs-{}-{}-"_f << mp_CurrentObjectID << Size;
		CStr UncompressedAssetName = "lfs-{}.bin"_f << mp_CurrentObjectID;

		bool bHasAsset = false;
		for (auto &Asset : Release.m_Assets)
		{
			if (Asset.m_Name.f_StartsWith(CompressedAssetNameStart) && Asset.m_Name.f_EndsWith(".tar.zst"))
			{
				CStr ObjectID;
				uint64 AssetSize = 0;
				uint64 CompressedSize = 0;
				aint nParsed = 0;
				(CStr::CParse("lfs-{}-{}-{}.tar.zst") >> ObjectID >> AssetSize >> CompressedSize).f_Parse(Asset.m_Name, nParsed);
				if (nParsed != 3)
					continue;

				if (Asset.m_Size != CompressedSize)
					co_return DMibErrorInstance("An asset already exists with the wrong size on the remote release");

				bHasAsset = true;
				break;
			}
			else if (Asset.m_Name == UncompressedAssetName)
			{
				if (Asset.m_Size != Size)
					co_return DMibErrorInstance("An asset already exists with the wrong size on the remote release");

				bHasAsset = true;
				break;
			}
		}

		if (!bHasAsset)
			co_await fp_UploadReleaseAsset(Repository, Release.m_Identifier, Path, Size);
		else
			DMibLog(Info, "Skipping upload, asset already exists");

		CEJsonSorted Message =
			{
				"event"_= "complete"
				, "oid"_= mp_CurrentObjectID
			}
		;

		co_await mp_pCommandLine->f_StdOut(CStr("{jp}\n"_f << Message));

		co_return {};
	}
}
