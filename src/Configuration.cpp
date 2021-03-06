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

#include <algorithm>
#include <array>
#include <boost/iostreams/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <cassert>
#include <fcntl.h>
#include <glob.h>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <sharemind/AssertReturn.h>
#include <sharemind/Concat.h>
#include <sharemind/Optional.h>
#include <sharemind/ReversedRange.h>
#include <sharemind/StrongType.h>
#include <sharemind/visibility.h>
#include <streambuf>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include "XdgBaseDirectory.h"


namespace sharemind {

using namespace StringViewLiterals;

namespace {

using LineNumber =
        StrongType<
            std::size_t,
            struct LineNumberTag,
            StrongTypePreIncrementable,
            StrongTypeStreamable
        >;

struct ConfigurationFileContextInfo {
    ConfigurationFileContextInfo(
            std::shared_ptr<boost::filesystem::path const> filename,
            LineNumber lineNumber)
        : m_filename(std::move(filename))
        , m_lineNumber(std::move(lineNumber))
    {}

    std::shared_ptr<boost::filesystem::path const> const m_filename;
    LineNumber const m_lineNumber;
};

struct ValueItem {

    ValueItem(std::string value,
              std::shared_ptr<boost::filesystem::path const> filename,
              LineNumber lineNumber)
            noexcept(std::is_nothrow_move_constructible<std::string>::value
                     && std::is_nothrow_move_constructible<
                            std::shared_ptr<std::string> >::value
                     && std::is_nothrow_move_constructible<LineNumber>::value)
        : m_value(std::move(value))
        , m_context(std::move(filename), std::move(lineNumber))
    {}

    ValueItem(ValueItem &&) = delete;
    ValueItem(ValueItem const &) = delete;

    ValueItem & operator=(ValueItem &&) = delete;
    ValueItem & operator=(ValueItem const &) = delete;

    std::string interpolated(Configuration::Interpolation const & interpolation)
            const
    {
        try {
            return interpolation.interpolate(m_value);
        } catch (...) {
            std::throw_with_nested(
                    Configuration::InterpolationException(
                        concat("Failed to interpolate configuration value from "
                               "file \"", m_context.m_filename->string(),
                               "\" line ", m_context.m_lineNumber)));
        }
    }

    std::string const m_value;
    ConfigurationFileContextInfo const m_context;
};

class TreeItem {

public: /* Methods: */

    TreeItem() noexcept = default;

    TreeItem(TreeItem &&) = delete;
    TreeItem(TreeItem const &) = delete;

    TreeItem & operator=(TreeItem &&) = delete;
    TreeItem & operator=(TreeItem const &) = delete;

    bool hasValueItem() const noexcept { return m_valueItem.hasValue(); }
    bool hasSectionItem() const noexcept { return m_haveSectionItem; }

    ValueItem const & valueItem() const noexcept { return *m_valueItem; }

    template <typename ... Args>
    void initializeValueItem(Args && ... args) {
        assert(!m_valueItem.hasValue());
        m_valueItem.emplace(std::forward<Args>(args)...);
    }

    void initializeSectionItem() {
        assert(!m_haveSectionItem);
        m_haveSectionItem = true;
    }

    void eraseValueItem() noexcept { m_valueItem.reset(); }
    void eraseSectionItem() noexcept { m_haveSectionItem = false; }

private: /* Fields: */

    Optional<ValueItem> m_valueItem;
    bool m_haveSectionItem;

};

inline TreeItem & getTreeItem(std::shared_ptr<void> const & ptr) noexcept
{ return *static_cast<TreeItem *>(assertReturn(ptr).get()); }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-template"
#endif
template <typename T>
constexpr inline auto capMaxToSizeT(T v)
        -> typename std::enable_if<
                (std::numeric_limits<T>::max()
                 <= std::numeric_limits<std::size_t>::max()),
                std::size_t
            >::type
{ return static_cast<std::size_t>(v); }

template <typename T>
constexpr inline auto capMaxToSizeT(T v)
        -> typename std::enable_if<
                (std::numeric_limits<T>::max()
                 > std::numeric_limits<std::size_t>::max()),
                std::size_t
            >::type
{
    return (v > std::numeric_limits<std::size_t>::max())
           ? std::numeric_limits<std::size_t>::max()
           : static_cast<std::size_t>(v);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

struct FileId {
    decltype(::stat::st_dev) deviceId;
    decltype(::stat::st_ino) inode;
};

inline constexpr bool operator<(FileId const & lhs, FileId const & rhs) noexcept
{
    if (lhs.deviceId < rhs.deviceId)
        return true;
    if (rhs.deviceId < lhs.deviceId)
        return false;
    return lhs.inode < rhs.inode;
};

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
                [](int const * const fdPtr) noexcept {
                    ::close(*fdPtr);
                    delete fdPtr;
                }))
    {}

    std::streamsize read(char * const buffer, std::streamsize const bufferSize)
    {
        assert(bufferSize >= 0);
        if (bufferSize <= 0)
            return 0;
        using US = std::make_unsigned<std::streamsize>::type;
        std::size_t const readSize = capMaxToSizeT(static_cast<US>(bufferSize));
        auto const r = ::read(*m_fd, buffer, readSize);
        if (r > 0u)
            return r;
        if (r == 0u)
            return -1;
        assert(r == -1);
        throw std::system_error(errno, std::system_category());
    }

    FileId fileId() const {
        struct ::stat fileStat;
        if (auto const r = ::fstat(*m_fd, &fileStat))
            throw std::system_error(errno, std::system_category());
        FileId r;
        r.deviceId = fileStat.st_dev;
        r.inode = fileStat.st_ino;
        return r;
    }

private: /* Fields: */

    std::shared_ptr<int> m_fd;

};

template <typename Ptree>
struct TopLevelParseState;

struct FileParseJob {
    struct ParseState {
        ParseState(boost::filesystem::path const & path)
            : m_inFile(path.string())
        {}

        ParseState(ParseState &&) = delete;
        ParseState(ParseState const &) = delete;

        ParseState & operator=(ParseState &&) = delete;
        ParseState & operator=(ParseState const &) = delete;

        template <typename Ptree>
        std::string parseFile(TopLevelParseState<Ptree> & tls,
                              FileParseJob const & fpj);

        PosixFileInputSource m_inFile;
        boost::iostreams::stream<PosixFileInputSource> m_inStream{m_inFile};
        LineNumber m_lineNumber{1u};
    };

    FileParseJob(boost::filesystem::path const & path)
        : m_canonicalPath(
              std::make_shared<boost::filesystem::path>(
                  boost::filesystem::canonical(path)))
    {}

    FileParseJob(FileParseJob &&) = delete;
    FileParseJob(FileParseJob const &) = delete;

    FileParseJob & operator=(FileParseJob &&) = delete;
    FileParseJob & operator=(FileParseJob const &) = delete;

    template <typename Ptree>
    std::string parseFile(TopLevelParseState<Ptree> & tls);

    std::string prepareValue(StringView s) const;

    // Helper to escape currentFileDirectory in a lazy fashion:
    std::string const & getEscapedCurrentFileDirectory() const {
        if (m_escapedCurrentFileDirectory.hasValue())
            return *m_escapedCurrentFileDirectory;
        auto cfd(m_canonicalPath->parent_path().string());
        auto it(std::find(cfd.cbegin(), cfd.cend(), '%'));
        if (it == cfd.cend())
            return m_escapedCurrentFileDirectory.emplace(std::move(cfd));

        // Escape % signs and setup diversion:
        auto & c = m_escapedCurrentFileDirectory.emplace();
        c.reserve(cfd.size() + 1u);
        for (auto startIt = cfd.cbegin();;) {
             c.append(startIt, ++it);
             c.push_back('%');
             startIt = it;
             it = std::find(startIt, cfd.cend(), '%');
             if (it == cfd.cend()) {
                 c.append(startIt, it);
                 return c;
             }
        }
    }

    std::shared_ptr<boost::filesystem::path const> const m_canonicalPath;
    mutable Optional<std::string> m_escapedCurrentFileDirectory;
    std::unique_ptr<FileParseJob> m_prev;
    Optional<ParseState> m_state;
};


std::string FileParseJob::prepareValue(StringView s) const {
    using I = Configuration::Interpolation;
    std::string r;
    r.reserve(s.size());
    for (auto escapePos = s.findFirstOf('%');
         escapePos < s.size();
         escapePos = s.findFirstOf('%', escapePos))
    {
        if (escapePos == s.size() - 1u)
            throw I::InterpolationSyntaxErrorException();
        switch (s[escapePos + 1u]) {
        case '%':
        case 'C': case 'd': case 'D': case 'e': case 'F': case 'H':
        case 'I': case 'j': case 'm': case 'M': case 'p': case 'R':
        case 'S': case 'T': case 'u': case 'U': case 'V': case 'w':
        case 'W': case 'y': case 'Y': case 'z': {
            escapePos += 2u;
            break;
        }
        case '{': {
            auto const escapeStartPos = escapePos + 2u;
            auto const escapeEndPos =
                    s.findFirstOf("{%}"_sv, escapeStartPos + 2u);
            if ((escapeEndPos == StringView::npos) || (s[escapeEndPos] != '}'))
                throw I::InterpolationSyntaxErrorException();
            // Do the replacement:
            if (s.substr(escapeStartPos, escapeEndPos - escapeStartPos)
                == "CurrentFileDirectory"_sv)
            {
                r.append(s.data(), escapePos) // Everything before '%'
                 .append(getEscapedCurrentFileDirectory());
                s.removePrefix(escapeEndPos + 1u);
                escapePos = 0u;
            } else {
                escapePos = escapeEndPos + 1u;
            }
            break;
        }
        default:
            throw I::InvalidInterpolationException();
        }
    }
    return r.append(s.data(), s.size());
}

template <typename Ptree>
struct TopLevelParseState {

    TopLevelParseState(Ptree & ptree)
        : m_result(ptree)
    {}

    void pushJob(boost::filesystem::path const & boostPath) {
        auto fileParseJob(std::make_unique<FileParseJob>(boostPath));
        fileParseJob->m_prev = std::move(m_fileParseJob);
        m_fileParseJob = std::move(fileParseJob);
    }

    void popJob() noexcept {
        assert(m_fileParseJob);
        m_fileParseJob =
                decltype(m_fileParseJob)(std::move(m_fileParseJob->m_prev));
    }

    Ptree & m_result;
    Ptree * m_currentSection = nullptr;
    std::unique_ptr<FileParseJob> m_fileParseJob;
    std::set<FileId> m_visitedFiles;
};

template <typename Ptree>
std::string FileParseJob::ParseState::parseFile(TopLevelParseState<Ptree> & tls,
                                                FileParseJob const & fpj)
{
    constexpr static auto const whitespace = " \t\n\r"_sv;
    std::string line;
    std::string currentSectionName;
    for (; m_inStream.good(); ++m_lineNumber) {
        std::getline(m_inStream, line);
        if (!m_inStream.good() && !m_inStream.eof())
            throw Configuration::FileReadException();

        // Ignore empty lines and comments:
        if (line.empty() || line.front() == ';')
            continue;

        if (line.front() == '@') {
            StringView const lv(line);
            auto const whitespacePos(lv.findFirstOf(whitespace, 1u));
            auto const directive(lv.substr(1u, whitespacePos - 1u));
            if (directive.empty())
                throw Configuration::InvalidSyntaxException();
            if (directive != "include"_sv)
                throw Configuration::UnknownDirectiveException();
            if (whitespacePos == StringView::npos)
                throw Configuration::IncludeDirectiveMissingArgumentException();
            auto arg(lv.substr(whitespacePos + 1u).trimmed(whitespace));
            if (arg.empty())
                throw Configuration::IncludeDirectiveMissingArgumentException();
            return fpj.prepareValue(std::move(arg));
        }

        StringView lv(StringView(line).leftTrimmed(whitespace));
        if (lv.front() == '[') { // Parse section headers:
            auto const end(lv.find(']', 1u));
            if ((end == StringView::npos)
                || lv.findFirstNotOf(whitespace, end + 1u) != StringView::npos)
                throw Configuration::InvalidSyntaxException();
            currentSectionName =
                    lv.substr(1, end - 1).trimmed(whitespace).str();
            if (currentSectionName.empty()) {
                tls.m_currentSection = nullptr;
            } else {
                auto keyStr(currentSectionName);
                auto const sectionIt(tls.m_result.find(keyStr));
                if (sectionIt != tls.m_result.not_found()) {
                    assert(sectionIt->second.data()); // Nothing erased yet
                    auto & t = getTreeItem(sectionIt->second.data());
                    if (!t.hasSectionItem())
                        t.initializeSectionItem();
                    tls.m_currentSection = &sectionIt->second;
                } else {
                    auto treeItem = std::make_shared<TreeItem>();
                    treeItem->initializeSectionItem();
                    tls.m_currentSection =
                            &tls.m_result.push_back(
                                std::make_pair(
                                    std::move(keyStr),
                                    Ptree(std::move(treeItem))))->second;
                }
            }
        } else { // Parse key-value pairs:
            auto const sepPos(lv.find('='));
            if ((sepPos == std::string::npos) || (sepPos == 0u))
                throw Configuration::InvalidSyntaxException();
            auto const key(lv.substr(0u, sepPos).rightTrimmed(whitespace));
            assert(!key.empty());
            auto keyStr(key.str());

            auto & container =
                    tls.m_currentSection ? *tls.m_currentSection : tls.m_result;
            auto const it(container.find(keyStr));
            if (it != container.not_found()) {
                assert(it->second.data()); // Nothing erased yet
                auto & t = getTreeItem(it->second.data());
                if (t.hasValueItem()) {
                    auto const & ctx = t.valueItem().m_context;
                    if (tls.m_currentSection) {
                        throw Configuration::DuplicateKeyException(
                                concat("Duplicate key \"", std::move(keyStr),
                                       "\" in section [",
                                       currentSectionName,
                                       "]! Previous declaration was in \"",
                                       ctx.m_filename->string(), "\" on line ",
                                       ctx.m_lineNumber, '.'));
                    } else {
                        throw Configuration::DuplicateKeyException(
                                concat("Duplicate top-level key \"",
                                       std::move(keyStr),
                                       "\"! Previous declaration was in \"",
                                       ctx.m_filename->string(), "\" on line ",
                                       ctx.m_lineNumber, '.'));
                    }
                } else {
                    t.initializeValueItem(
                                fpj.prepareValue(
                                    lv.substr(sepPos + 1u).trimmed(whitespace)),
                                fpj.m_canonicalPath,
                                m_lineNumber);
                }
            } else {
                auto treeItem = std::make_shared<TreeItem>();
                treeItem->initializeValueItem(
                            fpj.prepareValue(
                                lv.substr(sepPos + 1u).trimmed(whitespace)),
                            fpj.m_canonicalPath,
                            m_lineNumber);
                container.push_back(
                            std::make_pair(std::move(keyStr),
                                           Ptree(std::move(treeItem))));
            }
        }
    }
    return std::string();
}

template <typename Ptree>
std::string FileParseJob::parseFile(TopLevelParseState<Ptree> & tls) {
    if (!m_state.hasValue()) {
        try {
            m_state.emplace(*m_canonicalPath);
            auto fileId(m_state->m_inFile.fileId());
            if (tls.m_visitedFiles.find(fileId) != tls.m_visitedFiles.end())
                throw Configuration::IncludeLoopException();
            tls.m_visitedFiles.emplace(std::move(fileId));
        } catch (...) {
            std::throw_with_nested(
                        Configuration::FileOpenException(
                            concat("Failed to open file \"",
                                   m_canonicalPath->string(), "\"!")));
        }
    }
    try {
        return m_state->parseFile(tls, *this);
    } catch (...) {
        std::throw_with_nested(
                    Configuration::ParseException(
                        concat("Failed to parse file \"",
                               m_canonicalPath->string(), "\" (line ",
                               m_state->m_lineNumber, ")!")));
    }
}

template <typename T>
using Translator =
    typename boost::property_tree::translator_between<std::string, T>::type;

template <typename T> struct ValueHandler {
    static T parse(std::string value) {
        try {
            if (auto const optionalValue = Translator<T>().get_value(value))
                return *optionalValue;
            throw Configuration::FailedToParseValueException();
        } catch (...) {
            std::throw_with_nested(
                        Configuration::FailedToParseValueException());
        }
    }
    static T generateDefault(T value) noexcept { return value; }
};

template <> struct ValueHandler<std::string> {
    static std::string parse(std::string value) { return value; }
    static std::string generateDefault(StringView value) { return value.str(); }
};

template <typename Ptree>
ValueItem const * findValueItem(Ptree const & ptree) {
    if (auto const & valuePtr = ptree.data()) {
        auto const & treeItem = getTreeItem(valuePtr);
        if (treeItem.hasValueItem())
            return &treeItem.valueItem();
    }
    return nullptr;
}

template <typename Ptree>
bool hasSectionItem(Ptree const & ptree) {
    if (auto const & valuePtr = ptree.data())
        return getTreeItem(valuePtr).hasSectionItem();
    return false;
}

template <typename Ptree>
Ptree * findChild(Ptree & ptree, Path const & path) {
    auto r = &ptree;
    for (auto const & component : path.components()) {
        if (auto const p = r->get_child_optional(component)) {
            r = &*p;
        } else {
            return nullptr;
        }
    }
    return r;
}

template <typename Ptree>
ValueItem const * findValueItem(Ptree const & ptree, Path const & path) {
    if (auto const * child = findChild(ptree, path))
        return findValueItem(*child);
    return nullptr;
}

template <typename Ptree>
bool hasSectionItem(Ptree const & ptree, Path const & path) {
    if (auto const * child = findChild(ptree, path))
        return hasSectionItem(*child);
    return false;
}

template <typename Ptree>
inline bool valueEraser(TreeItem & t, Ptree &) noexcept {
    if (!t.hasSectionItem())
        return true;
    t.eraseValueItem();
    return false;
}

template <typename Ptree>
inline bool sectionEraser(TreeItem & t, Ptree & element) noexcept {
    if (!t.hasValueItem())
        return true;
    t.eraseSectionItem();
    element.erase(element.begin(), element.end());
    return false;
}

template <typename Ptree>
void erasePart(Ptree & root,
               Path const & path,
               bool (*eraseReturnWhetherNeedFullErasure)(TreeItem &,
                                                         Ptree &) noexcept)
        noexcept
{
    auto end(std::end(path.components()));
    auto it(std::begin(path.components()));
    if (it == end) {
        if (auto const & valuePtr = root.data())
            if (eraseReturnWhetherNeedFullErasure(getTreeItem(valuePtr), root))
                root.clear();
        return;
    }

    auto parent(&root);
    for (--end; it != end; ++it) {
        if (auto const child = parent->get_child_optional(*it)) {
            parent = &*child;
        } else {
            return;
        }
    }
    if (auto const child = parent->get_child_optional(*it)) {
        if (auto const & valuePtr = child->data())
            if (!eraseReturnWhetherNeedFullErasure(getTreeItem(valuePtr),
                                                   *child))
                return;
        parent->erase(*it);
    }
}

} // anonymous namespace

struct SHAREMIND_VISIBILITY_INTERNAL Configuration::Inner {

/* Methods: */

    Inner(std::vector<std::string> const & tryPaths,
          std::shared_ptr<Interpolation> interpolation)
        : m_interpolation(std::move(interpolation))
    {
        if (tryPaths.empty())
            throw NoTryPathsGivenException();
        for (auto const & path : tryPaths) {
            boost::filesystem::path boostPath(path);
            if (!boost::filesystem::exists(boostPath))
                continue;
            try {
                initFromPath(path, std::move(boostPath));
                return;
            } catch (std::exception const & e) {
                std::throw_with_nested(
                            FailedToOpenAndParseConfigurationException(
                                concat("Failed to load or parse a valid "
                                       "configuration from file \"", path,
                                       "\":", e.what())));
            } catch (...) {
                std::throw_with_nested(
                            FailedToOpenAndParseConfigurationException(
                                concat("Failed to load or parse a valid "
                                       "configuration from file \"", path,
                                       "\"!")));
            }
        }
        auto size =
                sizeof("No valid configuration file found after trying paths")
                + (tryPaths.size() * 4u);
        for (auto const & path : tryPaths)
            size += path.size();
        std::string errorMessage;
        errorMessage.reserve(size);
        errorMessage.append("No valid configuration file found after trying "
                            "paths \"");
        bool first = true;
        for (auto const & path : tryPaths) {
            if (first) {
                first = false;
                errorMessage.append(path).append("\"");
            } else {
                errorMessage.append(", \"").append(path).append("\"");
            }
        }
        errorMessage.append("!");
        throw NoValidConfigurationFileFound(errorMessage);
    }

    Inner(StringView filename,
          std::shared_ptr<Interpolation> interpolation)
        : m_interpolation(std::move(interpolation))
    {
        try {
            initFromPath(filename.str());
        } catch (...) {
            std::throw_with_nested(
                        FailedToOpenAndParseConfigurationException(
                            concat("Failed to load or parse a valid "
                                   "configuration from file \"", filename,
                                   "\"!")));
        }
    }

    Inner(Inner &&) = delete;
    Inner(Inner const &) = default;

    Inner & operator=(Inner &&) = delete;
    Inner & operator=(Inner const &) = delete;

    void initFromPath(std::string path) {
        boost::filesystem::path const boostPath(path);
        initFromPath(std::move(path), boostPath);
    }

    void initFromPath(std::string path,
                      boost::filesystem::path const & boostPath)
    {
        TopLevelParseState<decltype(m_ptree)> parser(m_ptree);
        parser.pushJob(boostPath);

        for (;;) {
            assert(parser.m_fileParseJob);
            auto & fps = *parser.m_fileParseJob;
            auto globStr(fps.parseFile(parser));
            if (globStr.empty()) {
                parser.popJob();
                if (!parser.m_fileParseJob)
                    break;
            } else {
                if (globStr.front() != '/') {
                    globStr = fps.getEscapedCurrentFileDirectory() + '/'
                              + globStr;
                    assert(globStr.front() == '/');
                }
                ::glob_t globResults;
                auto const r =
                        ::glob(globStr.c_str(),
                               GLOB_ERR | GLOB_NOCHECK | GLOB_NOSORT,
                               nullptr,
                               &globResults);
                if (r != 0) {
                    if (r == GLOB_NOSPACE)
                        throw std::bad_alloc();
                    throw GlobException();
                }
                try {
                    // We do our own LC_COLLATE-unaware sorting of glob paths:
                    std::set<std::string> includes;
                    for (auto i = globResults.gl_pathc; i > 0u; --i)
                        includes.emplace(globResults.gl_pathv[i - 1u]);
                    for (auto include : reverseRange(includes))
                        parser.pushJob(std::move(include));
                } catch (...) {
                    ::globfree(&globResults);
                    throw;
                }
                ::globfree(&globResults);
            }
        }

        m_filename = std::move(path);
    }

/* Fields: */

    std::shared_ptr<Interpolation> m_interpolation;
    std::string m_filename;
    ptree m_ptree;

};

#define SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DEFINE(C,c,...) \
    Configuration::C ## IteratorTransformer::C ## IteratorTransformer( \
            C ## IteratorTransformer &&) noexcept = default; \
    Configuration::C ## IteratorTransformer::C ## IteratorTransformer( \
            C ## IteratorTransformer const &) noexcept = default; \
    Configuration::C ## IteratorTransformer::C ## IteratorTransformer( \
            Configuration const & parent) \
        : m_path(parent.m_path) \
        , m_inner(parent.m_inner) \
    {} \
    Configuration::C ## IteratorTransformer::~C ## IteratorTransformer() \
            noexcept = default; \
    Configuration::C ## IteratorTransformer & \
    Configuration::C ## IteratorTransformer::operator=( \
            C ## IteratorTransformer &&) noexcept = default; \
    Configuration::C ## IteratorTransformer & \
    Configuration::C ## IteratorTransformer::operator=( \
            C ## IteratorTransformer const &) noexcept = default; \
    Configuration c Configuration::C ## IteratorTransformer::operator()( \
            ptree::value_type c & value) const \
    { \
        if (m_path) { \
            assert(!m_path->empty()); \
            return Configuration( \
                            std::make_shared<Path>((*m_path) + value.first), \
                            m_inner, \
                            __VA_ARGS__); \
        } else { \
            return Configuration(std::make_shared<Path>(value.first), \
                                 m_inner, \
                                 __VA_ARGS__); \
        } \
    }
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DEFINE(,,value.second)
SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DEFINE(Const,const,
    const_cast<ptree &>(value.second))
#undef SHAREMIND_LIBCONFIGURATION_CONFIGURATION_IF_DEFINE

SHAREMIND_DEFINE_EXCEPTION_NOINLINE(sharemind::Exception,
                                    Configuration::,
                                    Exception);
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        NonRootCopyException,
        "Copying a non-root Configuration object is not currently "
        "supported!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(Exception,
                                              Configuration::,
                                              NoTryPathsGivenException,
                                              "No try paths given!");
SHAREMIND_DEFINE_EXCEPTION_CONST_STDSTRING_NOINLINE(
        Exception,
        Configuration::,
        NoValidConfigurationFileFound);
SHAREMIND_DEFINE_EXCEPTION_CONST_STDSTRING_NOINLINE(Exception,
                                                    Configuration::,
                                                    InterpolationException);
SHAREMIND_DEFINE_EXCEPTION_CONST_STDSTRING_NOINLINE(
        Exception,
        Configuration::,
        FailedToOpenAndParseConfigurationException);
SHAREMIND_DEFINE_EXCEPTION_NOINLINE(Exception,
                                    Configuration::,
                                    NotFoundException);
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        NotFoundException,
        Configuration::,
        ValueNotFoundException,
        "No value defined at the requested path!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        NotFoundException,
        Configuration::,
        SectionNotFoundException,
        "No section defined at the requested path!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        FailedToParseValueException,
        "Failed to parse value in configuration");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        GlobException,
        "glob() failed!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        IncludeLoopException,
        "Include loop found: opened file already being parsed!");
SHAREMIND_DEFINE_EXCEPTION_CONST_STDSTRING_NOINLINE(Exception,
                                                    Configuration::,
                                                    ParseException);
SHAREMIND_DEFINE_EXCEPTION_CONST_STDSTRING_NOINLINE(Exception,
                                                    Configuration::,
                                                    FileOpenException);
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        FileReadException,
        "Failed to read from file!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        InvalidSyntaxException,
        "Invalid syntax!");
SHAREMIND_DEFINE_EXCEPTION_CONST_STDSTRING_NOINLINE(
        Exception,
        Configuration::,
        DuplicateKeyException);
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        UnknownDirectiveException,
        "Unknown directive!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::,
        IncludeDirectiveMissingArgumentException,
        "Missing argument to @include directive!");

SHAREMIND_DEFINE_EXCEPTION_NOINLINE(sharemind::Exception,
                                    Configuration::Interpolation::,
                                    Exception);
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::Interpolation::,
        UnknownVariableException,
        "Unknown configuration interpolation variable!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::Interpolation::,
        InterpolationSyntaxErrorException,
        "Interpolation syntax error!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::Interpolation::,
        InvalidInterpolationException,
        "Invalid interpolation given!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::Interpolation::,
        TimeException,
        "time() failed!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::Interpolation::,
        LocalTimeException,
        "localtime_r() failed!");
SHAREMIND_DEFINE_EXCEPTION_CONST_MSG_NOINLINE(
        Exception,
        Configuration::Interpolation::,
        StrftimeException,
        "strftime() failed!");

Configuration::Interpolation::Interpolation()
    : m_time(getLocalTimeTm())
{}

Configuration::Interpolation::~Interpolation() noexcept {}

std::string Configuration::Interpolation::interpolate(StringView s)
        const
{ return interpolate(s, m_time); }

std::string Configuration::Interpolation::interpolate(StringView s,
                                                      ::tm const & theTime)
        const
{
    std::string r;
    r.reserve(s.size());
    char format[3] = "% ";
    char buffer[32] = "";
    for (auto escapePos = s.findFirstOf('%');
         escapePos < s.size();
         escapePos = s.findFirstOf('%', escapePos))
    {
        if (escapePos == s.size() - 1u)
            throw InterpolationSyntaxErrorException();
        auto const escapeChar = s[escapePos + 1u];
        switch (escapeChar) {
        case '%':
            r.append(s.data(), escapePos + 1u);
            s.removePrefix(escapePos + 2u);
            escapePos = 0u;
            continue;
        case 'C': case 'd': case 'D': case 'e': case 'F': case 'H':
        case 'I': case 'j': case 'm': case 'M': case 'p': case 'R':
        case 'S': case 'T': case 'u': case 'U': case 'V': case 'w':
        case 'W': case 'y': case 'Y': case 'z': {
            format[1u] = escapeChar;
            if (!std::strftime(buffer, 32u, format, &theTime))
                throw StrftimeException();
            r.append(s.data(), escapePos).append(buffer);
            s.removePrefix(escapePos + 2u);
            escapePos = 0u;
            continue;
        }
        case '{': {
            auto const escapeStartPos = escapePos + 2u;
            auto const escapeEndPos =
                    s.findFirstOf("{%}"_sv, escapeStartPos + 2u);
            if ((escapeEndPos == StringView::npos) || (s[escapeEndPos] != '}'))
                throw InterpolationSyntaxErrorException();
            auto const matchIt(
                        m_map.find(
                            s.substr(escapeStartPos,
                                     escapeEndPos - escapeStartPos).str()));
            if (matchIt == m_map.cend())
                throw UnknownVariableException();
            r.append(s.data(), escapePos).append(matchIt->second);
            s.removePrefix(escapeEndPos + 1u);
            escapePos = 0u;
            break;
        }
        default:
            throw InvalidInterpolationException();
        }
    }
    return r.append(s.data(), s.size());
}

void Configuration::Interpolation::addVariable(std::string var,
                                               std::string value)
{ m_map.emplace(std::move(var), std::move(value)); }

void Configuration::Interpolation::resetTime()
{ return resetTime(getLocalTimeTm()); }

void Configuration::Interpolation::resetTime(std::time_t theTime)
{ return resetTime(getLocalTimeTm(theTime)); }

void Configuration::Interpolation::resetTime(::tm const & theTime)
{ m_time = theTime; }

::tm Configuration::Interpolation::getLocalTimeTm() { return getLocalTimeTm(::time(nullptr)); }

::tm Configuration::Interpolation::getLocalTimeTm(std::time_t const theTime) {
    if (theTime == std::time_t(-1))
        throw TimeException();
    ::tm theTimeTm;
    if (!localtime_r(&theTime, &theTimeTm))
        throw LocalTimeException();
    return theTimeTm;
}

Configuration::Configuration(Configuration && move) noexcept = default;

Configuration::Configuration(Configuration const & copy)
    : m_path(!copy.m_path ? nullptr : throw NonRootCopyException())
    , m_inner(std::make_shared<Inner>(*copy.m_inner))
    , m_ptree(&m_inner->m_ptree)
{}

Configuration::Configuration(StringView filename)
    : Configuration(filename, std::make_shared<Interpolation>())
{}

Configuration::Configuration(std::vector<std::string> const & tryPaths)
    : Configuration(tryPaths, std::make_shared<Interpolation>())
{}

Configuration::Configuration(StringView filename,
                             std::shared_ptr<Interpolation> interpolation)
    : m_inner(std::make_shared<Inner>(filename, std::move(interpolation)))
    , m_ptree(&m_inner->m_ptree)
{}

Configuration::Configuration(std::vector<std::string> const & tryPaths,
                             std::shared_ptr<Interpolation> interpolation)
    : m_inner(std::make_shared<Inner>(tryPaths, std::move(interpolation)))
    , m_ptree(&m_inner->m_ptree)
{}

Configuration::Configuration(std::shared_ptr<Path const> path,
                             std::shared_ptr<Inner> inner,
                             ptree & ptree)
        noexcept
    : m_path(std::move(path))
    , m_inner(std::move(inner))
    , m_ptree(&ptree)
{}

Configuration::~Configuration() noexcept {}

Configuration & Configuration::operator=(Configuration && move) noexcept
        = default;

Configuration & Configuration::operator=(Configuration const & copy) {
    if (copy.m_path)
        throw NonRootCopyException();

    m_inner = std::make_shared<Inner>(*copy.m_inner);
    m_path.reset();
    m_ptree = &m_inner->m_ptree;
    return *this;
}

std::shared_ptr<Configuration::Interpolation> const &
Configuration::interpolation() const noexcept
{ return m_inner->m_interpolation; }

void Configuration::setInterpolation(std::shared_ptr<Interpolation> i) noexcept
{ m_inner->m_interpolation = std::move(i); }

std::string const & Configuration::filename() const noexcept
{ return m_inner->m_filename; }

std::string const & Configuration::key() const noexcept {
    static std::string const emptyKey;
    if (!m_path || m_path->empty())
        return emptyKey;
    return m_path->components().back();
}

Path const & Configuration::path() const noexcept {
    static Path const emptyPath;
    return m_path ? *m_path : emptyPath;
}

bool Configuration::empty() const noexcept { return m_ptree->empty(); }

Configuration::SizeType Configuration::size() const noexcept
{ return m_ptree->size(); }

Configuration::Iterator Configuration::begin() noexcept
{ return Iterator(m_ptree->begin(), *this); }

Configuration::ConstIterator Configuration::begin() const noexcept
{ return ConstIterator(m_ptree->begin(), *this); }

Configuration::ConstIterator Configuration::cbegin() const noexcept
{ return ConstIterator(m_ptree->begin(), *this); }

Configuration::Iterator Configuration::end() noexcept
{ return Iterator(m_ptree->end(), *this); }

Configuration::ConstIterator Configuration::end() const noexcept
{ return ConstIterator(m_ptree->end(), *this); }

Configuration::ConstIterator Configuration::cend() const noexcept
{ return ConstIterator(m_ptree->end(), *this); }

void Configuration::clear() noexcept { m_ptree->clear(); }

void Configuration::erase() noexcept { clear(); }

void Configuration::erase(Path const & path) noexcept {
    auto end(std::end(path.components()));
    auto it(std::begin(path.components()));
    if (it == end) {
        m_ptree->clear();
    } else {
        auto parent(m_ptree);
        for (--end; it != end; ++it) {
            if (auto const child = parent->get_child_optional(*it)) {
                parent = &*child;
            } else {
                return;
            }
        }
        parent->erase(*it);
    }
}

void Configuration::eraseValue() noexcept {
    if (auto const & valuePtr = m_ptree->data())
        if (valueEraser(getTreeItem(valuePtr), *m_ptree))
            m_ptree->clear();
}

void Configuration::eraseValue(Path const & path) noexcept
{ erasePart(*m_ptree, path, &valueEraser); }

void Configuration::eraseSection() noexcept {
    if (auto const & valuePtr = m_ptree->data())
        if (sectionEraser(getTreeItem(valuePtr), *m_ptree))
            m_ptree->clear();
}

void Configuration::eraseSection(Path const & path) noexcept
{ erasePart(*m_ptree, path, &sectionEraser); }

std::string Configuration::interpolate(StringView value) const {
    return m_inner->m_interpolation
           ? m_inner->m_interpolation->interpolate(value)
           : value.str();
}

std::string Configuration::interpolate(StringView value,
                                       ::tm const & theTime) const
{
    return m_inner->m_interpolation
           ? m_inner->m_interpolation->interpolate(value, theTime)
           : value.str();
}

std::vector<std::string> Configuration::defaultSharemindToolTryPaths(
        std::string const & configName)
{
    assert(!configName.empty());
    std::string suffix("sharemind/" + configName + ".conf");
    std::vector<std::string> r(getXdgConfigPaths(suffix));
    r.emplace_back("/etc/" + std::move(suffix));
    return r;
}

template <typename T>
constexpr bool const isAlsoFixedSize =
        std::is_same<T, std::int8_t>::value
        || std::is_same<T, std::int16_t>::value
        || std::is_same<T, std::int32_t>::value
        || std::is_same<T, std::int64_t>::value
        || std::is_same<T, std::uint8_t>::value
        || std::is_same<T, std::uint16_t>::value
        || std::is_same<T, std::uint32_t>::value
        || std::is_same<T, std::uint64_t>::value;
static_assert(isAlsoFixedSize<std::size_t>, "");
static_assert(isAlsoFixedSize<signed char>, "");
static_assert(isAlsoFixedSize<unsigned char>, "");
static_assert(isAlsoFixedSize<signed short>, "");
static_assert(isAlsoFixedSize<unsigned short>, "");
static_assert(isAlsoFixedSize<signed int>, "");
static_assert(isAlsoFixedSize<unsigned int>, "");
static_assert(isAlsoFixedSize<signed long int>, "");
static_assert(isAlsoFixedSize<unsigned long int>, "");

bool Configuration::hasValue() const {
    if (auto const & valuePtr = m_ptree->data())
        return getTreeItem(valuePtr).hasValueItem();
    return false;
}
bool Configuration::hasValue(Path const & path) const
{ return findValueItem(*m_ptree, path); }

bool Configuration::hasSection() const {
    if (auto const & valuePtr = m_ptree->data())
        return getTreeItem(valuePtr).hasSectionItem();
    return false;
}

bool Configuration::hasSection(Path const & path) const
{ return hasSectionItem(*m_ptree, path); }

template <typename T>
auto Configuration::value() const
        -> typename std::enable_if<isReadableValueType<T>, T>::type
{
    if (auto const * valueItem = findValueItem(*m_ptree))
        return ValueHandler<T>::parse(
                    m_inner->m_interpolation
                    ? valueItem->interpolated(*m_inner->m_interpolation)
                    : valueItem->m_value);
    throw ValueNotFoundException();
}

template <typename T>
auto Configuration::get(Path const & path_) const
        -> typename std::enable_if<isReadableValueType<T>, T>::type
{
    if (auto const * valueItem = findValueItem(*m_ptree, path_))
        return ValueHandler<T>::parse(
                    m_inner->m_interpolation
                    ? valueItem->interpolated(*m_inner->m_interpolation)
                    : valueItem->m_value);
    throw ValueNotFoundException();
}

template <typename T>
auto Configuration::get(Path const & path_,
                        DefaultValueType<T> defaultValue) const
        -> typename std::enable_if<isReadableValueType<T>, T>::type
{
    if (auto const * valueItem = findValueItem(*m_ptree, path_))
        return ValueHandler<T>::parse(
                    m_inner->m_interpolation
                    ? valueItem->interpolated(*m_inner->m_interpolation)
                    : valueItem->m_value);
    return ValueHandler<T>::generateDefault(defaultValue);
}

Configuration Configuration::section(Path const & path) const {
    if (auto * child = findChild(*m_ptree, path))
        if (auto const & valuePtr = child->data())
            if (getTreeItem(valuePtr).hasSectionItem())
                return Configuration(std::make_shared<Path>(*m_path + path),
                                     m_inner,
                                     *child);
    throw SectionNotFoundException();
}

#define DEFINE_GETTERS(T) \
    template T Configuration::value<T>() const; \
    template T Configuration::get<T>(Path const &) const; \
    template T Configuration::get<T>(Path const &, DefaultValueType<T>) const
DEFINE_GETTERS(std::string);
DEFINE_GETTERS(std::int8_t);
DEFINE_GETTERS(std::int16_t);
DEFINE_GETTERS(std::int32_t);
DEFINE_GETTERS(std::int64_t);
DEFINE_GETTERS(std::uint8_t);
DEFINE_GETTERS(std::uint16_t);
DEFINE_GETTERS(std::uint32_t);
DEFINE_GETTERS(std::uint64_t);
DEFINE_GETTERS(float);
DEFINE_GETTERS(double);
DEFINE_GETTERS(long double);
#undef DEFINE_GETTERS

} /* namespace sharemind { */
