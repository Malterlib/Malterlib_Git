// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Git/HostingProvider>

namespace NMib::NGit
{
	struct CLfsReleaseStoreService : public NConcurrency::CActor
	{
		enum class EUpdateReleaseIndexOption : uint32
		{
			mc_None                  = 0
			, mc_Pretend             = DMibBit(0)
			, mc_PruneOrphanedAssets = DMibBit(1)
		};

		CLfsReleaseStoreService(NStorage::TCSharedPointer<NConcurrency::CCommandLineControl> const &_pCommandLine, NStr::CStr const &_WorkingDirectory);

		NConcurrency::TCFuture<void> f_InitService();
		NConcurrency::TCFuture<void> f_WaitForExit();
		NConcurrency::TCFuture<void> f_UpdateReleaseIndex(NStr::CStr _Remote, EUpdateReleaseIndexOption _Options, NFunction::TCFunction<void (NStr::CStr const &_Output)> _fOutputConsole);

		static constexpr umint mc_ReleaseBuckets = 1024;

	private:
		struct CCachedReleaseAsset
		{
			CGitHostingProvider::CReleaseAsset const *m_pSource = nullptr;
			NStr::CStr m_ObjectID;
			NStr::CStr m_PublicDownloadUrl;
			uint64 m_AssetSize = 0;
			uint64 m_CompressedSize = 0;
		};

		struct CCachedRelease
		{
			CGitHostingProvider::CRelease m_Release;
			NContainer::TCMap<NStr::CStr, CCachedReleaseAsset> m_ReleaseAssets;
		};

		struct CCachedReleases
		{
			NContainer::TCMap<NStr::CStr, CCachedRelease> m_Releases;
			NStorage::TCSharedPointer<CGitHostingProvider::CGetRepository> m_pRepository;
			bool m_bInitedReleases = false;
		};

		struct CReleaseAssetInfo
		{
			NStr::CStr m_ID;
			NStr::CStr m_Repository;
			NStr::CStr m_PublicDownloadUrl;
			uint64 m_Size = 0;
			uint64 m_CompressedSize = 0;
		};

		struct CReleaseIndexCache
		{
			NContainer::TCMap<NStr::CStr, CReleaseAssetInfo> m_Assets;
			bool m_bInitialized = false;
			bool m_bPublicRepository = false;
		};

		struct CPrivateReleaseIndexDownload
		{
			NStorage::TCSharedPointer<NContainer::CByteVector> m_pData;
			uint64 m_DecompressedSize = 0;
		};

		struct CLoginState
		{
			bool m_bCompleted = false;
			NException::CExceptionPointer m_pException;
			NContainer::TCVector<NConcurrency::TCPromise<void>> m_Waiters;
		};

		NConcurrency::TCFuture<void> fp_Destroy() override;

		NConcurrency::TCFuture<void> fp_ProcessPacket(NEncoding::CEJsonSorted _Packet);

		NConcurrency::TCFuture<bool> fp_Init(NStr::CStr _Remote);
		NConcurrency::TCFuture<void> fp_EnsureLogin(bool _bAllowInteractive);
		NConcurrency::TCFuture<void> fp_DoLogin(bool _bAllowInteractive);
		NConcurrency::TCFuture<CGitHostingProvider::CRelease> fp_GetOrCreateRelease(NStr::CStr _Repository, NStr::CStr _TagName, bool _bAllowCreate);

		NConcurrency::TCFuture<void> fp_Protocol_Init(NEncoding::CEJsonSorted const _Packet);
		NConcurrency::TCFuture<void> fp_Protocol_Upload(NEncoding::CEJsonSorted const _Packet);
		NConcurrency::TCFuture<void> fp_Protocol_Download(NEncoding::CEJsonSorted const _Packet);

		NConcurrency::TCFuture<void> fp_SendInitError(int32 _ErrorCode, NStr::CStr _ErrorMessage);
		NConcurrency::TCFuture<void> fp_SendProcessError(NStr::CStr _ObjectID, int32 _ErrorCode, NStr::CStr _ErrorMessage);
		NConcurrency::TCFuture<void> fp_SendProgress(NStr::CStr _ObjectID, uint64 _BytesSoFar, uint64 _BytesSinseLast);

		NConcurrency::TCFuture<void> fp_UploadReleaseAsset(NStr::CStr _Repository, NStr::CStr _ReleaseIdentifier, NStr::CStr _Path, uint64 _Size);
		NConcurrency::TCFuture<NStr::CStr> fp_DownloadReleaseAsset(CReleaseAssetInfo _AssetInfo, bool _bPublic);

		NConcurrency::TCFuture<NStr::CStr> fp_EnsureLfsReleaseTarget();

		NConcurrency::TCFuture<NStorage::TCSharedPointer<CCachedReleases>> fp_GetCachedReleases(NStr::CStr _Repository);
		NConcurrency::TCFuture<NStorage::TCSharedPointer<CGitHostingProvider::CGetRepository>> fp_GetCachedRepository(NStr::CStr _Repository);

		NConcurrency::TCFuture<NStorage::TCSharedPointer<NContainer::CByteVector>> fp_DownloadReleaseIndexPublic(NStr::CStr _Repository);
		NConcurrency::TCFuture<CPrivateReleaseIndexDownload> fp_DownloadReleaseIndexPrivate(NStr::CStr _Repository);
		NConcurrency::TCFuture<NStorage::TCSharedPointer<CReleaseIndexCache>> fp_GetReleaseIndexCache(NStr::CStr _Repository);
		auto fp_GetReleaseAssetInfo(NStorage::TCSharedPointer<CReleaseIndexCache> _pIndexCache, NStr::CStr _Repository, NStr::CStr _ObjectID, uint64 _Size)
			-> NConcurrency::TCFuture<CReleaseAssetInfo>
		;

		static NStr::CStr fsp_GetTagName(NStr::CStr const &_ObjectID);

		NStr::CStr fp_GetTempDir();

		NStorage::TCSharedPointer<NConcurrency::CCommandLineControl> mp_pCommandLine;
		NConcurrency::CActorSubscription mp_StdSubscription;
		NConcurrency::CActorSubscription mp_CancelSubscription;
		NConcurrency::TCActor<CGitHostingProvider> mp_HostingProvider;
		NConcurrency::TCPromise<void> mp_ExitPromise;
		NStr::CStr mp_WorkingDirectory;
		NStr::CStr mp_CurrentRemote;
		NStr::CStr mp_CurrentOperation;
		NStr::CStr mp_HostingProviderHost;
		NStr::CStr mp_HostingProviderPath;
		NStr::CStr mp_HostingProviderProtocol;
		NStr::CStr mp_RemoteUrl;
		NStr::CStr mp_TempDir;
		NStr::CStr mp_LastRemoteUrl;
		NStr::CStr mp_CurrentObjectID;
		NStr::CStr mp_LfsReleaseTargetHash;
		NContainer::TCMap<NStr::CStr, NStorage::TCSharedPointer<CCachedReleases>> mp_RepositoryCache;
		NContainer::TCMap<NStr::CStr, NStorage::TCSharedPointer<CReleaseIndexCache>> mp_ReleaseIndexCache;
		NFunction::TCFunction<void (NStr::CStr const &_Output)> mp_fOutputConsole;
		NStorage::TCSharedPointer<CLoginState> mp_pLoginState;
		NStorage::TCSharedPointer<CLoginState> mp_pLoginStateNonInteractive;
		bool mp_bLfsReleaseTargetInitialized = false;
		bool mp_bLoggedIn = false;
	};
}
