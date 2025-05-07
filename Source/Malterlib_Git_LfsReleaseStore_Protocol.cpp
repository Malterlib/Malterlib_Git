// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NGit
{
	TCFuture<void> CLfsReleaseStoreService::f_InitService()
	{
		mp_StdSubscription = co_await mp_pCommandLine->f_RegisterForStdIn
			(
				g_ActorFunctor / [this, StdInBuffer = CStr()](NProcess::EStdInReaderOutputType _Type, NStr::CStrIO _Input) mutable -> TCFuture<void>
				{
					if (_Type == NProcess::EStdInReaderOutputType_GeneralError)
					{
						DMibLog(Error, "StdIn reader error: {}", _Input);

						co_return {};
					}

					if (_Type == NProcess::EStdInReaderOutputType_EndOfFile)
					{
						if (!mp_ExitPromise.f_IsSet())
							mp_ExitPromise.f_SetResult();

						co_return {};
					}

					if (_Type != NProcess::EStdInReaderOutputType_StdIn)
						co_return {};

					StdInBuffer += _Input;
					auto iLastLine = StdInBuffer.f_FindCharReverse('\n');
					if (iLastLine >= 0)
					{
						auto Lines = StdInBuffer.f_Left(iLastLine + 1);
						StdInBuffer = StdInBuffer.f_Extract(iLastLine + 1);

						for (auto &Line : Lines.f_SplitLine<true>())
						{
							try
							{
								CEJsonSorted Json = CEJsonSorted::fs_FromString(Line);

								fp_ProcessPacket(Json) > fg_LogError("", "Failed to process packet");
							}
							catch ([[maybe_unused]] CException const &_Exeption)
							{
								DMibLog(Error, "Failed to parse JSON: {}", _Exeption);
							}
						}
					}
					co_return {};
				}
				, NProcess::EStdInReaderFlag_Exclusive
			)
		;

		mp_CancelSubscription = co_await mp_pCommandLine->f_RegisterForCancellation
			(
				g_ActorFunctor / [this]() -> TCFuture<bool>
				{
					if (!mp_ExitPromise.f_IsSet())
						mp_ExitPromise.f_SetResult();

					co_return false;
				}
			)
		;

		co_return {};
	}

	TCFuture<void> CLfsReleaseStoreService::fp_SendInitError(int32 _ErrorCode, CStr _ErrorMessage)
	{
		CEJsonSorted Message =
			{
				"error"_=
				{
					"code"_= _ErrorCode
					, "message"_= _ErrorMessage
				}
			}
		;

		co_await mp_pCommandLine->f_StdOut(CStr("{jp}\n"_f << Message));

		co_return {};
	}

	TCFuture<void> CLfsReleaseStoreService::fp_SendProcessError(CStr _ObjectID, int32 _ErrorCode, CStr _ErrorMessage)
	{
		CEJsonSorted Message =
			{
				"event"_= "complete"
				, "oid"_= _ObjectID
				, "error"_=
				{
					"code"_= _ErrorCode
					, "message"_= _ErrorMessage
				}
			}
		;

		co_await mp_pCommandLine->f_StdOut(CStr("{jp}\n"_f << Message));

		co_return {};
	}

	TCFuture<void> CLfsReleaseStoreService::fp_SendProgress(CStr _ObjectID, uint64 _BytesSoFar, uint64 _BytesSinseLast)
	{
		CEJsonSorted Message =
			{
				"event"_= "progress"
				, "oid"_= _ObjectID
				, "bytesSoFar"_= _BytesSoFar
				, "bytesSinceLast"_= _BytesSinseLast
			}
		;

		co_await mp_pCommandLine->f_StdOut(CStr("{jp}\n"_f << Message));

		co_return {};
	}
	
	TCFuture<void> CLfsReleaseStoreService::fp_ProcessPacket(CEJsonSorted _Packet)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;

		auto const &Packet = _Packet;

		auto &Event = Packet["event"].f_String();

		if (Event == "init")
		{
			auto Result = co_await fp_Protocol_Init(Packet).f_Wrap();
			if (!Result)
				co_await fp_SendInitError(1, Result.f_GetExceptionStr());

			co_return {};
		}
		else if (Event == "upload")
		{
			auto Result = co_await fp_Protocol_Upload(Packet).f_Wrap();
			if (!Result)
				co_await fp_SendProcessError(mp_CurrentObjectID, 1, Result.f_GetExceptionStr());

			co_return {};
		}
		else if (Event == "download")
		{
			auto Result = co_await fp_Protocol_Download(Packet).f_Wrap();
			if (!Result)
				co_await fp_SendProcessError(mp_CurrentObjectID, 1, Result.f_GetExceptionStr());

			co_return {};
		}
		else if (Event == "terminate")
		{
			if (!mp_ExitPromise.f_IsSet())
				mp_ExitPromise.f_SetResult();

			co_return {};
		}

		DMibLog(Info, "Unrecognized packet: {}", _Packet);

		co_return {};
	}
}
