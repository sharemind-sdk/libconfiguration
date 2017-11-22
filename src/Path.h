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

#ifndef SHAREMIND_LIBCONFIGURATION_PATH_H
#define SHAREMIND_LIBCONFIGURATION_PATH_H

#include <string>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <vector>


namespace sharemind {

class Path {

public: /* Types: */

    using Components = std::vector<std::string>;
    using SizeType = Components::size_type;

public: /* Methods: */

    Path();
    Path(Path &&) noexcept;
    Path(Path const &);

    Path(char const * path, char const separator = '.');
    Path(std::string const & path, char const separator = '.');

    Path & operator=(Path &&) noexcept;
    Path & operator=(Path const &);

    bool empty() const noexcept { return m_components.empty(); }

    SizeType numComponents() const noexcept { return m_components.size(); }

    Components & components() noexcept { return m_components; }
    Components const & components() const noexcept { return m_components; }

    std::string toString(char const separator = '.') const;

    Path & operator<<(std::string);

    template <typename T>
    auto operator<<(T && v)
            -> typename std::enable_if<
                    !std::is_convertible<T &&, std::string>::value,
                    Path &
               >::type
    {
        std::ostringstream oss;
        oss << std::forward<T>(v);
        m_components.emplace_back(oss.str());
        return *this;
    }

private: /* Fields: */

    Components m_components;

}; /* class Path */

} /* namespace sharemind { */


sharemind::Path operator+(sharemind::Path, std::string);
sharemind::Path operator+(sharemind::Path, sharemind::Path);

std::ostream & operator<<(std::ostream & os, sharemind::Path const & path);

#endif /* SHAREMIND_LIBCONFIGURATION_PATH_H */
