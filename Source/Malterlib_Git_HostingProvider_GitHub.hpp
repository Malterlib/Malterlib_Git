// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NGit
{
#ifndef DCompiler_MSVC_Workaround
	constexpr
#else
	inline
#endif
	COrdering_Strong CGitHostingProvider_GitHub::CFieldTranslationPair::operator <=> (CFieldTranslationPair const &_Right) const noexcept
	{
		return fg_StrCmp(m_GitHubField.m_pStr, _Right.m_GitHubField.m_pStr) <=> 0;
	}

#ifndef DCompiler_MSVC_Workaround
	constexpr
#else
	inline
#endif
	COrdering_Strong CGitHostingProvider_GitHub::CFieldTranslationPair::operator <=> (CStr const &_Right) const noexcept
	{
		return m_GitHubField.m_Str <=> _Right;
	}

	template <umint t_nFields>
	constexpr CGitHostingProvider_GitHub::TCFieldTranslations<t_nFields>::TCFieldTranslations(CFieldTranslationPair const (&_Pairs)[t_nFields])
		: CFieldTranslations
		{
			.m_pFields = _Pairs
			, .m_nFields = t_nFields
		}
	{
	}

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
