// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Every public header of the module, in a test target built with DMibPNoShortCuts, so that a header that needs the shortcuts of
// Malterlib's namespaces fails the build.

#include <Mib/Git/Helpers/ConfigParser>
#include <Mib/Git/Helpers/Credentials>
#include <Mib/Git/Helpers/Launch>
#include <Mib/Git/HostingProvider>
#include <Mib/Git/Ignore>
#include <Mib/Git/LfsReleaseStore>
#include <Mib/Git/Policy>
