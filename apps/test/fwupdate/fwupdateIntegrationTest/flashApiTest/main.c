 /**
  * This module is for integration testing of the flash component (dual system case)
  *
  * * You must issue the following commands:
  * @verbatim
  * $ app start flashApiTest
  * $ app runProc flashApiTest --exe=flashApiTest -- <arg1> [<arg2>]
  *
  * Example:
  * $ app runProc flashApiTest --exe=flashApiTest -- help
  * @endverbatim
  *
  * Copyright (C) Sierra Wireless Inc.
  *
  */

#include "interfaces.h"

//--------------------------------------------------------------------------------------------------
/**
 * Maximum of arguments expected for a command
 *
 */
//--------------------------------------------------------------------------------------------------
#define MAX_ARGS   4

//--------------------------------------------------------------------------------------------------
/**
 * Flash API tests structure: describe tests and number of arguments
 *
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    char *action;                         ///< Action to be performed
    uint8_t nbArg;                        ///< of arguments required
    le_result_t (*flashApi)(char **args); ///< The test function to call
    char *usage;                          ///< Usage help for this action
}
FlashApiTest_t;

//--------------------------------------------------------------------------------------------------
/**
 * Static Flash API tests routines.
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_Usage(char **args);
static le_result_t FlashApiTest_Info(char **args);
static le_result_t FlashApiTest_Dump(char **args);
static le_result_t FlashApiTest_Flash(char **args);
static le_result_t FlashApiTest_FlashErase(char **args);
static le_result_t FlashApiTest_InfoUbi(char **args);
static le_result_t FlashApiTest_DumpUbi(char **args);
static le_result_t FlashApiTest_FlashUbi(char **args);

//--------------------------------------------------------------------------------------------------
/**
 * Flash API tests array
 *
 */
//--------------------------------------------------------------------------------------------------
static FlashApiTest_t FlashApiTest[] =
{
    { "help",        0, FlashApiTest_Usage,
      "help: show this help",                                                   },
    { "info",        1, FlashApiTest_Info,
      "info paritionName: open the given partition and show information",       },
    { "dump",        2, FlashApiTest_Dump,
      "dump paritionName fileName: dump a whole partition into the given file", },
    { "flash",       2, FlashApiTest_Flash,
      "flash paritionName fileName: flash the file into the given partition",   },
    { "flash-erase", 2, FlashApiTest_FlashErase,
      "flash-erase paritionName fileName: flash the file into the given"
           " partition and erase remaining blocks",                             },
    { "ubi-info",    2, FlashApiTest_InfoUbi,
      "ubi-info paritionName volumeName: open the given UBI volume in the"
           " given partition and show information",                             },
    { "ubi-dump",    3, FlashApiTest_DumpUbi,
      "ubi-dump paritionName volumeName fileName: dump a whole UBI volume from"
           " the partition into the given file",                                },
    { "ubi-flash",   3, FlashApiTest_FlashUbi,
      "ubi-flash paritionName volume-name fileName: flash the file into the"
           " given UBI volume belonging to the partition",                      },
};

//--------------------------------------------------------------------------------------------------
/**
 * Print function.
 *
 */
//--------------------------------------------------------------------------------------------------
static void Print
(
    const char* fmt,
    const char* string
)
{
    bool sandboxed = (getuid() != 0);

    if(sandboxed)
    {
        LE_INFO(fmt, string);
    }
    else
    {
        fprintf(stderr, fmt, string);
        fputc( '\n', stderr);
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Help.
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_Usage
(
    char **args
)
{
    int idx;

    Print("Usage of the 'flashApiTest' application is:", NULL);
    for(idx = 0; idx < NUM_ARRAY_MEMBERS(FlashApiTest); idx++)
    {
        Print("flashApiTest -- %s", FlashApiTest[idx].usage);
    }
    return LE_FAULT;
}

//! [Info]
//--------------------------------------------------------------------------------------------------
/**
 * Retrieve information about an open partition
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_Info
(
    char **args
)
{
    const char *partOrUbiStr = args[0];
    le_flash_PartitionRef_t partRef = NULL;
    le_result_t res;
    uint32_t badBlock, numBlock, eraseBlockSize, pageSize;

    // Open the given MTD partition in R/O
    res = le_flash_OpenMtd(partOrUbiStr, LE_FLASH_READ_ONLY, &partRef);
    LE_INFO("partition \"%s\" open ref %p, res %d", partOrUbiStr, partRef, res);
    if (LE_OK != res)
    {
        return res;
    }

    // Retrieve MTD flash information
    res = le_flash_GetBlockInformation(partRef, &badBlock, &numBlock, &eraseBlockSize, &pageSize);
    LE_INFO("Bad Block %u, Block %u, Erase Block Size %u, Page Size %u",
            badBlock, numBlock, eraseBlockSize, pageSize);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Close the MTD
    res = le_flash_Close(partRef);
    LE_INFO("partition \"%s\" close ref %p, res %d", partOrUbiStr, partRef, res);

    return res;
}
//! [Info]

//! [Dump]
//--------------------------------------------------------------------------------------------------
/**
 * Dump all blocks from a MTD partition into a file
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_Dump
(
    char **args
)
{
    const char *partOrUbiStr = args[0];
    const char *toFile = args[1];
    le_flash_PartitionRef_t partRef = NULL;
    le_result_t res;
    uint32_t badBlock, numBlock, eraseBlockSize, pageSize, blockIdx, size;
    int writeSize;
    int toFd;
    uint8_t rData[LE_FLASH_MAX_READ_SIZE];

    toFd = open(toFile, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (-1 == toFd)
    {
        LE_ERROR("Failed to open '%s': %m", toFile);
        return LE_FAULT;
    }

    // Open the given MTD partition in R/O
    res = le_flash_OpenMtd(partOrUbiStr, LE_FLASH_READ_ONLY, &partRef);
    LE_INFO("partition \"%s\" open ref %p, res %d", partOrUbiStr, partRef, res);
    if (LE_OK != res)
    {
        close(toFd);
        return res;
    }

    // Retrieve MTD flash information
    res = le_flash_GetBlockInformation(partRef, &badBlock, &numBlock, &eraseBlockSize, &pageSize);
    LE_INFO("Bad Block %u, Block %u, Erase Block Size %u, Page Size %u",
            badBlock, numBlock, eraseBlockSize, pageSize);
    if (LE_OK != res)
    {
        close(toFd);
        le_flash_Close(partRef);
        return res;
    }

    // Loop for all blocks of the partition, try to read it and dump it to the file
    for(blockIdx = 0; blockIdx < numBlock; blockIdx++)
    {
        // Read the whole erase block size
        // As we read in RAW, the whole erase block is read by once
        size = sizeof(rData);
        res = le_flash_Read(partRef, blockIdx, rData, &size);
        if (LE_OK != res)
        {
            LE_ERROR("le_flash_Read failed: %d", res);
            break;
        }
        LE_DEBUG("Read blockIdx %u size %u", blockIdx, size);

        // Write to file
        writeSize = write(toFd, rData, size);
        if (-1 == writeSize)
        {
            LE_ERROR("Write to file failed: %m");
            break;
        }
    }
    if (LE_OK == res)
    {
        LE_INFO("Read %u blocks from partition \"%s\"", blockIdx, partOrUbiStr);
    }
    else
    {
        close(toFd);
        le_flash_Close(partRef);
        return res;
    }

    // Close the MTD
    res = le_flash_Close(partRef);
    LE_INFO("partition \"%s\" close ref %p, res %d", partOrUbiStr, partRef, res);
    close(toFd);

    return res;
}
//! [Dump]

//! [Flash]
//--------------------------------------------------------------------------------------------------
/**
 * Flash a file into a MTD partition
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_Flash
(
    char **args
)
{
    const char *partOrUbiStr = args[0];
    const char *fromFile = args[1];
    le_flash_PartitionRef_t partRef = NULL;
    le_result_t res;
    uint32_t badBlock, numBlock, eraseBlockSize, pageSize, blockIdx, size, readSize, writeBadBlock;
    int fromFd;
    uint8_t rData[LE_FLASH_MAX_READ_SIZE];

    fromFd = open(fromFile, O_RDONLY);
    if (-1 == fromFd)
    {
        LE_ERROR("Failed to open '%s': %m", fromFile);
        return LE_FAULT;
    }

    // Open the given MTD partition in W/O
    res = le_flash_OpenMtd(partOrUbiStr, LE_FLASH_WRITE_ONLY, &partRef);
    LE_INFO("partition \"%s\" open ref %p, res %d", partOrUbiStr, partRef, res);
    if (LE_OK != res)
    {
        close(fromFd);
        return res;
    }

    // Retrieve MTD flash information
    res = le_flash_GetBlockInformation(partRef, &badBlock, &numBlock, &eraseBlockSize, &pageSize);
    LE_INFO("Bad Block %u, Block %u, Erase Block Size %u, Page Size %u",
            badBlock, numBlock, eraseBlockSize, pageSize);
    if (LE_OK != res)
    {
        close(fromFd);
        le_flash_Close(partRef);
        return res;
    }

    // Loop for all blocks of the partition, try to read from the file and flash the erase block
    // into the partition
    for(blockIdx = 0; blockIdx < numBlock; blockIdx++)
    {
        // Read the whole erase block size
        size = eraseBlockSize;
        readSize = read(fromFd, rData, size);
        if (-1 == readSize)
        {
            LE_ERROR("Read from file failed: %m");
            res = LE_FAULT;
            break;
        }
        // Nothing to read, file is complete
        if (0 == readSize)
        {
            break;
        }

        // Write the whole erase block size
        // As we write in RAW, the whole erase block is written by once
        // The Flash layer will perform an erase before writing. We do not need to call it.
        // If the write fails or the erase fails, the block will be marked bad and the write
        // starts again at the next block.
        res = le_flash_Write(partRef, blockIdx, rData, readSize);
        if (LE_OK != res)
        {
            LE_ERROR("le_flash_Write failed: %d", res);
            break;
        }
        // As blocks are marked bad, it may happen that we cannot write the whole file into
        // the Flash partition if too many bad blocks are found.
        LE_DEBUG("Write blockIdx %u size %u", blockIdx, size);
    }
    close(fromFd);
    if (LE_OK == res)
    {
        LE_INFO("Written %u blocks to partition \"%s\"", blockIdx, partOrUbiStr);
    }
    else
    {
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve MTD flash information to look for "new bad blocks"
    writeBadBlock = 0;
    res = le_flash_GetBlockInformation(partRef,
                                       &writeBadBlock, &numBlock, &eraseBlockSize, &pageSize);
    if ((LE_OK != res) || (writeBadBlock > badBlock))
    {
        LE_ERROR("New bad blocks marked during write: %u (%u - %u)", writeBadBlock - badBlock,
                 writeBadBlock, badBlock);
    }

    // Close the MTD
    res = le_flash_Close(partRef);
    LE_INFO("partition \"%s\" close ref %p, res %d", partOrUbiStr, partRef, res);
    return res;
}
//! [Flash]

//! [FlashErase]
//--------------------------------------------------------------------------------------------------
/**
 * Flash a file into a MTD partition and erase remaining blocks up to end of the partition if any
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_FlashErase
(
    char **args
)
{
    const char *partOrUbiStr = args[0];
    const char *fromFile = args[1];
    le_flash_PartitionRef_t partRef = NULL;
    le_result_t res;
    uint32_t badBlock, numBlock, eraseBlockSize, pageSize, blockIdx, size, readSize;
    uint32_t writeBadBlock, eraseBadBlock;
    int fromFd;
    uint8_t rData[LE_FLASH_MAX_READ_SIZE];

    fromFd = open(fromFile, O_RDONLY);
    if (-1 == fromFd)
    {
        LE_ERROR("Failed to open '%s': %m", fromFile);
        return LE_FAULT;
    }

    // Open the given MTD partition in W/O
    res = le_flash_OpenMtd(partOrUbiStr, LE_FLASH_WRITE_ONLY, &partRef);
    LE_INFO("partition \"%s\" open ref %p, res %d", partOrUbiStr, partRef, res);
    if (LE_OK != res)
    {
        close(fromFd);
        return res;
    }

    // Retrieve MTD flash information
    res = le_flash_GetBlockInformation(partRef, &badBlock, &numBlock, &eraseBlockSize, &pageSize);
    LE_INFO("Bad Block %u, Block %u, Erase Block Size %u, Page Size %u",
            badBlock, numBlock, eraseBlockSize, pageSize);
    if (LE_OK != res)
    {
        close(fromFd);
        le_flash_Close(partRef);
        return res;
    }

    // Loop for all blocks of the partition, try to read from the file and flash the erase block
    // into the partition
    for(blockIdx = 0; blockIdx < numBlock; blockIdx++)
    {
        // Read the whole erase block size
        size = eraseBlockSize;
        readSize = read(fromFd, rData, size);
        if (-1 == readSize)
        {
            LE_ERROR("Read from file failed: %m");
            res = LE_FAULT;
            break;
        }
        // Nothing to read, file is complete
        if (0 == readSize)
        {
            break;
        }

        // Write the whole erase block size
        // As we write in RAW, the whole erase block is written by once
        // The Flash layer will perform an erase before writing. We do not need to call it.
        // If the write fails or the erase fails, the block will be marked bad and the write
        // starts again at the next block.
        res = le_flash_Write(partRef, blockIdx, rData, readSize);
        if (LE_OK != res)
        {
            LE_ERROR("le_flash_Write failed: %d", res);
            break;
        }
        // As blocks are marked bad, it may happen that we cannot write the whole file into
        // the Flash partition if too many bad blocks are found.
        LE_DEBUG("Write blockIdx %u size %u", blockIdx, size);
    }
    close(fromFd);
    if (LE_OK == res)
    {
        LE_INFO("Written %u blocks to partition \"%s\"", blockIdx, partOrUbiStr);
    }
    else
    {
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve MTD flash information to look for "new bad blocks"
    writeBadBlock = 0;
    res = le_flash_GetBlockInformation(partRef,
                                       &writeBadBlock, &numBlock, &eraseBlockSize, &pageSize);
    if ((LE_OK != res) || (writeBadBlock > badBlock))
    {
        LE_ERROR("New bad blocks marked during write: %u (%u - %u)", writeBadBlock - badBlock,
                 writeBadBlock, badBlock);
    }

    LE_INFO("Erasing remaining blocks: blockIdx %u numBlock %u", blockIdx, numBlock);
    for ( ; blockIdx < numBlock; blockIdx++)
    {
        // Erase the block. If the erase fails, the block is marked bad.
        res = le_flash_EraseBlock(partRef, blockIdx);
        if (LE_OK != res)
        {
            LE_ERROR("le_flash_EraseBlock %u failed: %d", blockIdx, res);
            le_flash_Close(partRef);
            return res;
        }
        LE_DEBUG("Erase blockIdx %u", blockIdx);
    }

    // Retrieve MTD flash information to look for "new bad blocks"
    eraseBadBlock = 0;
    res = le_flash_GetBlockInformation(partRef,
                                       &eraseBadBlock, &numBlock, &eraseBlockSize, &pageSize);
    if ((LE_OK != res) || (eraseBadBlock > writeBadBlock))
    {
        LE_ERROR("New bad blocks marked during erase: %u (%u - %u)", eraseBadBlock - writeBadBlock,
                 eraseBadBlock, writeBadBlock);
    }

    // Close the MTD
    res = le_flash_Close(partRef);
    LE_INFO("partition \"%s\" close ref %p, res %d", partOrUbiStr, partRef, res);
    return res;
}
//! [FlashErase]

//! [UbiInfo]
//--------------------------------------------------------------------------------------------------
/**
 * Retrieve information about an UBI volume
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_InfoUbi
(
    char **args
)
{
    const char *partOrUbiStr = args[0];
    const char *ubiVolStr = args[1];
    le_flash_PartitionRef_t partRef = NULL;
    le_result_t res;
    uint32_t badBlock, numBlock, eraseBlockSize, pageSize;
    uint32_t freeBlock, volBlock, volSize;

    // Open the given UBI partition in R/O
    res = le_flash_OpenUbi(partOrUbiStr, LE_FLASH_READ_ONLY, &partRef);
    LE_INFO("partition \"%s\" open ref %p, res %d", partOrUbiStr, partRef, res);
    if (LE_OK != res)
    {
        return res;
    }

    // Retrieve UBI flash information
    res = le_flash_GetBlockInformation(partRef, &badBlock, &numBlock, &eraseBlockSize, &pageSize);
    LE_INFO("Bad Block %u, Block %u, Erase Block Size %u, Page Size %u",
            badBlock, numBlock, eraseBlockSize, pageSize);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Open an UBI volume belonging to this UBI partition
    res = le_flash_OpenUbiVolume(partRef, ubiVolStr, LE_FLASH_UBI_VOL_NO_SIZE);
    LE_INFO("UBI volume \"%s\" open ref %p, res %d", ubiVolStr, partRef, res);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve UBI volume information
    res = le_flash_GetUbiVolumeInformation(partRef, &freeBlock, &volBlock, &volSize);
    LE_INFO("Free Block %u, Allocated Block to Volume %u, Volume Size %u",
            freeBlock, volBlock, volSize);
    if (LE_OK != res)
    {
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }

    // Close the UBI volume
    res = le_flash_CloseUbiVolume(partRef);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Close the UBI partition
    res = le_flash_Close(partRef);
    LE_INFO("partition \"%s\" close ref %p, res %d", partOrUbiStr, partRef, res);
    return res;
}
//! [UbiInfo]

//! [UbiDump]
//--------------------------------------------------------------------------------------------------
/**
 * Dump a whole UBI volume from an UBI partition
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_DumpUbi
(
    char **args
)
{
    const char *partOrUbiStr = args[0];
    const char *ubiVolStr = args[1];
    const char *toFile = args[2];
    le_flash_PartitionRef_t partRef = NULL;
    le_result_t res;
    uint32_t badBlock, numBlock, eraseBlockSize, pageSize, blockIdx, size;
    uint32_t freeBlock, volBlock, volSize, readVolSize = 0;
    int writeSize;
    int toFd;
    uint8_t rData[LE_FLASH_MAX_READ_SIZE];

    toFd = open(toFile, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (-1 == toFd)
    {
        LE_ERROR("Failed to open '%s': %m", toFile);
        return LE_FAULT;
    }

    // Open the given UBI partition in R/O
    res = le_flash_OpenUbi(partOrUbiStr, LE_FLASH_READ_ONLY, &partRef);
    LE_INFO("partition \"%s\" open ref %p, res %d", partOrUbiStr, partRef, res);
    if (LE_OK != res)
    {
        close(toFd);
        return res;
    }

    // Open an UBI volume belonging to this UBI partition
    // As the UBI is open is R/O, discard the volume size to adjust when le_flash_CloseUbiVolume()
    // is called.
    res = le_flash_OpenUbiVolume(partRef, ubiVolStr, LE_FLASH_UBI_VOL_NO_SIZE);
    LE_INFO("UBI volume \"%s\" open ref %p, res %d", ubiVolStr, partRef, res);
    if (LE_OK != res)
    {
        close(toFd);
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve UBI flash information
    res = le_flash_GetBlockInformation(partRef, &badBlock, &numBlock, &eraseBlockSize, &pageSize);
    LE_INFO("Bad Block %u, Block %u, Erase Block Size %u, Page Size %u",
            badBlock, numBlock, eraseBlockSize, pageSize);
    if (LE_OK != res)
    {
        close(toFd);
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve UBI volume information
    res = le_flash_GetUbiVolumeInformation(partRef, &freeBlock, &volBlock, &volSize);
    LE_INFO("Free Block %u, Allocated Block to Volume %u, Volume Size %u",
            freeBlock, volBlock, volSize);
    if (LE_OK != res)
    {
        close(toFd);
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }

    // Loop for all blocks of the UBI volume, try to read it and dump it to the file
    for(blockIdx = 0; blockIdx < volBlock; blockIdx++)
    {
        // Read the whole erase block size
        // As we read in UBI, the whole erase block is read by once minus some administrative
        // pages. The size reported by the le_flash_Read() is real size read.
        size = sizeof(rData);
        res = le_flash_Read(partRef, blockIdx, rData, &size);
        if (LE_OK != res)
        {
            LE_ERROR("le_flash_Read failed: %d", res);
            break;
        }
        LE_DEBUG("Read blockIdx %u size %u", blockIdx, size);
        readVolSize += size;

        writeSize = write(toFd, rData, size);
        if (-1 == writeSize)
        {
            LE_ERROR("Write to file failed: %m");
            break;
        }
    }
    close(toFd);
    if (LE_OK == res)
    {
        LE_INFO("Read %u blocks from UBI partition \"%s\" volume \"%s\"",
                blockIdx, partOrUbiStr, ubiVolStr);
        LE_INFO("Volume size read %u, expected volume size %u", readVolSize, volSize);
    }
    else
    {
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }

    // Close the UBI volume
    res = le_flash_CloseUbiVolume(partRef);
    LE_INFO("UBI volume \"%s\" close ref %p, res %d", ubiVolStr, partRef, res);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Close the UBI partition
    res = le_flash_Close(partRef);
    LE_INFO("partition \"%s\" close ref %p, res %d", partOrUbiStr, partRef, res);
    return res;
}
//! [UbiDump]

//! [UbiFlash]
//--------------------------------------------------------------------------------------------------
/**
 * Flash a whole UBI volume from a file into an UBI partition
 *
 */
//--------------------------------------------------------------------------------------------------
static le_result_t FlashApiTest_FlashUbi
(
    char **args
)
{
    const char *partOrUbiStr = args[0];
    const char *ubiVolStr = args[1];
    const char *fromFile = args[2];
    le_flash_PartitionRef_t partRef = NULL;
    le_result_t res;
    uint32_t badBlock, numBlock, eraseBlockSize, pageSize, blockIdx, size;
    uint32_t freeBlock, volBlock, volSize, writeVolSize = 0;
    int readSize;
    int fromFd;
    uint8_t rData[LE_FLASH_MAX_READ_SIZE];
    struct stat st;

    fromFd = open(fromFile, O_RDONLY);
    if (-1 == fromFd)
    {
        LE_ERROR("Failed to open '%s': %m", fromFile);
        return LE_FAULT;
    }

    // Open the given UBI partition in R/O
    res = le_flash_OpenUbi(partOrUbiStr, LE_FLASH_WRITE_ONLY, &partRef);
    LE_INFO("partition \"%s\" open ref %p, res %d", partOrUbiStr, partRef, res);
    if (LE_OK != res)
    {
        close(fromFd);
        return res;
    }

    // Open an UBI volume belonging to this UBI partition
    // Get the size of file. This will be needed to "adjust" the UBI volume size after it was
    // fully written. This size is passed to le_flash_OpenUbiVolume() and the UBI volume will be
    // resized when le_flash_CloseUbiVolume() is called. Using LE_FLASH_UBI_VOL_NO_SIZE instead
    // will keep the volume size unchanged.
    fstat(fromFd, &st);
    res = le_flash_OpenUbiVolume(partRef, ubiVolStr, st.st_size);
    LE_INFO("UBI volume \"%s\" open ref %p, res %d", ubiVolStr, partRef, res);
    if (LE_OK != res)
    {
        close(fromFd);
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve UBI flash information
    res = le_flash_GetBlockInformation(partRef, &badBlock, &numBlock, &eraseBlockSize, &pageSize);
    LE_INFO("Bad Block %u, Block %u, Erase Block Size %u, Page Size %u",
            badBlock, numBlock, eraseBlockSize, pageSize);
    if (LE_OK != res)
    {
        close(fromFd);
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve UBI volume information
    res = le_flash_GetUbiVolumeInformation(partRef, &freeBlock, &volBlock, &volSize);
    LE_INFO("Free Block %u, Allocated Block to Volume %u, Volume Size %u",
            freeBlock, volBlock, volSize);
    if (LE_OK != res)
    {
        close(fromFd);
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }

    // Loop until the whole size of the file has been read.
    for(blockIdx = 0; writeVolSize < st.st_size; blockIdx++)
    {
        // The erase block size contains all UBI header and data information. We need to
        // remove the 2 write pages to get the whole data size.
        size = eraseBlockSize - (2*pageSize);
        readSize = read(fromFd, rData, size);
        if (-1 == readSize)
        {
            LE_ERROR("Read from file failed: %m");
            res = LE_FAULT;
            break;
        }
        // Nothing to read, file is complete
        if (0 == readSize)
        {
            break;
        }

        // Write the whole erase block size
        // As we write in UBI, the whole erase block is read by once minus some administrative
        // pages.
        // The Flash layer will perform an erase before writing. We do not need to call it.
        // If the write fails or the erase fails, the block will be marked bad and the write
        // starts again at the next block.
        // If a new block is required to store data into the volume, the Flash layer will allocate
        // it to the volume and fill the administrative headers.
        res = le_flash_Write(partRef, blockIdx, rData, readSize);
        if (LE_OK != res)
        {
            LE_ERROR("le_flash_Write failed: %d", res);
            break;
        }
        LE_DEBUG("Write blockIdx %u size %u", blockIdx, readSize);
        writeVolSize += readSize;
    }
    close(fromFd);
    if (LE_OK == res)
    {
        LE_INFO("Write %u blocks to UBI partition \"%s\" volume \"%s\"",
                blockIdx, partOrUbiStr, ubiVolStr);
        LE_INFO("Volume size written %u, expected volume size %u",
                writeVolSize, (uint32_t)st.st_size);
    }
    else
    {
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }

    // Close the UBI volume. If a specific volume size was passed to le_flash_OpenUbiVolume(), the
    // volume size will be adjusted to it. Blocks over the volume size will be released and given
    // back to the UBI partition.
    res = le_flash_CloseUbiVolume(partRef);
    LE_INFO("UBI volume \"%s\" close ref %p, res %d", ubiVolStr, partRef, res);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Just re-open the same volume without size to check that the UBI volume was resized correctly.
    res = le_flash_OpenUbiVolume(partRef, ubiVolStr, LE_FLASH_UBI_VOL_NO_SIZE);
    LE_INFO("UBI volume \"%s\" open ref %p, res %d", ubiVolStr, partRef, res);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Retrieve UBI volume information and check that the volume size reports the good size.
    res = le_flash_GetUbiVolumeInformation(partRef, &freeBlock, &volBlock, &volSize);
    if (LE_OK != res)
    {
        le_flash_CloseUbiVolume(partRef);
        le_flash_Close(partRef);
        return res;
    }
    LE_INFO("Volume size adjusted to %u", volSize);
    if ((volSize != st.st_size) || (volSize != writeVolSize))
    {
        LE_ERROR("UBI voluma has bad size: %u, expected %u", volSize, writeVolSize);
    }

    // Close the UBI volume
    res = le_flash_CloseUbiVolume(partRef);
    if (LE_OK != res)
    {
        le_flash_Close(partRef);
        return res;
    }

    // Close the UBI partition
    res = le_flash_Close(partRef);
    LE_INFO("partition \"%s\" close ref %p, res %d", partOrUbiStr, partRef, res);
    return res;
}
//! [UbiFlash]

//--------------------------------------------------------------------------------------------------
/**
 * Main thread.
 *
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    const char* actionStr = "";
    const char* args[MAX_ARGS];
    le_result_t res;
    int idx;
    int iarg;

    LE_INFO("Start flashApiTest app.");

    /* Get the test identifier */
    if (le_arg_NumArgs() >= 1)
    {
        actionStr = le_arg_GetArg(0);
        for(idx = 0; idx < NUM_ARRAY_MEMBERS(FlashApiTest); idx++)
        {
            if((0 == strcmp(actionStr, FlashApiTest[idx].action)) &&
               (le_arg_NumArgs() >= (FlashApiTest[idx].nbArg + 1)))
            {
               for(iarg = 0; iarg < FlashApiTest[idx].nbArg; iarg++)
               {
                   args[iarg] = le_arg_GetArg(1 + iarg);
               }
               res = le_flash_RequestAccess();
               if (LE_OK == res)
               {
                   res = FlashApiTest[idx].flashApi((char **)args);
               }
               else
               {
                   LE_ERROR("Unable to request flash access");
               }
               le_flash_ReleaseAccess();
               exit(LE_OK == res ? EXIT_SUCCESS : EXIT_FAILURE);
            }
        }
    }

    LE_DEBUG ("flashApiTest: unsupported action '%s'", actionStr);
    FlashApiTest_Usage(NULL);
    exit(EXIT_FAILURE);
}
