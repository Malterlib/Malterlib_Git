// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Vector>
#include <Mib/File/PathGlob>
#include <Mib/String/String>

namespace NMib::NGit
{
	// The ignore rules of one repository, gathered as a walk meets its ignore files. A path is
	// judged relative to the repository root, on the assumption that no directory above it is
	// ignored: a walk that does not enter an ignored directory never asks about anything below.
	struct CGitIgnore
	{
		// Adds the rules of one ignore file. _Directory is the repository-relative directory the
		// file was found in, empty at the root; rules added later take precedence over rules
		// added earlier, so a walk adds each directory's file after its parents'.
		void f_AddRules(NStr::CStr const &_Directory, NStr::CStr const &_Contents);

		bool f_IsIgnored(NStr::CStr const &_Path, bool _bDirectory) const;

	private:
		struct CRule
		{
			NFile::CPathGlob m_Glob;
			bool m_bNegated = false;
			bool m_bDirectoryOnly = false;
		};

		struct CRuleSet
		{
			NStr::CStr m_Directory;
			NContainer::TCVector<CRule> m_Rules;
		};

		NContainer::TCVector<CRuleSet> mp_Sets;
	};
}
