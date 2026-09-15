// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Git/Ignore>
#include <Mib/File/File>
#include <Mib/Test/Test>

namespace
{
	using namespace NMib;
	using namespace NMib::NFile;
	using namespace NMib::NGit;
	using namespace NMib::NStr;
	using namespace NMib::NTest;

	void fg_WriteFile(CStr const &_Path, CStr const &_Contents)
	{
		CFile::fs_CreateDirectoryForFile(_Path);
		CFile::fs_WriteStringToFile(_Path, _Contents, false);
	}

	struct CIgnore_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Rules")
			{
				CGitIgnore Ignore;
				Ignore.f_AddRules("", "# comment\n*.o\n!keep.o\nbuild/\n/root-only\ndocs/*.md\n**/generated\ntrailing   \n\\#literal\n");

				DMibTestCategory("Names")
				{
					DMibExpectTrue(Ignore.f_IsIgnored("main.o", false));
					DMibExpectTrue(Ignore.f_IsIgnored("src/deep/main.o", false));
					DMibExpectFalse(Ignore.f_IsIgnored("src/keep.o", false));
					DMibExpectFalse(Ignore.f_IsIgnored("main.c", false));
					DMibExpectTrue(Ignore.f_IsIgnored("src/trailing", false));
					DMibExpectTrue(Ignore.f_IsIgnored("#literal", false));
				};

				DMibTestCategory("Directories")
				{
					// A rule ending in a separator meets directories only.
					DMibExpectTrue(Ignore.f_IsIgnored("build", true));
					DMibExpectTrue(Ignore.f_IsIgnored("src/build", true));
					DMibExpectFalse(Ignore.f_IsIgnored("build", false));
					DMibExpectTrue(Ignore.f_IsIgnored("src/generated", true));
					DMibExpectTrue(Ignore.f_IsIgnored("generated", false));
				};

				DMibTestCategory("Anchored")
				{
					DMibExpectTrue(Ignore.f_IsIgnored("root-only", false));
					DMibExpectFalse(Ignore.f_IsIgnored("src/root-only", false));
					DMibExpectTrue(Ignore.f_IsIgnored("docs/readme.md", false));
					DMibExpectFalse(Ignore.f_IsIgnored("docs/nested/readme.md", false));
					DMibExpectFalse(Ignore.f_IsIgnored("src/docs/readme.md", false));
				};
			};

			DMibTestSuite("Precedence")
			{
				// A deeper file's rules are relative to its directory and override the rules
				// above it; within a file the last matching rule decides.
				CGitIgnore Ignore;
				Ignore.f_AddRules("", "*.log\n/sub/*.tmp\n");
				Ignore.f_AddRules("sub", "!important.log\n*.tmp\n!*.tmp\n");
				DMibExpectTrue(Ignore.f_IsIgnored("top.log", false));
				DMibExpectTrue(Ignore.f_IsIgnored("sub/other.log", false));
				DMibExpectFalse(Ignore.f_IsIgnored("sub/important.log", false));
				DMibExpectFalse(Ignore.f_IsIgnored("sub/scratch.tmp", false));
				// The deeper file's negation reaches nothing outside its directory.
				DMibExpectTrue(Ignore.f_IsIgnored("other/important.log", false));
			};

			DMibTestSuite("GitSyntax")
			{
				// Braces are literal, '**' crosses directories only as a whole component, and '^' negates a set.
				CGitIgnore Ignore;
				Ignore.f_AddRules("", "foo{1..3}\n{a,b}.txt\nsrc/a**b\nfile[^0-9]\n");
				DMibExpectTrue(Ignore.f_IsIgnored("foo{1..3}", false));
				DMibExpectTrue(Ignore.f_IsIgnored("{a,b}.txt", false));
				DMibExpectFalse(Ignore.f_IsIgnored("a.txt", false));
				DMibExpectTrue(Ignore.f_IsIgnored("src/axxb", false));
				DMibExpectFalse(Ignore.f_IsIgnored("src/a/x/b", false));
				DMibExpectTrue(Ignore.f_IsIgnored("filex", false));
				DMibExpectFalse(Ignore.f_IsIgnored("file1", false));

				// core.ignoreCase folds ASCII letters, in ranges too.
				CGitIgnore IgnoreCase;
				IgnoreCase.f_AddRules("", "build/\n[L-N]akefile\n", true);
				DMibExpectTrue(IgnoreCase.f_IsIgnored("BUILD", true));
				DMibExpectTrue(IgnoreCase.f_IsIgnored("makefile", false));
				DMibExpectFalse(Ignore.f_IsIgnored("BUILD", true));

				// A ']' leading a set is a member, a '[:class:]' is a class, and a run of stars that is a whole component crosses
				// directories.
				CGitIgnore Sets;
				Sets.f_AddRules("", "file[]].txt\n[[:digit:]].log\na/***/b\n");
				DMibExpectTrue(Sets.f_IsIgnored("file].txt", false));
				DMibExpectTrue(Sets.f_IsIgnored("7.log", false));
				DMibExpectFalse(Sets.f_IsIgnored("x.log", false));
				DMibExpectTrue(Sets.f_IsIgnored("a/x/y/b", false));

				// A slash in a set anchors the pattern, and a pattern git rejects, an unclosed set or an unknown class, matches
				// nothing.
				CGitIgnore Odd;
				Odd.f_AddRules("", "[a/]b\n[abc\n[[:foo:]a]\n[[:]\n");
				DMibExpectTrue(Odd.f_IsIgnored("ab", false));
				DMibExpectFalse(Odd.f_IsIgnored("dir/ab", false));
				DMibExpectFalse(Odd.f_IsIgnored("[abc", false));
				DMibExpectFalse(Odd.f_IsIgnored("a", false));

				// A '[:' that no ':]' closes is two members of its set, and an escape of nothing rejects the pattern.
				CGitIgnore Open;
				Open.f_AddRules("", "[[:]ab]\nfoo\\\n");
				DMibExpectTrue(Open.f_IsIgnored("[ab]", false));
				DMibExpectFalse(Open.f_IsIgnored("foo\\", false));

				// Patterns match bytes, as git's do: 'é' is two of them, and an escaped separator ends a recursive star's component.
				CGitIgnore Bytes;
				Bytes.f_AddRules("", "??.txt\n**\\/x\n");
				DMibExpectTrue(Bytes.f_IsIgnored("\xC3\xA9.txt", false));
				DMibExpectTrue(Bytes.f_IsIgnored("a/b/x", false));

				// A backslash is a byte of a name.
				CGitIgnore Backslash;
				Backslash.f_AddRules("", "foo\\\\bar\n");
				DMibExpectTrue(Backslash.f_IsIgnored("dir/foo\\bar", false));
			};

			DMibTestSuite("RepositoryExcludes")
			{
				CStr Root = CFile::fs_GetProgramDirectory() / "GitExcludesTests";
				if (CFile::fs_FileExists(Root))
					CFile::fs_DeleteDirectoryRecursive(Root);
				fg_TestAddCleanupPath(Root);

				CStr Home = Root / "home";
				CGitEnvironment Environment;
				Environment.m_Home = Home;
				Environment.m_bNoSystem = true;

				DMibTestCategory("NotARepository")
				{
					CFile::fs_CreateDirectory(Root / "plain");
					auto Directories = fg_GetGitDirectories(Root / "plain");
					DMibExpectTrue(!Directories.m_GitDirectory);
					auto Excludes = fg_GetGitRepositoryExcludes(Root / "plain", Directories, Environment);
					DMibExpectTrue(!Excludes.m_InfoExclude);
				};

				DMibTestCategory("RepositoryConfiguration")
				{
					CStr Main = Root / "main";
					fg_WriteFile(Main / ".git/config", "[core]\n\texcludesFile = ~/ignore-list\n");
					fg_WriteFile(Main / ".git/info/exclude", "Local/\n");
					fg_WriteFile(Home / "ignore-list", "Generated/\n");
					auto Directories = fg_GetGitDirectories(Main);
					DMibExpect(Directories.m_GitDirectory, ==, Main / ".git");
					DMibExpect(Directories.m_CommonDirectory, ==, Main / ".git");
					auto Excludes = fg_GetGitRepositoryExcludes(Main, Directories, Environment);
					DMibExpect(Excludes.m_ExcludesFile, ==, Home / "ignore-list");
					DMibExpect(Excludes.m_InfoExclude, ==, Main / ".git/info/exclude");
				};

				DMibTestCategory("ConfigurationValues")
				{
					// A value stays on its key's line, blanks behind it are no part of it, and a boolean is true as a key alone
					// or any integer but zero.
					CStr Values = Root / "values";
					fg_WriteFile(Values / ".git/config", "[core]\n\texcludesFile =\n\tignoreCase\n");
					auto Directories = fg_GetGitDirectories(Values);
					auto Empty = fg_GetGitRepositoryExcludes(Values, Directories, Environment);
					DMibExpectTrue(!Empty.m_ExcludesFile);
					DMibExpectTrue(Empty.m_bIgnoreCase);

					fg_WriteFile(Values / "list", "");
					fg_WriteFile(Values / ".git/config", "[core]\n\texcludesFile = list   \n\tignoreCase = -1\n");
					auto Spaced = fg_GetGitRepositoryExcludes(Values, Directories, Environment);
					DMibExpect(Spaced.m_ExcludesFile, ==, Values / "list");
					DMibExpectTrue(Spaced.m_bIgnoreCase);
				};

				DMibTestCategory("DefaultAndGlobal")
				{
					CStr Bare = Root / "bare";
					fg_WriteFile(Bare / ".git/config", "[core]\n\tbare = false\n");
					auto Directories = fg_GetGitDirectories(Bare);

					// Nothing names a file, and the default does not exist.
					DMibExpectTrue(!fg_GetGitRepositoryExcludes(Bare, Directories, Environment).m_ExcludesFile);

					fg_WriteFile(Home / ".config/git/ignore", "*.tmp\n");
					DMibExpect(fg_GetGitRepositoryExcludes(Bare, Directories, Environment).m_ExcludesFile, ==, Home / ".config/git/ignore");

					// An empty value turns the default off.
					fg_WriteFile(Bare / ".git/config", "[core]\n\texcludesFile =\n");
					auto Disabled = fg_GetGitRepositoryExcludes(Bare, Directories, Environment);
					DMibExpectTrue(!Disabled.m_ExcludesFile);
					fg_WriteFile(Bare / ".git/config", "[core]\n\tbare = false\n");

					// XDG_CONFIG_HOME moves the default and the global configuration file.
					auto Xdg = Environment;
					Xdg.m_ConfigHome = Root / "xdg";
					fg_WriteFile(Root / "xdg/git/ignore", "*.bak\n");
					DMibExpect(fg_GetGitRepositoryExcludes(Bare, Directories, Xdg).m_ExcludesFile, ==, Root / "xdg/git/ignore");
					fg_WriteFile(Root / "xdg/git/config", "[core]\n\texcludesFile = " + Root / "from-xdg" + "\n");
					fg_WriteFile(Root / "from-xdg", "");
					DMibExpect(fg_GetGitRepositoryExcludes(Bare, Directories, Xdg).m_ExcludesFile, ==, Root / "from-xdg");

					// ~/.gitconfig is read after the XDG file, and GIT_CONFIG_GLOBAL replaces both.
					fg_WriteFile(Home / ".gitconfig", "[core]\n\texcludesFile = " + Root / "from-home" + "\n");
					fg_WriteFile(Root / "from-home", "");
					DMibExpect(fg_GetGitRepositoryExcludes(Bare, Directories, Xdg).m_ExcludesFile, ==, Root / "from-home");
					auto Global = Xdg;
					Global.m_GlobalConfiguration = Root / "global-config";
					fg_WriteFile(Root / "global-config", "[core]\n\texcludesFile = " + Root / "from-global" + "\n");
					fg_WriteFile(Root / "from-global", "");
					DMibExpect(fg_GetGitRepositoryExcludes(Bare, Directories, Global).m_ExcludesFile, ==, Root / "from-global");

					// The repository's own value wins over every global one; a relative path is
					// taken from the working tree root, and a missing file is no file.
					fg_WriteFile(Bare / ".git/config", "[core]\n\texcludesFile = own-excludes\n");
					DMibExpectTrue(!fg_GetGitRepositoryExcludes(Bare, Directories, Global).m_ExcludesFile);
					fg_WriteFile(Bare / "own-excludes", "");
					DMibExpect(fg_GetGitRepositoryExcludes(Bare, Directories, Global).m_ExcludesFile, ==, Bare / "own-excludes");
				};

				DMibTestCategory("Worktree")
				{
					CStr Main = Root / "wt-main";
					CStr Worktree = Root / "wt-other";
					fg_WriteFile(Main / ".git/config", "[core]\n\texcludesFile = " + Root / "shared-excludes" + "\n[extensions]\n\tworktreeConfig = true\n");
					fg_WriteFile(Main / ".git/info/exclude", "Local/\n");
					fg_WriteFile(Main / ".git/worktrees/other/commondir", "../..\n");
					fg_WriteFile(Root / "shared-excludes", "");
					fg_WriteFile(Worktree / ".git", "gitdir: " + Main / ".git/worktrees/other" + "\n");
					auto Directories = fg_GetGitDirectories(Worktree);
					DMibExpect(Directories.m_GitDirectory, ==, Main / ".git/worktrees/other");
					DMibExpect(Directories.m_CommonDirectory, ==, Main / ".git");
					auto Excludes = fg_GetGitRepositoryExcludes(Worktree, Directories, Environment);
					DMibExpect(Excludes.m_ExcludesFile, ==, Root / "shared-excludes");
					DMibExpect(Excludes.m_InfoExclude, ==, Main / ".git/info/exclude");

					// With the extension on, the worktree's own configuration is read last.
					fg_WriteFile(Main / ".git/worktrees/other/config.worktree", "[core]\n\texcludesFile = " + Root / "worktree-excludes" + "\n");
					fg_WriteFile(Root / "worktree-excludes", "");
					DMibExpect(fg_GetGitRepositoryExcludes(Worktree, Directories, Environment).m_ExcludesFile, ==, Root / "worktree-excludes");
				};
			};
		}
	};

	DMibTestRegister(CIgnore_Tests, Malterlib::Git);
}
