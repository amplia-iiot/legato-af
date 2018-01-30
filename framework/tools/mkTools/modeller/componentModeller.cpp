//--------------------------------------------------------------------------------------------------
/**
 * @file componentModeller.cpp
 *
 * Copyright (C) Sierra Wireless Inc.  Use of this work is subject to license.
 */
//--------------------------------------------------------------------------------------------------

#include "mkTools.h"
#include "modellerCommon.h"
#include "componentModeller.h"

namespace modeller
{


//--------------------------------------------------------------------------------------------------
/**
 * Find a source code file for a component.
 *
 * @return the absolute path to the file, or an empty string if environment variable substitution
 *         resulted in an empty string.
 *
 * @throw Exception_t if a non-empty path was provided, but a file doesn't exist at that path.
 */
//--------------------------------------------------------------------------------------------------
static std::string FindSourceFile
(
    model::Component_t* componentPtr,
    const parseTree::Token_t* tokenPtr, ///< Parse tree token containing the file path.
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    auto filePath = path::Unquote(envVars::DoSubstitution(tokenPtr->text));

    // If the environment variable substitution resulted in an empty string, then just
    // skip this file.
    if (filePath.empty())
    {
        return filePath;
    }

    // Check the component's directory first.
    auto fullFilePath = file::FindFile(filePath, { componentPtr->dir });
    if (fullFilePath.empty())
    {
        fullFilePath = file::FindFile(filePath, buildParams.sourceDirs);

        if (fullFilePath.empty())
        {
            tokenPtr->ThrowException("Couldn't find source file '" + filePath + "'.");
        }
    }

    return path::MakeAbsolute(fullFilePath);
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the source files from a given "sources:" section to a given Component_t object.
 */
//--------------------------------------------------------------------------------------------------
static void AddSources
(
    model::Component_t* componentPtr,
    parseTree::CompoundItem_t* sectionPtr, ///< The parse tree object for the "sources:" section.
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    auto tokenListPtr = static_cast<parseTree::TokenList_t*>(sectionPtr);

    for (auto contentPtr: tokenListPtr->Contents())
    {
        // Find the file (returns an absolute path or "" if not found).
        auto filePath = FindSourceFile(componentPtr, contentPtr, buildParams);

        // If the environment variable substitution resulted in an empty string, then just
        // skip this file.
        if (!filePath.empty())
        {
            auto objFilePath = path::Combine(componentPtr->workingDir, "obj/")
                             + md5(path::MakeCanonical(filePath)) + ".o";

            if (path::IsCSource(filePath))
            {
                auto objFilePtr = new model::ObjectFile_t(objFilePath, filePath);

                componentPtr->cObjectFiles.push_back(objFilePtr);
            }
            else if (path::IsCxxSource(filePath))
            {
                auto objFilePtr = new model::ObjectFile_t(objFilePath, filePath);

                componentPtr->cxxObjectFiles.push_back(objFilePtr);
            }
            else
            {
                contentPtr->ThrowException("Unrecognized file name extension on source code file '"
                                           + filePath + "'.");
            }
        }
    }

    if (componentPtr->HasIncompatibleLanguageCode())
    {
        sectionPtr->ThrowException("C/C++ source code file can't be part of a component that also "
                                   "has Java sources.");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the source files from a given "javaPackage:" section to a given Component_t object.
 */
//--------------------------------------------------------------------------------------------------
static void AddJavaPackage
(
    model::Component_t* componentPtr,
    parseTree::CompoundItem_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    auto tokenListPtr = static_cast<parseTree::TokenList_t*>(sectionPtr);

    for (auto contentPtr: tokenListPtr->Contents())
    {
        std::string packagePath = contentPtr->text;
        auto packagePtr = new model::JavaPackage_t(contentPtr->text, componentPtr->dir);

        componentPtr->javaPackages.push_back(packagePtr);
    }

    if (componentPtr->HasIncompatibleLanguageCode())
    {
        sectionPtr->ThrowException("Incompatible language insert detected.");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the contents of a "cflags:" section to the list of cFlags for a given Component_t object.
 */
//--------------------------------------------------------------------------------------------------
static void AddCFlags
(
    model::Component_t* componentPtr,
    parseTree::CompoundItem_t* sectionPtr  ///< Parse tree object for the section.
)
//--------------------------------------------------------------------------------------------------
{
    auto tokenListPtr = static_cast<parseTree::TokenList_t*>(sectionPtr);

    // The section contains a list of FILE_PATH tokens.
    for (auto contentPtr: tokenListPtr->Contents())
    {
        componentPtr->cFlags.push_back(envVars::DoSubstitution(contentPtr->text));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the contents of a "cxxflags:" section to the list of cxxFlags for a given Component_t.
 */
//--------------------------------------------------------------------------------------------------
static void AddCxxFlags
(
    model::Component_t* componentPtr,
    parseTree::CompoundItem_t* sectionPtr  ///< Parse tree object for the section.
)
//--------------------------------------------------------------------------------------------------
{
    auto tokenListPtr = static_cast<parseTree::TokenList_t*>(sectionPtr);

    // The section contains a list of FILE_PATH tokens.
    for (auto contentPtr: tokenListPtr->Contents())
    {
        componentPtr->cxxFlags.push_back(envVars::DoSubstitution(contentPtr->text));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the contents of a "ldflags:" section to the list of ldFlags for a given Component_t.
 */
//--------------------------------------------------------------------------------------------------
static void AddLdFlags
(
    model::Component_t* componentPtr,
    parseTree::CompoundItem_t* sectionPtr  ///< Parse tree object for the section.
)
//--------------------------------------------------------------------------------------------------
{
    auto tokenListPtr = static_cast<parseTree::TokenList_t*>(sectionPtr);

    // The section contains a list of FILE_PATH tokens.
    for (auto contentPtr: tokenListPtr->Contents())
    {
        componentPtr->ldFlags.push_back(envVars::DoSubstitution(contentPtr->text));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the items from a given "bundles:" section to a given Component_t object.
 */
//--------------------------------------------------------------------------------------------------
static void AddBundledItems
(
    model::Component_t* componentPtr,
    const parseTree::CompoundItem_t* sectionPtr   ///< Ptr to "bundles:" section in the parse tree.
)
//--------------------------------------------------------------------------------------------------
{
    // Bundles section is comprised of subsections (either "file:" or "dir:") which all have
    // the same basic structure (ComplexSection_t).
    // "file:" sections contain BundledFile_t objects (with type BUNDLED_FILE).
    // "dir:" sections contain BundledDir_t objects (with type BUNDLED_DIR).
    for (auto memberPtr : static_cast<const parseTree::ComplexSection_t*>(sectionPtr)->Contents())
    {
        auto subsectionPtr = parseTree::ToCompoundItemListPtr(memberPtr);

        if (subsectionPtr->Name() == "file")
        {
            for (auto itemPtr : subsectionPtr->Contents())
            {
                auto bundledFileTokenListPtr = parseTree::ToTokenListPtr(itemPtr);

                auto bundledFilePtr = GetBundledItem(bundledFileTokenListPtr);

                // If the source path is not absolute, then it is relative to the directory
                // containing the .cdef file.
                if (!path::IsAbsolute(bundledFilePtr->srcPath))
                {
                    bundledFilePtr->srcPath = path::Combine(componentPtr->dir,
                                                            bundledFilePtr->srcPath);
                }

                // Make sure that the source path exists and is a file.
                if (file::FileExists(bundledFilePtr->srcPath))
                {
                    componentPtr->bundledFiles.push_back(bundledFilePtr);
                }
                else if (file::AnythingExists(bundledFilePtr->srcPath))
                {
                    bundledFileTokenListPtr->ThrowException("Not a regular file: '"
                                                            + bundledFilePtr->srcPath + "'");
                }
                else
                {
                    bundledFileTokenListPtr->ThrowException("File not found: '"
                                                            + bundledFilePtr->srcPath + "'");
                }
            }
        }
        else if (subsectionPtr->Name() == "dir")
        {
            for (auto itemPtr : subsectionPtr->Contents())
            {
                auto bundledDirTokenListPtr = parseTree::ToTokenListPtr(itemPtr);

                auto bundledDirPtr = GetBundledItem(bundledDirTokenListPtr);

                // If the source path is not absolute, then it is relative to the directory
                // containing the .cdef file.
                if (!path::IsAbsolute(bundledDirPtr->srcPath))
                {
                    bundledDirPtr->srcPath = path::Combine(componentPtr->dir,
                                                           bundledDirPtr->srcPath);
                }

                // Make sure that the source path exists and is a directory.
                if (file::DirectoryExists(bundledDirPtr->srcPath))
                {
                    componentPtr->bundledDirs.push_back(bundledDirPtr);
                }
                else if (file::AnythingExists(bundledDirPtr->srcPath))
                {
                    bundledDirTokenListPtr->ThrowException("Not a directory: '"
                                                           + bundledDirPtr->srcPath + "'");
                }
                else
                {
                    bundledDirTokenListPtr->ThrowException("Directory not found: '"
                                                           + bundledDirPtr->srcPath + "'");
                }
            }
        }
        else
        {
            subsectionPtr->ThrowException("Internal error: Unexpected content item: "
                                          + subsectionPtr->TypeName()   );
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * If a given .api has any USETYPES statements in it, add those to a given set of
 * USETYPES included .api files.
 */
//--------------------------------------------------------------------------------------------------
static void GetUsetypesApis
(
    std::set<const model::ApiFile_t*>& set,    ///< Set to add the USETYPES-included .api files to.
    model::ApiFile_t* apiFilePtr
)
//--------------------------------------------------------------------------------------------------
{
    for (auto usetypesApiFilePtr : apiFilePtr->includes)
    {
        set.insert(usetypesApiFilePtr);

        GetUsetypesApis(set, usetypesApiFilePtr); // Recurse.
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds a server-side IPC API instance to a component for a given provided api in the parse tree.
 */
//--------------------------------------------------------------------------------------------------
static void GetProvidedApi
(
    model::Component_t* componentPtr,
    const parseTree::TokenList_t* itemPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    std::string internalName;
    std::string apiFilePath;

    // If the first content item is a NAME, then it is the internal alias and the API file path
    // will follow.
    const auto& contentList = itemPtr->Contents();
    if (contentList[0]->type == parseTree::Token_t::NAME)
    {
        internalName = contentList[0]->text;
        apiFilePath = file::FindFile(envVars::DoSubstitution(contentList[1]->text),
                                     buildParams.interfaceDirs);
        if (apiFilePath == "")
        {
            contentList[1]->ThrowException("Couldn't find file '" + contentList[1]->text + "'.");
        }
    }
    // If the first content item is not a NAME, then it must be the file path.
    else
    {
        apiFilePath = file::FindFile(envVars::DoSubstitution(contentList[0]->text),
                                     buildParams.interfaceDirs);
        if (apiFilePath == "")
        {
            contentList[0]->ThrowException("Couldn't find file '" + contentList[0]->text + "'.");
        }
    }

    // Check for options.
    bool async = false;
    bool manualStart = false;
    for (auto contentPtr : contentList)
    {
        if (contentPtr->type == parseTree::Token_t::SERVER_IPC_OPTION)
        {
            if (contentPtr->text == "[async]")
            {
                async = true;
            }
            else if (contentPtr->text == "[manual-start]")
            {
                manualStart = true;
            }
        }
    }

    // Get a pointer to the .api file object.
    auto apiFilePtr = GetApiFilePtr(apiFilePath, buildParams.interfaceDirs, contentList[0]);

    // If no internal alias was specified, then use the .api file's default prefix.
    if (internalName.empty())
    {
        internalName = apiFilePtr->defaultPrefix;
    }

    // Create the new interface object and add it to the appropriate list.
    auto ifPtr = new model::ApiServerInterface_t(apiFilePtr,
                                                 componentPtr,
                                                 internalName,
                                                 async);
    ifPtr->manualStart = manualStart;

    componentPtr->serverApis.push_back(ifPtr);

    // If the .api has any USETYPES statements in it, add those to the component's list
    // of server-side USETYPES included .api files.
    GetUsetypesApis(componentPtr->serverUsetypesApis, apiFilePtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the items from a given "provides:" section to a given Component_t object.
 */
//--------------------------------------------------------------------------------------------------
static void AddProvidedItems
(
    model::Component_t* componentPtr,
    const parseTree::CompoundItem_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    // "provides:" section is comprised of subsections, but only "api:" is supported right now.
    for (auto memberPtr : static_cast<const parseTree::ComplexSection_t*>(sectionPtr)->Contents())
    {
        auto subsectionName = memberPtr->firstTokenPtr->text;

        if (subsectionName == "api")
        {
            auto subsectionPtr = parseTree::ToCompoundItemListPtr(memberPtr);
            for (auto itemPtr : subsectionPtr->Contents())
            {
                auto apiPtr = parseTree::ToTokenListPtr(itemPtr);

                GetProvidedApi(componentPtr, apiPtr, buildParams);
            }
        }
        else
        {
            memberPtr->ThrowException("Internal error: Unexpected provided item: "
                                      + subsectionName   );
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds a client-side IPC API instance to a component for a given required API in the parse tree.
 */
//--------------------------------------------------------------------------------------------------
static void GetRequiredApi
(
    model::Component_t* componentPtr,
    const parseTree::TokenList_t* itemPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    const auto& contentList = itemPtr->Contents();

    // If the first content item is a NAME, then it is the internal alias and the API file path
    // will follow.
    std::string internalName;
    std::string apiFilePath;
    if (contentList[0]->type == parseTree::Token_t::NAME)
    {
        internalName = contentList[0]->text;
        apiFilePath = file::FindFile(envVars::DoSubstitution(contentList[1]->text),
                                     buildParams.interfaceDirs);
        if (apiFilePath == "")
        {
            contentList[1]->ThrowException("Couldn't find file '" + contentList[1]->text + "'.");
        }
    }
    // If the first content item is not a NAME, then it must be the file path.
    else
    {
        apiFilePath = file::FindFile(envVars::DoSubstitution(contentList[0]->text),
                                     buildParams.interfaceDirs);
        if (apiFilePath == "")
        {
            contentList[0]->ThrowException("Couldn't find file '"
                                           + envVars::DoSubstitution(contentList[0]->text) + "'.");
        }
    }

    // Check for options.
    bool typesOnly = false;
    bool manualStart = false;
    bool optional = false;
    for (auto contentPtr : contentList)
    {
        if (contentPtr->type == parseTree::Token_t::CLIENT_IPC_OPTION)
        {
            if (contentPtr->text == "[types-only]")
            {
                typesOnly = true;
            }
            else if (contentPtr->text == "[manual-start]")
            {
                manualStart = true;
            }
            else if (contentPtr->text == "[optional]")
            {
                manualStart = true; // [optional] implies [manual-start].
                optional = true;
            }
        }
    }
    if (typesOnly && manualStart)
    {
        itemPtr->ThrowException("Can't use [types-only] with [manual-start] or [optional]"
                                " for the same interface.");
    }

    // Get a pointer to the .api file object.
    auto apiFilePtr = GetApiFilePtr(apiFilePath, buildParams.interfaceDirs, contentList[0]);

    // If no internal alias was specified, then use the .api file's default prefix.
    if (internalName.empty())
    {
        internalName = apiFilePtr->defaultPrefix;
    }

    // If we are only importing data types from this .api file.
    if (typesOnly)
    {
        auto ifPtr = new model::ApiTypesOnlyInterface_t(apiFilePtr, componentPtr, internalName);

        componentPtr->typesOnlyApis.push_back(ifPtr);
    }
    else
    {
        auto ifPtr = new model::ApiClientInterface_t(apiFilePtr, componentPtr, internalName);

        ifPtr->manualStart = manualStart;
        ifPtr->optional = optional;

        componentPtr->clientApis.push_back(ifPtr);
    }

    // If the .api has any USETYPES statements in it, add those to the component's list
    // of client-side USETYPES included .api files.
    GetUsetypesApis(componentPtr->clientUsetypesApis, apiFilePtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Processes an item from the "lib:" subsection of the "requires:" section.
 */
//--------------------------------------------------------------------------------------------------
static void AddRequiredLib
(
    model::Component_t* componentPtr,
    const parseTree::Token_t* tokenPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    auto lib = envVars::DoSubstitution(tokenPtr->text);
    // Skip if environment variable substitution resulted in an empty string.
    if (!lib.empty())
    {
        // If the library specifier ends in a .jar extension,
        // it is a java library, which has to be included in the classPath.
        if (path::HasSuffix(lib, ".jar"))
        {
            // Try to find it relative to the component directory or the library output
            // directory.
            std::list<std::string> searchDirs = buildParams.libDirs;
            searchDirs.push_back(componentPtr->dir);
            searchDirs.push_back(buildParams.libOutputDir);
            auto libPath = file::FindFile(lib, searchDirs);

            // If found,
            if (!libPath.empty())
            {
                // Add the library to the list of the component's implicit dependencies.
                componentPtr->implicitDependencies.insert(libPath);

                // Replace lib path
                lib = libPath;
            }

            componentPtr->javaLibs.insert(lib);
        }
        // If the library specifier ends in a .a extension,
        // it is a static library, which may not be compiled with position-independent code,
        // so this has to be linked with the executable at the last linking stage.
        else if (path::HasSuffix(lib, ".a"))
        {
            componentPtr->staticLibs.insert(lib);
        }
        // If it's not a .a file,
        else
        {
            // If the library specifier contains a ".so" extension,
            if (lib.find(".so") != std::string::npos)
            {
                // Try to find it relative to the component directory or the library output
                // directory.
                std::list<std::string> searchDirs = { componentPtr->dir, buildParams.libOutputDir };
                auto libPath = file::FindFile(lib, searchDirs);

                // If found,
                if (!libPath.empty())
                {
                    // Add the library to the list of the component's implicit dependencies.
                    componentPtr->implicitDependencies.insert(libPath);

                    // Add a -L ldflag for the directory that the library is in.
                    componentPtr->ldFlags.push_back("-L" + path::GetContainingDir(libPath));

                    // Compute the short name for this library.
                    lib = path::GetLibShortName(lib);
                }
            }

            // Add a -l option to the component's LDFLAGS.
            componentPtr->ldFlags.push_back("-l" + lib);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds the items from a given "requires:" section to a given Component_t object.
 */
//--------------------------------------------------------------------------------------------------
static void AddRequiredItems
(
    model::Component_t* componentPtr,
    const parseTree::CompoundItem_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    // "requires:" section is comprised of subsections,
    for (auto memberPtr : static_cast<const parseTree::ComplexSection_t*>(sectionPtr)->Contents())
    {
        const std::string& subsectionName = memberPtr->firstTokenPtr->text;

        if (subsectionName == "api")
        {
            auto subsectionPtr = parseTree::ToCompoundItemListPtr(memberPtr);
            for (auto itemPtr : subsectionPtr->Contents())
            {
                auto apiPtr = parseTree::ToTokenListPtr(itemPtr);

                GetRequiredApi(componentPtr, apiPtr, buildParams);
            }
        }
        else if (subsectionName == "file")
        {
            auto subsectionPtr = parseTree::ToCompoundItemListPtr(memberPtr);
            for (auto itemPtr : subsectionPtr->Contents())
            {
                auto fileSpecPtr = parseTree::ToTokenListPtr(itemPtr);

                componentPtr->requiredFiles.push_back(GetRequiredFileOrDir(fileSpecPtr));
            }
        }
        else if (subsectionName == "dir")
        {
            auto subsectionPtr = parseTree::ToCompoundItemListPtr(memberPtr);
            for (auto itemPtr : subsectionPtr->Contents())
            {
                auto dirSpecPtr = parseTree::ToTokenListPtr(itemPtr);

                componentPtr->requiredDirs.push_back(GetRequiredFileOrDir(dirSpecPtr));
            }
        }
        else if (subsectionName == "device")
        {
            auto subsectionPtr = parseTree::ToCompoundItemListPtr(memberPtr);

            for (auto itemPtr : subsectionPtr->Contents())
            {
                auto deviceSpecPtr = parseTree::ToTokenListPtr(itemPtr);

                componentPtr->requiredDevices.push_back(GetRequiredDevice(deviceSpecPtr));
            }
        }
        else if (subsectionName == "component")
        {
            auto subsectionPtr = parseTree::ToTokenListPtr(memberPtr);
            for (auto itemPtr : subsectionPtr->Contents())
            {
                // Get a pointer to the component object for this sub-component.
                // Note: may trigger parsing of the sub-component's .cdef.
                auto subcomponentPtr = GetComponent(itemPtr, buildParams, { componentPtr->dir });

                // Skip if environment variable substitution resulted in an empty string.
                if (subcomponentPtr != NULL)
                {
                    componentPtr->subComponents.push_back(subcomponentPtr);
                }
            }
        }
        else if (subsectionName == "lib")
        {
            auto subsectionPtr = parseTree::ToTokenListPtr(memberPtr);
            for (auto itemPtr : subsectionPtr->Contents())
            {
                AddRequiredLib(componentPtr, itemPtr, buildParams);
            }
        }
        else
        {
            memberPtr->ThrowException("Internal error: Unexpected required item: " + subsectionName);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Pull an asset setting or a variable field from the token stream.
 **/
//--------------------------------------------------------------------------------------------------
static void AddAssetDataFields
(
    model::AssetField_t::ActionType_t actionType,
    model::Asset_t* modelAssetPtr,
    parseTree::CompoundItemList_t* sectionPtr
)
//--------------------------------------------------------------------------------------------------
{
    for (auto subItemPtr : sectionPtr->Contents())
    {
        auto tokenListPtr = static_cast<parseTree::TokenList_t*>(subItemPtr);
        auto& contents = tokenListPtr->Contents();

        // Get the name, type, (setting or variable,) and data type from the token stream.
        auto fieldPtr = new model::AssetField_t(actionType,
                                                tokenListPtr->firstTokenPtr->text,
                                                contents[0]->text);

        // If there is a default value, add that as well.
        if (contents.size() == 2)
        {
            fieldPtr->SetDefaultValue(contents[1]->text);
        }

        modelAssetPtr->fields.push_back(fieldPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Pull an asset command field from the token stream.
 **/
//--------------------------------------------------------------------------------------------------
static void AddAssetCommand
(
    model::Asset_t* modelAssetPtr,
    parseTree::CompoundItemList_t* sectionPtr
)
//--------------------------------------------------------------------------------------------------
{
    for (auto subItemPtr : sectionPtr->Contents())
    {
        auto tokenListPtr = static_cast<parseTree::TokenList_t*>(subItemPtr);
        auto fieldPtr = new model::AssetField_t(model::AssetField_t::TYPE_COMMAND,
                                                "",
                                                tokenListPtr->firstTokenPtr->text);

        modelAssetPtr->fields.push_back(fieldPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add user defined assets to the component model.
 **/
//--------------------------------------------------------------------------------------------------
static void AddUserAssets
(
    model::Component_t* componentPtr,
    const parseTree::CompoundItem_t* sectionPtr,
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    auto assetSectionPtr = static_cast<const parseTree::ComplexSection_t*>(sectionPtr);

    for (auto subsectionPtr : assetSectionPtr->Contents())
    {
        auto parsedAssetPtr = static_cast<const parseTree::Asset_t*>(subsectionPtr);
        auto modelAssetPtr = new model::Asset_t();

        modelAssetPtr->SetName(parsedAssetPtr->Name());

        for (auto assetSubsectionPtr : parsedAssetPtr->Contents())
        {
            const auto& assetSubsectionName = assetSubsectionPtr->firstTokenPtr->text;

            if (assetSubsectionName == "settings")
            {
                AddAssetDataFields(model::AssetField_t::TYPE_SETTING,
                                   modelAssetPtr,
                                   static_cast<parseTree::CompoundItemList_t*>(assetSubsectionPtr));
            }
            else if (assetSubsectionName == "variables")
            {
                AddAssetDataFields(model::AssetField_t::TYPE_VARIABLE,
                                   modelAssetPtr,
                                   static_cast<parseTree::CompoundItemList_t*>(assetSubsectionPtr));
            }
            else if (assetSubsectionName == "commands")
            {
                AddAssetCommand(modelAssetPtr,
                                static_cast<parseTree::CompoundItemList_t*>(assetSubsectionPtr));
            }
            else
            {
                assetSubsectionPtr->ThrowException("Unexpected asset subsection, '" +
                                                   assetSubsectionName + "'.");
            }
        }

        componentPtr->assets.push_back(modelAssetPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print a summary of a component model.
 **/
//--------------------------------------------------------------------------------------------------
static void PrintSummary
(
    model::Component_t* componentPtr
)
//--------------------------------------------------------------------------------------------------
{
    std::cout << "== '" << componentPtr->name << "' component summary ==" << std::endl;

    if (componentPtr->lib != "")
    {
        std::cout << "  Component library: '" << componentPtr->lib << "'" << std::endl;

        if (!componentPtr->cObjectFiles.empty())
        {
            std::cout << "  C sources:" << std::endl;

            for (auto objFilePtr : componentPtr->cObjectFiles)
            {
                std::cout << "    '" << objFilePtr->sourceFilePath << "'" << std::endl;
            }
        }

        if (!componentPtr->cxxObjectFiles.empty())
        {
            std::cout << "  C++ sources:" << std::endl;

            for (auto objFilePtr : componentPtr->cxxObjectFiles)
            {
                std::cout << "    '" << objFilePtr->sourceFilePath << "'" << std::endl;
            }
        }
    }

    if (!componentPtr->subComponents.empty())
    {
        std::cout << "  Depends on components:" << std::endl;

        for (auto subComponentPtr : componentPtr->subComponents)
        {
            std::cout << "    '" << subComponentPtr->name << "'" << std::endl;
        }
    }

    if (!componentPtr->bundledFiles.empty())
    {
        std::cout << "  Includes files from the build host:" << std::endl;

        for (auto itemPtr : componentPtr->bundledFiles)
        {
            std::cout << "    '" << itemPtr->srcPath << "':" << std::endl;
            std::cout << "      appearing inside app as: '" << itemPtr->destPath
                                                           << "'" << std::endl;
            std::cout << "      permissions:";
            PrintPermissions(itemPtr->permissions);
            std::cout << std::endl;
        }
    }

    if (!componentPtr->bundledDirs.empty())
    {
        std::cout << "  Includes directories from the build host:" << std::endl;

        for (auto itemPtr : componentPtr->bundledDirs)
        {
            std::cout << "    '" << itemPtr->srcPath << "':" << std::endl;
            std::cout << "      appearing inside app as: '" << itemPtr->destPath
                                                           << "'" << std::endl;
            std::cout << "      permissions:";
            PrintPermissions(itemPtr->permissions);
            std::cout << std::endl;
        }
    }

    if (!componentPtr->requiredFiles.empty())
    {
        std::cout << "  Imports files from the target host:" << std::endl;

        for (auto itemPtr : componentPtr->requiredFiles)
        {
            std::cout << "    '" << itemPtr->srcPath << "':" << std::endl;
            std::cout << "      appearing inside app as: '" << itemPtr->destPath
                                                           << "'" << std::endl;
        }
    }

    if (!componentPtr->requiredDirs.empty())
    {
        std::cout << "  Imports directories from the target host:" << std::endl;

        for (auto itemPtr : componentPtr->requiredDirs)
        {
            std::cout << "    '" << itemPtr->srcPath << "':" << std::endl;
            std::cout << "      appearing inside app as: '" << itemPtr->destPath
                                                           << "'" << std::endl;
        }
    }

    if (!componentPtr->typesOnlyApis.empty())
    {
        std::cout << "  Type definitions imported from:" << std::endl;

        for (auto itemPtr : componentPtr->typesOnlyApis)
        {
            std::cout << "    '" << itemPtr->apiFilePtr->path << "'" << std::endl;
            std::cout << "      With identifier prefix: '" << itemPtr->internalName << "':"
                      << std::endl;
        }
    }

    if (!componentPtr->clientApis.empty())
    {
        std::cout << "  IPC API client-side interfaces:" << std::endl;

        for (auto itemPtr : componentPtr->clientApis)
        {
            std::cout << "    '" << itemPtr->internalName << "':" << std::endl;
            std::cout << "      API defined in: '" << itemPtr->apiFilePtr->path << "'" << std::endl;
            if (itemPtr->manualStart)
            {
                std::cout << "      Automatic service connection at start-up suppressed."
                          << std::endl;
            }
            if (itemPtr->optional)
            {
                std::cout << "      Binding this to a service is optional."
                          << std::endl;
            }
        }
    }

    if (!componentPtr->serverApis.empty())
    {
        std::cout << "  IPC API server-side interfaces:" << std::endl;

        for (auto itemPtr : componentPtr->serverApis)
        {
            std::cout << "    '" << itemPtr->internalName << "':" << std::endl;
            std::cout << "      API defined in: '" << itemPtr->apiFilePtr->path << "'" << std::endl;
            if (itemPtr->async)
            {
                std::cout << "      Asynchronous server-side processing mode selected."
                          << std::endl;
            }
            if (itemPtr->manualStart)
            {
                std::cout << "      Automatic service advertisement at start-up suppressed."
                          << std::endl;
            }
        }
    }

    if (!componentPtr->assets.empty())
    {
        std::cout << "  AirVantage Cloud Interface:" << std::endl;

        for (const auto asset : componentPtr->assets)
        {
            std::cout << "    '" << asset->GetName() << "'" << std::endl;

            for (auto field : asset->fields)
            {
                auto& dataType = field->GetDataType();
                auto& name = field->GetName();

                std::cout << "      ";

                switch (field->GetActionType())
                {
                    case model::AssetField_t::TYPE_SETTING:
                        std::cout << "setting ";
                        break;

                    case model::AssetField_t::TYPE_VARIABLE:
                        std::cout << "variable ";
                        break;

                    case model::AssetField_t::TYPE_COMMAND:
                        std::cout << "command ";
                        break;

                    case model::AssetField_t::TYPE_UNSET:
                        throw mk::Exception_t("Internal error: Unset AssetField_t action type.");
                }

                if (!dataType.empty())
                {
                    std::cout << dataType << " ";
                }

                std::cout << name << std::endl;
            }
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a conceptual model for a single component residing in a given directory.
 *
 * @return Pointer to the component object.
 */
//--------------------------------------------------------------------------------------------------
model::Component_t* GetComponent
(
    const std::string& componentDir,    ///< Directory the component is found in.
    const mk::BuildParams_t& buildParams
)
//--------------------------------------------------------------------------------------------------
{
    // If component has already been modelled, return a pointer to the previously created object.
    auto componentPtr = model::Component_t::GetComponent(componentDir);
    if (componentPtr != NULL)
    {
        return componentPtr;
    }

    // Save the old CURDIR environment variable value and set it to the dir containing this file.
    auto oldDir = envVars::Get("CURDIR");
    envVars::Set("CURDIR", componentDir);

    // Parse the .cdef file.
    auto cdefFilePath = path::Combine(componentDir, "Component.cdef");
    auto cdefFilePtr = parser::cdef::Parse(cdefFilePath, buildParams.beVerbose);

    // Create a new object for this component.
    // By default, it will be built in a sub-directory called "component/<compName>" under the
    // build's root working directory.
    componentPtr = model::Component_t::CreateComponent(cdefFilePtr);

    if (buildParams.beVerbose)
    {
        std::cout << "Modelling component: '" << componentPtr->name << "'" << std::endl
                  << "  found at: '" << componentPtr->dir << "'" << std::endl;
    }

    // Iterate over the .cdef file's list of sections.
    for (auto sectionPtr : cdefFilePtr->sections)
    {
        const std::string& sectionName = sectionPtr->firstTokenPtr->text;

        if (sectionName == "sources")
        {
            AddSources(componentPtr, sectionPtr, buildParams);
        }
        else if (sectionName == "javaPackage")
        {
            AddJavaPackage(componentPtr, sectionPtr, buildParams);
        }
        else if (sectionName == "cflags")
        {
            AddCFlags(componentPtr, sectionPtr);
        }
        else if (sectionName == "cxxflags")
        {
            AddCxxFlags(componentPtr, sectionPtr);
        }
        else if (sectionName == "ldflags")
        {
            AddLdFlags(componentPtr, sectionPtr);
        }
        else if (sectionName == "bundles")
        {
            AddBundledItems(componentPtr, sectionPtr);
        }
        else if (sectionName == "provides")
        {
            AddProvidedItems(componentPtr, sectionPtr, buildParams);
        }
        else if (sectionName == "requires")
        {
            AddRequiredItems(componentPtr, sectionPtr, buildParams);
        }
        else if (sectionName == "assets")
        {
            AddUserAssets(componentPtr, sectionPtr, buildParams);
        }
        else
        {
            sectionPtr->ThrowException("Internal error: Unrecognized section '" + sectionName
                                       + "'.");
        }
    }

    // Build a path to the Legato library dir and the Legato lib so that we can add it as a
    // dependency for code components.
    auto baseLibPath = path::Combine(envVars::Get("LEGATO_ROOT"),
                                                 "build/" + buildParams.target + "/framework/lib/");
    auto liblegatoPath = path::Combine(baseLibPath, "liblegato.so");

    // If the library output directory has not been specified, then put each component's library
    // file under its own working directory.
    std::string baseComponentPath;

    if (buildParams.libOutputDir.empty())
    {
        baseComponentPath = path::Combine(path::Combine(buildParams.workingDir,
                                                        componentPtr->workingDir),
                                          "obj");
    }
    // Otherwise, put the component library directly into the library output directory.
    else
    {
        baseComponentPath = buildParams.libOutputDir;
    }

    // If there are C sources or C++ sources, a library will be built for this component.
    if (componentPtr->HasCOrCppCode())
    {
        componentPtr->lib = path::Combine(baseComponentPath,
                                          "libComponent_" + componentPtr->name + ".so");

        // It will have an init function that will need to be executed (unless it is built
        // "stand-alone").
        componentPtr->initFuncName = "_" + componentPtr->name + "_COMPONENT_INIT";

        // Changes to liblegato should trigger re-linking of the component library.
        componentPtr->implicitDependencies.insert(liblegatoPath);
    }
    else if (componentPtr->HasJavaCode())
    {
        componentPtr->lib = path::Combine(baseComponentPath,
                                          "libComponent_" + componentPtr->name + ".jar");

        // Changes to liblegato should trigger re-linking of the component library.
        auto legatoJarPath = path::Combine(baseLibPath, "legato.jar");
        auto liblegatoJniPath = path::Combine(baseLibPath, "liblegatoJni.so");

        componentPtr->implicitDependencies.insert(liblegatoPath);
        componentPtr->implicitDependencies.insert(legatoJarPath);
        componentPtr->implicitDependencies.insert(liblegatoJniPath);
    }

    if (buildParams.beVerbose)
    {
        PrintSummary(componentPtr);
    }

    // Restore the previous contents of the CURDIR environment variable.
    envVars::Set("CURDIR", oldDir);

    return componentPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a conceptual model for a single component residing in a directory specified by a given
 * FILE_PATH token.
 *
 * @return Pointer to the component object, or NULL if the token specifies an empty environment
 *         variable.
 *
 * @throw mkTools::Exception_t on error.
 */
//--------------------------------------------------------------------------------------------------
model::Component_t* GetComponent
(
    const parseTree::Token_t* tokenPtr,
    const mk::BuildParams_t& buildParams,
    const std::list<std::string>& preSearchDirs ///< Dirs to search before buildParams source dirs
)
//--------------------------------------------------------------------------------------------------
{
    // Resolve the path to the component.
    std::string componentPath = path::Unquote(envVars::DoSubstitution(tokenPtr->text));

    // Skip if environment variable substitution resulted in an empty string.
    if (componentPath.empty())
    {
        return NULL;
    }

    auto resolvedPath = file::FindComponent(componentPath, preSearchDirs);
    if (resolvedPath.empty())
    {
        resolvedPath = file::FindComponent(componentPath, buildParams.sourceDirs);
    }
    if (resolvedPath.empty())
    {
        tokenPtr->ThrowException("Couldn't find component '" + componentPath + "'.");
    }

    // Get the component object.
    return GetComponent(path::MakeAbsolute(resolvedPath), buildParams);
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds an instance of a given component to a given executable.
 **/
//--------------------------------------------------------------------------------------------------
void AddComponentInstance
(
    model::Exe_t* exePtr,
    model::Component_t* componentPtr
)
//--------------------------------------------------------------------------------------------------
{
    // If there is already an instance of this component in this executable, ignore this.
    // Someday we may support multiple instances of the same component, but not today.
    for (auto instancePtr : exePtr->componentInstances)
    {
        if (instancePtr->componentPtr == componentPtr)
        {
            return;
        }
    }

    // Recursively add instances of any sub-components to the executable first, so the exe's
    // resulting component instance list will be sorted in the order in which the component
    // initialization functions must be called.
    for (auto subComponentPtr : componentPtr->subComponents)
    {
        AddComponentInstance(exePtr, subComponentPtr);
    }

    // Add an instance of the component to the executable.
    auto componentInstancePtr = new model::ComponentInstance_t(exePtr, componentPtr);
    exePtr->AddComponentInstance(componentInstancePtr);

    // For each of the component's client-side interfaces, create an interface instance.
    for (auto ifPtr : componentPtr->clientApis)
    {
        auto ifInstancePtr = new model::ApiClientInterfaceInstance_t(componentInstancePtr, ifPtr);
        componentInstancePtr->clientApis.push_back(ifInstancePtr);
    }

    // For each of the component's server-side interfaces, create an interface instance.
    for (auto ifPtr : componentPtr->serverApis)
    {
        auto ifInstancePtr = new model::ApiServerInterfaceInstance_t(componentInstancePtr, ifPtr);
        componentInstancePtr->serverApis.push_back(ifInstancePtr);

        ifInstancePtr->name = exePtr->name + '.' + componentPtr->name + '.' + ifPtr->internalName;
    }
}


} // namespace modeller
