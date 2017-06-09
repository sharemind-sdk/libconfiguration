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
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>


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

    private: /* Fields: */

        std::shared_ptr<std::string const> const m_path;
        std::shared_ptr<Inner> const m_inner;

    };
    friend class IteratorTransformer;

public: /* Types: */

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

    class FailedToOpenAndParseConfigurationException: public Exception {

    public: /* Methods: */

        FailedToOpenAndParseConfigurationException(std::string const & path);

        char const * what() const noexcept override;

    private: /* Fields: */

        std::shared_ptr<std::string> m_message;

    };

    class ValueNotFoundException: public Exception {

    public: /* Methods: */

        ValueNotFoundException(std::string const & path);

        char const * what() const noexcept override;

    private: /* Fields: */

        std::shared_ptr<std::string> m_message;

    };

    class FailedToParseValueException: public Exception {

    public: /* Methods: */

        FailedToParseValueException(std::string const & path);

        char const * what() const noexcept override;

    private: /* Fields: */

        std::shared_ptr<std::string> m_message;

    };

    using Iterator =
            boost::transform_iterator<
                IteratorTransformer,
                boost::property_tree::ptree::iterator>;

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

    std::string const & filename() const noexcept;

    std::string key() const;

    std::string const & path() const noexcept;

    Iterator begin() noexcept;

    Iterator end() noexcept;

    template <typename T>
    T value() const { return parseValue<T>(*m_ptree, path()); }

    template <typename T>
    T get(std::string const & path) const {
        auto const fullPath(composePath(path));
        decltype(std::addressof(m_ptree->get_child(path))) child;
        {
            ValueNotFoundException notFoundException(fullPath);
            try {
                child = std::addressof(m_ptree->get_child(path));
            } catch (...) {
                std::throw_with_nested(std::move(notFoundException));
            }
        }
        return parseValue<T>(*child, fullPath);
    }

    template <typename T, typename ... Args>
    T get(std::string const & path, Args && ... defaultValueArgs) const {
        decltype(std::addressof(m_ptree->get_child(path))) child;
        try {
            child = std::addressof(m_ptree->get_child(path));
        } catch (...) {
            return T(std::forward<Args>(defaultValueArgs)...);
        }
        return parseValue<T>(*child, composePath(path));
    }

    void erase(std::string const & key) noexcept;

    std::string interpolate(std::string const & value) const;
    std::string interpolate(std::string const & value,
                            ::tm const & theTime) const;

    static std::vector<std::string> defaultSharemindToolTryPaths(
            std::string const & configName);

    static ::tm getLocalTimeTm();
    static ::tm getLocalTimeTm(std::time_t theTime);

private: /* Methods: */

    Configuration(std::shared_ptr<std::string const> path,
                  std::shared_ptr<Inner> inner,
                  boost::property_tree::ptree & ptree) noexcept;

    std::string composePath(std::string const & path) const;

    template <typename T>
    T parseValue(boost::property_tree::ptree const & ptree,
                 std::string const & fullPath) const
    {
        FailedToParseValueException parseException(fullPath);
        try {
            if (auto const optionalValue =
                        Translator<T>().get_value(interpolate(ptree.data())))
                return *optionalValue;
            throw std::move(parseException);
        } catch (...) {
            std::throw_with_nested(std::move(parseException));
        }
    }

private: /* Fields: */

    std::shared_ptr<std::string const> m_path;
    std::shared_ptr<Inner> m_inner;
    boost::property_tree::ptree * m_ptree;

};

} /* namespace sharemind { */

#endif /* SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H */
