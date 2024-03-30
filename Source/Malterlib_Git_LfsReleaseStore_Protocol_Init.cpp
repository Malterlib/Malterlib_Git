// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_LfsReleaseStore.h"

namespace NMib::NGit
{
	TCFuture<void> CLfsReleaseStoreService::fp_Protocol_Init(CEJSONSorted const _Packet)
	{
		auto ExceptionCapture = co_await g_CaptureExceptions;

		mp_CurrentRemote = _Packet["event"].f_String();
		mp_CurrentOperation = _Packet["operation"].f_String();

		CStr Remote = _Packet["remote"].f_String();

		co_await fp_Init(Remote);

		co_await mp_pCommandLine->f_StdOut("{}\n");

		co_return {};
	}
}
