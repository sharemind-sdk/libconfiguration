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

#include "Configuration.h"

#include <array>
#include <boost/iostreams/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/xpressive/xpressive_static.hpp>
#include <cassert>
#include <fcntl.h>
#include <sharemind/MakeUnique.h>
#include <sharemind/visibility.h>
#include <sharemind/XdgBaseDirectory.h>
#include <sstream>
#include <streambuf>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>


namespace sharemind {

namespace {

class PosixFileInputSource {

public: /* Types: */

    using char_type = char;
    using category = boost::iostreams::source_tag;

public: /* Methods: */

    PosixFileInputSource(std::string const & filename)
        : m_fd(
            std::shared_ptr<int>(
                new int(
                    [&filename]() {
                        auto const r = ::open(filename.c_str(), O_RDONLY);
                        if (r >= 0)
                            return r;
                        throw std::system_error(errno, std::system_category());
                    }()),
                [](int const * const fdPtr) noexcept { ::close(*fdPtr); }))
    {}

    std::streamsize read(char * const buffer, std::streamsize const bufferSize)
    {
        assert(bufferSize >= 0);
        if (bufferSize <= 0)
            return 0;
        using US = std::make_unsigned<std::streamsize>::type;
        std::size_t const readSize =
                (static_cast<US>(bufferSize)
                 > std::numeric_limits<std::size_t>::max())
                ? std::numeric_limits<std::size_t>::max()
                : static_cast<std::size_t>(bufferSize);
        auto const r = ::read(*m_fd, buffer, readSize);
        if (r > 0u)
            return r;
        if (r == 0u)
            return -1;
        assert(r == -1);
        throw std::system_error(errno, std::system_category());
    }

private: /* Fields: */

    std::shared_ptr<int> m_fd;

};

}

struct SHAREMIND_VISIBILITY_INTERNAL Configuration::Inner {

/* Methods: */

    Inner(std::vector<std::string> const & tryPaths,
          Interpolation interpolation_)
        : interpolation(std::move(interpolation_))
    {
        for (auto const & path : tryPaths) {
            FailedToOpenAndParseConfigurationException exception(path);
            try {
                initFromPath(path);
                return;
            } catch (std::system_error const & e) {
                if ((e.code() != std::errc::no_such_file_or_directory)
                    /* Work around libstdc++ std::system_category() not properly
                       mapping its values to std::generic_category(), i.e.
                       https://gcc.gnu.org/bugzilla/show_bug.cgi?id=60555 : */
                    && ((e.code().category() != std::system_category())
                        || (e.code().value() != ENOENT)))
                    std::throw_with_nested(std::move(exception));
            } catch (...) {
                std::throw_with_nested(std::move(exception));
            }
        }
        throw NoValidConfigurationFileFound();
    }

    Inner(std::string const & filename,
          Interpolation interpolation_)
        : interpolation(std::move(interpolation_))
    {
        FailedToOpenAndParseConfigurationException exception(filename);
        try {
            initFromPath(filename);
        } catch (...) {
            std::throw_with_nested(std::move(exception));
        }
    }

    void initFromPath(std::string const & path) {
        PosixFileInputSource inFile(path);
        boost::iostreams::stream<PosixFileInputSource> inStream(inFile);
        boost::property_tree::read_ini(inStream, ptree);
        interpolation.addVariable(
                "CurrentFileDirectory",
                boost::filesystem::canonical(
                    boost::filesystem::path(
                            path)).parent_path().string());
        filename = path;
    }

/* Fields: */

    Interpolation interpolation;
    boost::property_tree::ptree ptree;
    std::string filename;

};

Configuration::FailedToOpenAndParseConfigurationException
    ::FailedToOpenAndParseConfigurationException(
        std::string const & path)
    : m_message(
        std::make_shared<std::string>(
              "Failed to load or parse valid configuration format from file \""
              + path + "\"!"))
{}

char const * Configuration::FailedToOpenAndParseConfigurationException::what()
        const noexcept
{ return m_message->c_str(); }

Configuration::ValueNotFoundException::ValueNotFoundException(
        std::string const & path)
    : m_message(
        std::make_shared<std::string>(
                "Value \"" + path + "\" not found in configuration!"))
{}

char const * Configuration::ValueNotFoundException::what() const noexcept
{ return m_message->c_str(); }

Configuration::FailedToParseValueException::FailedToParseValueException(
        std::string const & path)
    : m_message(
        std::make_shared<std::string>(
                "Failed to parse value for \"" + path + "\"!"))
{}

char const * Configuration::FailedToParseValueException::what() const noexcept
{ return m_message->c_str(); }

Configuration::IteratorTransformer::IteratorTransformer(
        Configuration const & parent)
    : m_path(parent.m_path)
    , m_inner(parent.m_inner)
{}

Configuration Configuration::IteratorTransformer::operator()(
        boost::property_tree::ptree::value_type & value) const
{
    if (m_path) {
        assert(!m_path->empty());
        return Configuration(std::make_shared<std::string>(
                                 (*m_path) + '.' + value.first),
                             m_inner,
                             value.second);
    } else {
        return Configuration(std::make_shared<std::string>(value.first),
                             m_inner,
                             value.second);
    }
}

std::string Configuration::Interpolation::interpolate(std::string const & s)
        const
{
    namespace xp = boost::xpressive;

    /* "([^%]|^)%\{([a-zA-Z\.]+)\}" */
    static xp::sregex const re =
        xp::imbue(std::locale("POSIX"))(
            (xp::bos | ~(xp::set= '%')) >>
            xp::as_xpr('%') >> '{' >>
            (xp::s1= + xp::set[ xp::range('a', 'z') |
                                xp::range('A', 'Z') |
                                '.' ]) >>
            '}');

    auto sIt(s.cbegin());
    std::stringstream ss;

    xp::sregex_iterator const reEnd;
    for (xp::sregex_iterator reIt(s.cbegin(), s.cend(), re);
         reIt != reEnd;
         ++reIt)
    {
        auto const & match = *reIt;

        auto const it(m_map.find(match[1].str()));
        if (it == m_map.cend())
            throw UnknownVariableException();

        if (match[0].first != s.cbegin())
            ss.write(&*sIt, match[0].first + 1 - sIt);

        ss << it->second;
        sIt = match[0].second;
    }
    if (sIt != s.end())
        ss.write(&*sIt, s.end() - sIt);

    return ss.str();
}

void Configuration::Interpolation::addVariable(std::string var,
                                               std::string value)
{ m_map.emplace(std::move(var), std::move(value)); }

Configuration::Configuration(Configuration && move)
    : m_path(std::move(move.m_path))
    , m_inner(std::move(move.m_inner))
    , m_ptree(&m_inner->ptree)
{}

Configuration::Configuration(Configuration const & copy)
    : m_path(!copy.m_path ? nullptr : throw NonRootCopy())
    , m_inner(std::make_shared<Inner>(*copy.m_inner))
    , m_ptree(&m_inner->ptree)
{}

Configuration::Configuration(std::string const & filename)
    : Configuration(filename, Interpolation())
{}

Configuration::Configuration(std::vector<std::string> const & tryPaths)
    : Configuration(tryPaths, Interpolation())
{}

Configuration::Configuration(std::string const & filename,
                             Interpolation interpolation)
    : m_inner(std::make_shared<Inner>(filename, std::move(interpolation)))
    , m_ptree(&m_inner->ptree)
{}

Configuration::Configuration(std::vector<std::string> const & tryPaths,
                             Interpolation interpolation)
    : m_inner(std::make_shared<Inner>(tryPaths, std::move(interpolation)))
    , m_ptree(&m_inner->ptree)
{}

Configuration::Configuration(std::shared_ptr<std::string const> path,
                             std::shared_ptr<Inner> inner,
                             boost::property_tree::ptree & ptree)
        noexcept
    : m_path(std::move(path))
    , m_inner(std::move(inner))
    , m_ptree(&ptree)
{}

Configuration::~Configuration() noexcept {}

Configuration::Interpolation & Configuration::interpolation() noexcept
{ return m_inner->interpolation; }

Configuration::Interpolation const & Configuration::interpolation()
        const noexcept
{ return m_inner->interpolation; }

std::string const & Configuration::filename() const noexcept
{ return m_inner->filename; }

std::string Configuration::key() const {
    if (!m_path)
        return std::string();
    assert(!m_path->empty());
    auto const i(m_path->find_last_of('.'));
    if (i == std::string::npos)
        return *m_path;
    return m_path->substr(i + 1);
}

std::string const & Configuration::path() const noexcept {
    static std::string const emptyPath;
    return m_path ? *m_path : emptyPath;
}

Configuration::Iterator Configuration::begin() noexcept
{ return Iterator(m_inner->ptree.begin(), *this); }

Configuration::Iterator Configuration::end() noexcept
{ return Iterator(m_inner->ptree.end(), *this); }

void Configuration::erase(std::string const & key) noexcept
{ m_ptree->erase(key); }

std::string Configuration::interpolate(std::string const & value) const
{ return m_inner->interpolation.interpolate(value); }

std::vector<std::string> Configuration::defaultSharemindToolTryPaths(
        std::string const & configName)
{
    std::string suffix("/sharemind/" + configName + ".conf");
    std::vector<std::string> r(getXdgConfigPaths(suffix));
    r.emplace_back("/etc" + std::move(suffix));
    return r;
}

std::string Configuration::composePath(std::string const & path) const {
    if (!m_path)
        return path;
    assert(!m_path->empty());
    return (*m_path) + '.' + path;
}

} /* namespace sharemind { */
