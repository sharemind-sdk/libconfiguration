/*
 * Copyright (C) 2017 Cybernetica
 *
 * Research/Commercial License Usage
 * Licensees holding a valid Research License or Commercial License
 * for the Software may use this file according to the written
 * agreement between you and Cybernetica.
 *
 * GNU General Public License Usage
 * Alternatively, this file may be used under the terms of the GNU
 * General Public License version 3.0 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.  Please review the following information to
 * ensure the GNU General Public License version 3.0 requirements will be
 * met: http://www.gnu.org/copyleft/gpl-3.0.html.
 *
 * For further information, please contact us at sharemind@cyber.ee.
 */

#include "XdgBaseDirectory.h"

#include <cstdlib>
#include <cstring>
#include <sharemind/Split.h>
#include "HomeDirectory.h"


namespace sharemind {

namespace {

template <typename DefaultGenerator>
inline std::string getDir(char const * const envVarName,
                          DefaultGenerator defaultGenerator)
{
    auto const * const e = std::getenv(envVarName);
    if (!e || !*e)
        return defaultGenerator();
    return e;
}

template <typename DefaultGenerator>
inline std::vector<std::string> getDirs(char const * const envVarName,
                                        DefaultGenerator defaultGenerator)
{
    auto const * const e = std::getenv(envVarName);
    if (!e || !*e)
        return defaultGenerator();
    auto const * const end = e + std::strlen(e);
    std::vector<std::string> r;
    split(e,
          end,
          [](char const & c) noexcept { return c == ':'; },
          [&r](char const * const begin_, char const * const end_) noexcept
          { r.emplace_back(begin_, end_); });
    return r;
}

} /* anonymous namespace */

std::string getDefaultXdgDataHome()
{ return getHomeDirectory() + "/.local/share"; }

std::string getDefaultXdgConfigHome()
{ return getHomeDirectory() + "/.config"; }

std::vector<std::string> const & getDefaultXdgDataDirs() {
    static std::vector<std::string> const r{std::string("/usr/local/share/"),
                                            std::string("/usr/share/")};
    return r;
}

std::vector<std::string> const & getDefaultXdgConfigDirs() {
    static std::vector<std::string> const r{std::string("/etc/xdg/")};
    return r;
}

std::string getDefaultXdgCacheHome() { return getHomeDirectory() + "/.cache"; }

std::string getXdgDataHome()
{ return getDir("XDG_DATA_HOME", &getDefaultXdgDataHome); }

std::string getXdgConfigHome()
{ return getDir("XDG_CONFIG_HOME", &getDefaultXdgConfigHome); }

std::vector<std::string> getXdgDataDirs()
{ return getDirs("XDG_DATA_DIRS", &getDefaultXdgDataDirs); }

std::vector<std::string> getXdgConfigDirs()
{ return getDirs("XDG_CONFIG_DIRS", &getDefaultXdgConfigDirs); }

std::string getXdgCacheHome()
{ return getDir("XDG_CACHE_HOME", &getDefaultXdgCacheHome); }

std::vector<std::string> getXdgConfigPaths(std::string const & suffix) {
    assert(suffix.empty() || suffix[0u] == '/');
    std::vector<std::string> r;
    r.emplace_back(getXdgConfigHome() + suffix);
    for (auto & path : getXdgConfigDirs())
        r.emplace_back(std::move(path) + suffix);
    return r;
}

std::vector<std::string> getXdgDataPaths(std::string const & suffix) {
    assert(suffix.empty() || suffix[0u] == '/');
    std::vector<std::string> r;
    r.emplace_back(getXdgDataHome() + suffix);
    for (auto & path : getXdgDataDirs())
        r.emplace_back(std::move(path) + suffix);
    return r;
}

} /* namespace Sharemind { */
