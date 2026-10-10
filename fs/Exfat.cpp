/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <sys/mount.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <logwrap/logwrap.h>

#include "Exfat.h"
#include "Utils.h"

using android::base::StringPrintf;

namespace android {
namespace vold {
namespace exfat {

static const char* kMkfsPath = "/system/bin/mkfs.exfat";
static const char* kFsckPath = "/system/bin/fsck.exfat";

bool IsSupported() {
    return access(kMkfsPath, X_OK) == 0 && access(kFsckPath, X_OK) == 0 &&
           IsFilesystemSupported("exfat");
}

status_t Check(const std::string& source) {
    std::vector<std::string> cmd;
    cmd.push_back(kFsckPath);
    cmd.push_back("-a");
    cmd.push_back(source);

    // fsck.exfat (exfatprogs) exit codes: 0 no errors, 1 errors corrected, 4 errors left.
    // After a power loss "-a" often fixes something and exits 1; that is a good check, not a
    // failed one (the volume used to stay unmounted until the next boot).
    int rc = ForkExecvp(cmd, nullptr, sFsckUntrustedContext);
    if (rc == 0) {
        LOG(INFO) << "Check OK";
        return 0;
    } else if (rc == 1) {
        LOG(WARNING) << "Check OK, errors fixed";
        return 0;
    } else if (rc == 4) {
        // Left alone by "-a": problems only fixable by cutting files short. The caller
        // mounts read-only so the files can still be read and copied off.
        LOG(ERROR) << "Check failed (code 4), errors left";
        errno = EUCLEAN;
        return -1;
    } else {
        LOG(ERROR) << "Check failed (code " << rc << ")";
        errno = EIO;
        return -1;
    }
}

status_t Mount(const std::string& source, const std::string& target, int ownerUid, int ownerGid,
               int permMask, bool readOnly) {
    int mountFlags = MS_NODEV | MS_NOSUID | MS_DIRSYNC | MS_NOATIME | MS_NOEXEC;
    if (readOnly) mountFlags |= MS_RDONLY;
    auto mountData = android::base::StringPrintf("uid=%d,gid=%d,fmask=%o,dmask=%o", ownerUid,
                                                 ownerGid, permMask, permMask);

    if (mount(source.c_str(), target.c_str(), "exfat", mountFlags, mountData.c_str()) == 0) {
        return 0;
    }

    PLOG(ERROR) << "Mount failed; attempting read-only";
    mountFlags |= MS_RDONLY;
    if (mount(source.c_str(), target.c_str(), "exfat", mountFlags, mountData.c_str()) == 0) {
        return 0;
    }

    return -1;
}

status_t Format(const std::string& source) {
    std::vector<std::string> cmd;
    cmd.push_back(kMkfsPath);
    cmd.push_back("-n");
    cmd.push_back("android");
    cmd.push_back(source);

    int rc = ForkExecvp(cmd);
    if (rc == 0) {
        LOG(INFO) << "Format OK";
        return 0;
    } else {
        LOG(ERROR) << "Format failed (code " << rc << ")";
        errno = EIO;
        return -1;
    }
    return 0;
}

}  // namespace exfat
}  // namespace vold
}  // namespace android
