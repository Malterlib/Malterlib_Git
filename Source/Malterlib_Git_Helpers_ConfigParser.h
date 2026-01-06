// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NGit
{
	struct CGitConfigParser
	{
		struct CSubSection
		{
			TCMap<CStr, TCVector<CStr>> m_Values;
		};

		struct CSection
		{
			TCMap<CStr, CSubSection> m_SubSections;
		};

		static CGitConfigParser fs_Parse(CStr const &_String);
		static bool fs_ToBoolean(CStr const &_String);

		CStr const *f_GetValue(CStr const &_Section, CStr const &_SubSection, CStr const &_Key) const;
		CStr const *f_GetValue(CStr const &_Section, CStr const &_Key) const;

		TCVector<CStr> const *f_GetValues(CStr const &_Section, CStr const &_SubSection, CStr const &_Key) const;
		TCVector<CStr> const *f_GetValues(CStr const &_Section, CStr const &_Key) const;

		TCMap<CStr, CSection> m_Sections;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NGit;
#endif
