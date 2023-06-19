// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Concurrency/LogError>

#include "Malterlib_Git_App_GitPolicyManager.h"

namespace NMib::NGit::NGitPolicyManager
{
	CGitPolicyManagerActor::CGitPolicyManagerActor()
		: CDistributedAppActor(CDistributedAppActor_Settings("GitPolicyManager").f_AuditCategory("Malterlib/Git/GitPolicyManager"))
	{
	}

	CGitPolicyManagerActor::~CGitPolicyManagerActor() = default;

	TCFuture<void> CGitPolicyManagerActor::fp_StartApp(NEncoding::CEJSONSorted const &_Params)
	{
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");
					return {};
				}
			)
		;

		co_await fp_RegisterSensors();
		fp_PeriodicUpdate() > fg_LogError("Malterlib/Git/GitPolicyManager", "Failed to run initial periodic update");

		mp_PeriodicUpdateTimerSubscription = co_await fg_RegisterTimer
			(
				24.0 * 60.0 * 60.0 // 24 h
				, [this]() -> TCFuture<void>
				{
					co_await self(&CGitPolicyManagerActor::fp_PeriodicUpdate);

					co_return {};
				}
			)
		;

		co_return {};
	}

	TCFuture<void> CGitPolicyManagerActor::fp_StopApp()
	{
		TCActorResultVector<void> Destroys;

		fg_Exchange(mp_PeriodicUpdateTimerSubscription, nullptr)->f_Destroy() > Destroys.f_AddResult();

		co_await Destroys.f_GetResults();

		co_return {};
	}

	TCFuture<void> CGitPolicyManagerActor::fp_RegisterSensors()
	{
		CDistributedAppSensorReporter::CSensorInfo SensorInfo;
		SensorInfo.m_Identifier = "org.malterlib.git.policy-manager.status";
		SensorInfo.m_IdentifierScope = "All Policies";
		SensorInfo.m_Name = "Policy Manager Status";
		SensorInfo.m_ExpectedReportInterval = 24.0 * 60.0 * 60.0;
		SensorInfo.m_Type = NConcurrency::CDistributedAppSensorReporter::ESensorDataType_Status;

		mp_SensorReporter_Status = co_await self(&CGitPolicyManagerActor::fp_OpenSensorReporter, fg_Move(SensorInfo));

		co_return {};
	}

	TCFuture<void> CGitPolicyManagerActor::fp_PeriodicUpdate()
	{
		auto Result = co_await fp_ApplyPolicies().f_Wrap();

		CDistributedAppSensorReporter::CStatus Status;
		if (!Result)
		{
			CStr ErrorString = "Failed to apply Git hosting provider policies: {}"_f << Result.f_GetExceptionStr();
			DMibLogWithCategory(Mib/Git/GitPolicyManager, Error, ErrorString);

			Status.m_Severity = CDistributedAppSensorReporter::EStatusSeverity_Error;
			Status.m_Description = ErrorString;

			DMibLogWithCategory(Malterlib/Git/GitPolicyManager, Error, "{}", ErrorString);
		}
		else
		{
			Status.m_Severity = CDistributedAppSensorReporter::EStatusSeverity_Ok;
			Status.m_Description = "All policies applied successfully";

			DMibLogWithCategory(Malterlib/Git/GitPolicyManager, Info, "{}", Status.m_Description);
		}

		TCVector<CDistributedAppSensorReporter::CSensorReading> SensorReadings;
		SensorReadings.f_Insert().m_Data = Status;

		if (!mp_SensorReporter_Status.m_fReportReadings.f_IsEmpty())
			co_await mp_SensorReporter_Status.m_fReportReadings(fg_Move(SensorReadings));

		co_return {};
	}
}

namespace NMib::NGit
{
	TCActor<CDistributedAppActor> fg_ConstructApp_GitPolicyManager()
	{
		return fg_Construct<NGitPolicyManager::CGitPolicyManagerActor>();
	}
}
