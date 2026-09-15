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

	// Where a working tree keeps its repository data: its '.git' directory, or the directory
	// a worktree's '.git' file names, and the directory those share with the main worktree.
	struct CGitDirectories
	{
		NStr::CStr m_GitDirectory;
		NStr::CStr m_CommonDirectory;
	};

	// The variables git reads to find its global and system configuration.
	struct CGitEnvironment
	{
		static CGitEnvironment fs_FromProcess();

		NStr::CStr m_Home;
		NStr::CStr m_ConfigHome;						// XDG_CONFIG_HOME
		NStr::CStr m_GlobalConfiguration;				// GIT_CONFIG_GLOBAL replaces both global files.
		NStr::CStr m_SystemConfiguration;				// GIT_CONFIG_SYSTEM
		bool m_bNoSystem = false;						// GIT_CONFIG_NOSYSTEM
	};

	// The ignore files git applies to a whole repository below every .gitignore: the one
	// core.excludesFile names, or its default, and the repository's info/exclude.
	struct CGitRepositoryExcludes
	{
		NStr::CStr m_ExcludesFile;						// Empty when no file applies.
		NStr::CStr m_InfoExclude;
	};

	// Both empty when the root holds no '.git' entry this understands.
	CGitDirectories fg_GetGitDirectories(NStr::CStr const &_Root);
	CGitRepositoryExcludes fg_GetGitRepositoryExcludes(NStr::CStr const &_Root, CGitDirectories const &_Directories, CGitEnvironment const &_Environment);
}
