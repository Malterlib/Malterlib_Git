// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_Ignore.h"

#include <Mib/File/File>

namespace NMib::NGit
{
	using namespace NStr;
	using namespace NContainer;
	using namespace NFile;

	void CGitIgnore::f_AddRules(CStr const &_Directory, CStr const &_Contents)
	{
		CRuleSet Set;
		Set.m_Directory = _Directory;
		for (auto const &RawLine : _Contents.f_SplitLine())
		{
			// Trailing spaces are ignored unless quoted with a backslash.
			auto Line = RawLine;
			while (Line.f_EndsWith(" ") && !Line.f_EndsWith("\\ "))
				Line = Line.f_Left(Line.f_GetLen() - 1);

			if (!Line || Line.f_StartsWith("#"))
				continue;

			CRule Rule
				{
					CPathGlob("*")
				}
			;
			auto Pattern = Line;
			if (Pattern.f_StartsWith("!"))
			{
				Rule.m_bNegated = true;
				Pattern = Pattern.f_Extract(1);
			}
			else if (Pattern.f_StartsWith("\\!") || Pattern.f_StartsWith("\\#"))
				Pattern = Pattern.f_Extract(1);

			if (Pattern.f_EndsWith("/"))
			{
				Rule.m_bDirectoryOnly = true;
				Pattern = Pattern.f_Left(Pattern.f_GetLen() - 1);
			}

			if (!Pattern)
				continue;

			// A separator anywhere but the end anchors the pattern to the file's directory; a
			// leading one only anchors, which is how the glob reads it too. Without one the
			// pattern meets a name at any depth below.
			Rule.m_Glob = CPathGlob(Pattern);
			Set.m_Rules.f_Insert(fg_Move(Rule));
		}

		mp_Sets.f_Insert(fg_Move(Set));
	}

	bool CGitIgnore::f_IsIgnored(CStr const &_Path, bool _bDirectory) const
	{
		auto FileName = CPathGlob::fs_ToUnicode(CFile::fs_GetFile(_Path));
		CPathGlob::CScratch Scratch;
		// Deeper files and later rules take precedence, so the last matching rule decides.
		for (umint iSet = mp_Sets.f_GetLen(); iSet; --iSet)
		{
			auto const &Set = mp_Sets[iSet - 1];
			CStr Relative = _Path;
			if (Set.m_Directory)
			{
				if (!_Path.f_StartsWith(Set.m_Directory + "/"))
					continue;

				Relative = _Path.f_Extract(Set.m_Directory.f_GetLen() + 1);
			}

			auto Path = CPathGlob::fs_ToUnicode(Relative);
			for (umint iRule = Set.m_Rules.f_GetLen(); iRule; --iRule)
			{
				auto const &Rule = Set.m_Rules[iRule - 1];
				if (Rule.m_bDirectoryOnly && !_bDirectory)
					continue;

				if (Rule.m_Glob.f_Match(Path, FileName, Scratch))
					return !Rule.m_bNegated;
			}
		}

		return false;
	}
}
