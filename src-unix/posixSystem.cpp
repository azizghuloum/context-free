// posixSystem.cpp
// Context Free
// ---------------------
// Copyright (C) 2005-2007 Mark Lentczner - markl@glyphic.com
// Copyright (C) 2007-2013 John Horigan - john@glyphic.com
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
// 
// Mark Lentczner can be contacted at markl@glyphic.com or at
// Mark Lentczner, 1209 Villa St., Mountain View, CA 94041-1123, USA
//
// John Horigan can be contacted at john@glyphic.com or at
// John Horigan, 1209 Villa St., Mountain View, CA 94041-1123, USA
//
//

#include "posixSystem.h"

#ifdef __APPLE__
// Linking with libicucore, not full libicu
#define U_DISABLE_RENAMING 1
#endif

#define UCHAR_TYPE char16_t
#include <unicode/ucnv.h>
#include <unicode/unorm2.h>
#include <unicode/putil.h>
#include <unicode/unistr.h> // ICU header for UnicodeString


#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <cstring>

#ifndef NOSYSCTL
#if defined(__GNU__) || (defined(__ILP32__) && defined(__x86_64__))
  #define NOSYSCTL
#else
  #ifndef __linux__
    #include <sys/sysctl.h>
  #endif
#endif
#endif
#include <sys/types.h>
#include <unistd.h>
#include <cstdint>

using std::cerr;
using std::endl;

#include "fdostream.h"

void
PosixSystem::clearAndCR()
{
    static const char* EraseEndofLine = "\x1b[K";
    cerr << EraseEndofLine << '\r';
}

void
PosixSystem::catastrophicError(const char* what)
{
    cerr << "\n\nUnexpected error: " << what << endl;
    std::exit(33);
}

const AbstractSystem::FileChar*
PosixSystem::tempFileDirectory()
{
    struct stat sb;
    const char *                                              tmpenv = getenv("TMPDIR");
    if (!tmpenv || stat(tmpenv, &sb) || !S_ISDIR(sb.st_mode)) tmpenv = getenv("TEMP");
    if (!tmpenv || stat(tmpenv, &sb) || !S_ISDIR(sb.st_mode)) tmpenv = getenv("TMP");
    if (!tmpenv || stat(tmpenv, &sb) || !S_ISDIR(sb.st_mode)) tmpenv = "/tmp/"; // give up
    return tmpenv;
}

namespace {
    struct MallocDeleter {
        void operator()(void* ptr) const {
            std::free(ptr);
        }
    };
    struct DirCloser {
        void operator()(DIR* ptr) const {
            (void)closedir(ptr);    // not called if nullptr
        }
    };
}

AbstractSystem::ostr_ptr
PosixSystem::tempFileForWrite(AbstractSystem::TempType tt, FileString& nameOut)
{
    std::string t(tempFileDirectory());
    if (t.back() != '/')
        t.push_back('/');
    t.append(TempPrefixes[tt]);
    t.append("XXXXXX");
    t.append(TempSuffixes[tt]);
    
    ostr_ptr f;
    
    std::unique_ptr<char, MallocDeleter> b(strdup(t.c_str()));
    int tfd = mkstemps(b.get(), (int)std::strlen(TempSuffixes[tt]));
    if (tfd != -1) {
        f = std::make_unique<boost::fdostream>(tfd);
        nameOut.assign(b.get());
    }
    
    return f;
}

std::string
PosixSystem::relativeFilePath(const std::string& base, const std::string& rel)
{
    std::string s = base;
    
    std::string::size_type i = s.rfind('/');
    if (i == std::string::npos) {
        return rel;
    }
    i += 1;
    s.replace(i, s.length() - i, rel);
    return s;
}

int
PosixSystem::deleteTempFile(const FileString& name)
{
    return remove(name.c_str());
}

std::vector<AbstractSystem::FileString>
PosixSystem::findTempFiles()
{
    std::vector<FileString> ret;
    const char* dirname = tempFileDirectory();
    std::size_t len = std::strlen(TempPrefixAll);
    std::unique_ptr<DIR, DirCloser> dirp(opendir(dirname));
    if (!dirp) return ret;
    while (dirent* der = readdir(dirp.get())) {
        if (std::strncmp(TempPrefixAll, der->d_name, len) == 0) {
            ret.emplace_back(dirname);
            if (ret.back().back() != '/')
                ret.back().push_back('/');
            ret.back().append(der->d_name);
        }
    }
    
    return ret;
}

std::size_t
PosixSystem::getPhysicalMemory()
{
#ifdef NOSYSCTL
    return 0;
#elif defined(__linux__)
  #if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    std::uint64_t size = sysconf(_SC_PHYS_PAGES) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    if (size > MaximumMemory)
        size = MaximumMemory;
    return static_cast<std::size_t>(size);
  #else
    return 0;
  #endif
#else // not __linux__ and not NOSYSCTL
    int mib[2];
  #ifdef CTL_HW
    mib[0] = CTL_HW;
  #else
    return 0;
  #endif
  #if defined(HW_MEMSIZE)
    mib[1] = HW_MEMSIZE;    // OSX
    std::uint64_t size = 0;      // 64-bit
  #elif defined(HW_PHYSMEM64)
    mib[1] = HW_PHYSMEM64;  // NetBSD, OpenBSD
    std::uint64_t size = 0;      // 64-bit
  #elif defined(HW_REALMEM)
    mib[1] = HW_REALMEM;    // FreeBSD
    unsigned int size = 0;  // 32-bit
  #elif defined(HW_PHYSMEM)
    mib[1] = HW_PHYSMEM;    // DragonFly BSD
    unsigned int size = 0;  // 32-bit
  #else
    std::uint64_t size = 0;      // need to define this anyway
    return 0;
  #endif
    std::size_t len = sizeof(size);
    if (sysctl(mib, 2, &size, &len, NULL, 0) == 0) {
        if (size > MaximumMemory)
            size = MaximumMemory;
        return static_cast<std::size_t>(size);
    }
    return 0;
#endif // __linux__ || NOSYSCTL
}

PosixSystem::PosixSystem()
: mConverter(nullptr), mNormalizer(nullptr), mErrorReported(false)
{
}

PosixSystem::~PosixSystem()
{
    if (mConverter)
        ucnv_close(mConverter);
}

#ifdef NONORMALIZE
std::wstring
PosixSystem::normalize(const std::string& u8name) {
    icu::UnicodeString ustr = icu::UnicodeString::fromUTF8(icu::StringPiece(u8name.c_str()));
    std::wstring wstr(ustr.length(), L' ');
    for (int32_t i = 0; i < ustr.length(); ++i) {
        wstr[i] = ustr.charAt(i);
    }
    return wstr;
}
#else
std::wstring
PosixSystem::normalize(const std::string& u8name)
{
    // Setup ICU library items
    UErrorCode status = U_ZERO_ERROR;
    if (!mConverter)
        mConverter = ucnv_open("utf-8", &status);
    if (U_FAILURE(status)) {
        catastrophicError("No Converter");
    }
    if (!mNormalizer)
        mNormalizer = unorm2_getNFKCInstance(&status);
    if (U_FAILURE(status)) {
        std::cerr << "Error getting NFKC normalizer: " << u_errorName(status) << std::endl;
        catastrophicError("No Normalizer");
    }
    
    // Convert from utf-8 to utf-16
    std::u16string u16name(u8name.length(), L' ');
    for (;;) {
        auto sz = ucnv_toUChars(mConverter,
                                &u16name[0], static_cast<int32_t>(u16name.length()+1),
                                u8name.data(), static_cast<int32_t>(u8name.length()),
                                &status);
        if (U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) {
            CfdgError::Error(CfdgError::Default, "String conversion error");
        }
        if (sz <= static_cast<int32_t>(u16name.length())) {
            u16name.resize(sz);
            break;
        } else {
            u16name.resize(sz + 1, L' ');
        }
    }
    
    // NFKC normalize utf-16 text 
    std::wstring ret(u16name.length(), L' ');
    for (;;) {
        UChar* dest = reinterpret_cast<UChar*>(&ret[0]);
        auto sz = unorm2_normalize(mNormalizer,
                                   u16name.data(), static_cast<int32_t>(u16name.length()),
                                   dest, static_cast<int32_t>(ret.length()+1),
                                   &status);
        if (U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) {
            CfdgError::Error(CfdgError::Default, "String conversion error");
        }
        if (sz <= static_cast<int32_t>(ret.length())) {
            ret.resize(sz);
            break;
        } else {
            ret.resize(sz + 1, L' ');
        }
    }
    return ret;
}
#endif
