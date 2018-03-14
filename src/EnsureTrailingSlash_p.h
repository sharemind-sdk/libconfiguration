/*
 * Copyright (C) 2015 Cybernetica
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

#ifndef SHAREMIND_ENSURETRAILINGSLASH_P_H
#define SHAREMIND_ENSURETRAILINGSLASH_P_H

#include <cassert>
#include <limits>
#include <new>
#include <string>


namespace sharemind {

inline std::string ensureTrailingSlash(char const * const str)
        __attribute__((visibility("internal")));
inline std::string ensureTrailingSlash(char const * const str) {
    assert(str);
    assert(*str);
    std::size_t size = 1u;
    for (auto end = str + 1u; *end; ++end)
        if (!++size)
            throw std::bad_alloc();
    if (str[size - 1u] == '/')
        return std::string(str, size);

    auto const sizeIncludingSlash = size + 1u;
    if (!sizeIncludingSlash)
        throw std::bad_alloc();

    std::string s;
    s.reserve(sizeIncludingSlash);
    s.append(str, size).push_back('/');
    return s;
}

inline std::string ensureTrailingSlash(char const * const begin,
                                       char const * const end)
        __attribute__((visibility("internal")));
inline std::string ensureTrailingSlash(char const * const begin,
                                       char const * const end)
{
    assert(begin);
    assert(end);
    assert(begin < end);
    assert(*begin);

    if (*(end - 1u) == '/')
        return std::string(begin, end);

    static_assert(std::numeric_limits<std::size_t>::max()
                  >= std::numeric_limits<decltype(end - begin)>::max(), "");
    auto const sizeIncludingSlash = static_cast<std::size_t>(end - begin) + 1u;
    if (!sizeIncludingSlash)
        throw std::bad_alloc();

    std::string s;
    s.reserve(sizeIncludingSlash);
    s.append(begin, end).push_back('/');
    return s;
}

} /* namespace Sharemind { */

#endif /* SHAREMIND_ENSURETRAILINGSLASH_P_H */
