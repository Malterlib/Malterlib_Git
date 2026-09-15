// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Git_Ignore.h"

#include <Mib/File/File>
#include <Mib/Git/Helpers/ConfigParser>

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

	CGitEnvironment CGitEnvironment::fs_FromProcess()
	{
		auto *pSys = fg_GetSys();
		CGitEnvironment Environment;
		Environment.m_Home = pSys->f_GetEnvironmentVariable("HOME");
		if (!Environment.m_Home)
			Environment.m_Home = pSys->f_GetEnvironmentVariable("USERPROFILE");
		Environment.m_ConfigHome = pSys->f_GetEnvironmentVariable("XDG_CONFIG_HOME");
		Environment.m_GlobalConfiguration = pSys->f_GetEnvironmentVariable("GIT_CONFIG_GLOBAL");
		Environment.m_SystemConfiguration = pSys->f_GetEnvironmentVariable("GIT_CONFIG_SYSTEM");
		Environment.m_bNoSystem = CGitConfigParser::fs_ToBoolean(pSys->f_GetEnvironmentVariable("GIT_CONFIG_NOSYSTEM"));

		return Environment;
	}

	CGitDirectories fg_GetGitDirectories(CStr const &_Root)
	{
		CGitDirectories Directories;
		auto GitDirectory = _Root / ".git";
		if (CFile::fs_FileExists(GitDirectory, EFileAttrib_File))
		{
			auto Contents = CFile::fs_ReadStringFromFile(GitDirectory, true).f_TrimRight("\r\n");
			if (!Contents.f_StartsWith("gitdir: "))
				return Directories;

			GitDirectory = CFile::fs_GetExpandedPath(Contents.f_Extract(8), _Root);
		}

		if (!CFile::fs_FileExists(GitDirectory, EFileAttrib_Directory))
			return Directories;

		Directories.m_GitDirectory = GitDirectory;
		Directories.m_CommonDirectory = GitDirectory;
		auto CommonDirectoryFile = GitDirectory / "commondir";
		if (CFile::fs_FileExists(CommonDirectoryFile, EFileAttrib_File))
		{
			auto Contents = CFile::fs_ReadStringFromFile(CommonDirectoryFile, true).f_TrimRight("\r\n");
			Directories.m_CommonDirectory = CFile::fs_GetExpandedPath(Contents, GitDirectory);
		}

		return Directories;
	}

	// The configuration files in the order git reads them, so that a later value wins: the
	// system file, the two global files, the repository's, and a worktree's own when the
	// repository has opted into worktree configuration.
	static TCVector<CStr> fg_GetGitConfigurationFiles(CGitDirectories const &_Directories, CGitEnvironment const &_Environment)
	{
		TCVector<CStr> Files;
		if (!_Environment.m_bNoSystem)
			Files.f_Insert(_Environment.m_SystemConfiguration ? _Environment.m_SystemConfiguration : CStr("/etc/gitconfig"));

		if (_Environment.m_GlobalConfiguration)
			Files.f_Insert(_Environment.m_GlobalConfiguration);
		else
		{
			if (_Environment.m_ConfigHome)
				Files.f_Insert(_Environment.m_ConfigHome / "git/config");
			else if (_Environment.m_Home)
				Files.f_Insert(_Environment.m_Home / ".config/git/config");

			if (_Environment.m_Home)
				Files.f_Insert(_Environment.m_Home / ".gitconfig");
		}

		if (_Directories.m_CommonDirectory)
			Files.f_Insert(_Directories.m_CommonDirectory / "config");

		return Files;
	}

	// git expands a leading '~/' in a pathname value; a relative path is taken from the
	// working tree root, where git resolves the ignore files of a walk.
	static CStr fg_ExpandGitPath(CStr const &_Value, CStr const &_Root, CGitEnvironment const &_Environment)
	{
		if (_Value == "~" || _Value.f_StartsWith("~/"))
		{
			if (!_Environment.m_Home)
				return {};

			return _Value == "~" ? _Environment.m_Home : _Environment.m_Home / _Value.f_Extract(2);
		}

		if (_Value.f_StartsWith("~"))
			return {};

		return CFile::fs_GetExpandedPath(_Value, _Root);
	}

	CGitRepositoryExcludes fg_GetGitRepositoryExcludes(CStr const &_Root, CGitDirectories const &_Directories, CGitEnvironment const &_Environment)
	{
		CGitRepositoryExcludes Excludes;
		if (_Directories.m_CommonDirectory)
		{
			auto InfoExclude = _Directories.m_CommonDirectory / "info/exclude";
			if (CFile::fs_FileExists(InfoExclude, EFileAttrib_File))
				Excludes.m_InfoExclude = InfoExclude;
		}

		CStr ExcludesFile;
		bool bWorktreeConfiguration = false;
		auto fRead = [&](CStr const &_File)
			{
				if (!CFile::fs_FileExists(_File, EFileAttrib_File))
					return;

				auto Configuration = CGitConfigParser::fs_Parse(CFile::fs_ReadStringFromFile(_File, true));
				if (auto pValue = Configuration.f_GetValue("extensions", "worktreeconfig"))
					bWorktreeConfiguration = CGitConfigParser::fs_ToBoolean(*pValue);

				if (auto pValue = Configuration.f_GetValue("core", "excludesfile"))
					ExcludesFile = fg_ExpandGitPath(*pValue, _Root, _Environment);
			}
		;
		for (auto const &File : fg_GetGitConfigurationFiles(_Directories, _Environment))
			fRead(File);

		// A worktree's own file is read only once the repository has opted into it.
		if (bWorktreeConfiguration && _Directories.m_GitDirectory)
			fRead(_Directories.m_GitDirectory / "config.worktree");

		if (!ExcludesFile)
		{
			if (_Environment.m_ConfigHome)
				ExcludesFile = _Environment.m_ConfigHome / "git/ignore";
			else if (_Environment.m_Home)
				ExcludesFile = _Environment.m_Home / ".config/git/ignore";
		}

		if (ExcludesFile && CFile::fs_FileExists(ExcludesFile, EFileAttrib_File))
			Excludes.m_ExcludesFile = ExcludesFile;

		return Excludes;
	}
}
