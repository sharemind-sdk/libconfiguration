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
#include <cstdint>
#include <exception>
#include <memory>
#include <sharemind/comma.h>
#include <sharemind/Exception.h>
#include <sharemind/ExceptionMacros.h>
#include <sharemind/StringView.h>
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

    using ptree =
            boost::property_tree::basic_ptree<
                std::string,
                std::shared_ptr<void>
            >;

    struct Inner;

    #define SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DECLARE(C,c) \
        class C ## IteratorTransformer { \
        public: /* Types: */ \
            using result_type = Configuration c; \
        public: /* Methods: */ \
            C ## IteratorTransformer(C ## IteratorTransformer &&) noexcept; \
            C ## IteratorTransformer(C ## IteratorTransformer const &) \
                    noexcept; \
            C ## IteratorTransformer(Configuration const & parent); \
            ~C ## IteratorTransformer() noexcept; \
            C ## IteratorTransformer & operator=(C ## IteratorTransformer &&) \
                    noexcept; \
            C ## IteratorTransformer & operator=( \
                    C ## IteratorTransformer const &) noexcept; \
            Configuration c operator()(ptree::value_type c & value) const; \
        private: /* Fields: */ \
            std::shared_ptr<Path const> m_path; \
            std::shared_ptr<Inner> m_inner; \
        }; \
        friend class C ## IteratorTransformer;
    SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DECLARE(,)
    SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DECLARE(Const,const)
    #undef SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DECLARE

public: /* Types: */

    template <typename T>
    static constexpr bool const isReadableValueType =
            std::is_same<T, std::string>::value
            || std::is_same<T, std::int8_t>::value
            || std::is_same<T, std::int16_t>::value
            || std::is_same<T, std::int32_t>::value
            || std::is_same<T, std::int64_t>::value
            || std::is_same<T, std::uint8_t>::value
            || std::is_same<T, std::uint16_t>::value
            || std::is_same<T, std::uint32_t>::value
            || std::is_same<T, std::uint64_t>::value
            || std::is_same<T, float>::value
            || std::is_same<T, double>::value
            || std::is_same<T, long double>::value;

    template <typename T>
    using DefaultValueType =
        typename std::conditional<
            std::is_same<T, std::string>::value,
            StringView,
            T
        >::type;

    using SizeType = ptree::size_type;

    SHAREMIND_DECLARE_EXCEPTION_NOINLINE(sharemind::Exception, Exception);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                   NonRootCopyException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                   NoTryPathsGivenException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_STDSTRING_NOINLINE(
            Exception,
            NoValidConfigurationFileFound);
    SHAREMIND_DECLARE_EXCEPTION_CONST_STDSTRING_NOINLINE(
            Exception,
            InterpolationException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_STDSTRING_NOINLINE(
            Exception,
            FailedToOpenAndParseConfigurationException);
    SHAREMIND_DECLARE_EXCEPTION_NOINLINE(Exception, NotFoundException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(NotFoundException,
                                                   ValueNotFoundException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(NotFoundException,
                                                   SectionNotFoundException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                   FailedToParseValueException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception, GlobException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                   IncludeLoopException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_STDSTRING_NOINLINE(Exception,
                                                         FileOpenException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_STDSTRING_NOINLINE(Exception,
                                                         ParseException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                   FileReadException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(
            Exception,
            InvalidSyntaxException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_STDSTRING_NOINLINE(
            Exception,
            DuplicateSectionNameException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_STDSTRING_NOINLINE(
            Exception,
            DuplicateKeyException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(
            Exception,
            UnknownDirectiveException);
    SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(
            Exception,
            IncludeDirectiveMissingArgumentException);

    using Iterator =
            boost::transform_iterator<IteratorTransformer, ptree::iterator>;

    using ConstIterator =
            boost::transform_iterator<ConstIteratorTransformer,
                                      ptree::const_iterator>;

    class Interpolation {

    public: /* Types: */

        SHAREMIND_DECLARE_EXCEPTION_NOINLINE(sharemind::Exception, Exception);
        SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(
                Exception,
                UnknownVariableException);
        SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(
                Exception,
                InterpolationSyntaxErrorException);
        SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(
                Exception,
                InvalidInterpolationException);
        SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                       TimeException);
        SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                       LocalTimeException);
        SHAREMIND_DECLARE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                                       StrftimeException);

    public: /* Methods: */

        Interpolation();
        virtual ~Interpolation() noexcept;

        std::string interpolate(StringView s) const;
        std::string interpolate(StringView s, ::tm const & theTime) const;

        void addVariable(std::string var, std::string value);

        void resetTime();
        void resetTime(std::time_t theTime);
        void resetTime(::tm const & theTime);

        static ::tm getLocalTimeTm();
        static ::tm getLocalTimeTm(std::time_t theTime);

    private: /* Fields: */

        std::unordered_map<std::string, std::string> m_map;
        ::tm m_time;

    }; /* class Interpolation */

public: /* Methods: */

    Configuration(Configuration && move) noexcept;
    Configuration(Configuration const & copy);

    Configuration(StringView filename);

    Configuration(std::vector<std::string> const & tryPaths);

    Configuration(StringView filename,
                  std::shared_ptr<Interpolation> interpolation);

    Configuration(std::vector<std::string> const & tryPaths,
                  std::shared_ptr<Interpolation> interpolation);

    virtual ~Configuration() noexcept;

    Configuration & operator=(Configuration && move) noexcept;
    Configuration & operator=(Configuration const & copy);

    std::shared_ptr<Interpolation> const & interpolation() const noexcept;
    void setInterpolation(std::shared_ptr<Interpolation> i) noexcept;

    /** \returns the path of the file from which the root of the configuration
                 was loaded from. */
    std::string const & filename() const noexcept;

    std::string const & key() const noexcept;

    Path const & path() const noexcept;

    bool empty() const noexcept;
    SizeType size() const noexcept;

    Iterator begin() noexcept;
    ConstIterator begin() const noexcept;
    ConstIterator cbegin() const noexcept;

    Iterator end() noexcept;
    ConstIterator end() const noexcept;
    ConstIterator cend() const noexcept;

    bool hasValue() const;
    bool hasValue(Path const & path) const;
    bool hasSection() const;
    bool hasSection(Path const & path) const;

    template <typename T>
    auto value() const
            -> typename std::enable_if<isReadableValueType<T>, T>::type;

    template <typename T>
    auto get(Path const & path_) const
            -> typename std::enable_if<isReadableValueType<T>, T>::type;

    template <typename T>
    auto get(Path const & path_, DefaultValueType<T> defaultValue) const
            -> typename std::enable_if<isReadableValueType<T>, T>::type;

    Configuration section(Path const & path) const;

    void erase(Path const & path) noexcept;

    std::string interpolate(StringView value) const;
    std::string interpolate(StringView value, ::tm const & theTime) const;

    static std::vector<std::string> defaultSharemindToolTryPaths(
            std::string const & configName);

private: /* Methods: */

    Configuration(std::shared_ptr<Path const> path,
                  std::shared_ptr<Inner> inner,
                  ptree & ptree) noexcept;

private: /* Fields: */

    std::shared_ptr<Path const> m_path;
    std::shared_ptr<Inner> m_inner;
    ptree * m_ptree;

};

#define SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(T) \
    extern template T Configuration::value<T>() const; \
    extern template T Configuration::get<T>(Path const &) const; \
    extern template T Configuration::get<T>(Path const &, DefaultValueType<T>) const
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::string);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::int8_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::int16_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::int32_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::int64_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::uint8_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::uint16_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::uint32_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(std::uint64_t);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(float);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(double);
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_(long double);
#undef SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H_

} /* namespace sharemind { */

#endif /* SHAREMIND_LIBCONFIGURATION_CONFIGURATION_H */
