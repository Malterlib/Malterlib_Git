// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Git/Helpers/Launch>

#include <zstd.h>
#include <zstd_errors.h>

namespace NMib::NGit
{
	namespace
	{
		constexpr CStr gc_IndexAssetName = gc_Str<"lfs-index.json.zst">;
		constexpr CStr gc_IndexTagName = gc_Str<"lfs/index">;
	}

	TCFuture<void> CLfsReleaseStoreService::f_UpdateReleaseIndex
		(
			CStr const &_Remote
			, EUpdateReleaseIndexOption _Options
			, TCFunction<void (CStr const &_Output)> const &_fOutputConsole
		)
	{
		mp_fOutputConsole = _fOutputConsole;

		co_await fp_Init(_Remote);

		bool bPruneOrphanAssets = fg_IsSet(_Options, EUpdateReleaseIndexOption::mc_PruneOrphanedAssets);
		bool bPretend = fg_IsSet(_Options, EUpdateReleaseIndexOption::mc_Pretend);

		auto Files = (co_await fg_LaunchGit({"lfs", "ls-files", "--all", "--long"}, mp_WorkingDirectory)).f_Trim().f_SplitLine();

		TCMap<CStr, TCSet<CStr>> ObjectIDsByTagName;
		for (auto &File : Files)
		{
			auto iSeparator = File.f_FindChar(' ');
			CStr ObjectID;
			if (iSeparator >= 0)
				ObjectID = File.f_Left(iSeparator);
			else
				ObjectID = File;

			ObjectIDsByTagName[fsp_GetTagName(ObjectID)][ObjectID];
		}

		auto Repository = mp_HostingProviderPath.f_RemoveSuffix(".git").f_RemovePrefix("/");

		CEJSONSorted IndexJson;
		auto &IndexObject = IndexJson.f_Object();

		for (auto &ObjectIDsEntry : ObjectIDsByTagName.f_Entries())
		{
			auto &TagName = ObjectIDsEntry.f_Key();
			auto &ObjectIDs = ObjectIDsEntry.f_Value();

			for (auto &ObjectID : ObjectIDs)
			{
				CStr AssetRepository = Repository;
				while (AssetRepository)
				{
					auto pReleases = co_await fp_GetCachedReleases(AssetRepository);

					auto *pRelease = pReleases->m_Releases.f_FindEqual(TagName);
					if (pRelease)
					{
						auto *pReleaseAsset = pRelease->m_ReleaseAssets.f_FindEqual(ObjectID);
						if (pReleaseAsset)
						{
							IndexObject[ObjectID] =
								{
									"ID"_= pReleaseAsset->m_pSource->m_Identifier
									, "Repository"_= AssetRepository
									, "Size"_= pReleaseAsset->m_AssetSize
									, "CompressedSize"_= pReleaseAsset->m_CompressedSize
									, "PublicDownloadUrl"_= pReleaseAsset->m_PublicDownloadUrl
								}
							;

							break;
						}
					}

					auto pRepository = co_await fp_GetCachedRepository(AssetRepository);
					if (!pRepository->m_ForkedFromRepository)
					{
						co_return DMibErrorInstance("Could not find release for tag '{}' on any remote in the chain of forked repositories"_f << TagName);
						break;
					}

					AssetRepository = *pRepository->m_ForkedFromRepository;
				}
			}
		}

		if (bPruneOrphanAssets)
		{
			auto pReleases = co_await fp_GetCachedReleases(Repository);
			for (auto &Release : pReleases->m_Releases)
			{
				auto *pObjectIDs = ObjectIDsByTagName.f_FindEqual(Release.m_Release.m_Name);

				for (auto &AssetEntry : Release.m_ReleaseAssets.f_Entries())
				{
					auto &ObjectID = AssetEntry.f_Key();
					if (pObjectIDs && pObjectIDs->f_FindEqual(ObjectID))
						continue;

					auto &Asset = AssetEntry.f_Value();

					if (mp_fOutputConsole)
						mp_fOutputConsole("{} release asset {}\n"_f << (bPretend ? "Would have deleted" : "Deleting") << Asset.m_pSource->m_Name);

					if (!bPretend)
						co_await mp_HostingProvider(&CGitHostingProvider::f_DeleteReleaseAsset, Repository, Asset.m_pSource->m_Identifier);
				}
			}
		}

		auto Release = co_await fp_GetOrCreateRelease(Repository, gc_IndexTagName, true);

		auto Contents = IndexJson.f_ToString();

		mint NeededSize = ZSTD_compressBound(Contents.f_GetLen());
		CByteVector CompressedData;
		CompressedData.f_SetLen(NeededSize);

		mint CompressedSize = ZSTD_compress(CompressedData.f_GetArray(), NeededSize, Contents.f_GetStr(), Contents.f_GetLen(), 8);

		if (ZSTD_isError(CompressedSize))
			co_return DMibErrorInstance("Failed to compress LFS index: {}"_f << ZSTD_getErrorName(CompressedSize));

		CompressedData.f_SetLen(CompressedSize);

		auto Digest = CHash_SHA256::fs_DigestFromData(CompressedData);
		CStr Label = CStr("{}: {}"_f << Contents.f_GetLen() << Digest.f_GetString());

		for (auto &OldAsset : Release.m_Assets)
		{
			if (OldAsset.m_Name == gc_IndexAssetName)
			{
				if (OldAsset.m_Label == Label)
				{
					if (mp_fOutputConsole)
						mp_fOutputConsole("Index is already up to date\n");
					co_return {};
				}

				if (!bPretend)
				{
					if (mp_fOutputConsole)
						mp_fOutputConsole("{} old index asset\n"_f << (bPretend ? "Would have deleted" : "Deleting"));
					co_await mp_HostingProvider(&CGitHostingProvider::f_DeleteReleaseAsset, Repository, OldAsset.m_Identifier);
				}
			}
		}

		if (mp_fOutputConsole)
			mp_fOutputConsole("{} index asset\n"_f << (bPretend ? "Would have updated" : "Updating"));

		if (bPretend)
			co_return {};

		co_await mp_HostingProvider
			(
				&CGitHostingProvider::f_UploadReleaseAsset
				, Repository
				, Release.m_Identifier
				, CGitHostingProvider::CUploadReleaseAsset
				{
					.m_Name = gc_IndexAssetName
					, .m_Label = Label
					, .m_AssetSize = CompressedSize
					, .m_fReadData = g_ActorFunctor / [CompressedData = fg_Move(CompressedData), StartByte = mint(0)](mint _nBytes) mutable -> TCFuture<CByteVector>
					{
						mint nBytes = fg_Min(_nBytes, CompressedData.f_GetLen() - StartByte);

						CByteVector Result;
						Result.f_Insert(CompressedData.f_GetArray() + StartByte, nBytes);

						StartByte += nBytes;

						co_return fg_Move(Result);
					}
				}
			)
		;

		co_return {};
	}

	TCFuture<TCSharedPointer<CByteVector>> CLfsReleaseStoreService::fp_DownloadReleaseIndexPublic(CStr _Repository)
	{
		auto PublicDownloadUrl = co_await mp_HostingProvider(&CGitHostingProvider::f_GetPublicReleaseAssetUrl, _Repository, gc_IndexTagName, gc_IndexAssetName);

		TCSharedPointer<CByteVector> pReleaseData = fg_Construct();
		auto PublicDownloadResult = co_await mp_HostingProvider
			(
				&CGitHostingProvider::f_DownloadPublicReleaseAsset
				, _Repository
				, CGitHostingProvider::CDownloadPublicReleaseAsset
				{
					.m_Url = PublicDownloadUrl
					, .m_fWriteData = g_ActorFunctor / [pReleaseData](CByteVector &&_Data) mutable -> TCFuture<void>
					{
						pReleaseData->f_Insert(fg_Move(_Data));

						co_return {};
					}
				}
			).f_Wrap()
		;

		if (PublicDownloadResult)
			co_return pReleaseData;

		co_return {};
	}

	auto CLfsReleaseStoreService::fp_DownloadReleaseIndexPrivate(CStr _Repository) -> TCFuture<CPrivateReleaseIndexDownload>
	{
		CPrivateReleaseIndexDownload Return;

		auto Release = co_await fp_GetOrCreateRelease(_Repository, gc_IndexTagName, false);

		TCOptional<CGitHostingProvider::CReleaseAsset> IndexAsset;
		for (auto &Asset : Release.m_Assets)
		{
			if (Asset.m_Name == gc_IndexAssetName)
			{
				IndexAsset = Asset;
				break;
			}
		}

		if (!IndexAsset || !IndexAsset->m_Label)
			co_return fg_Move(Return);

		TCSharedPointer<CByteVector> pReleaseData = fg_Construct();
		co_await mp_HostingProvider
			(
				&CGitHostingProvider::f_DownloadReleaseAsset
				, _Repository
				, CGitHostingProvider::CDownloadReleaseAsset
				{
					.m_Identifier = IndexAsset->m_Identifier
					, .m_fWriteData = g_ActorFunctor / [pReleaseData](CByteVector &&_Data) mutable -> TCFuture<void>
					{
						pReleaseData->f_Insert(fg_Move(_Data));

						co_return {};
					}
				}
			)
		;

		CStr Hash;

		aint nParsed = 0;

		(CStr::CParse("{}: {}") >> Return.m_DecompressedSize >> Hash).f_Parse(IndexAsset->m_Label, nParsed);

		if (nParsed != 2)
			co_return fg_Move(Return);

		Return.m_pData = fg_Move(pReleaseData);

		co_return fg_Move(Return);
	}

	auto CLfsReleaseStoreService::fp_GetReleaseIndexCache(CStr _Repository) -> TCFuture<TCSharedPointer<CReleaseIndexCache>>
	{
		auto &pCache = *mp_ReleaseIndexCache(_Repository, fg_Construct());
		auto &Cache = *pCache;

		if (Cache.m_bInitialized)
			co_return pCache;

		auto Cleanup = g_OnScopeExit / [&]
			{
				Cache.m_bInitialized = true;
			}
		;

		// Optimistically try to download index publically
		TCSharedPointer<CByteVector> pReleaseData = co_await fp_DownloadReleaseIndexPublic(_Repository);

		mint DecompressedSize = 0;
		bool bKnowDecompressSize = false;

		if (pReleaseData)
			Cache.m_bPublicRepository = true;
		else
		{
			auto PrivateData = co_await fp_DownloadReleaseIndexPrivate(_Repository);

			if (!PrivateData.m_pData)
			{
				auto pRepository = co_await fp_GetCachedRepository(_Repository);
				if (!pRepository->m_IsPrivate || !*pRepository->m_IsPrivate)
					Cache.m_bPublicRepository = true;

				co_return pCache;
			}

			pReleaseData = fg_Move(PrivateData.m_pData);
			DecompressedSize = PrivateData.m_DecompressedSize;
			bKnowDecompressSize = true;
		}

		static constexpr mint c_MaxBufferSize = 128 * 1024 * 1024;

		if (!bKnowDecompressSize)
			DecompressedSize = fg_Min(fg_Max(pReleaseData->f_GetLen(), 16u) * 20u, c_MaxBufferSize);

		CByteVector DecompressedData;
		DecompressedData.f_SetLen(DecompressedSize);

		mint DecompressResult = ZSTD_decompress(DecompressedData.f_GetArray(), DecompressedSize, pReleaseData->f_GetArray(), pReleaseData->f_GetLen());
		
		for (; !bKnowDecompressSize && ZSTD_getErrorCode(DecompressResult) == ZSTD_error_dstSize_tooSmall && DecompressedSize < c_MaxBufferSize;)
		{
			DecompressedSize *= 2;
			DecompressedData.f_SetLen(DecompressedSize);
			DecompressResult = ZSTD_decompress(DecompressedData.f_GetArray(), DecompressedSize, pReleaseData->f_GetArray(), pReleaseData->f_GetLen());
		}

		if (ZSTD_isError(DecompressResult))
			co_return DMibErrorInstance("Failed to decompress LFS index: {}"_f << ZSTD_getErrorName(DecompressResult));

		if (bKnowDecompressSize && DecompressResult != DecompressedSize)
			co_return DMibErrorInstance("Invalid decompressed size: {} != {}"_f << DecompressResult << DecompressedSize);
		else
			DecompressedSize = DecompressResult;

		auto ExceptionCapture = co_await g_CaptureExceptions;
		CStr IndexString((ch8 const *)DecompressedData.f_GetArray(), DecompressedSize);

		auto IndexJson = CEJSONSorted::fs_FromString(IndexString);
		for (auto &KeyValue : fg_Const(IndexJson).f_Object())
		{
			auto &Value = KeyValue.f_Value();

			auto &OutAsset = pCache->m_Assets[KeyValue.f_Name()];

			OutAsset.m_ID = Value["ID"].f_String();
			OutAsset.m_Repository = Value["Repository"].f_String();
			OutAsset.m_PublicDownloadUrl = Value["PublicDownloadUrl"].f_String();
			OutAsset.m_Size = Value["Size"].f_Integer();
			OutAsset.m_CompressedSize = Value["CompressedSize"].f_Integer();
		}

		co_return pCache;
	}
}
