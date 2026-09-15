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

	// git's spelling of a boolean, its words in any case.
	static bool fg_GitBoolean(CStr const &_Value)
	{
		return CGitConfigParser::fs_ToBoolean(_Value.f_LowerCase());
	}

	// The name a path ends in, after its last '/': a backslash is a byte of the name in git's paths.
	static CStr fg_GetGitBaseName(CStr const &_Path)
	{
		auto iSlash = _Path.f_FindReverse("/");
		return iSlash >= 0 ? _Path.f_Extract(iSlash + 1) : _Path;
	}

	void CGitIgnore::f_AddRules(CStr const &_Directory, CStr const &_Contents, bool _bIgnoreCase)
	{
		CRuleSet Set;
		Set.m_Directory = _Directory;
		for (auto const &RawLine : _Contents.f_SplitLine())
		{
			// Trailing spaces are ignored unless quoted with a backslash that is not itself quoted.
			auto Line = RawLine;
			while (Line.f_EndsWith(" "))
			{
				umint nBackslashes = 0;
				auto pLine = Line.f_GetStr();
				for (auto i = Line.f_GetLen() - 1; i > 0 && pLine[i - 1] == '\\'; --i)
					++nBackslashes;

				if (nBackslashes % 2)
					break;

				Line = Line.f_Left(Line.f_GetLen() - 1);
			}

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
			Rule.m_Glob = CPathGlob(Pattern, EPathGlobDialect::mc_Git, _bIgnoreCase);
			Set.m_Rules.f_Insert(fg_Move(Rule));
		}

		mp_Sets.f_Insert(fg_Move(Set));
	}

	bool CGitIgnore::f_IsIgnored(CStr const &_Path, bool _bDirectory) const
	{
		auto FileName = CPathGlob::fs_ToText(fg_GetGitBaseName(_Path), EPathGlobDialect::mc_Git);
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

			auto Path = CPathGlob::fs_ToText(Relative, EPathGlobDialect::mc_Git);
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

	// A line names a pattern and then attributes, each set, unset with a leading '-', or
	// reset with a leading '!'; setting the 'binary' macro unsets 'diff' where nothing
	// that takes precedence assigns 'diff', and unsetting or resetting it assigns nothing. A pattern with spaces would need
	// quoting, which no Malterlib file uses. '[attr]' lines define macros, which only
	// 'binary' is read as.
	auto CGitAttributes::fsp_ParseRules(CStr const &_Directory, CStr const &_Contents, bool _bIgnoreCase) -> CRuleSet
	{
		CRuleSet Set;
		Set.m_Directory = _Directory;
		for (auto const &RawLine : _Contents.f_SplitLine())
		{
			auto Line = RawLine.f_Trim();
			if (!Line || Line.f_StartsWith("#") || Line.f_StartsWith("[attr]"))
				continue;

			auto Words = Line.f_Replace("\t", " ").f_Split<true>(" ");
			if (Words.f_GetLen() < 2)
				continue;

			// git rejects a negated attribute pattern; a quoted '!' is literal.
			auto Pattern = Words[0];
			if (Pattern.f_StartsWith("!"))
				continue;

			if (Pattern.f_StartsWith("\\#") || Pattern.f_StartsWith("\\!"))
				Pattern = Pattern.f_Extract(1);

			CRule Rule
				{
					CPathGlob(Pattern, EPathGlobDialect::mc_Git, _bIgnoreCase)
				}
			;
			for (umint iWord = 1; iWord < Words.f_GetLen(); ++iWord)
			{
				auto const &Word = Words[iWord];
				if (Word == "diff")
					Rule.m_Assignments.f_Insert({.m_Diff = EGitTextAttribute::mc_Text});
				else if (Word == "-diff")
					Rule.m_Assignments.f_Insert({.m_Diff = EGitTextAttribute::mc_Binary});
				else if (Word == "!diff" || Word.f_StartsWith("diff="))
					Rule.m_Assignments.f_Insert({.m_Diff = EGitTextAttribute::mc_Unspecified});
				else if (Word == "binary")
					Rule.m_Assignments.f_Insert({.m_bMacro = true, .m_bMacroSet = true});
				else if (Word == "-binary" || Word == "!binary" || Word.f_StartsWith("binary="))
					Rule.m_Assignments.f_Insert({.m_bMacro = true});
			}

			if (!Rule.m_Assignments.f_IsEmpty())
				Set.m_Rules.f_Insert(fg_Move(Rule));
		}

		return Set;
	}

	void CGitAttributes::f_AddRules(CStr const &_Directory, CStr const &_Contents, bool _bIgnoreCase)
	{
		mp_Sets.f_Insert(fsp_ParseRules(_Directory, _Contents, _bIgnoreCase));
	}

	void CGitAttributes::f_AddInfoRules(CStr const &_Contents, bool _bIgnoreCase)
	{
		auto Set = fsp_ParseRules({}, _Contents, _bIgnoreCase);
		mp_Info.m_Rules.f_Insert(fg_Move(Set.m_Rules));
	}

	// Within a set the last matching rule takes precedence, and within a rule its last word. True once 'diff' is decided.
	bool CGitAttributes::fsp_Decide(CRuleSet const &_Set, CStr const &_Path, CUStr const &_FileName, CPathGlob::CScratch &_Scratch, CDecision &o_Decision)
	{
		CStr Relative = _Path;
		if (_Set.m_Directory)
		{
			if (!_Path.f_StartsWith(_Set.m_Directory + "/"))
				return false;

			Relative = _Path.f_Extract(_Set.m_Directory.f_GetLen() + 1);
		}

		auto Path = CPathGlob::fs_ToText(Relative, EPathGlobDialect::mc_Git);
		for (umint iRule = _Set.m_Rules.f_GetLen(); iRule; --iRule)
		{
			auto const &Rule = _Set.m_Rules[iRule - 1];
			if (!Rule.m_Glob.f_Match(Path, _FileName, _Scratch))
				continue;

			for (umint iAssignment = Rule.m_Assignments.f_GetLen(); iAssignment; --iAssignment)
			{
				auto const &Assignment = Rule.m_Assignments[iAssignment - 1];
				if (Assignment.m_bMacro)
				{
					if (o_Decision.m_bMacro)
						continue;

					o_Decision.m_bMacro = true;
					if (!Assignment.m_bMacroSet)
						continue;

					o_Decision.m_Text = EGitTextAttribute::mc_Binary;
				}
				else
					o_Decision.m_Text = Assignment.m_Diff;

				o_Decision.m_bDiff = true;

				return true;
			}
		}

		return false;
	}

	EGitTextAttribute CGitAttributes::f_GetTextAttribute(CStr const &_Path) const
	{
		auto FileName = CPathGlob::fs_ToText(fg_GetGitBaseName(_Path), EPathGlobDialect::mc_Git);
		CPathGlob::CScratch Scratch;
		CDecision Decision;
		if (fsp_Decide(mp_Info, _Path, FileName, Scratch, Decision))
			return Decision.m_Text;

		// Deeper files and later rules take precedence.
		for (umint iSet = mp_Sets.f_GetLen(); iSet; --iSet)
		{
			if (fsp_Decide(mp_Sets[iSet - 1], _Path, FileName, Scratch, Decision))
				return Decision.m_Text;
		}

		return EGitTextAttribute::mc_Unspecified;
	}

	CStr fg_FindGitWorkingTreeRoot(CStr const &_Directory)
	{
		for (auto Directory = _Directory; Directory; )
		{
			if (CFile::fs_FileExists(Directory / ".git"))
				return Directory;

			auto Parent = CFile::fs_GetPath(Directory);
			if (Parent == Directory)
				break;

			Directory = fg_Move(Parent);
		}

		return {};
	}

	CGitEnvironment CGitEnvironment::fs_FromProcess()
	{
		auto *pSys = fg_GetSys();
		CGitEnvironment Environment;
		Environment.m_Home = pSys->f_GetEnvironmentVariable("HOME");
		if (!Environment.m_Home)
			Environment.m_Home = pSys->f_GetEnvironmentVariable("USERPROFILE");
		Environment.m_ConfigHome = pSys->f_GetEnvironmentVariable("XDG_CONFIG_HOME");
		Environment.m_GlobalConfiguration = pSys->f_GetEnvironmentVariable("GIT_CONFIG_GLOBAL", {}, &Environment.m_bGlobalConfigurationSet);
		Environment.m_SystemConfiguration = pSys->f_GetEnvironmentVariable("GIT_CONFIG_SYSTEM", {}, &Environment.m_bSystemConfigurationSet);
		Environment.m_bNoSystem = fg_GitBoolean(pSys->f_GetEnvironmentVariable("GIT_CONFIG_NOSYSTEM"));

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
	// repository has opted into worktree configuration. The system file is '/etc/gitconfig'
	// unless GIT_CONFIG_SYSTEM names another: a git installed under another prefix, as
	// Homebrew's or Git for Windows, reads its own, which is not looked for without asking git.
	static TCVector<CStr> fg_GetGitConfigurationFiles(CGitDirectories const &_Directories, CGitEnvironment const &_Environment)
	{
		// A variable set empty reads no file in place of the default.
		TCVector<CStr> Files;
		if (!_Environment.m_bNoSystem && _Environment.m_SystemConfiguration)
			Files.f_Insert(_Environment.m_SystemConfiguration);
		else if (!_Environment.m_bNoSystem && !_Environment.m_bSystemConfigurationSet)
			Files.f_Insert("/etc/gitconfig");

		if (_Environment.m_GlobalConfiguration)
			Files.f_Insert(_Environment.m_GlobalConfiguration);
		else if (!_Environment.m_bGlobalConfigurationSet)
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

			auto InfoAttributes = _Directories.m_CommonDirectory / "info/attributes";
			if (CFile::fs_FileExists(InfoAttributes, EFileAttrib_File))
				Excludes.m_InfoAttributes = InfoAttributes;
		}

		// Files that [include] and [includeIf] name are not followed: a core.excludesFile, core.attributesFile or core.ignoreCase set only
		// through one is not seen, which is out of scope for the formatting walk this serves.
		CStr ExcludesFile;
		bool bExcludesFileSet = false; // An empty value turns the default file off.
		CStr AttributesFile;
		bool bAttributesFileSet = false;
		bool bWorktreeConfiguration = false;
		auto fRead = [&](CStr const &_File)
			{
				if (!CFile::fs_FileExists(_File, EFileAttrib_File))
					return;

				auto Configuration = CGitConfigParser::fs_Parse(CFile::fs_ReadStringFromFile(_File, true));

				// Only the repository's own configuration opts it into worktree configuration.
				bool bRepositoryConfiguration = _Directories.m_CommonDirectory && _File == _Directories.m_CommonDirectory / "config";
				if (auto pValue = Configuration.f_GetValue("extensions", "worktreeconfig"); pValue && bRepositoryConfiguration)
					bWorktreeConfiguration = fg_GitBoolean(*pValue);

				if (auto pValue = Configuration.f_GetValue("core", "excludesfile"))
				{
					ExcludesFile = fg_ExpandGitPath(*pValue, _Root, _Environment);
					bExcludesFileSet = true;
				}

				if (auto pValue = Configuration.f_GetValue("core", "attributesfile"))
				{
					AttributesFile = fg_ExpandGitPath(*pValue, _Root, _Environment);
					bAttributesFileSet = true;
				}

				if (auto pValue = Configuration.f_GetValue("core", "ignorecase"))
					Excludes.m_bIgnoreCase = fg_GitBoolean(*pValue);
			}
		;
		for (auto const &File : fg_GetGitConfigurationFiles(_Directories, _Environment))
			fRead(File);

		// A worktree's own file is read only once the repository has opted into it.
		if (bWorktreeConfiguration && _Directories.m_GitDirectory)
			fRead(_Directories.m_GitDirectory / "config.worktree");

		if (!bExcludesFileSet)
		{
			if (_Environment.m_ConfigHome)
				ExcludesFile = _Environment.m_ConfigHome / "git/ignore";
			else if (_Environment.m_Home)
				ExcludesFile = _Environment.m_Home / ".config/git/ignore";
		}

		if (ExcludesFile && CFile::fs_FileExists(ExcludesFile, EFileAttrib_File))
			Excludes.m_ExcludesFile = ExcludesFile;

		if (!bAttributesFileSet)
		{
			if (_Environment.m_ConfigHome)
				AttributesFile = _Environment.m_ConfigHome / "git/attributes";
			else if (_Environment.m_Home)
				AttributesFile = _Environment.m_Home / ".config/git/attributes";
		}

		if (AttributesFile && CFile::fs_FileExists(AttributesFile, EFileAttrib_File))
			Excludes.m_AttributesFile = AttributesFile;

		return Excludes;
	}
}
