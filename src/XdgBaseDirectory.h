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

#ifndef SHAREMIND_XDGBASEDIRECTORY_H
#define SHAREMIND_XDGBASEDIRECTORY_H

/**
  \file Confirming to XDG Base Directory Specification version 0.6,
        https://specifications.freedesktop.org/basedir-spec/basedir-spec-0.6.html
*/

#include <string>
#include <vector>


namespace sharemind {

std::string getDefaultXdgDataHome();
std::string getDefaultXdgConfigHome();
std::vector<std::string> const & getDefaultXdgDataDirs();
std::vector<std::string> const & getDefaultXdgConfigDirs();
std::string getDefaultXdgCacheHome();

/** \returns $XDG_DATA_HOME */
std::string getXdgDataHome();

/** \returns $XDG_CONFIG_HOME */
std::string getXdgConfigHome();

/** \returns $XDG_DATA_DIRS */
std::vector<std::string> getXdgDataDirs();

/** \returns $XDG_CONFIG_DIRS */
std::vector<std::string> getXdgConfigDirs();

/** \returns $XDG_CACHE_HOME */
std::string getXdgCacheHome();

/**
    \brief Shorthand to retrieve all configuration paths combined with the given
           suffix.
    \arg[in] suffix Suffix to append to each path. Should be either empty or
                    begin with a directory separator.
    \returns a vector of configuration paths containing the value of
             getXdgConfigHome() and all values from getXdgConfigDirs() in order,
             with the given suffix appended to each element.
*/
std::vector<std::string> getXdgConfigPaths(
        std::string const & suffix = std::string());

/**
    \brief Shorthand to retrieve all data paths combined with the given suffix.
    \arg[in] suffix Suffix to append to each path. Should be either empty or
                    begin with a directory separator.
    \returns a vector of data paths containing the value of getXdgDataHome() and
             all values from getXdgDataDirs() in order, with the given suffix
             appended to each element.
*/
std::vector<std::string> getXdgDataPaths(
        std::string const & suffix = std::string());

} /* namespace Sharemind { */

#endif /* SHAREMIND_XDGBASEDIRECTORY_H */
