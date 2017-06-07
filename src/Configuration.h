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
#include <exception>
#include <memory>
#include <sharemind/ConfigurationInterpolation.h>
#include <sharemind/Exception.h>
#include <string>
#include <utility>
#include <vector>


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
            NonRootCopy,
            "Copying a non-root Configuration object is not currently "
            "supported!");
    SHAREMIND_DEFINE_EXCEPTION_CONST_MSG(
            Exception,
            NoValidConfigurationFileFound,
            "No valid configuration file found!");

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

public: /* Types: */

    using Iterator =
            boost::transform_iterator<
                IteratorTransformer,
                boost::property_tree::ptree::iterator>;

public: /* Methods: */

    Configuration(Configuration && move);
    Configuration(Configuration const & copy);

    Configuration(std::string const & filename);

    Configuration(std::vector<std::string> const & tryPaths);

    Configuration(std::string const & filename,
                  ConfigurationInterpolation interpolation);

    Configuration(std::vector<std::string> const & tryPaths,
                  ConfigurationInterpolation interpolation);

    virtual ~Configuration() noexcept;

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

    static std::vector<std::string> defaultSharemindToolTryPaths(
            std::string const & configName);

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
