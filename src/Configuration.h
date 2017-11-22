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

#ifndef SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H
#define SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree.hpp>
#include <ctime>
#include <exception>
#include <memory>
#include <sharemind/Exception.h>
#include <sharemind/TemplateFirstType.h>
#include <sharemind/TemplateContainsType.h>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <unordered_map>
#include "Path.h"


namespace sharemind {

class Configuration {

private: /* Types: */

    struct Inner;

    template <typename T>
    using Translator =
        typename boost::property_tree::translator_between<std::string, T>::type;

    class IteratorTransformer {

    public: /* Methods: */

        IteratorTransformer(Configuration const & parent);

        Configuration operator()(
                boost::property_tree::ptree::value_type & value) const;

        Configuration const operator()(
                boost::property_tree::ptree::value_type const & value) const;

    private: /* Fields: */

        std::shared_ptr<Path const> const m_path;
        std::shared_ptr<Inner> const m_inner;

    };
    friend class IteratorTransformer;

public: /* Types: */

    using SizeType = boost::property_tree::ptree::size_type;

    SHAREMIND_DEFINE_EXCEPTION(sharemind::Exception, Exception);
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            Exception,
            NonRootCopyException,
            "Copying a non-root Configuration object is not currently "
            "supported!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            Exception,
            NoValidConfigurationFileFound,
            "No valid configuration file found!");
    SHAREMIND_DEFINE_EXCEPTION(Exception, InterpolationException);
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            InterpolationException,
            UnknownVariableException,
            "Unknown configuration interpolation variable!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            InterpolationException,
            InterpolationSyntaxErrorException,
            "Interpolation syntax error!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            InterpolationException,
            InvalidInterpolationException,
            "Invalid interpolation given!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            InterpolationException,
            TimeException,
            "time() failed!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            InterpolationException,
            LocalTimeException,
            "localtime_r() failed!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            InterpolationException,
            StrftimeException,
            "strftime() failed!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            Exception,
            FailedToOpenAndParseConfigurationException,
            "Failed to load or parse a valid configuration!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            Exception,
            PathNotFoundException,
            "Path not found in configuration!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            Exception,
            FailedToParseValueException,
            "Failed to parse value in configuration");

    using Iterator =
            boost::transform_iterator<
                IteratorTransformer,
                boost::property_tree::ptree::iterator>;

    using ConstIterator =
            boost::transform_iterator<
                IteratorTransformer,
                boost::property_tree::ptree::const_iterator>;

    class Interpolation {

    public: /* Methods: */

        Interpolation();
        virtual ~Interpolation() noexcept;

        std::string interpolate(std::string const & s) const;
        std::string interpolate(std::string const & s,
                                ::tm const & theTime) const;

        void addVariable(std::string var, std::string value);

        void resetTime();
        void resetTime(std::time_t theTime);
        void resetTime(::tm const & theTime);

    private: /* Fields: */

        std::unordered_map<std::string, std::string> m_map;
        ::tm m_time;

    }; /* class Interpolation */

public: /* Methods: */

    Configuration(Configuration && move);
    Configuration(Configuration const & copy);

    Configuration(std::string const & filename);

    Configuration(std::vector<std::string> const & tryPaths);

    Configuration(std::string const & filename,
                  std::shared_ptr<Interpolation> interpolation);

    Configuration(std::vector<std::string> const & tryPaths,
                  std::shared_ptr<Interpolation> interpolation);

    virtual ~Configuration() noexcept;

    std::shared_ptr<Interpolation> const & interpolation() const noexcept;
    void setInterpolation(std::shared_ptr<Interpolation> i) noexcept;

    void loadInterpolationOverridesFromSection(
            std::string const & sectionName = "Interpolations");

    std::string const & filename() const noexcept;

    std::string const & key() const noexcept;

    Path const & path() const noexcept;

    bool empty() const noexcept { return m_ptree->empty(); }
    SizeType size() const noexcept { return m_ptree->size(); }

    Iterator begin() noexcept;
    ConstIterator begin() const noexcept;
    ConstIterator cbegin() const noexcept;

    Iterator end() noexcept;
    ConstIterator end() const noexcept;
    ConstIterator cend() const noexcept;

    template <typename T>
    T value() const { return parseValue<T>(*m_ptree); }

    template <typename T>
    T get(Path const & path_) const { return parseValue<T>(findChild(path_)); }

    template <typename T, typename Default>
    typename std::enable_if<std::is_same<typename std::remove_cv<T>::type,
                                         std::size_t>::value,
                            T>::type
    get(Path const & path, Default && defaultValue) const
    { return getSizeValue(path, std::forward<Default>(defaultValue)); }

    template <typename T, typename ... Args>
    typename std::enable_if<
            std::is_same<T, std::string>::value
            && (sizeof...(Args) == 1u)
            && (TemplateContainsType<
                        typename std::decay<TemplateFirstType<Args...> >::type,
                        char const *,
                        char *,
                        std::string>::value),
            T>::type
    get(Path const & path, Args && ... defaultValueArgs) const
    { return getStringValue(path, std::forward<Args>(defaultValueArgs)...); }

    template <typename T, typename ... Args>
    typename std::enable_if<
            (sizeof...(Args) > 0u)
            && !std::is_same<typename std::remove_cv<T>::type,
                             std::size_t>::value
            && !(std::is_same<T, std::string>::value
                 && (sizeof...(Args) == 1u)
                 && (TemplateContainsType<
                         typename std::decay<TemplateFirstType<Args...> >::type,
                         char const *,
                         char *,
                         std::string>::value)),
            T>::type
    get(Path const & path_, Args && ... defaultValueArgs) const {
        boost::property_tree::ptree const * child = m_ptree;
        try {
            child = &findChild(path_);
        } catch (...) {
            return T(std::forward<Args>(defaultValueArgs)...);
        }
        return parseValue<T>(*child);
    }

    void erase(Path const & path) noexcept;

    std::string interpolate(std::string const & value) const;
    std::string interpolate(std::string const & value,
                            ::tm const & theTime) const;

    static std::vector<std::string> defaultSharemindToolTryPaths(
            std::string const & configName);

    static ::tm getLocalTimeTm();
    static ::tm getLocalTimeTm(std::time_t theTime);

private: /* Methods: */

    Configuration(std::shared_ptr<Path const> path,
                  std::shared_ptr<Inner> inner,
                  boost::property_tree::ptree & ptree) noexcept;

    std::size_t getSizeValue(Path const &, std::size_t) const;
    std::string getStringValue(Path const &, char const *) const;
    std::string getStringValue(Path const &, std::string const &) const;
    std::string getStringValue(Path const &, std::string && v) const;

    boost::property_tree::ptree const & findChild(Path const & path_) const;

    template <typename T>
    T parseValue(boost::property_tree::ptree const & ptree) const {
        try {
            if (auto const optionalValue =
                        Translator<T>().get_value(interpolate(ptree.data())))
                return *optionalValue;
            throw FailedToParseValueException();
        } catch (...) {
            std::throw_with_nested(FailedToParseValueException());
        }
    }
private: /* Fields: */

    std::shared_ptr<Path const> m_path;
    std::shared_ptr<Inner> m_inner;
    boost::property_tree::ptree * m_ptree;

};

extern template std::string Configuration::value<std::string>() const;
extern template std::size_t Configuration::value<std::size_t>() const;

extern template std::string Configuration::get<std::string>(
        Path const &) const;
extern template std::size_t Configuration::get<std::size_t>(
        Path const &) const;

extern template std::string Configuration::parseValue<std::string>(
        boost::property_tree::ptree const &) const;
extern template std::size_t Configuration::parseValue<std::size_t>(
        boost::property_tree::ptree const &) const;

} /* namespace sharemind { */

#endif /* SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H */
