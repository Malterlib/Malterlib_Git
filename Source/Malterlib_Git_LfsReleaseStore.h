// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Git/HostingProvider>

namespace NMib::NGit
{
	struct CLfsReleaseStoreService : public CActor
	{
		enum class EUpdateReleaseIndexOption : uint32
 		{
			mc_None = 0
			, mc_Pretend = DMibBit(0)
			, mc_PruneOrphanedAssets = DMibBit(1)
		};

		CLfsReleaseStoreService(NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine, CStr const &_WorkingDirectory);

		TCFuture<void> f_InitService();
		TCFuture<void> f_WaitForExit();
		TCFuture<void> f_UpdateReleaseIndex
			(
				CStr _Remote
				, EUpdateReleaseIndexOption _Options
				, TCFunction<void (CStr const &_Output)> _fOutputConsole
			)
		;

		static constexpr umint mc_ReleaseBuckets = 1024;

	private:
		struct CCachedReleaseAsset
		{
			CGitHostingProvider::CReleaseAsset const * m_pSource = nullptr;
			CStr m_ObjectID;
			CStr m_PublicDownloadUrl;
			uint64 m_AssetSize = 0;
			uint64 m_CompressedSize = 0;
		};

		struct CCachedRelease
		{
			CGitHostingProvider::CRelease m_Release;
			TCMap<CStr, CCachedReleaseAsset> m_ReleaseAssets;
		};

		struct CCachedReleases
		{
			TCMap<CStr, CCachedRelease> m_Releases;
			TCSharedPointer<CGitHostingProvider::CGetRepository> m_pRepository;
			bool m_bInitedReleases = false;
		};

		struct CReleaseAssetInfo
		{
			CStr m_ID;
			CStr m_Repository;
			CStr m_PublicDownloadUrl;
			uint64 m_Size = 0;
			uint64 m_CompressedSize = 0;
		};

		struct CReleaseIndexCache
		{
			TCMap<CStr, CReleaseAssetInfo> m_Assets;
			bool m_bInitialized = false;
			bool m_bPublicRepository = false;
		};

		struct CPrivateReleaseIndexDownload
		{
			TCSharedPointer<CByteVector> m_pData;
			uint64 m_DecompressedSize = 0;
		};

		TCFuture<void> fp_Destroy() override;

		TCFuture<void> fp_ProcessPacket(CEJsonSorted _Packet);

		TCFuture<bool> fp_Init(CStr _Remote);
		TCFuture<void> fp_EnsureLogin(bool _bAllowInteractive);
		TCFuture<void> fp_DoLogin(bool _bAllowInteractive);
		TCFuture<CGitHostingProvider::CRelease> fp_GetOrCreateRelease(CStr _Repository, CStr _TagName, bool _bAllowCreate);

		TCFuture<void> fp_Protocol_Init(CEJsonSorted const _Packet);
		TCFuture<void> fp_Protocol_Upload(CEJsonSorted const _Packet);
		TCFuture<void> fp_Protocol_Download(CEJsonSorted const _Packet);

		TCFuture<void> fp_SendInitError(int32 _ErrorCode, CStr _ErrorMessage);
		TCFuture<void> fp_SendProcessError(CStr _ObjectID, int32 _ErrorCode, CStr _ErrorMessage);
		TCFuture<void> fp_SendProgress(CStr _ObjectID, uint64 _BytesSoFar, uint64 _BytesSinseLast);

		TCFuture<void> fp_UploadReleaseAsset(CStr _Repository, CStr _ReleaseIdentifier, CStr _Path, uint64 _Size);
		TCFuture<CStr> fp_DownloadReleaseAsset(CReleaseAssetInfo _AssetInfo, bool _bPublic);

		TCFuture<CStr> fp_EnsureLfsReleaseTarget();

		TCFuture<TCSharedPointer<CCachedReleases>> fp_GetCachedReleases(CStr _Repository);
		TCFuture<TCSharedPointer<CGitHostingProvider::CGetRepository>> fp_GetCachedRepository(CStr _Repository);

		TCFuture<TCSharedPointer<CByteVector>> fp_DownloadReleaseIndexPublic(CStr _Repository);
		TCFuture<CPrivateReleaseIndexDownload> fp_DownloadReleaseIndexPrivate(CStr _Repository);
		TCFuture<TCSharedPointer<CReleaseIndexCache>> fp_GetReleaseIndexCache(CStr _Repository);
		TCFuture<CReleaseAssetInfo> fp_GetReleaseAssetInfo(TCSharedPointer<CReleaseIndexCache> _pIndexCache, CStr _Repository, CStr _ObjectID, uint64 _Size);

		static CStr fsp_GetTagName(CStr const &_ObjectID);

		CStr fp_GetTempDir();

		NStorage::TCSharedPointer<CCommandLineControl> mp_pCommandLine;
		CActorSubscription mp_StdSubscription;
		CActorSubscription mp_CancelSubscription;
		TCActor<CGitHostingProvider> mp_HostingProvider;
		TCPromise<void> mp_ExitPromise;
		CStr mp_WorkingDirectory;
		CStr mp_CurrentRemote;
		CStr mp_CurrentOperation;
		CStr mp_HostingProviderHost;
		CStr mp_HostingProviderPath;
		CStr mp_HostingProviderProtocol;
		CStr mp_RemoteUrl;
		CStr mp_TempDir;
		CStr mp_LastRemoteUrl;
		CStr mp_CurrentObjectID;
		CStr mp_LfsReleaseTargetHash;
		TCMap<CStr, TCSharedPointer<CCachedReleases>> mp_RepositoryCache;
		TCMap<CStr, TCSharedPointer<CReleaseIndexCache>> mp_ReleaseIndexCache;
		TCFunction<void (CStr const &_Output)> mp_fOutputConsole;
		struct CLoginState
		{
			bool m_bCompleted = false;
			NException::CExceptionPointer m_pException;
			NContainer::TCVector<TCPromise<void>> m_Waiters;
		};

		NStorage::TCSharedPointer<CLoginState> mp_pLoginState;
		NStorage::TCSharedPointer<CLoginState> mp_pLoginStateNonInteractive;
		bool mp_bLfsReleaseTargetInitialized = false;
		bool mp_bLoggedIn = false;
	};
}
