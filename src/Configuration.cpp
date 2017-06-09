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
#include <cassert>
#include <fcntl.h>
#include <sharemind/MakeUnique.h>
#include <sharemind/visibility.h>
#include <sharemind/XdgBaseDirectory.h>
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
          std::shared_ptr<Interpolation> interpolation_)
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
          std::shared_ptr<Interpolation> interpolation_)
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
        interpolation->addVariable(
                "CurrentFileDirectory",
                boost::filesystem::canonical(
                    boost::filesystem::path(
                            path)).parent_path().string());
        filename = path;
    }

/* Fields: */

    std::shared_ptr<Interpolation> interpolation;
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

Configuration::Interpolation::Interpolation()
    : m_time(Configuration::getLocalTimeTm())
{}

Configuration::Interpolation::~Interpolation() noexcept {}

std::string Configuration::Interpolation::interpolate(std::string const & s)
        const
{ return interpolate(s, Configuration::getLocalTimeTm()); }

std::string Configuration::Interpolation::interpolate(std::string const & s,
                                                      ::tm const & theTime)
        const
{
    std::string r;
    r.reserve(s.size());
    char format[3] = "% ";
    char buffer[32] = "";
    for (auto it = s.cbegin(); it != s.cend(); ++it) {
        if (*it != '%') { // Handle regular characters:
            auto const start(it); // Regular characters range start
            do {
                ++it;
                if (it == s.cend()) {
                    r.insert(r.end(), start, it);
                    return r;
                }
            } while (*it != '%');
            r.insert(r.end(), start, it);
        }
        assert(*it == '%');
        // Handle escapes
        if (++it == s.cend())
            throw InterpolationSyntaxErrorException();
        switch (*it) {
        case '%': r.push_back('%'); break;
        case 'C': case 'd': case 'D': case 'e': case 'F': case 'H':
        case 'I': case 'j': case 'm': case 'M': case 'p': case 'R':
        case 'S': case 'T': case 'u': case 'U': case 'V': case 'w':
        case 'W': case 'y': case 'Y': case 'z': {
            format[1u] = *it;
            if (strftime(buffer, 32u, format, &theTime)) {
                r.append(buffer);
            } else {
                throw StrftimeException();
            }
            break;
        }
        case '{': {
            ++it;
            auto const start(it); // Start of range between curly braces
            for (;; ++it) {
                if (it == s.cend())
                    throw InterpolationSyntaxErrorException();
                switch (*it) {
                case '%': case '{':
                    throw InterpolationSyntaxErrorException();
                case '}':
                    break;
                default: // All other characters allowed between {}
                    continue;
                }
                // Do the replacement:
                auto const matchIt(m_map.find(std::string(start, it)));
                if (matchIt == m_map.cend())
                    throw UnknownVariableException();
                r.append(matchIt->second);
                break;
            }
            break;
        }
        default:
            throw InvalidInterpolationException();
        }
    }
    return r;
}

void Configuration::Interpolation::addVariable(std::string var,
                                               std::string value)
{ m_map.emplace(std::move(var), std::move(value)); }

void Configuration::Interpolation::resetTime()
{ return resetTime(Configuration::getLocalTimeTm()); }

void Configuration::Interpolation::resetTime(std::time_t theTime)
{ return resetTime(Configuration::getLocalTimeTm(theTime)); }

void Configuration::Interpolation::resetTime(::tm const & theTime)
{ m_time = theTime; }

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
    : Configuration(filename, std::make_shared<Interpolation>())
{}

Configuration::Configuration(std::vector<std::string> const & tryPaths)
    : Configuration(tryPaths, std::make_shared<Interpolation>())
{}

Configuration::Configuration(std::string const & filename,
                             std::shared_ptr<Interpolation> interpolation)
    : m_inner(std::make_shared<Inner>(filename, std::move(interpolation)))
    , m_ptree(&m_inner->ptree)
{}

Configuration::Configuration(std::vector<std::string> const & tryPaths,
                             std::shared_ptr<Interpolation> interpolation)
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

std::shared_ptr<Configuration::Interpolation> const &
Configuration::interpolation() const noexcept
{ return m_inner->interpolation; }

void Configuration::setInterpolation(std::shared_ptr<Interpolation> i) noexcept
{ m_inner->interpolation = std::move(i); }

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
{ return m_inner->interpolation->interpolate(value); }

std::string Configuration::interpolate(std::string const & value,
                                       ::tm const & theTime) const
{ return m_inner->interpolation->interpolate(value, theTime); }

std::vector<std::string> Configuration::defaultSharemindToolTryPaths(
        std::string const & configName)
{
    std::string suffix("/sharemind/" + configName + ".conf");
    std::vector<std::string> r(getXdgConfigPaths(suffix));
    r.emplace_back("/etc" + std::move(suffix));
    return r;
}

::tm Configuration::getLocalTimeTm() { return getLocalTimeTm(::time(nullptr)); }

::tm Configuration::getLocalTimeTm(std::time_t const theTime) {
    if (theTime == std::time_t(-1))
        throw TimeException();
    ::tm theTimeTm;
    if (!localtime_r(&theTime, &theTimeTm))
        throw LocalTimeException();
    return theTimeTm;
}

std::string Configuration::composePath(std::string const & path) const {
    if (!m_path)
        return path;
    assert(!m_path->empty());
    return (*m_path) + '.' + path;
}

} /* namespace sharemind { */
