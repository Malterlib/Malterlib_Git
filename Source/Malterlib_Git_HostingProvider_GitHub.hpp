// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NGit
{
#ifdef DCompiler_MSVC_Workaround
	template <umint t_nFields>
	CGitHostingProvider_GitHub::CFieldTranslations CGitHostingProvider_GitHub::fsp_FieldTranslations(CFieldTranslationPair const (&_Pairs)[t_nFields])
	{
		return CFieldTranslations
			{
				.m_pFields = _Pairs
				, .m_nFields = t_nFields
			}
		;
	}
#else
	template <CGitHostingProvider_GitHub::TCFieldTranslations t_Fields>
	CGitHostingProvider_GitHub::CFieldTranslations CGitHostingProvider_GitHub::fsp_FieldTranslations()
	{
		static_assert(fg_IsSorted(t_Fields.m_pFields, t_Fields.m_nFields));
		return t_Fields;
	}
#endif
}
