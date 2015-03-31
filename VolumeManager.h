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

#ifndef ANDROID_VOLD_VOLUME_MANAGER_H
#define ANDROID_VOLD_VOLUME_MANAGER_H

#include <pthread.h>
#include <fnmatch.h>
#include <stdlib.h>

#ifdef __cplusplus

#include <string>
#include <list>

#include <cutils/multiuser.h>
#include <utils/List.h>
#include <sysutils/SocketListener.h>

#include "Disk.h"
#include "Volume.h"
#include "VolumeBase.h"

/* The length of an MD5 hash when encoded into ASCII hex characters */
#define MD5_ASCII_LENGTH_PLUS_NULL ((MD5_DIGEST_LENGTH*2)+1)

typedef enum { ASEC, OBB } container_type_t;

class ContainerData {
public:
    ContainerData(char* _id, container_type_t _type)
            : id(_id)
            , type(_type)
    {}

    ~ContainerData() {
        if (id != NULL) {
            free(id);
            id = NULL;
        }
    }

    char *id;
    container_type_t type;
};

typedef android::List<ContainerData*> AsecIdCollection;

class VolumeManager {
private:
    static VolumeManager *sInstance;

    SocketListener        *mBroadcaster;

    VolumeCollection      *mVolumes;
    AsecIdCollection      *mActiveContainers;
    bool                   mDebug;

    // for adjusting /proc/sys/vm/dirty_ratio when UMS is active
    int                    mUmsSharingCount;
    int                    mSavedDirtyRatio;
    int                    mUmsDirtyRatio;
    int                    mVolManagerDisabled;

public:
    virtual ~VolumeManager();

    int start();
    int stop();

    void handleBlockEvent(NetlinkEvent *evt);

    int addVolume(Volume *v);

    class DiskSource {
    public:
        DiskSource(const std::string& sysPattern, const std::string& nickname, int flags) :
                mSysPattern(sysPattern), mNickname(nickname), mFlags(flags) {
        }

        bool matches(const std::string& sysPath) {
            return !fnmatch(mSysPattern.c_str(), sysPath.c_str(), 0);
        }

        const std::string& getNickname() { return mNickname; }
        int getFlags() { return mFlags; }

    private:
        std::string mSysPattern;
        std::string mNickname;
        int mFlags;
    };

    void addDiskSource(const std::shared_ptr<DiskSource>& diskSource);

    std::shared_ptr<android::vold::Disk> findDisk(const std::string& id);
    std::shared_ptr<android::vold::VolumeBase> findVolume(const std::string& id);

    int startUser(userid_t userId);
    int cleanupUser(userid_t userId);

    int setPrimary(const std::shared_ptr<android::vold::VolumeBase>& vol);

    /* Reset all internal state, typically during framework boot */
    int reset();
    /* Prepare for device shutdown, safely unmounting all devices */
    int shutdown();
    /* Unmount all volumes, usually for encryption */
    int unmountAll();

    int listVolumes(SocketClient *cli, bool broadcast);
    int mountVolume(const char *label);
    int unmountVolume(const char *label, bool force, bool revert);
    int shareVolume(const char *label, const char *method);
    int unshareVolume(const char *label, const char *method);
    int shareEnabled(const char *path, const char *method, bool *enabled);
    int formatVolume(const char *label, bool wipe);
    void disableVolumeManager(void) { mVolManagerDisabled = 1; }

    /* ASEC */
    int findAsec(const char *id, char *asecPath = NULL, size_t asecPathLen = 0,
            const char **directory = NULL) const;
    int createAsec(const char *id, unsigned numSectors, const char *fstype,
                   const char *key, const int ownerUid, bool isExternal);
    int resizeAsec(const char *id, unsigned numSectors, const char *key);
    int finalizeAsec(const char *id);

    /**
     * Fixes ASEC permissions on a filesystem that has owners and permissions.
     * This currently means EXT4-based ASEC containers.
     *
     * There is a single file that can be marked as "private" and will not have
     * world-readable permission. The group for that file will be set to the gid
     * supplied.
     *
     * Returns 0 on success.
     */
    int fixupAsecPermissions(const char *id, gid_t gid, const char* privateFilename);
    int destroyAsec(const char *id, bool force);
    int mountAsec(const char *id, const char *key, int ownerUid, bool readOnly);
    int unmountAsec(const char *id, bool force);
    int renameAsec(const char *id1, const char *id2);
    int getAsecMountPath(const char *id, char *buffer, int maxlen);
    int getAsecFilesystemPath(const char *id, char *buffer, int maxlen);

    /* Loopback images */
    int listMountedObbs(SocketClient* cli);
    int mountObb(const char *fileName, const char *key, int ownerUid);
    int unmountObb(const char *fileName, bool force);
    int getObbMountPath(const char *id, char *buffer, int maxlen);

    Volume* getVolumeForFile(const char *fileName);

    /* Shared between ASEC and Loopback images */
    int unmountLoopImage(const char *containerId, const char *loopId,
            const char *fileName, const char *mountPoint, bool force);

    void setDebug(bool enable);

    // XXX: Post froyo this should be moved and cleaned up
    int cleanupAsec(Volume *v, bool force);

    void setBroadcaster(SocketListener *sl) { mBroadcaster = sl; }
    SocketListener *getBroadcaster() { return mBroadcaster; }

    static VolumeManager *Instance();

    static char *asecHash(const char *id, char *buffer, size_t len);

    Volume *lookupVolume(const char *label);
    int getNumDirectVolumes(void);
    int getDirectVolumeList(struct volume_info *vol_list);
    int unmountAllAsecsInDir(const char *directory);

    /*
     * Ensure that all directories along given path exist, creating parent
     * directories as needed.  Validates that given path is absolute and that
     * it contains no relative "." or ".." paths or symlinks.  Last path segment
     * is treated as filename and ignored, unless the path ends with "/".  Also
     * ensures that path belongs to a volume managed by vold.
     */
    int mkdirs(char* path);

private:
    VolumeManager();
    void readInitialState();
    bool isMountpointMounted(const char *mp);
    bool isAsecInDirectory(const char *dir, const char *asec) const;
    bool isLegalAsecId(const char *id) const;

    int linkPrimary(userid_t userId);

    std::list<std::shared_ptr<DiskSource>> mDiskSources;
    std::list<std::shared_ptr<android::vold::Disk>> mDisks;

    std::list<userid_t> mUsers;

    std::shared_ptr<android::vold::VolumeBase> mInternalEmulated;
    std::shared_ptr<android::vold::VolumeBase> mPrimary;
};

extern "C" {
#endif /* __cplusplus */
#define UNMOUNT_NOT_MOUNTED_ERR -2
    int vold_unmountAll(void);
#ifdef __cplusplus
}
#endif

#endif
