// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_Policy.h"

namespace NMib::NGit
{
	CGitPolicyActor::CGitPolicyActor() = default;
	CGitPolicyActor::~CGitPolicyActor() = default;

	/// Applies the BranchProtection and GenericRules sections of _Policy, and its RemoveOther of them. Deletes only after everything
	/// listed is applied, so that replacements are in place first, and deletes nothing when applying fails or leaves a listed generic
	/// ruleset unchanged, since that ruleset might not be what replaces them. Returns false when it left a listed generic ruleset
	/// unchanged, because updating it would lose what the policy can't express.
	auto CGitPolicyActor::f_ApplyPolicy_Rules(CApplyPolicyContext _BranchProtectionContext, CApplyPolicyContext _GenericRulesContext, CEJsonSorted _Policy) -> TCFuture<bool>
	{
		co_await ECoroutineFlag_CaptureExceptions;

		auto fRemoveOther = [&](CStr const &_Section)
			{
				if (auto pRemoveOther = _Policy.f_GetMember("RemoveOther"))
					return pRemoveOther->f_GetMemberValue(_Section, false).f_Boolean();

				return false;
			}
		;

		bool bRemoveOtherBranchProtection = fRemoveOther("BranchProtection");
		bool bRemoveOtherGenericRules = fRemoveOther("GenericRules");

		// Removing the others applies also when the policy lists none, which removes them all
		CEJsonSorted const NoRules(EJsonType_Object);
		auto pBranchProtection = _Policy.f_GetMember("BranchProtection");
		auto pGenericRules = _Policy.f_GetMember("GenericRules");

		// A section that lists no rules has nothing to apply, and reading the rules in full could fail on one that only the deletion reads
		if (pBranchProtection && !(pBranchProtection->f_IsObject() && pBranchProtection->f_Object().f_IsEmpty()))
			co_await fp_ApplyPolicy_BranchProtection(_BranchProtectionContext, *pBranchProtection);

		bool bGenericRulesApplied = true;
		if (pGenericRules && !(pGenericRules->f_IsObject() && pGenericRules->f_Object().f_IsEmpty()))
			bGenericRulesApplied = co_await fp_ApplyPolicy_GenericRules(_GenericRulesContext, *pGenericRules);

		if ((bRemoveOtherGenericRules || bRemoveOtherBranchProtection) && !bGenericRulesApplied)
			co_return DMibErrorInstance("Nothing the policy doesn't list was deleted, since a generic ruleset it lists was left unchanged");

		if (bRemoveOtherGenericRules)
			co_await fp_RemoveOther_GenericRules(_GenericRulesContext, pGenericRules ? *pGenericRules : NoRules);

		if (bRemoveOtherBranchProtection)
			co_await fp_RemoveOther_BranchProtection(_BranchProtectionContext, pBranchProtection ? *pBranchProtection : NoRules);

		co_return bGenericRulesApplied;
	}
}
