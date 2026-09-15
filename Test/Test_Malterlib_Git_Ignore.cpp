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

			DMibTestSuite("Attributes")
			{
				CGitAttributes Attributes;
				Attributes.f_AddRules("", "*.cpp diff\n*.dat -diff\n*.bin binary\n*.img diff=custom\n*.txt text\n");
				Attributes.f_AddRules("nested", "*.dat diff\n*.cpp !diff\n");
				DMibExpectTrue(Attributes.f_GetTextAttribute("src/main.cpp") == EGitTextAttribute::mc_Text);
				DMibExpectTrue(Attributes.f_GetTextAttribute("data.dat") == EGitTextAttribute::mc_Binary);
				DMibExpectTrue(Attributes.f_GetTextAttribute("blob.bin") == EGitTextAttribute::mc_Binary);
				// A named driver and a plain text attribute say nothing about binariness.
				DMibExpectTrue(Attributes.f_GetTextAttribute("disk.img") == EGitTextAttribute::mc_Unspecified);
				DMibExpectTrue(Attributes.f_GetTextAttribute("notes.txt") == EGitTextAttribute::mc_Unspecified);
				DMibExpectTrue(Attributes.f_GetTextAttribute("other.md") == EGitTextAttribute::mc_Unspecified);
				// The deeper file's rules win within its directory and reach nothing outside it.
				DMibExpectTrue(Attributes.f_GetTextAttribute("nested/data.dat") == EGitTextAttribute::mc_Text);
				DMibExpectTrue(Attributes.f_GetTextAttribute("nested/main.cpp") == EGitTextAttribute::mc_Unspecified);
				DMibExpectTrue(Attributes.f_GetTextAttribute("other/data.dat") == EGitTextAttribute::mc_Binary);
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
					DMibExpectTrue(!fg_FindGitWorkingTreeRoot(Root / "plain") || !fg_FindGitWorkingTreeRoot(Root / "plain").f_StartsWith(Root));
					auto Excludes = fg_GetGitRepositoryExcludes(Root / "plain", Directories, Environment);
					DMibExpectTrue(!Excludes.m_InfoExclude);
				};

				DMibTestCategory("RepositoryConfiguration")
				{
					CStr Main = Root / "main";
					fg_WriteFile(Main / ".git/config", "[core]\n\texcludesFile = ~/ignore-list\n");
					fg_WriteFile(Main / ".git/info/exclude", "Local/\n");
					fg_WriteFile(Home / "ignore-list", "Generated/\n");
					fg_WriteFile(Main / ".git/info/attributes", "*.dat -diff\n");
					auto Directories = fg_GetGitDirectories(Main);
					DMibExpect(Directories.m_GitDirectory, ==, Main / ".git");
					DMibExpect(Directories.m_CommonDirectory, ==, Main / ".git");
					auto Excludes = fg_GetGitRepositoryExcludes(Main, Directories, Environment);
					DMibExpect(Excludes.m_ExcludesFile, ==, Home / "ignore-list");
					DMibExpect(Excludes.m_InfoExclude, ==, Main / ".git/info/exclude");
					DMibExpect(Excludes.m_InfoAttributes, ==, Main / ".git/info/attributes");
					CFile::fs_CreateDirectory(Main / "src/deep");
					DMibExpect(fg_FindGitWorkingTreeRoot(Main / "src/deep"), ==, Main);
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
