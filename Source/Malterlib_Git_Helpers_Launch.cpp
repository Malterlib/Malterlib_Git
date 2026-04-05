// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_LfsReleaseStore.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Process/ProcessLaunchActor>

namespace NMib::NGit
{
	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> fg_LaunchGitWithResult(TCVector<CStr> _Params, CStr _WorkingDirectory, CSystemEnvironment _Environment = {})
	{
		TCVector<CStr> CommandLineParams;
		CommandLineParams.f_Insert(_Params);

		CProcessLaunchActor::CSimpleLaunch LaunchParams{"git", CommandLineParams, _WorkingDirectory};

		LaunchParams.m_Params.m_Environment = fg_Move(_Environment);
		LaunchParams.m_Params.m_bMergeEnvironment = true;

		TCActor<CProcessLaunchActor> ProcessLaunch = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(ProcessLaunch);

		co_return co_await ProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(LaunchParams));
	}

	TCFuture<CStr> fg_LaunchGit(TCVector<CStr> _Params, CStr _WorkingDirectory, CSystemEnvironment _Environment)
	{
		TCVector<CStr> CommandLineParams;
		CommandLineParams.f_Insert(_Params);

		CProcessLaunchActor::CSimpleLaunch LaunchParams{"git", CommandLineParams, _WorkingDirectory};
		LaunchParams.m_SimpleFlags = CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode;

		LaunchParams.m_Params.m_Environment = fg_Move(_Environment);
		LaunchParams.m_Params.m_bMergeEnvironment = true;

		TCActor<CProcessLaunchActor> ProcessLaunch = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(ProcessLaunch);

		auto Result = co_await ProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(LaunchParams));

		co_return Result.f_GetStdOut();
	}

	TCFuture<CStr> fg_LaunchGitSendStdIn(TCVector<CStr> _Params, CStr _StdIn, CStr _WorkingDirectory)
	{
		TCVector<CStr> CommandLineParams;
		CommandLineParams.f_Insert(_Params);

		TCActor<CProcessLaunchActor> ProcessLaunch = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(ProcessLaunch);

		TCSharedPointer<CStr> pBufferedStdOut = fg_Construct();
		TCSharedPointer<CStr> pBufferedStdErr = fg_Construct();
		NConcurrency::TCPromiseFuturePair<CStr> LaunchResult;
		CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutable
			(
				"git"
				, CommandLineParams
				, _WorkingDirectory
				, [pBufferedStdOut, pBufferedStdErr, LaunchResult = fg_Move(LaunchResult.m_Promise), ProcessLaunch, _StdIn](CProcessLaunchStateChangeVariant const &_State, fp64 _Time)
				{
					switch (_State.f_GetTypeID())
					{
					case NMib::NProcess::EProcessLaunchState_LaunchFailed:
						{
							LaunchResult.f_SetException(DMibErrorInstance(_State.f_Get<NMib::NProcess::EProcessLaunchState_LaunchFailed>()));
						}
						break;
					case NMib::NProcess::EProcessLaunchState_Launched:
						{
							ProcessLaunch(&CProcessLaunchActor::f_SendStdIn, _StdIn) > [ProcessLaunch](TCAsyncResult<void> &&_Result)
								{
									if (!_Result)
										_Result > fg_LogError("", "Failed to send stdin to git process");

									ProcessLaunch(&CProcessLaunchActor::f_CloseStdIn) > fg_LogError("", "Failed to close stdin for git process");
								}
							;
						}
						break;
					case NMib::NProcess::EProcessLaunchState_Exited:
						{
							auto ExitCode = _State.f_Get<NMib::NProcess::EProcessLaunchState_Exited>();

							if (ExitCode != 0)
								LaunchResult.f_SetException(DMibErrorInstance(*pBufferedStdErr));
							else
								LaunchResult.f_SetResult(fg_Move(*pBufferedStdOut));
						}
						break;
					}
				}
			)
		;

		LaunchParams.m_bAllowExecutableLocate = true;
		LaunchParams.m_fOnOutput = [pBufferedStdOut, pBufferedStdErr](EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
			{
				if (_OutputType == EProcessLaunchOutputType_StdOut)
					*pBufferedStdOut += _Output;
				else
					*pBufferedStdErr += _Output;
			}
		;

		auto LaunchSubscription = co_await ProcessLaunch(&CProcessLaunchActor::f_Launch, fg_Move(LaunchParams), fg_CurrentActor());

		co_return co_await fg_Move(LaunchResult.m_Future);
	}
}
