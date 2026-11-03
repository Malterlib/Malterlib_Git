// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NGit
{
#ifdef DCompiler_MSVC_Workaround
	template <umint tf_nFields>
	CGitHostingProvider_GitHub::CFieldTranslations CGitHostingProvider_GitHub::fsp_FieldTranslations(CFieldTranslationPair const (&_Pairs)[tf_nFields])
	{
		return CFieldTranslations
			{
				.m_pFields = _Pairs
				, .m_nFields = tf_nFields
			}
		;
	}
#else
	template <CGitHostingProvider_GitHub::TCFieldTranslations tf_Fields>
	CGitHostingProvider_GitHub::CFieldTranslations CGitHostingProvider_GitHub::fsp_FieldTranslations()
	{
		static_assert(fg_IsSorted(tf_Fields.m_pFields, tf_Fields.m_nFields));
		return tf_Fields;
	}
#endif
}
