// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Git/Ignore>
#include <Mib/Test/Test>

namespace
{
	using namespace NMib;
	using namespace NMib::NGit;
	using namespace NMib::NStr;
	using namespace NMib::NTest;

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
		}
	};

	DMibTestRegister(CIgnore_Tests, Malterlib::Git);
}
