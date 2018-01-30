//--------------------------------------------------------------------------------------------------
/**
 *  Implements the "mksys" functionality of the "mk" tool.
 *
 *  Run 'mksys --help' for command-line options and usage help.
 *
 *  Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */
//--------------------------------------------------------------------------------------------------

#include <sys/sendfile.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <string.h>

#include "mkTools.h"
#include "commandLineInterpreter.h"


namespace cli
{



/// Object that stores build parameters that we gather.
static mk::BuildParams_t BuildParams;

/// Path to the directory into which the final, built system file should be placed.
static std::string OutputDir;

/// The root object for this system's object model.
static std::string SdefFilePath;

/// The system's name.
static std::string SystemName;

/// true if the build.ninja file should be ignored and everything should be regenerated, including
/// a new build.ninja.
static bool DontRunNinja = false;


//--------------------------------------------------------------------------------------------------
/**
 * Parse the command-line arguments and update the static operating parameters variables.
 *
 * Throws a std::runtime_error exception on failure.
 **/
//--------------------------------------------------------------------------------------------------
static void GetCommandLineArgs
(
    int argc,
    const char** argv
)
//--------------------------------------------------------------------------------------------------
{
    // Lambda function that gets called once for each occurence of the --cflags (or -C)
    // argument on the command line.
    auto cFlagsPush = [&](const char* arg)
        {
            BuildParams.cFlags += " ";
            BuildParams.cFlags += arg;
        };

    // Lambda function that gets called for each occurence of the --cxxflags, (or -X) argument on
    // the command line.
    auto cxxFlagsPush = [&](const char* arg)
        {
            BuildParams.cxxFlags += " ";
            BuildParams.cxxFlags += arg;
        };

    // Lambda function that gets called once for each occurence of the --ldflags (or -L)
    // argument on the command line.
    auto ldFlagsPush = [&](const char* arg)
        {
            BuildParams.ldFlags += " ";
            BuildParams.ldFlags += arg;
        };

    // Lambda function that gets called once for each occurence of the interface search path
    // argument on the command line.
    auto ifPathPush = [&](const char* path)
        {
            BuildParams.interfaceDirs.push_back(path);
        };

    // Lambda function that gets called once for each occurence of the source search path
    // argument on the command line.
    auto sourcePathPush = [&](const char* path)
        {
            BuildParams.sourceDirs.push_back(path);
        };

    // Lambda function that gets called once for each occurence of the lib search path
    // argument on the command line.
    auto libPathPush = [&](const char* path)
        {
            BuildParams.libDirs.push_back(path);
        };

    // Lambda function that gets called once for each occurence of a .sdef file name on the
    // command line.
    auto sdefFileNameSet = [&](const char* param)
            {
                if (SdefFilePath != "")
                {
                    throw mk::Exception_t("Only one system definition (.sdef) file allowed.");
                }
                SdefFilePath = param;
            };

    args::AddOptionalString(&OutputDir,
                           ".",
                            'o',
                            "output-dir",
                            "Specify the directory into which the final, built system file"
                            "(ready to be installed on the target) should be put.");

    args::AddOptionalString(&BuildParams.workingDir,
                            "",
                            'w',
                            "object-dir",
                            "Specify the directory into which any intermediate build artifacts"
                            " (such as .o files and generated source code files) should be put.");

    args::AddMultipleString('i',
                            "interface-search",
                            "Add a directory to the interface search path.",
                            ifPathPush);

    args::AddMultipleString('s',
                            "source-search",
                            "Add a directory to the source search path.",
                            sourcePathPush);

    args::AddMultipleString('z',
                            "lib-search",
                            "Add a directory to the lib search path.",
                            libPathPush);

    args::AddOptionalString(&BuildParams.target,
                            "localhost",
                            't',
                            "target",
                            "Set the compile target (e.g., localhost or ar7).");

    args::AddOptionalFlag(&BuildParams.beVerbose,
                          'v',
                          "verbose",
                          "Set into verbose mode for extra diagnostic information.");

    args::AddMultipleString('C',
                            "cflags",
                            "Specify extra flags to be passed to the C compiler.",
                            cFlagsPush);

    args::AddMultipleString('X',
                            "cxxflags",
                            "Specify extra flags to be passed to the C++ compiler.",
                            cxxFlagsPush);

    args::AddMultipleString('L',
                            "ldflags",
                            "Specify extra flags to be passed to the linker when linking "
                            "executables.",
                            ldFlagsPush);

    args::AddOptionalFlag(&DontRunNinja,
                           'n',
                           "dont-run-ninja",
                           "Even if a build.ninja file exists, ignore it, delete the staging area,"
                           " parse all inputs, and generate all output files, including a new copy"
                           " of the build.ninja, then exit without running ninja.  This is used by"
                           " the build.ninja to to regenerate itself and any other files that need"
                           " to be regenerated when the build.ninja finds itself out of date.");

    args::AddOptionalFlag(&BuildParams.codeGenOnly,
                          'g',
                          "generate-code",
                          "Only generate code, but don't compile, link, or bundle anything."
                          " The interface definition (include) files will be generated, along"
                          " with component and executable main files and configuration files."
                          " This is useful for supporting context-sensitive auto-complete and"
                          " related features in source code editors, for example.");

    // Any remaining parameters on the command-line are treated as the .sdef file path.
    // Note: there should only be one parameter not prefixed by an argument identifier.
    args::SetLooseArgHandler(sdefFileNameSet);

    args::Scan(argc, argv);

    // Were we given an system definition?
    if (SdefFilePath == "")
    {
        throw mk::Exception_t("A system definition must be supplied.");
    }

    // Compute the system name from the .sdef file path.
    SystemName = path::RemoveSuffix(path::GetLastNode(SdefFilePath), ".sdef");

    // If we were not given a working directory (intermediate build output directory) path,
    // use a subdirectory of the current directory, and use a different working dir for
    // different systems and for the same system built for different targets.
    if (BuildParams.workingDir == "")
    {
        BuildParams.workingDir = "./_build_" + SystemName + "/" + BuildParams.target;
    }
    else if (BuildParams.workingDir.back() == '/')
    {
        // Strip the trailing slash from the workingDir so the generated system will be exactly the
        // same if the only difference is whether or not the working dir path has a trailing slash.
        BuildParams.workingDir.erase(--BuildParams.workingDir.end());
    }

    // Add the directory containing the .sdef file to the list of source search directories
    // and the list of interface search directories.
    std::string sdefFileDir = path::GetContainingDir(SdefFilePath);
    BuildParams.sourceDirs.push_back(sdefFileDir);
    BuildParams.interfaceDirs.push_back(sdefFileDir);
}


//--------------------------------------------------------------------------------------------------
/**
 * Implements the mksys functionality.
 */
//--------------------------------------------------------------------------------------------------
void MakeSystem
(
    int argc,           ///< Count of the number of command line parameters.
    const char** argv   ///< Pointer to an array of pointers to command line argument strings.
)
//--------------------------------------------------------------------------------------------------
{
    GetCommandLineArgs(argc, argv);

    // Set the target-specific environment variables (e.g., LEGATO_TARGET).
    envVars::SetTargetSpecific(BuildParams.target);

    // Compute the staging directory path.
    auto stagingDir = path::Combine(BuildParams.workingDir, "staging");

    // If we have been asked not to run Ninja, then delete the staging area because it probably
    // will contain some of the wrong files now that .Xdef file have changed.
    if (DontRunNinja)
    {
        file::DeleteDir(stagingDir);
    }
    // If we have not been asked to ignore any already existing build.ninja, and the command-line
    // arguments and environment variables we were given are the same as last time, just run ninja.
    else if (args::MatchesSaved(BuildParams, argc, argv) && envVars::MatchesSaved(BuildParams))
    {
        RunNinja(BuildParams);
        // NOTE: If build.ninja exists, RunNinja() will not return.  If it doesn't it will.
    }
    // If we have not been asked to ignore any already existing build.ninja and there has
    // been a change in either the argument list or the environment variables,
    // save the command-line arguments and environment variables for future comparison.
    // Note: we don't need to do this if we have been asked not to run ninja, because
    // that only happens when ninja is already running and asking us to regenerate its
    // script for us, and that only happens if the args and env vars have already been saved.
    else
    {
        // Save the command line arguments.
        args::Save(BuildParams, argc, argv);

        // Save the environment variables.
        // Note: we must do this before we parse the definition file, because parsing the file
        // will result in the CURDIR environment variable being set.
        // Also, the .sdef file can contain environment variable settings.
        envVars::Save(BuildParams);
    }

    // Construct a model of the system.
    model::System_t* systemPtr = modeller::GetSystem(SdefFilePath, BuildParams);

    // If verbose mode is on, print a summary of the system model.
    if (BuildParams.beVerbose)
    {
//        modeller::PrintSummary(systemPtr);
    }

    // Create the working directory and the staging directory, if they don't already exist.
    file::MakeDir(stagingDir);

    // Generate code for all the components in the system.
    GenerateCode(model::Component_t::GetComponentMap(), BuildParams);

    // Generate code and configuration files for all the apps in the system.
    for (auto appMapEntry : systemPtr->apps)
    {
        GenerateCode(appMapEntry.second, BuildParams);
    }

    // Generate the configuration files for the system.
    config::Generate(systemPtr, BuildParams);

    // Generate the build script for the system.
    ninja::Generate(systemPtr, BuildParams, OutputDir, argc, argv);

    // If we haven't been asked not to, run ninja.
    if (!DontRunNinja)
    {
        RunNinja(BuildParams);
    }
}



} // namespace cli
