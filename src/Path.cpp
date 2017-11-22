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

#include "Path.h"

#include <algorithm>
#include <cassert>
#include <new>
#include <limits>
#include <utility>


namespace sharemind {

Path::Path() {}

Path::Path(Path &&) noexcept = default;
Path::Path(Path const &) = default;

Path & Path::operator=(Path &&) noexcept = default;
Path & Path::operator=(Path const &) = default;

Path::Path(char const * start, char const separator) {
    assert(start);
    for (;;) {
        /* We ignore empty path components (e.g. between consecutive separators).
           First advance start pointer to the first non-separator character: */
        while (*start == separator)
            ++start;

        if (*start == '\0')
            break; // End of string, nothing to add

        // Advance end pointer to the first end-of-string or separator:
        auto end(start);
        do {
            ++end;
            if (*end == '\0') {
                // End of string found. Add last component and stop.
                m_components.emplace_back(start, end);
                return;
            }
        } while (*end != separator);

        // Add component between separators:
        m_components.emplace_back(start, end);

        // Restart algorithm to start searching from beyond the last separator:
        start = ++end;
    }
}

Path::Path(std::string const & path, char const separator)
    : Path(path.c_str(), separator)
{}

std::string Path::toString(char const separator) const {
    static_assert(std::is_same<decltype(m_components)::size_type,
                               std::string::size_type>::value, "");
    std::string::size_type const numElems = m_components.size();
    if (numElems <= 0u)
        return std::string();
    if (numElems == 1u) {
        assert(!m_components[0u].empty());
        return m_components[0u];
    }
    auto sizeToReserve(numElems - 1u);
    for (auto const & element : m_components) {
        assert(!element.empty());
        if (std::numeric_limits<decltype(numElems)>::max() - numElems < element.size())
            throw std::bad_alloc();
        sizeToReserve += element.size();
    }
    std::string r;
    r.reserve(sizeToReserve);
    r.append(m_components[0u]);
    for (auto i = 1u; i < numElems; ++i) {
        r.push_back(separator);
        r.append(m_components[i]);
    }
    return r;
}

Path & Path::operator<<(std::string str) {
    m_components.emplace_back(std::move(str));
    return *this;
}

} /* namespace sharemind { */

sharemind::Path operator+(sharemind::Path path, std::string newComponent) {
    path.components().emplace_back(std::move(newComponent));
    return path;
}

sharemind::Path operator+(sharemind::Path lhs, sharemind::Path rhs) {
    auto & components = lhs.components();
    for (auto & component : rhs.components())
        components.emplace_back(std::move(component));
    return lhs;
}

std::ostream & operator<<(std::ostream & os, sharemind::Path const & path)
{ return os << path.toString(); }
