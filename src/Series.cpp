/* Copyright 2017-2018 Fabian Koller
 *
 * This file is part of openPMD-api.
 *
 * openPMD-api is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * openPMD-api is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with openPMD-api.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include "openPMD/auxiliary/Filesystem.hpp"
#include "openPMD/auxiliary/StringManip.hpp"
#include "openPMD/IO/AbstractIOHandler.hpp"
#include "openPMD/Series.hpp"

#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

#if defined(__GNUC__)
#   if (__GNUC__ == 4 && __GNUC_MINOR__ < 9)
#       define IS_GCC_48 1
#   endif
#endif

#if defined(IS_GCC_48)
#   include <regex.h>
#else
#   include <regex>
#endif


namespace openPMD
{
/** Determine the storage format of a Series from the used filename extension.
 *
 * @param   filename    tring containing the filename.
 * @return  Format that best fits the filename extension.
 */
Format determineFormat(std::string const& filename);

/** Remove the filename extension of a given storage format.
 *
 * @param   filename    String containing the filename, possibly with filename extension.
 * @param   f           File format to remove filename extension for.
 * @return  String containing the filename without filename extension.
 */
std::string cleanFilename(std::string const& filename, Format f);

/** Create a functor to determine if a file can be of a format and matches an iterationEncoding, given the filename on disk.
 *
 * @param   prefix      String containing head (i.e. before %T) of desired filename without filename extension.
 * @param   padding     Amount of padding allowed in iteration number %T. If zero, any amount of padding is matched.
 * @param   postfix     String containing tail (i.e. after %T) of desired filename without filename extension.
 * @param   f           File format to check backend applicability for.
 * @return  Functor returning tuple of bool and int.
 *          bool is True if file could be of type f and matches the iterationEncoding. False otherwise.
 *          int is the amount of padding present in the iteration number %T. Is 0 if bool is False.
 */
std::function< std::tuple< bool, int >(std::string const&) >
matcher(std::string const& prefix, int padding, std::string const& postfix, Format f);

struct Series::ParsedInput
{
    std::string path;
    std::string name;
    Format format;
    IterationEncoding iterationEncoding;
    std::string filenamePrefix;
    std::string filenamePostfix;
    int filenamePadding;
};  //ParsedInput

#if openPMD_HAVE_MPI
Series::Series(std::string const& filepath,
               AccessType at,
               MPI_Comm comm)
        : iterations{Container< Iteration, uint64_t >()},
          m_iterationEncoding{std::make_shared< IterationEncoding >()}
{
    auto input = parseInput(filepath);
    auto handler = AbstractIOHandler::createIOHandler(input->path, at, input->format, comm);
    init(handler, std::move(input));
}
#endif

Series::Series(std::string const& filepath,
               AccessType at)
        : iterations{Container< Iteration, uint64_t >()},
          m_iterationEncoding{std::make_shared< IterationEncoding >()}
{
    auto input = parseInput(filepath);
    auto handler = AbstractIOHandler::createIOHandler(input->path, at, input->format);
    init(handler, std::move(input));
}

Series::~Series()
{
    flush();
}

std::string
Series::openPMD() const
{
    return getAttribute("openPMD").get< std::string >();
}

Series&
Series::setOpenPMD(std::string const& o)
{
    setAttribute("openPMD", o);
    return *this;
}

uint32_t
Series::openPMDextension() const
{
    return getAttribute("openPMDextension").get< uint32_t >();
}

Series&
Series::setOpenPMDextension(uint32_t oe)
{
    setAttribute("openPMDextension", oe);
    return *this;
}

std::string
Series::basePath() const
{
    return getAttribute("basePath").get< std::string >();
}

Series&
Series::setBasePath(std::string const& bp)
{
    std::string version = openPMD();
    if( version == "1.0.0" || version == "1.0.1" || version == "1.1.0" )
        throw std::runtime_error("Custom basePath not allowed in openPMD <=1.1.0");

    setAttribute("basePath", bp);
    return *this;
}

std::string
Series::meshesPath() const
{
    return getAttribute("meshesPath").get< std::string >();
}

Series&
Series::setMeshesPath(std::string const& mp)
{
    if( std::any_of(iterations.begin(), iterations.end(),
                    [](Container< Iteration, uint64_t >::value_type const& i){ return i.second.meshes.written; }) )
        throw std::runtime_error("A files meshesPath can not (yet) be changed after it has been written.");

    if( auxiliary::ends_with(mp, '/') )
        setAttribute("meshesPath", mp);
    else
        setAttribute("meshesPath", mp + "/");
    dirty = true;
    return *this;
}

std::string
Series::particlesPath() const
{
    return getAttribute("particlesPath").get< std::string >();
}

Series&
Series::setParticlesPath(std::string const& pp)
{
    if( std::any_of(iterations.begin(), iterations.end(),
                    [](Container< Iteration, uint64_t >::value_type const& i){ return i.second.particles.written; }) )
        throw std::runtime_error("A files particlesPath can not (yet) be changed after it has been written.");

    if( auxiliary::ends_with(pp, '/') )
        setAttribute("particlesPath", pp);
    else
        setAttribute("particlesPath", pp + "/");
    dirty = true;
    return *this;
}

std::string
Series::author() const
{
    return getAttribute("author").get< std::string >();
}

Series&
Series::setAuthor(std::string const& a)
{
    setAttribute("author", a);
    return *this;
}

std::string
Series::software() const
{
    return getAttribute("software").get< std::string >();
}

Series&
Series::setSoftware(std::string const& s)
{
    setAttribute("software", s);
    return *this;
}

std::string
Series::softwareVersion() const
{
    return getAttribute("softwareVersion").get< std::string >();
}

Series&
Series::setSoftwareVersion(std::string const& sv)
{
    setAttribute("softwareVersion", sv);
    return *this;
}

std::string
Series::date() const
{
    return getAttribute("date").get< std::string >();
}

Series&
Series::setDate(std::string const& d)
{
    setAttribute("date", d);
    return *this;
}

std::string
Series::softwareDependencies() const
{
    return getAttribute("softwareDependencies").get< std::string >();
}

Series&
Series::setSoftwareDependencies(std::string const &newSoftwareDependencies)
{
    setAttribute("softwareDependencies", newSoftwareDependencies);
    return *this;
}

std::string
Series::machine() const
{
    return getAttribute("machine").get< std::string >();
}

Series&
Series::setMachine(std::string const &newMachine)
{
    setAttribute("machine", newMachine);
    return *this;
}

IterationEncoding
Series::iterationEncoding() const
{
    return *m_iterationEncoding;
}

Series&
Series::setIterationEncoding(IterationEncoding ie)
{
    if( written )
        throw std::runtime_error("A files iterationEncoding can not (yet) be changed after it has been written.");

    *m_iterationEncoding = ie;
    switch( ie )
    {
        case IterationEncoding::fileBased:
            setIterationFormat(*m_name);
            setAttribute("iterationEncoding", std::string("fileBased"));
            break;
        case IterationEncoding::groupBased:
            setIterationFormat(BASEPATH);
            setAttribute("iterationEncoding", std::string("groupBased"));
            break;
    }
    return *this;
}

std::string
Series::iterationFormat() const
{
    return getAttribute("iterationFormat").get< std::string >();
}

Series&
Series::setIterationFormat(std::string const& i)
{
    if( written )
        throw std::runtime_error("A files iterationFormat can not (yet) be changed after it has been written.");

    if( *m_iterationEncoding == IterationEncoding::groupBased )
        if( basePath() != i && (openPMD() == "1.0.1" || openPMD() == "1.0.0") )
            throw std::invalid_argument("iterationFormat must not differ from basePath " + basePath() + " for groupBased data");

    setAttribute("iterationFormat", i);
    return *this;
}

std::string
Series::name() const
{
    return *m_name;
}

Series&
Series::setName(std::string const& n)
{
    if( written )
        throw std::runtime_error("A files name can not (yet) be changed after it has been written.");

    if( *m_iterationEncoding == IterationEncoding::fileBased && !auxiliary::contains(*m_name, "%T") )
            throw std::runtime_error("For fileBased formats the iteration regex %T must be included in the file name");

    *m_name = n;
    dirty = true;
    return *this;
}

void
Series::flush()
{
    switch( *m_iterationEncoding )
    {
        using IE = IterationEncoding;
        case IE::fileBased:
            flushFileBased();
            break;
        case IE::groupBased:
            flushGroupBased();
            break;
    }

    IOHandler->flush();
}

std::unique_ptr< Series::ParsedInput >
Series::parseInput(std::string filepath)
{
    std::unique_ptr< Series::ParsedInput > input{new Series::ParsedInput};

#ifdef _WIN32
    if( auxiliary::contains(filepath, '/') )
    {
        std::cerr << "Filepaths on WINDOWS platforms may not contain slashes '/'! "
                  << "Replacing with backslashes '\\' unconditionally!" << std::endl;
        filepath = auxiliary::replace_all(filepath, "/", "\\");
    }
#else
    if( auxiliary::contains(filepath, '\\') )
    {
        std::cerr << "Filepaths on UNIX platforms may not include backslashes '\\'! "
                  << "Replacing with slashes '/' unconditionally!" << std::endl;
        filepath = auxiliary::replace_all(filepath, "\\", "/");
    }
#endif
    auto const pos = filepath.find_last_of(auxiliary::directory_separator);
    if( std::string::npos == pos )
    {
        input->path = ".";
        input->path.append(1, auxiliary::directory_separator);
        input->name = filepath;
    }
    else
    {
        input->path = filepath.substr(0, pos + 1);
        input->name = filepath.substr(pos + 1);
    }

    input->format = determineFormat(input->name);

#if defined(IS_GCC_48)
    regex_t pattern;
    if( regcomp(&pattern, "(.*)%(0[[:digit:]]+)?T(.*)", REG_EXTENDED) )
        throw std::runtime_error(std::string("Regex for iterationFormat '(.*)%(0[[:digit:]]+)?T(.*)' can not be compiled!"));

    constexpr uint8_t regexGroups = 4u;
    std::array< regmatch_t, regexGroups > regexMatch;
    if( regexec(&pattern, input->name.c_str(), regexGroups, regexMatch.data(), 0) )
        input->iterationEncoding = IterationEncoding::groupBased;
    else
    {
        input->iterationEncoding = IterationEncoding::fileBased;

        if( regexMatch[0].rm_so == -1 || regexMatch[0].rm_eo == -1 )
            throw std::runtime_error("Can not determine iterationFormat from filename " + input->name);

        if( regexMatch[1].rm_so == -1 || regexMatch[1].rm_eo == -1 )
            throw std::runtime_error("Can not determine iterationFormat (prefix) from filename " + input->name);
        else
            input->filenamePrefix = std::string(&input->name[regexMatch[1].rm_so], regexMatch[1].rm_eo - regexMatch[1].rm_so);

        if( regexMatch[2].rm_so == -1 || regexMatch[2].rm_eo == -1 )
            input->filenamePadding = 0;
        else
            input->filenamePadding = std::stoi(std::string(&input->name[regexMatch[2].rm_so], regexMatch[2].rm_eo - regexMatch[2].rm_so));

        if( regexMatch[3].rm_so == -1 || regexMatch[3].rm_eo == -1 )
            throw std::runtime_error("Can not determine iterationFormat (postfix) from filename " + input->name);
        else
            input->filenamePostfix = std::string(&input->name[regexMatch[3].rm_so], regexMatch[3].rm_eo - regexMatch[3].rm_so);
    }

    regfree(&pattern);
#else
    std::regex pattern("(.*)%(0[[:digit:]]+)?T(.*)");
    std::smatch regexMatch;
    std::regex_match(input->name, regexMatch, pattern);
    if( regexMatch.empty() )
        input->iterationEncoding = IterationEncoding::groupBased;
    else if( regexMatch.size() == 4 )
    {
        input->iterationEncoding = IterationEncoding::fileBased;
        input->filenamePrefix = regexMatch[1].str();
        std::string const& pad = regexMatch[2];
        if( pad.empty() )
            input->filenamePadding = 0;
        else
        {
            if( pad.front() != '0' )
                throw std::runtime_error("Invalid iterationEncoding " + input->name);
            input->filenamePadding = std::stoi(pad);
        }
        input->filenamePostfix = regexMatch[3].str();
    } else
        throw std::runtime_error("Can not determine iterationFormat from filename " + input->name);
#endif

    input->filenamePostfix = cleanFilename(input->filenamePostfix, input->format);

    input->name = cleanFilename(input->name, input->format);

    return input;
}

void
Series::init(std::shared_ptr< AbstractIOHandler > ioHandler,
             std::unique_ptr< Series::ParsedInput > input)
{
    m_writable->IOHandler = ioHandler;
    IOHandler = m_writable->IOHandler.get();
    iterations.linkHierarchy(m_writable);

    m_name = std::make_shared< std::string >(input->name);

    m_format = std::make_shared< Format >(input->format);

    m_filenamePrefix = std::make_shared< std::string >(input->filenamePrefix);
    m_filenamePostfix = std::make_shared< std::string >(input->filenamePostfix);
    m_filenamePadding = std::make_shared< int >(input->filenamePadding);

    switch( IOHandler->accessType )
    {
        case AccessType::CREATE:
        {
            setOpenPMD(OPENPMD);
            setOpenPMDextension(0);
            setAttribute("basePath", std::string(BASEPATH));
            setIterationEncoding(input->iterationEncoding);
            break;
        }
        case AccessType::READ_ONLY:
        case AccessType::READ_WRITE:
        {
            /* Allow creation of values in Containers and setting of Attributes
             * Would throw for AccessType::READ_ONLY */
            auto oldType = IOHandler->accessType;
            auto newType = const_cast< AccessType* >(&m_writable->IOHandler->accessType);
            *newType = AccessType::READ_WRITE;

            if( input->iterationEncoding == IterationEncoding::fileBased )
                readFileBased(oldType);
            else
                readGroupBased();

            *newType = oldType;
            break;
        }
    }
}

void
Series::flushFileBased()
{
    if( iterations.empty() )
        throw std::runtime_error("fileBased output can not be written with no iterations.");

    if( IOHandler->accessType == AccessType::READ_ONLY )
        for( auto& i : iterations )
            i.second.flush();
    else
    {
        bool allDirty = dirty;
        for( auto& i : iterations )
        {
            /* as there is only one series,
             * emulate the file belonging to each iteration as not yet written */
            written = false;
            iterations.written = false;

            std::stringstream iteration("");
            iteration << std::setw(*m_filenamePadding) << std::setfill('0') << i.first;
            std::string filename = *m_filenamePrefix + iteration.str() + *m_filenamePostfix;

            /* flush attributes to newly created iterations */
            dirty |= i.second.dirty;
            i.second.flushFileBased(filename, i.first);

            iterations.flush(auxiliary::replace_first(basePath(), "%T/", ""));

            flushAttributes();

            IOHandler->flush();

            /* reset the dirty bit for every iteration (i.e. file)
             * otherwise only the first iteration will have updates attributes */
            dirty = allDirty;
        }
        dirty = false;
    }
}

void
Series::flushGroupBased()
{
    if( IOHandler->accessType == AccessType::READ_ONLY )
        for( auto& i : iterations )
            i.second.flush();
    else
    {
        if( !written )
        {
            Parameter< Operation::CREATE_FILE > fCreate;
            fCreate.name = *m_name;
            IOHandler->enqueue(IOTask(this, fCreate));
        }

        iterations.flush(auxiliary::replace_first(basePath(), "%T/", ""));

        for( auto& i : iterations )
        {
            if( !i.second.written )
            {
                i.second.m_writable->parent = getWritable(&iterations);
                i.second.parent = getWritable(&iterations);
            }
            i.second.flushGroupBased(i.first);
        }

        flushAttributes();
    }
}

void
Series::flushMeshesPath()
{
    //TODO fileBased
    Parameter< Operation::WRITE_ATT > aWrite;
    aWrite.name = "meshesPath";
    Attribute a = getAttribute("meshesPath");
    aWrite.resource = a.getResource();
    aWrite.dtype = a.dtype;
    IOHandler->enqueue(IOTask(this, aWrite));
}

void
Series::flushParticlesPath()
{
    //TODO fileBased
    Parameter< Operation::WRITE_ATT > aWrite;
    aWrite.name = "particlesPath";
    Attribute a = getAttribute("particlesPath");
    aWrite.resource = a.getResource();
    aWrite.dtype = a.dtype;
    IOHandler->enqueue(IOTask(this, aWrite));
}

void
Series::readFileBased(AccessType actualAccessType)
{
    Parameter< Operation::OPEN_FILE > fOpen;
    Parameter< Operation::READ_ATT > aRead;

    if( !auxiliary::directory_exists(IOHandler->directory) )
        throw no_such_file_error("Supplied directory is not valid: " + IOHandler->directory);

    auto isPartOfSeries = matcher(*m_filenamePrefix, *m_filenamePadding, *m_filenamePostfix, *m_format);
    bool isContained;
    int padding;
    std::set< int > paddings;
    for( auto const& entry : auxiliary::list_directory(IOHandler->directory) )
    {
        std::tie(isContained, padding) = isPartOfSeries(entry);
        if( isContained )
        {
            paddings.insert(padding);

            fOpen.name = entry;
            IOHandler->enqueue(IOTask(this, fOpen));
            IOHandler->flush();
            iterations.parent = getWritable(this);

            /* allow all attributes to be set */
            written = false;

            readBase();

            using DT = Datatype;
            aRead.name = "iterationEncoding";
            IOHandler->enqueue(IOTask(this, aRead));
            IOHandler->flush();
            if( *aRead.dtype == DT::STRING )
            {
                std::string encoding = Attribute(*aRead.resource).get< std::string >();
                if( encoding == "fileBased" )
                    *m_iterationEncoding = IterationEncoding::fileBased;
                else if( encoding == "groupBased" )
                {
                    *m_iterationEncoding = IterationEncoding::groupBased;
                    std::cerr << "Series constructor called with iteration regex '%T' suggests loading a "
                              << "time series with fileBased iteration encoding. Loaded file is groupBased.\n";
                } else
                    throw std::runtime_error("Unknown iterationEncoding: " + encoding);
                setAttribute("iterationEncoding", encoding);
            }
            else
                throw std::runtime_error("Unexpected Attribute datatype for 'iterationEncoding'");

            aRead.name = "iterationFormat";
            IOHandler->enqueue(IOTask(this, aRead));
            IOHandler->flush();
            if( *aRead.dtype == DT::STRING )
                setIterationFormat(Attribute(*aRead.resource).get< std::string >());
            else
                throw std::runtime_error("Unexpected Attribute datatype for 'iterationFormat'");

            read();
        }
    }

    if( iterations.empty() )
    {
        if( actualAccessType == AccessType::READ_ONLY  )
            throw std::runtime_error("No matching iterations found: " + name());
        else
            std::cerr << "No matching iterations found: " << name() << std::endl;
    }

    if( paddings.size() > 1 )
        throw std::runtime_error("Can not determine iteration padding from existing filenames. "
                                 "Please specify '%0<N>T'.");
    else
        *m_filenamePadding = *paddings.begin();

    /* this file need not be flushed */
    iterations.written = true;
    written = true;
}

void
Series::readGroupBased()
{
    Parameter< Operation::OPEN_FILE > fOpen;
    fOpen.name = *m_name;
    IOHandler->enqueue(IOTask(this, fOpen));
    IOHandler->flush();

    /* allow all attributes to be set */
    written = false;

    readBase();

    using DT = Datatype;
    Parameter< Operation::READ_ATT > aRead;
    aRead.name = "iterationEncoding";
    IOHandler->enqueue(IOTask(this, aRead));
    IOHandler->flush();
    if( *aRead.dtype == DT::STRING )
    {
        std::string encoding = Attribute(*aRead.resource).get< std::string >();
        if( encoding == "groupBased" )
            *m_iterationEncoding = IterationEncoding::groupBased;
        else if( encoding == "fileBased" )
        {
            *m_iterationEncoding = IterationEncoding::fileBased;
            std::cerr << "Series constructor called with explicit iteration suggests loading a "
                      << "single file with groupBased iteration encoding. Loaded file is fileBased.\n";
        } else
            throw std::runtime_error("Unknown iterationEncoding: " + encoding);
        setAttribute("iterationEncoding", encoding);
    }
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'iterationEncoding'");

    aRead.name = "iterationFormat";
    IOHandler->enqueue(IOTask(this, aRead));
    IOHandler->flush();
    if( *aRead.dtype == DT::STRING )
        setIterationFormat(Attribute(*aRead.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'iterationFormat'");

    /* do not use the public checked version
     * at this point we can guarantee clearing the container won't break anything */
    iterations.clear_unchecked();

    read();

    /* this file need not be flushed */
    iterations.written = true;
    written = true;
}

void
Series::readBase()
{
    using DT = Datatype;
    Parameter< Operation::READ_ATT > aRead;

    aRead.name = "openPMD";
    IOHandler->enqueue(IOTask(this, aRead));
    IOHandler->flush();
    if( *aRead.dtype == DT::STRING )
        setOpenPMD(Attribute(*aRead.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'openPMD'");

    aRead.name = "openPMDextension";
    IOHandler->enqueue(IOTask(this, aRead));
    IOHandler->flush();
    if( *aRead.dtype == DT::UINT32 )
        setOpenPMDextension(Attribute(*aRead.resource).get< uint32_t >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'openPMDextension'");

    aRead.name = "basePath";
    IOHandler->enqueue(IOTask(this, aRead));
    IOHandler->flush();
    if( *aRead.dtype == DT::STRING )
        setAttribute("basePath", Attribute(*aRead.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'basePath'");

    Parameter< Operation::LIST_ATTS > aList;
    IOHandler->enqueue(IOTask(this, aList));
    IOHandler->flush();
    if( std::count(aList.attributes->begin(), aList.attributes->end(), "meshesPath") == 1 )
    {
        /* allow setting the meshes path after completed IO */
        for( auto& it : iterations )
            it.second.meshes.written = false;

        aRead.name = "meshesPath";
        IOHandler->enqueue(IOTask(this, aRead));
        IOHandler->flush();
        if( *aRead.dtype == DT::STRING )
            setMeshesPath(Attribute(*aRead.resource).get< std::string >());
        else
            throw std::runtime_error("Unexpected Attribute datatype for 'meshesPath'");

        for( auto& it : iterations )
            it.second.meshes.written = true;
    }

    if( std::count(aList.attributes->begin(), aList.attributes->end(), "particlesPath") == 1 )
    {
        /* allow setting the meshes path after completed IO */
        for( auto& it : iterations )
            it.second.particles.written = false;

        aRead.name = "particlesPath";
        IOHandler->enqueue(IOTask(this, aRead));
        IOHandler->flush();
        if( *aRead.dtype == DT::STRING )
            setParticlesPath(Attribute(*aRead.resource).get< std::string >());
        else
            throw std::runtime_error("Unexpected Attribute datatype for 'particlesPath'");

        for( auto& it : iterations )
            it.second.particles.written = true;
    }
}

void
Series::read()
{
    Parameter< Operation::OPEN_PATH > pOpen;
    std::string version = openPMD();
    if( version == "1.0.0" || version == "1.0.1" || version == "1.1.0" )
        pOpen.path = auxiliary::replace_first(basePath(), "/%T/", "");
    else
        throw std::runtime_error("Unknown openPMD version - " + version);
    IOHandler->enqueue(IOTask(&iterations, pOpen));

    iterations.readAttributes();

    /* obtain all paths inside the basepath (i.e. all iterations) */
    Parameter< Operation::LIST_PATHS > pList;
    IOHandler->enqueue(IOTask(&iterations, pList));
    IOHandler->flush();

    for( auto const& it : *pList.paths )
    {
        Iteration& i = iterations[std::stoull(it)];
        pOpen.path = it;
        IOHandler->enqueue(IOTask(&i, pOpen));
        i.read();
    }

    readAttributes();
}

Format
determineFormat(std::string const& filename)
{
    if( auxiliary::ends_with(filename, ".h5") )
        return Format::HDF5;
    if( auxiliary::ends_with(filename, ".bp") )
        return Format::ADIOS1;

    if( std::string::npos != filename.find('.') /* extension is provided */ )
        throw std::runtime_error("Unknown file format. Did you append a valid filename extension?");

    return Format::DUMMY;
}

std::string
cleanFilename(std::string const& filename, Format f)
{
    switch( f )
    {
        case Format::HDF5:
            return auxiliary::replace_last(filename, ".h5", "");
        case Format::ADIOS1:
        case Format::ADIOS2:
            return auxiliary::replace_last(filename, ".bp", "");
        default:
            return filename;
    }
}

std::function< std::tuple< bool, int >(std::string const&) >
buildMatcher(std::string const& regexPattern)
{
#if defined(IS_GCC_48)
    auto pattern = std::shared_ptr< regex_t >(new regex_t, [](regex_t* p){ regfree(p); delete p; });
    if( regcomp(pattern.get(), regexPattern.c_str(), REG_EXTENDED) )
        throw std::runtime_error(std::string("Regex for name '") + regexPattern + std::string("' can not be compiled!"));

    return [pattern](std::string const& filename) -> std::tuple< bool, int >
        {
            std::array< regmatch_t, 2 > regexMatches;
            bool match = !regexec(pattern.get(), filename.c_str(), regexMatches.size(), regexMatches.data(), 0);
            int padding = match ? regexMatches[1].rm_eo - regexMatches[1].rm_so : 0;
            return std::tuple< bool, int >{match, padding};
        };
#else
    std::regex pattern(regexPattern);

    return [pattern](std::string const& filename) -> std::tuple< bool, int >
        {
            std::smatch regexMatches;
            bool match = std::regex_match(filename, regexMatches, pattern);
            int padding = match ? regexMatches[1].length() : 0;
            return std::tuple< bool, int >{match, padding};
        };
#endif
}

std::function< std::tuple< bool, int >(std::string const&) >
matcher(std::string const& prefix, int padding, std::string const& postfix, Format f)
{
    switch( f )
    {
        case Format::HDF5:
        {
            std::string nameReg = "^" + prefix + "([[:digit:]]";
            if( padding != 0 )
                nameReg += "{" + std::to_string(padding) + "}";
            else
                nameReg += "+";
            nameReg += + ")" + postfix + ".h5$";
            return buildMatcher(nameReg);
        }
        case Format::ADIOS1:
        case Format::ADIOS2:
        {
            std::string nameReg = "^" + prefix + "([[:digit:]]";
            if( padding != 0 )
                nameReg += "{" + std::to_string(padding) + "}";
            else
                nameReg += "+";
            nameReg += + ")" + postfix + ".bp$";
            return buildMatcher(nameReg);
        }
        default:
            return [](std::string const&) -> std::tuple< bool, int > { return std::tuple< bool, int >{false, 0}; };
    }
}
} // openPMD
