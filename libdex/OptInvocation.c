/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Utility functions for managing an invocation of "dexopt".
 */
#include "vm/DalvikVersion.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>

#include "OptInvocation.h"
#include "DexFile.h"

#include <cutils/properties.h>

static const char* kClassesDex = "classes.dex";


/*
 * Given the filename of a .jar or .dex file, construct the DEX file cache
 * name.
 *
 * For a Jar, "subFileName" is the name of the entry (usually "classes.dex").
 * For a DEX, it may be NULL.
 *
 * Returns a newly-allocated string, or NULL on failure.
 */
char* dexOptGenerateCacheFileName(const char* fileName, const char* subFileName)
{
    char nameBuf[512];
    static const char kDexCachePath[] = "dalvik-cache";
    char absoluteFile[sizeof(nameBuf)];
    const size_t kBufLen = sizeof(nameBuf) - 1;
    const char* dataRoot;
    const char* dexRoot;
    const char* cacheRoot;
    const char* systemRoot;
    char* cp;
    char dexoptDataOnly[PROPERTY_VALUE_MAX];
    char dexoptCacheOnly[PROPERTY_VALUE_MAX];

    /*
     * Get the absolute path of the Jar or DEX file.
     */
    absoluteFile[0] = '\0';
    if (fileName[0] != '/') {
        /*
         * Generate the absolute path.  This doesn't do everything it
         * should, e.g. if filename is "./out/whatever" it doesn't crunch
         * the leading "./" out, but it'll do.
         */
        if (getcwd(absoluteFile, kBufLen) == NULL) {
            LOGE("Can't get CWD while opening jar file\n");
            return NULL;
        }
        strncat(absoluteFile, "/", kBufLen);
    }
    strncat(absoluteFile, fileName, kBufLen);

    /*
     * Append the name of the Jar file entry, if any.  This is not currently
     * required, but will be if we start putting more than one DEX file
     * in a Jar.
     */
    if (subFileName != NULL) {
        strncat(absoluteFile, "/", kBufLen);
        strncat(absoluteFile, subFileName, kBufLen);
    }

    /* Turn the path into a flat filename by replacing
     * any slashes after the first one with '@' characters.
     */
    cp = absoluteFile + 1;
    while (*cp != '\0') {
        if (*cp == '/') {
            *cp = '@';
        }
        cp++;
    }

    /* Build the name of the cache directory.
     */

    /* load paths from the system environment */
    cacheRoot = getenv("ANDROID_CACHE");
    dataRoot = getenv("ANDROID_DATA");
    systemRoot = getenv("ANDROID_ROOT");

    /* make sure we didn't get any NULL values */
    if (cacheRoot == NULL)
        cacheRoot = "/cache";

    if (dataRoot == NULL)
        dataRoot = "/data";

    if (systemRoot == NULL)
        systemRoot = "/system";

    if (dexRoot == NULL)
        dexRoot = "/data";

    /* Cache anything stored on /system in cacheRoot, everything else in dataRoot */
    if (!strncmp(absoluteFile, systemRoot, strlen(systemRoot))) {
        property_get("dalvik.vm.dexopt-data-only", dexoptDataOnly, "");
        if (strcmp(dexoptDataOnly, "1") != 0) {
            dexRoot = cacheRoot;
        }
    }

    /* Cache anything in cacheRoot (dexopt-cache-only overrides dexopt-data-only) */
    property_get("dalvik.vm.dexopt-cache-only", dexoptCacheOnly, "");
    if (!strcmp(dexoptCacheOnly, "1")) {
        dexRoot = cacheRoot;
    }

    snprintf(nameBuf, kBufLen, "%s/%s", dexRoot, kDexCachePath);

    /* Tack on the file name for the actual cache file path.
     */
    strncat(nameBuf, absoluteFile, kBufLen);

    LOGV("Cache file for '%s' '%s' is '%s'\n", fileName, subFileName, nameBuf);
    return strdup(nameBuf);
}

/*
 * Create a skeletal "opt" header in a new file.  Most of the fields are
 * initialized to garbage, but we fill in "dexOffset" so others can
 * see how large the header is.
 *
 * "fd" must be positioned at the start of the file.  On return, it will
 * be positioned just past the header, and the place where the DEX data
 * should go.
 *
 * Returns 0 on success, errno on failure.
 */
DEX_EXPORT
int dexOptCreateEmptyHeader(int fd)
{
    DexOptHeader optHdr;
    ssize_t actual;

    assert(lseek(fd, 0, SEEK_CUR) == 0);

    /*
     * The data is only expected to be readable on the current system, so
     * we just write the structure.  We do need the file offset to be 64-bit
     * aligned to fulfill a DEX requirement.
     */
    assert((sizeof(optHdr) & 0x07) == 0);
    memset(&optHdr, 0xff, sizeof(optHdr));
    optHdr.dexOffset = sizeof(optHdr);
    actual = write(fd, &optHdr, sizeof(optHdr));
    if (actual != sizeof(optHdr)) {
        int err = errno ? errno : -1;
        LOGE("opt header write failed: %s", strerror(errno));
        return errno;
    }

    return 0;
}

