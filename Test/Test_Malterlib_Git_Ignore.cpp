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
		}
	};

	DMibTestRegister(CIgnore_Tests, Malterlib::Git);
}
