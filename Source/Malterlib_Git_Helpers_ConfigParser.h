// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NGit
{
	struct CGitConfigParser
	{
		struct CSubSection
		{
			NContainer::TCMap<NStr::CStr, NContainer::TCVector<NStr::CStr>> m_Values;
		};

		struct CSection
		{
			NContainer::TCMap<NStr::CStr, CSubSection> m_SubSections;
		};

		static CGitConfigParser fs_Parse(NStr::CStr const &_String);
		static bool fs_ToBoolean(NStr::CStr const &_String);

		NStr::CStr const *f_GetValue(NStr::CStr const &_Section, NStr::CStr const &_SubSection, NStr::CStr const &_Key) const;
		NStr::CStr const *f_GetValue(NStr::CStr const &_Section, NStr::CStr const &_Key) const;

		NContainer::TCVector<NStr::CStr> const *f_GetValues(NStr::CStr const &_Section, NStr::CStr const &_SubSection, NStr::CStr const &_Key) const;
		NContainer::TCVector<NStr::CStr> const *f_GetValues(NStr::CStr const &_Section, NStr::CStr const &_Key) const;

		NContainer::TCMap<NStr::CStr, CSection> m_Sections;
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NGit;
#endif
