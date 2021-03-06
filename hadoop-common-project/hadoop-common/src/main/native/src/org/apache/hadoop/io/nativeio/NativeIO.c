/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "org_apache_hadoop.h"
#include "org_apache_hadoop_io_nativeio_NativeIO.h"
#include "org_apache_hadoop_io_nativeio_NativeIO_POSIX.h"
#include "exception.h"

#ifdef UNIX
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <jni.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#if !(defined(__FreeBSD__) || defined(__MACH__))
#include <sys/sendfile.h>
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "config.h"
#endif

#ifdef WINDOWS
#include <assert.h>
#include <Windows.h>
#include "winutils.h"
#endif

#include <fcntl.h>
#include <x86intrin.h>
#include "pmc_defines.h"
#include "pmc_nvram_api_expo.h"
#include "file_descriptor.h"
#include "errno_enum.h"

#define MMAP_PROT_READ org_apache_hadoop_io_nativeio_NativeIO_POSIX_MMAP_PROT_READ
#define MMAP_PROT_WRITE org_apache_hadoop_io_nativeio_NativeIO_POSIX_MMAP_PROT_WRITE
#define MMAP_PROT_EXEC org_apache_hadoop_io_nativeio_NativeIO_POSIX_MMAP_PROT_EXEC

//#define NVM_GRAN 805306368
#define NVM_GRAN 1610612736
//#define NVM_GRAN 1610616832
//#define NVM_GRAN 40960
// the NativeIO$POSIX$Stat inner class and its constructor
static jclass stat_clazz;
static jmethodID stat_ctor;
static jmethodID stat_ctor2;

// the NativeIOException class and its constructor
static jclass nioe_clazz;
static jmethodID nioe_ctor;

// the monitor used for working around non-threadsafe implementations
// of getpwuid_r, observed on platforms including RHEL 6.0.
// Please see HADOOP-7156 for details.
jobject pw_lock_object;

/*
 * Throw a java.IO.IOException, generating the message from errno.
 * NB. this is also used form windows_secure_container_executor.c
 */
extern void throw_ioe(JNIEnv* env, int errnum);

// Internal functions
#ifdef UNIX
static ssize_t get_pw_buflen();
#endif

/**
 * Returns non-zero if the user has specified that the system
 * has non-threadsafe implementations of getpwuid_r or getgrgid_r.
 **/
static void loadu_16bytes(void* dest, const void * src_in_pci) {
	*((__m128i *)((char*)dest)) = _mm_loadu_si128((__m128i *)src_in_pci);
}

static void loadu_32bytes(void* dest, const void * src_in_pci) {
	*((__m256i *)((char*)dest)) = _mm256_loadu_si256((__m256i *)src_in_pci);
}

static void storeu_16bytes(void* dest, const void * src_in_pci) {
	_mm_storeu_si128((__m128i *)((char*)dest), *((__m128i *)((char*)src_in_pci)));
}

static void storeu_32bytes(void* dest, const void * src_in_pci) {
	_mm256_storeu_si256((__m256i *)((char*)dest), *((__m256i *)((char*)src_in_pci)));
}

static int workaround_non_threadsafe_calls(JNIEnv *env, jclass clazz) {
  jboolean result;
  jfieldID needs_workaround_field = (*env)->GetStaticFieldID(
    env, clazz,
    "workaroundNonThreadSafePasswdCalls",
    "Z");
  PASS_EXCEPTIONS_RET(env, 0);
  assert(needs_workaround_field);

  result = (*env)->GetStaticBooleanField(
    env, clazz, needs_workaround_field);
  return result;
}

static void stat_init(JNIEnv *env, jclass nativeio_class) {
  jclass clazz = NULL;
  jclass obj_class = NULL;
  jmethodID  obj_ctor = NULL;
  // Init Stat
  clazz = (*env)->FindClass(env, "org/apache/hadoop/io/nativeio/NativeIO$POSIX$Stat");
  if (!clazz) {
    return; // exception has been raised
  }
  stat_clazz = (*env)->NewGlobalRef(env, clazz);
  if (!stat_clazz) {
    return; // exception has been raised
  }
  stat_ctor = (*env)->GetMethodID(env, stat_clazz, "<init>",
    "(III)V");
  if (!stat_ctor) {
    return; // exception has been raised
  }
  stat_ctor2 = (*env)->GetMethodID(env, stat_clazz, "<init>",
    "(Ljava/lang/String;Ljava/lang/String;I)V");
  if (!stat_ctor2) {
    return; // exception has been raised
  }
  obj_class = (*env)->FindClass(env, "java/lang/Object");
  if (!obj_class) {
    return; // exception has been raised
  }
  obj_ctor = (*env)->GetMethodID(env, obj_class,
    "<init>", "()V");
  if (!obj_ctor) {
    return; // exception has been raised
  }

  if (workaround_non_threadsafe_calls(env, nativeio_class)) {
    pw_lock_object = (*env)->NewObject(env, obj_class, obj_ctor);
    PASS_EXCEPTIONS(env);
    pw_lock_object = (*env)->NewGlobalRef(env, pw_lock_object);

    PASS_EXCEPTIONS(env);
  }
}

static void stat_deinit(JNIEnv *env) {
  if (stat_clazz != NULL) {  
    (*env)->DeleteGlobalRef(env, stat_clazz);
    stat_clazz = NULL;
  }
  if (pw_lock_object != NULL) {
    (*env)->DeleteGlobalRef(env, pw_lock_object);
    pw_lock_object = NULL;
  }
}

static void nioe_init(JNIEnv *env) {
  // Init NativeIOException
  nioe_clazz = (*env)->FindClass(
    env, "org/apache/hadoop/io/nativeio/NativeIOException");
  PASS_EXCEPTIONS(env);

  nioe_clazz = (*env)->NewGlobalRef(env, nioe_clazz);
#ifdef UNIX
  nioe_ctor = (*env)->GetMethodID(env, nioe_clazz, "<init>",
    "(Ljava/lang/String;Lorg/apache/hadoop/io/nativeio/Errno;)V");
#endif

#ifdef WINDOWS
  nioe_ctor = (*env)->GetMethodID(env, nioe_clazz, "<init>",
    "(Ljava/lang/String;I)V");
#endif
}

static void nioe_deinit(JNIEnv *env) {
  if (nioe_clazz != NULL) {
    (*env)->DeleteGlobalRef(env, nioe_clazz);
    nioe_clazz = NULL;
  }
  nioe_ctor = NULL;
}

static int addChildrenInDirectory(jlong* addr, jint directory_location,
		jint target_offset, jint inodeNum) {
	int commit = 1;
	int success = 0;
	int child_num;

	int quo = directory_location / NVM_GRAN;
	int cal_index = directory_location % NVM_GRAN;
	void * dmiBuffer = (void *) addr[quo];
	void * dmiBuffer_init = (void *) addr[0];
	int inode_num = inodeNum;

	memcpy(&child_num, dmiBuffer + cal_index + 4092 - 4, sizeof(child_num));
	if (child_num == 1021) {
		int next_dir;
		memcpy(&next_dir, dmiBuffer + cal_index + 4092 - 8, sizeof(next_dir));
		if (next_dir == 0) {
		//	int inode_num;
		//	memcpy(&inode_num, dmiBuffer_init, sizeof(inode_num));
			inode_num = inode_num + 1;
		//	memcpy(dmiBuffer_init, &inode_num, sizeof(inode_num));
			next_dir = 4096 + 4096 * (inode_num - 1);
			memcpy(dmiBuffer + cal_index + 4092 - 8, &next_dir, sizeof(next_dir));
		}
		return addChildrenInDirectory(addr, next_dir, target_offset, inode_num);
	}
	int next_location = 4 * child_num;
	memcpy(dmiBuffer + cal_index + next_location, &target_offset, sizeof(target_offset));
	child_num = child_num + 1;
	memcpy(dmiBuffer + cal_index + 4092 - 4, &child_num, sizeof(child_num));
	//memcpy(dmiBuffer + cal_index + 4092, &commit, sizeof(commit));

	return inode_num;

}

//static int addChildrenInDirectoryFast(jlong* addr, jint directory_location,
//		jint target_offset, jint initLocation, jint inodeNum) {
//	int commit = 1;
//	int success = 0;
//	int child_num;
//
//	int quo = directory_location / NVM_GRAN;
//	int cal_index = directory_location % NVM_GRAN;
//	int quo_sec = initLocation / NVM_GRAN;
//	int sec_index = initLocation / NVM_GRAN;
//	void * dmiBuffer = (void *) addr[quo];
//	void * dmiBuffer_sec = (void *) addr[quo_sec];
//	int inode_num = inodeNum;
//
//	memcpy(&child_num, dmiBuffer + cal_index + 4092 - 4, sizeof(child_num));
//	if (child_num == 1021) {
//		int next_dir;
//		memcpy(&next_dir, dmiBuffer + cal_index + 4092 - 8, sizeof(next_dir));
//		if (next_dir == 0) {
//			inode_num = inode_num + 1;
//			next_dir = 4096 + 4096 * (inode_num - 1);
//			//next_dir = 4096 + 4096 * inodeNum;
//			memcpy(dmiBuffer + cal_index + 4092 - 8, &next_dir, sizeof(next_dir));
//			memcpy(dmiBuffer_sec + sec_index + 4092 - 12, &next_dir, sizeof(next_dir));
//		}
//		return addChildrenInDirectoryFast(addr, next_dir, target_offset, initLocation, inode_num);
//		//return addChildrenInDirectoryFast(addr, next_dir, target_offset, initLocation, inodeNum + 1);
//	}
//	int next_location = 4 * child_num;
//	memcpy(dmiBuffer + cal_index + next_location, &target_offset, sizeof(target_offset));
//	child_num = child_num + 1;
//	memcpy(dmiBuffer + cal_index + 4092 - 4, &child_num, sizeof(child_num));
//	return inode_num;
//
//}

static int addChildrenInDirectoryFast(jlong* addr, jint directory_location,
		jint target_offset, jint children_num) {
	int commit = 1;
	int success = 0;
	int child_num = children_num;

	int quo = directory_location / NVM_GRAN;
	int cal_index = directory_location % NVM_GRAN;
	void * dmiBuffer = (void *) addr[quo];

	int next_location = 4 * child_num;
	memcpy(dmiBuffer + cal_index + next_location, &target_offset, sizeof(target_offset));
	child_num = child_num + 1;
	memcpy(dmiBuffer + cal_index + 4092 - 4, &child_num, sizeof(child_num));
	return 1;
}

static int updateNextDir(jlong* addr, jint directory_location,	jint next_dir) {

	int quo = directory_location / NVM_GRAN;
	int cal_index = directory_location % NVM_GRAN;
	void * dmiBuffer = (void *) addr[quo];

	memcpy(dmiBuffer + cal_index + 4092 - 8 , &next_dir, sizeof(next_dir));
	return 1;
}

static int isPageFull(jlong* addr, jint location) {
	int child_num;
	int quo = location / NVM_GRAN;
	int cal_index = location % NVM_GRAN;
	void * dmiBuffer = (void *) addr[quo];

	memcpy(&child_num, dmiBuffer + cal_index + 4092 - 4, sizeof(child_num));
	return child_num;
}


/*
 * Compatibility mapping for fadvise flags. Return the proper value from fnctl.h.
 * If the value is not known, return the argument unchanged.
 */
static int map_fadvise_flag(jint flag) {
#ifdef HAVE_POSIX_FADVISE
  switch(flag) {
    case org_apache_hadoop_io_nativeio_NativeIO_POSIX_POSIX_FADV_NORMAL:
      return POSIX_FADV_NORMAL;
      break;
    case org_apache_hadoop_io_nativeio_NativeIO_POSIX_POSIX_FADV_RANDOM:
      return POSIX_FADV_RANDOM;
      break;
    case org_apache_hadoop_io_nativeio_NativeIO_POSIX_POSIX_FADV_SEQUENTIAL:
      return POSIX_FADV_SEQUENTIAL;
      break;
    case org_apache_hadoop_io_nativeio_NativeIO_POSIX_POSIX_FADV_WILLNEED:
      return POSIX_FADV_WILLNEED;
      break;
    case org_apache_hadoop_io_nativeio_NativeIO_POSIX_POSIX_FADV_DONTNEED:
      return POSIX_FADV_DONTNEED;
      break;
    case org_apache_hadoop_io_nativeio_NativeIO_POSIX_POSIX_FADV_NOREUSE:
      return POSIX_FADV_NOREUSE;
      break;
    default:
      return flag;
  }
#else
  return flag;
#endif
}

/*
 * private static native void initNative();
 *
 * We rely on this function rather than lazy initialization because
 * the lazy approach may have a race if multiple callers try to
 * init at the same time.
 */
JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_initNative(
  JNIEnv *env, jclass clazz) {
  stat_init(env, clazz);
  PASS_EXCEPTIONS_GOTO(env, error);
  nioe_init(env);
  PASS_EXCEPTIONS_GOTO(env, error);
  fd_init(env);
  PASS_EXCEPTIONS_GOTO(env, error);
#ifdef UNIX
  errno_enum_init(env);
  PASS_EXCEPTIONS_GOTO(env, error);
#endif
  return;
error:
  // these are all idempodent and safe to call even if the
  // class wasn't initted yet
#ifdef UNIX
  stat_deinit(env);
#endif
  nioe_deinit(env);
  fd_deinit(env);
#ifdef UNIX
  errno_enum_deinit(env);
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_POSIX
 * Method:    fstat
 * Signature: (Ljava/io/FileDescriptor;)Lorg/apache/hadoop/io/nativeio/NativeIO$POSIX$Stat;
 * public static native Stat fstat(FileDescriptor fd);
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT jobject JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_fstat(
  JNIEnv *env, jclass clazz, jobject fd_object)
{
#ifdef UNIX
  jobject ret = NULL;

  int fd = fd_get(env, fd_object);
  PASS_EXCEPTIONS_GOTO(env, cleanup);

  struct stat s;
  int rc = fstat(fd, &s);
  if (rc != 0) {
    throw_ioe(env, errno);
    goto cleanup;
  }

  // Construct result
  ret = (*env)->NewObject(env, stat_clazz, stat_ctor,
    (jint)s.st_uid, (jint)s.st_gid, (jint)s.st_mode);

cleanup:
  return ret;
#endif

#ifdef WINDOWS
  LPWSTR owner = NULL;
  LPWSTR group = NULL;
  int mode = 0;
  jstring jstr_owner = NULL;
  jstring jstr_group = NULL;
  int rc;
  jobject ret = NULL;
  HANDLE hFile = (HANDLE) fd_get(env, fd_object);
  PASS_EXCEPTIONS_GOTO(env, cleanup);

  rc = FindFileOwnerAndPermissionByHandle(hFile, &owner, &group, &mode);
  if (rc != ERROR_SUCCESS) {
    throw_ioe(env, rc);
    goto cleanup;
  }

  jstr_owner = (*env)->NewString(env, owner, (jsize) wcslen(owner));
  if (jstr_owner == NULL) goto cleanup;

  jstr_group = (*env)->NewString(env, group, (jsize) wcslen(group));;
  if (jstr_group == NULL) goto cleanup;

  ret = (*env)->NewObject(env, stat_clazz, stat_ctor2,
    jstr_owner, jstr_group, (jint)mode);

cleanup:
  if (ret == NULL) {
    if (jstr_owner != NULL)
      (*env)->ReleaseStringChars(env, jstr_owner, owner);

    if (jstr_group != NULL)
      (*env)->ReleaseStringChars(env, jstr_group, group);
  }

  LocalFree(owner);
  LocalFree(group);

  return ret;
#endif
}



/**
 * public static native void posix_fadvise(
 *   FileDescriptor fd, long offset, long len, int flags);
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_posix_1fadvise(
  JNIEnv *env, jclass clazz,
  jobject fd_object, jlong offset, jlong len, jint flags)
{
#ifndef HAVE_POSIX_FADVISE
  THROW(env, "java/lang/UnsupportedOperationException",
        "fadvise support not available");
#else
  int fd = fd_get(env, fd_object);
  PASS_EXCEPTIONS(env);

  int err = 0;
  if ((err = posix_fadvise(fd, (off_t)offset, (off_t)len, map_fadvise_flag(flags)))) {
#ifdef __FreeBSD__
    throw_ioe(env, errno);
#else
    throw_ioe(env, err);
#endif
  }
#endif
}

#if defined(HAVE_SYNC_FILE_RANGE)
#  define my_sync_file_range sync_file_range
#elif defined(SYS_sync_file_range)
// RHEL 5 kernels have sync_file_range support, but the glibc
// included does not have the library function. We can
// still call it directly, and if it's not supported by the
// kernel, we'd get ENOSYS. See RedHat Bugzilla #518581
static int manual_sync_file_range (int fd, __off64_t from, __off64_t to, unsigned int flags)
{
#ifdef __x86_64__
  return syscall( SYS_sync_file_range, fd, from, to, flags);
#else
  return syscall (SYS_sync_file_range, fd,
    __LONG_LONG_PAIR ((long) (from >> 32), (long) from),
    __LONG_LONG_PAIR ((long) (to >> 32), (long) to),
    flags);
#endif
}
#define my_sync_file_range manual_sync_file_range
#endif

/**
 * public static native void sync_file_range(
 *   FileDescriptor fd, long offset, long len, int flags);
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_sync_1file_1range(
  JNIEnv *env, jclass clazz,
  jobject fd_object, jlong offset, jlong len, jint flags)
{
#ifndef my_sync_file_range
  THROW(env, "java/lang/UnsupportedOperationException",
        "sync_file_range support not available");
#else
  int fd = fd_get(env, fd_object);
  PASS_EXCEPTIONS(env);

  if (my_sync_file_range(fd, (off_t)offset, (off_t)len, flags)) {
    if (errno == ENOSYS) {
      // we know the syscall number, but it's not compiled
      // into the running kernel
      THROW(env, "java/lang/UnsupportedOperationException",
            "sync_file_range kernel support not available");
      return;
    } else {
      throw_ioe(env, errno);
    }
  }
#endif
}

#define CHECK_DIRECT_BUFFER_ADDRESS(buf) \
  { \
    if (!buf) { \
      THROW(env, "java/lang/UnsupportedOperationException", \
        "JNI access to direct buffers not available"); \
      return; \
    } \
  }

/**
 * public static native void mlock_native(
 *   ByteBuffer buffer, long offset);
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_mlock_1native(
  JNIEnv *env, jclass clazz,
  jobject buffer, jlong len)
{
  void* buf = (void*)(*env)->GetDirectBufferAddress(env, buffer);
  PASS_EXCEPTIONS(env);

#ifdef UNIX
  if (mlock(buf, len)) {
    CHECK_DIRECT_BUFFER_ADDRESS(buf);
    throw_ioe(env, errno);
  }
#endif

#ifdef WINDOWS
  if (!VirtualLock(buf, len)) {
    CHECK_DIRECT_BUFFER_ADDRESS(buf);
    throw_ioe(env, GetLastError());
  }
#endif
}

#ifdef __FreeBSD__
static int toFreeBSDFlags(int flags)
{
  int rc = flags & 03;
  if ( flags &  0100 ) rc |= O_CREAT;
  if ( flags &  0200 ) rc |= O_EXCL;
  if ( flags &  0400 ) rc |= O_NOCTTY;
  if ( flags & 01000 ) rc |= O_TRUNC;
  if ( flags & 02000 ) rc |= O_APPEND;
  if ( flags & 04000 ) rc |= O_NONBLOCK;
  if ( flags &010000 ) rc |= O_SYNC;
  if ( flags &020000 ) rc |= O_ASYNC;
  return rc;
}
#endif

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_POSIX
 * Method:    open
 * Signature: (Ljava/lang/String;II)Ljava/io/FileDescriptor;
 * public static native FileDescriptor open(String path, int flags, int mode);
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT jobject JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_open(
  JNIEnv *env, jclass clazz, jstring j_path,
  jint flags, jint mode)
{
#ifdef UNIX
#ifdef __FreeBSD__
  flags = toFreeBSDFlags(flags);
#endif
  jobject ret = NULL;

  const char *path = (*env)->GetStringUTFChars(env, j_path, NULL);
  if (path == NULL) goto cleanup; // JVM throws Exception for us

  int fd;  
  if (flags & O_CREAT) {
    fd = open(path, flags, mode);
  } else {
    fd = open(path, flags);
  }

  if (fd == -1) {
    throw_ioe(env, errno);
    goto cleanup;
  }

  ret = fd_create(env, fd);

cleanup:
  if (path != NULL) {
    (*env)->ReleaseStringUTFChars(env, j_path, path);
  }
  return ret;
#endif

#ifdef WINDOWS
  THROW(env, "java/io/IOException",
    "The function POSIX.open() is not supported on Windows");
  return NULL;
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_Windows
 * Method:    createDirectoryWithMode0
 * Signature: (Ljava/lang/String;I)V
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT void JNICALL
  Java_org_apache_hadoop_io_nativeio_NativeIO_00024Windows_createDirectoryWithMode0
  (JNIEnv *env, jclass clazz, jstring j_path, jint mode)
{
#ifdef WINDOWS
  DWORD dwRtnCode = ERROR_SUCCESS;

  LPCWSTR path = (LPCWSTR) (*env)->GetStringChars(env, j_path, NULL);
  if (!path) {
    goto done;
  }

  dwRtnCode = CreateDirectoryWithMode(path, mode);

done:
  if (path) {
    (*env)->ReleaseStringChars(env, j_path, (const jchar*) path);
  }
  if (dwRtnCode != ERROR_SUCCESS) {
    throw_ioe(env, dwRtnCode);
  }
#else
  THROW(env, "java/io/IOException",
    "The function Windows.createDirectoryWithMode0() is not supported on this platform");
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_Windows
 * Method:    createFileWithMode0
 * Signature: (Ljava/lang/String;JJJI)Ljava/io/FileDescriptor;
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT jobject JNICALL
  Java_org_apache_hadoop_io_nativeio_NativeIO_00024Windows_createFileWithMode0
  (JNIEnv *env, jclass clazz, jstring j_path,
  jlong desiredAccess, jlong shareMode, jlong creationDisposition, jint mode)
{
#ifdef WINDOWS
  DWORD dwRtnCode = ERROR_SUCCESS;
  HANDLE hFile = INVALID_HANDLE_VALUE;
  jobject fd = NULL;

  LPCWSTR path = (LPCWSTR) (*env)->GetStringChars(env, j_path, NULL);
  if (!path) {
    goto done;
  }

  dwRtnCode = CreateFileWithMode(path, desiredAccess, shareMode,
      creationDisposition, mode, &hFile);
  if (dwRtnCode != ERROR_SUCCESS) {
    goto done;
  }

  fd = fd_create(env, (long) hFile);

done:
  if (path) {
    (*env)->ReleaseStringChars(env, j_path, (const jchar*) path);
  }
  if (dwRtnCode != ERROR_SUCCESS) {
    throw_ioe(env, dwRtnCode);
  }
  return fd;
#else
  THROW(env, "java/io/IOException",
    "The function Windows.createFileWithMode0() is not supported on this platform");
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_Windows
 * Method:    createFile
 * Signature: (Ljava/lang/String;JJJ)Ljava/io/FileDescriptor;
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT jobject JNICALL Java_org_apache_hadoop_io_nativeio_NativeIO_00024Windows_createFile
  (JNIEnv *env, jclass clazz, jstring j_path,
  jlong desiredAccess, jlong shareMode, jlong creationDisposition)
{
#ifdef UNIX
  THROW(env, "java/io/IOException",
    "The function Windows.createFile() is not supported on Unix");
  return NULL;
#endif

#ifdef WINDOWS
  DWORD dwRtnCode = ERROR_SUCCESS;
  BOOL isSymlink = FALSE;
  BOOL isJunction = FALSE;
  DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS;
  jobject ret = (jobject) NULL;
  HANDLE hFile = INVALID_HANDLE_VALUE;
  WCHAR *path = (WCHAR *) (*env)->GetStringChars(env, j_path, (jboolean*)NULL);
  if (path == NULL) goto cleanup;

  // Set the flag for a symbolic link or a junctions point only when it exists.
  // According to MSDN if the call to CreateFile() function creates a file,
  // there is no change in behavior. So we do not throw if no file is found.
  //
  dwRtnCode = SymbolicLinkCheck(path, &isSymlink);
  if (dwRtnCode != ERROR_SUCCESS && dwRtnCode != ERROR_FILE_NOT_FOUND) {
    throw_ioe(env, dwRtnCode);
    goto cleanup;
  }
  dwRtnCode = JunctionPointCheck(path, &isJunction);
  if (dwRtnCode != ERROR_SUCCESS && dwRtnCode != ERROR_FILE_NOT_FOUND) {
    throw_ioe(env, dwRtnCode);
    goto cleanup;
  }
  if (isSymlink || isJunction)
    dwFlagsAndAttributes |= FILE_FLAG_OPEN_REPARSE_POINT;

  hFile = CreateFile(path,
    (DWORD) desiredAccess,
    (DWORD) shareMode,
    (LPSECURITY_ATTRIBUTES ) NULL,
    (DWORD) creationDisposition,
    dwFlagsAndAttributes,
    NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    throw_ioe(env, GetLastError());
    goto cleanup;
  }

  ret = fd_create(env, (long) hFile);
cleanup:
  if (path != NULL) {
    (*env)->ReleaseStringChars(env, j_path, (const jchar*)path);
  }
  return (jobject) ret;
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_POSIX
 * Method:    chmod
 * Signature: (Ljava/lang/String;I)V
 */
JNIEXPORT void JNICALL Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_chmodImpl
  (JNIEnv *env, jclass clazz, jstring j_path, jint mode)
{
#ifdef UNIX
  const char *path = (*env)->GetStringUTFChars(env, j_path, NULL);
  if (path == NULL) return; // JVM throws Exception for us

  if (chmod(path, mode) != 0) {
    throw_ioe(env, errno);
  }

  (*env)->ReleaseStringUTFChars(env, j_path, path);
#endif

#ifdef WINDOWS
  DWORD dwRtnCode = ERROR_SUCCESS;
  LPCWSTR path = (LPCWSTR) (*env)->GetStringChars(env, j_path, NULL);
  if (path == NULL) return; // JVM throws Exception for us

  if ((dwRtnCode = ChangeFileModeByMask((LPCWSTR) path, mode)) != ERROR_SUCCESS)
  {
    throw_ioe(env, dwRtnCode);
  }

  (*env)->ReleaseStringChars(env, j_path, (const jchar*) path);
#endif
}

/*
 * static native String getUserName(int uid);
 */
JNIEXPORT jstring JNICALL 
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_getUserName(
  JNIEnv *env, jclass clazz, jint uid)
{
#ifdef UNIX
  jstring jstr_username = NULL;
  char *pw_buf = NULL;
  int pw_lock_locked = 0;
  if (pw_lock_object != NULL) {
    if ((*env)->MonitorEnter(env, pw_lock_object) != JNI_OK) {
      goto cleanup;
    }
    pw_lock_locked = 1;
  }

  int rc;
  size_t pw_buflen = get_pw_buflen();
  if ((pw_buf = malloc(pw_buflen)) == NULL) {
    THROW(env, "java/lang/OutOfMemoryError", "Couldn't allocate memory for pw buffer");
    goto cleanup;
  }

  // Grab username
  struct passwd pwd, *pwdp;
  while ((rc = getpwuid_r((uid_t)uid, &pwd, pw_buf, pw_buflen, &pwdp)) != 0) {
    if (rc != ERANGE) {
      throw_ioe(env, rc);
      goto cleanup;
    }
    free(pw_buf);
    pw_buflen *= 2;
    if ((pw_buf = malloc(pw_buflen)) == NULL) {
      THROW(env, "java/lang/OutOfMemoryError", "Couldn't allocate memory for pw buffer");
      goto cleanup;
    }
  }
  if (pwdp == NULL) {
    char msg[80];
    snprintf(msg, sizeof(msg), "uid not found: %d", uid);
    THROW(env, "java/io/IOException", msg);
    goto cleanup;
  }
  if (pwdp != &pwd) {
    char msg[80];
    snprintf(msg, sizeof(msg), "pwd pointer inconsistent with reference. uid: %d", uid);
    THROW(env, "java/lang/IllegalStateException", msg);
    goto cleanup;
  }

  jstr_username = (*env)->NewStringUTF(env, pwd.pw_name);

cleanup:
  if (pw_lock_locked) {
    (*env)->MonitorExit(env, pw_lock_object);
  }
  if (pw_buf != NULL) free(pw_buf);
  return jstr_username;
#endif // UNIX

#ifdef WINDOWS
  THROW(env, "java/io/IOException",
    "The function POSIX.getUserName() is not supported on Windows");
  return NULL;
#endif
}

JNIEXPORT jlong JNICALL 
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_mmap(
  JNIEnv *env, jclass clazz, jobject jfd, jint jprot,
  jboolean jshared, jlong length)
{
#ifdef UNIX
  void *addr = 0;
  int prot, flags, fd;
  
  prot = ((jprot & MMAP_PROT_READ) ? PROT_READ : 0) |
         ((jprot & MMAP_PROT_WRITE) ? PROT_WRITE : 0) |
         ((jprot & MMAP_PROT_EXEC) ? PROT_EXEC : 0);
  flags = (jshared == JNI_TRUE) ? MAP_SHARED : MAP_PRIVATE;
  fd = fd_get(env, jfd);
  addr = mmap(NULL, length, prot, flags, fd, 0);
  if (addr == MAP_FAILED) {
    throw_ioe(env, errno);
  }
  return (jlong)(intptr_t)addr;
#endif  //   UNIX

#ifdef WINDOWS
  THROW(env, "java/io/IOException",
    "The function POSIX.mmap() is not supported on Windows");
  return (jlong)(intptr_t)NULL;
#endif
}

JNIEXPORT void JNICALL 
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_munmap(
  JNIEnv *env, jclass clazz, jlong jaddr, jlong length)
{
#ifdef UNIX
  void *addr;

  addr = (void*)(intptr_t)jaddr;
  if (munmap(addr, length) < 0) {
    throw_ioe(env, errno);
  }
#endif  //   UNIX

#ifdef WINDOWS
  THROW(env, "java/io/IOException",
    "The function POSIX.munmap() is not supported on Windows");
#endif
}


/*
 * static native String getGroupName(int gid);
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT jstring JNICALL 
Java_org_apache_hadoop_io_nativeio_NativeIO_00024POSIX_getGroupName(
  JNIEnv *env, jclass clazz, jint gid)
{
#ifdef UNIX
  jstring jstr_groupname = NULL;
  char *pw_buf = NULL;
  int pw_lock_locked = 0;
 
  if (pw_lock_object != NULL) {
    if ((*env)->MonitorEnter(env, pw_lock_object) != JNI_OK) {
      goto cleanup;
    }
    pw_lock_locked = 1;
  }
  
  int rc;
  size_t pw_buflen = get_pw_buflen();
  if ((pw_buf = malloc(pw_buflen)) == NULL) {
    THROW(env, "java/lang/OutOfMemoryError", "Couldn't allocate memory for pw buffer");
    goto cleanup;
  }
  
  // Grab group
  struct group grp, *grpp;
  while ((rc = getgrgid_r((uid_t)gid, &grp, pw_buf, pw_buflen, &grpp)) != 0) {
    if (rc != ERANGE) {
      throw_ioe(env, rc);
      goto cleanup;
    }
    free(pw_buf);
    pw_buflen *= 2;
    if ((pw_buf = malloc(pw_buflen)) == NULL) {
      THROW(env, "java/lang/OutOfMemoryError", "Couldn't allocate memory for pw buffer");
      goto cleanup;
    }
  }
  if (grpp == NULL) {
    char msg[80];
    snprintf(msg, sizeof(msg), "gid not found: %d", gid);
    THROW(env, "java/io/IOException", msg);
    goto cleanup;
  }
  if (grpp != &grp) {
    char msg[80];
    snprintf(msg, sizeof(msg), "pwd pointer inconsistent with reference. gid: %d", gid);
    THROW(env, "java/lang/IllegalStateException", msg);
    goto cleanup;
  }

  jstr_groupname = (*env)->NewStringUTF(env, grp.gr_name);
  PASS_EXCEPTIONS_GOTO(env, cleanup);
  
cleanup:
  if (pw_lock_locked) {
    (*env)->MonitorExit(env, pw_lock_object);
  }
  if (pw_buf != NULL) free(pw_buf);
  return jstr_groupname;
#endif  //   UNIX

#ifdef WINDOWS
  THROW(env, "java/io/IOException",
    "The function POSIX.getUserName() is not supported on Windows");
  return NULL;
#endif
}

/*
 * Throw a java.IO.IOException, generating the message from errno.
 */
void throw_ioe(JNIEnv* env, int errnum)
{
#ifdef UNIX
  char message[80];
  jstring jstr_message;

  snprintf(message,sizeof(message),"%s",terror(errnum));

  jobject errno_obj = errno_to_enum(env, errnum);

  if ((jstr_message = (*env)->NewStringUTF(env, message)) == NULL)
    goto err;

  jthrowable obj = (jthrowable)(*env)->NewObject(env, nioe_clazz, nioe_ctor,
    jstr_message, errno_obj);
  if (obj == NULL) goto err;

  (*env)->Throw(env, obj);
  return;

err:
  if (jstr_message != NULL)
    (*env)->ReleaseStringUTFChars(env, jstr_message, message);
#endif

#ifdef WINDOWS
  DWORD len = 0;
  LPWSTR buffer = NULL;
  const jchar* message = NULL;
  jstring jstr_message = NULL;
  jthrowable obj = NULL;

  len = FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
    NULL, *(DWORD*) (&errnum), // reinterpret cast
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPWSTR) &buffer, 0, NULL);

  if (len > 0)
  {
    message = (const jchar*) buffer;
  }
  else
  {
    message = (const jchar*) L"Unknown error.";
  }

  if ((jstr_message = (*env)->NewString(env, message, len)) == NULL)
    goto err;
  LocalFree(buffer);
  buffer = NULL; // Set buffer to NULL to avoid double free

  obj = (jthrowable)(*env)->NewObject(env, nioe_clazz, nioe_ctor,
    jstr_message, errnum);
  if (obj == NULL) goto err;

  (*env)->Throw(env, obj);
  return;

err:
  if (jstr_message != NULL)
    (*env)->ReleaseStringChars(env, jstr_message, message);
  LocalFree(buffer);
  return;
#endif
}

#ifdef UNIX
/*
 * Determine how big a buffer we need for reentrant getpwuid_r and getgrnam_r
 */
ssize_t get_pw_buflen() {
  long ret = 0;
  #ifdef _SC_GETPW_R_SIZE_MAX
  ret = sysconf(_SC_GETPW_R_SIZE_MAX);
  #endif
  return (ret > 512) ? ret : 512;
}
#endif


/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_Windows
 * Method:    getOwnerOnWindows
 * Signature: (Ljava/io/FileDescriptor;)Ljava/lang/String;
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT jstring JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024Windows_getOwner
  (JNIEnv *env, jclass clazz, jobject fd_object)
{
#ifdef UNIX
  THROW(env, "java/io/IOException",
    "The function Windows.getOwner() is not supported on Unix");
  return NULL;
#endif

#ifdef WINDOWS
  PSID pSidOwner = NULL;
  PSECURITY_DESCRIPTOR pSD = NULL;
  LPWSTR ownerName = (LPWSTR)NULL;
  DWORD dwRtnCode = ERROR_SUCCESS;
  jstring jstr_username = NULL;
  HANDLE hFile = (HANDLE) fd_get(env, fd_object);
  PASS_EXCEPTIONS_GOTO(env, cleanup);

  dwRtnCode = GetSecurityInfo(
    hFile,
    SE_FILE_OBJECT,
    OWNER_SECURITY_INFORMATION,
    &pSidOwner,
    NULL,
    NULL,
    NULL,
    &pSD);
  if (dwRtnCode != ERROR_SUCCESS) {
    throw_ioe(env, dwRtnCode);
    goto cleanup;
  }

  dwRtnCode = GetAccntNameFromSid(pSidOwner, &ownerName);
  if (dwRtnCode != ERROR_SUCCESS) {
    throw_ioe(env, dwRtnCode);
    goto cleanup;
  }

  jstr_username = (*env)->NewString(env, ownerName, (jsize) wcslen(ownerName));
  if (jstr_username == NULL) goto cleanup;

cleanup:
  LocalFree(ownerName);
  LocalFree(pSD);
  return jstr_username;
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_Windows
 * Method:    setFilePointer
 * Signature: (Ljava/io/FileDescriptor;JJ)J
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024Windows_setFilePointer
  (JNIEnv *env, jclass clazz, jobject fd_object, jlong distanceToMove, jlong moveMethod)
{
#ifdef UNIX
  THROW(env, "java/io/IOException",
    "The function setFilePointer(FileDescriptor) is not supported on Unix");
  return (jlong)(intptr_t)NULL;
#endif

#ifdef WINDOWS
  DWORD distanceToMoveLow = (DWORD) distanceToMove;
  LONG distanceToMoveHigh = (LONG) (distanceToMove >> 32);
  DWORD distanceMovedLow = 0;
  HANDLE hFile = (HANDLE) fd_get(env, fd_object);
  PASS_EXCEPTIONS_GOTO(env, cleanup);

  distanceMovedLow = SetFilePointer(hFile,
    distanceToMoveLow, &distanceToMoveHigh, (DWORD) moveMethod);

  if (distanceMovedLow == INVALID_SET_FILE_POINTER) {
     throw_ioe(env, GetLastError());
     return -1;
  }

cleanup:

  return ((jlong) distanceToMoveHigh << 32) | (jlong) distanceMovedLow;
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_Windows
 * Method:    access0
 * Signature: (Ljava/lang/String;I)Z
 */
JNIEXPORT jboolean JNICALL Java_org_apache_hadoop_io_nativeio_NativeIO_00024Windows_access0
  (JNIEnv *env, jclass clazz, jstring jpath, jint jaccess)
{
#ifdef UNIX
  THROW(env, "java/io/IOException",
    "The function access0(path, access) is not supported on Unix");
  return (jlong)(intptr_t)NULL;
#endif

#ifdef WINDOWS
  LPCWSTR path = NULL;
  DWORD dwRtnCode = ERROR_SUCCESS;
  ACCESS_MASK access = (ACCESS_MASK)jaccess;
  BOOL allowed = FALSE;

  path = (LPCWSTR) (*env)->GetStringChars(env, jpath, NULL);
  if (!path) goto cleanup; // exception was thrown

  dwRtnCode = CheckAccessForCurrentUser(path, access, &allowed);
  if (dwRtnCode != ERROR_SUCCESS) {
    throw_ioe(env, dwRtnCode);
    goto cleanup;
  }

cleanup:
  if (path) (*env)->ReleaseStringChars(env, jpath, path);

  return (jboolean)allowed;
#endif
}

/*
 * Class:     org_apache_hadoop_io_nativeio_NativeIO_Windows
 * Method:    extendWorkingSetSize
 * Signature: (J)V
 *
 * The "00024" in the function name is an artifact of how JNI encodes
 * special characters. U+0024 is '$'.
 */
JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_00024Windows_extendWorkingSetSize(
  JNIEnv *env, jclass clazz, jlong delta)
{
#ifdef UNIX
  THROW(env, "java/io/IOException",
    "The function extendWorkingSetSize(delta) is not supported on Unix");
#endif

#ifdef WINDOWS
  SIZE_T min, max;
  HANDLE hProcess = GetCurrentProcess();
  if (!GetProcessWorkingSetSize(hProcess, &min, &max)) {
    throw_ioe(env, GetLastError());
    return;
  }
  if (!SetProcessWorkingSetSizeEx(hProcess, min + delta, max + delta,
      QUOTA_LIMITS_HARDWS_MIN_DISABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE)) {
    throw_ioe(env, GetLastError());
    return;
  }
  // There is no need to call CloseHandle on the pseudo-handle returned from
  // GetCurrentProcess.
#endif
}

JNIEXPORT void JNICALL 
Java_org_apache_hadoop_io_nativeio_NativeIO_renameTo0(JNIEnv *env, 
jclass clazz, jstring jsrc, jstring jdst)
{
#ifdef UNIX
  const char *src = NULL, *dst = NULL;
  
  src = (*env)->GetStringUTFChars(env, jsrc, NULL);
  if (!src) goto done; // exception was thrown
  dst = (*env)->GetStringUTFChars(env, jdst, NULL);
  if (!dst) goto done; // exception was thrown
  if (rename(src, dst)) {
    throw_ioe(env, errno);
  }

done:
  if (src) (*env)->ReleaseStringUTFChars(env, jsrc, src);
  if (dst) (*env)->ReleaseStringUTFChars(env, jdst, dst);
#endif

#ifdef WINDOWS
  LPCWSTR src = NULL, dst = NULL;

  src = (LPCWSTR) (*env)->GetStringChars(env, jsrc, NULL);
  if (!src) goto done; // exception was thrown
  dst = (LPCWSTR) (*env)->GetStringChars(env, jdst, NULL);
  if (!dst) goto done; // exception was thrown
  if (!MoveFile(src, dst)) {
    throw_ioe(env, GetLastError());
  }

done:
  if (src) (*env)->ReleaseStringChars(env, jsrc, src);
  if (dst) (*env)->ReleaseStringChars(env, jdst, dst);
#endif
}

JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_link0(JNIEnv *env,
jclass clazz, jstring jsrc, jstring jdst)
{
#ifdef UNIX
  const char *src = NULL, *dst = NULL;

  src = (*env)->GetStringUTFChars(env, jsrc, NULL);
  if (!src) goto done; // exception was thrown
  dst = (*env)->GetStringUTFChars(env, jdst, NULL);
  if (!dst) goto done; // exception was thrown
  if (link(src, dst)) {
    throw_ioe(env, errno);
  }

done:
  if (src) (*env)->ReleaseStringUTFChars(env, jsrc, src);
  if (dst) (*env)->ReleaseStringUTFChars(env, jdst, dst);
#endif

#ifdef WINDOWS
  LPCTSTR src = NULL, dst = NULL;

  src = (LPCTSTR) (*env)->GetStringChars(env, jsrc, NULL);
  if (!src) goto done; // exception was thrown
  dst = (LPCTSTR) (*env)->GetStringChars(env, jdst, NULL);
  if (!dst) goto done; // exception was thrown
  if (!CreateHardLink(dst, src, NULL)) {
    throw_ioe(env, GetLastError());
  }

done:
  if (src) (*env)->ReleaseStringChars(env, jsrc, src);
  if (dst) (*env)->ReleaseStringChars(env, jdst, dst);
#endif
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_getMemlockLimit0(
JNIEnv *env, jclass clazz)
{
#ifdef WINDOWS
  return 0;
#else
  struct rlimit rlim;
  int rc = getrlimit(RLIMIT_MEMLOCK, &rlim);
  if (rc != 0) {
    throw_ioe(env, errno);
    return 0;
  }
  return (rlim.rlim_cur == RLIM_INFINITY) ?
    INT64_MAX : rlim.rlim_cur;
#endif
}

JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_copyFileUnbuffered0(
JNIEnv *env, jclass clazz, jstring jsrc, jstring jdst)
{
#ifdef UNIX
  THROW(env, "java/lang/UnsupportedOperationException",
    "The function copyFileUnbuffered0 should not be used on Unix. Use FileChannel#transferTo instead.");
#endif

#ifdef WINDOWS
  LPCWSTR src = NULL, dst = NULL;

  src = (LPCWSTR) (*env)->GetStringChars(env, jsrc, NULL);
  if (!src) goto cleanup; // exception was thrown
  dst = (LPCWSTR) (*env)->GetStringChars(env, jdst, NULL);
  if (!dst) goto cleanup; // exception was thrown
  if (!CopyFileEx(src, dst, NULL, NULL, NULL, COPY_FILE_NO_BUFFERING)) {
    throw_ioe(env, GetLastError());
  }

cleanup:
  if (src) (*env)->ReleaseStringChars(env, jsrc, src);
  if (dst) (*env)->ReleaseStringChars(env, jdst, dst);
#endif
}

/* test for editLog JNI
 */

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_ReturnNVRAMAddress(JNIEnv *env, jclass clazz, jlong size, jlong offset)
{
  P_STATUS status;
  NVRAM_card_s card_list[1];
  uint16_t card_list_count;
  uint16_t card_total_count;
  dev_handle_t dev_handle;
  volatile void * dmiBuffer;

  status = PMC_NVRAM_card_list_get(card_list, 1, &card_list_count, &card_total_count);
  status = PMC_NVRAM_init(card_list[0].card_uid, &dev_handle);

  status = PMC_NVRAM_mem_map(dev_handle, size,
		  NVRAM_MEM_MAP_FLAGS_DATA_WRITE | NVRAM_MEM_MAP_FLAGS_DATA_READ, (void **) &dmiBuffer, offset);

  return (jlong)(intptr_t)dmiBuffer;
  //return directBuffer;
}

JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_munmap(JNIEnv *env, jclass clazz, jlong size, jlong offset)
{
	munmap(offset, size);
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putLongTest(JNIEnv * env, jclass clazz, jlongArray addr, jlong data, jint index)
{
//	  void * dmiBuffer;
//	  dmiBuffer = (void *) addr;
//		FILE * fp;
//		fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];
	  int cal_index = index % NVM_GRAN;

//	  fprintf(fp, "success quo :%d\n" , quo);
//	  fprintf(fp, "success cal_index :%d\n" , cal_index);
//	  fprintf(fp, "success dmiBuffer :%ld\n" , nvram_addr[quo]);
//	  fprintf(fp, "success data : %ld\n" , data);
//	  fclose(fp);

	  memcpy(dmiBuffer + cal_index, &data, sizeof(data));
	  (*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  /*test*/
	  return (index + sizeof(data));
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putLongTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jlong data, jlong index)
{
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = (int) (index / NVM_GRAN);
	  void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];
	  int cal_index = (int) (index % NVM_GRAN);


	  memcpy(dmiBuffer + cal_index, &data, sizeof(data));
	  (*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  /*test*/
	  return (jlong)(index + sizeof(data));
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putBlockTest(JNIEnv * env, jclass clazz,
		jlongArray addr, jlong id, jlong genstamp, jint index)
{
//	  void * dmiBuffer;
//	  dmiBuffer = (void *) addr;
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

	  memcpy(dmiBuffer + cal_index, &id, sizeof(id));
	  //memcpy(dmiBuffer + cal_index + sizeof(id), &numbyte, sizeof(numbyte));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + 8, &genstamp, sizeof(genstamp));

	  (*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  /*test*/
	  return (index + sizeof(id) + 8 + sizeof(genstamp));
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putBlockTestLong(JNIEnv * env, jclass clazz,
		jlongArray addr, jlong id, jlong genstamp, jlong index)
{
//	  void * dmiBuffer;
//	  dmiBuffer = (void *) addr;
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = (int) (index / NVM_GRAN);
	  int cal_index = (int) (index % NVM_GRAN);
	  void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];

	  memcpy(dmiBuffer + cal_index, &id, sizeof(id));
	  //memcpy(dmiBuffer + cal_index + sizeof(id), &numbyte, sizeof(numbyte));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + 8, &genstamp, sizeof(genstamp));

	  (*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  /*test*/
	  return (jlong)(index + sizeof(id) + 8 + sizeof(genstamp));
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putBlockChunkTest(JNIEnv * env, jclass clazz,
		jlongArray addr, jbyteArray block, jint index)
{
//	  void * dmiBuffer;
//	  dmiBuffer = (void *) addr;
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

		int remains = 24;
	  //int len = (*env)->GetArrayLength(env, meta);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, block, NULL);

		char * dest = (char *)( dmiBuffer + cal_index );
		char * src = (char *) nativeBytes;

		while(remains >= 16) {
			storeu_16bytes((void *)dest , (void *)src);
			dest += 16;
			src += 16;
			remains-= 16;
		}
		if(remains !=0) {
			memcpy(dest, src, remains);
		}
		(*env)->ReleaseByteArrayElements(env, block, nativeBytes, 0);
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  return (index + 24);
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putChildrenInDirectory(JNIEnv * env, jclass clazz, jlongArray addr, jint target_offset,
		 jint directory_location, jlong inodeNum)
{
//	  void * dmiBuffer;
//	  dmiBuffer = (void *) addr;
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = directory_location / NVM_GRAN;
	  int cal_directory_location = directory_location % NVM_GRAN;

	  void * dmiBuffer = (void *) nvram_addr[quo];
	  void * dmiBuffer_init = (void *) nvram_addr[0];
	  int child_num;

		int next_dir;
		int success = 1;
		int inode_num = inodeNum;

	  memcpy(&child_num, dmiBuffer + cal_directory_location + 4092 - 4  , sizeof(child_num));
		if (child_num == 787) {
			memcpy(&next_dir, dmiBuffer + cal_directory_location + 4092 - 8 , sizeof(next_dir));
			if (next_dir == 0) {
				//int inode_num = 0;
				//memcpy(&inode_num, dmiBuffer_init, sizeof(inode_num));
				inode_num = inode_num + 1;
				//memcpy(dmiBuffer_init, &inode_num, sizeof(inode_num));
				next_dir = 4096 + 4096 * (inode_num - 1);
				memcpy(dmiBuffer + cal_directory_location + 4092 - 8, &next_dir, sizeof(next_dir));
			}
			inode_num = addChildrenInDirectory(nvram_addr, next_dir, target_offset, inode_num);
		} else {
			int next_location = 4 * (child_num);
			memcpy(dmiBuffer + cal_directory_location + 936 + next_location , &target_offset, sizeof(target_offset));
			child_num = child_num + 1;
			memcpy(dmiBuffer + cal_directory_location + 4092 - 4 , &child_num, sizeof(child_num));
		}
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  return inode_num;
}

//JNIEXPORT jint JNICALL
//Java_org_apache_hadoop_io_nativeio_NativeIO_putChildrenInDirectoryFast(JNIEnv * env, jclass clazz, jlongArray addr, jint target_offset,
//		 jint directory_location, jlong inodeNum)
//{
//
//	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
//	  int quo = directory_location / NVM_GRAN;
//	  int cal_directory_location = directory_location % NVM_GRAN;
//
//	  void * dmiBuffer = (void *) nvram_addr[quo];
//	  void * dmiBuffer_init = (void *) nvram_addr[0];
//	  int child_num;
//
//		int next_dir;
//		int success = 1;
//		int inode_num = inodeNum;
//		int init_location= directory_location;
//		int last_page = 0;
//
//		/* pick first page's last page info*/
//		memcpy(&last_page, dmiBuffer + cal_directory_location + 4092 - 12, sizeof(last_page));
//
//		if (last_page == 0) {
//			memcpy(&child_num, dmiBuffer + cal_directory_location + 4092 - 4, sizeof(child_num));
//			if (child_num == 782) {
//			memcpy(&next_dir, dmiBuffer + cal_directory_location + 4092 - 8,
//					sizeof(next_dir));
//			if (next_dir == 0) {
//				inode_num = inode_num + 1;
//				next_dir = 4096 + 4096 * (inode_num - 1);
//				memcpy(dmiBuffer + cal_directory_location + 4092 - 8, &next_dir,
//						sizeof(next_dir));
//				/* last page info update */
//				memcpy(dmiBuffer + cal_directory_location + 4092 - 12,
//						&next_dir, sizeof(next_dir));
//			}
//			inode_num = addChildrenInDirectoryFast(nvram_addr, next_dir, target_offset, init_location, inode_num);
//			//inode_num = addChildrenInDirectoryFast(nvram_addr, next_dir, target_offset, init_location, inode_num + 1);
//		}  else { // last page = 0 & child_num < 786
//			int next_location = 4 * (child_num);
//			memcpy(dmiBuffer + cal_directory_location + 952 + next_location,
//					&target_offset, sizeof(target_offset));
//			child_num = child_num + 1;
//			memcpy(dmiBuffer + cal_directory_location + 4092 - 4, &child_num,
//					sizeof(child_num));
//		}
//	} else {
//		inode_num = addChildrenInDirectoryFast(nvram_addr, last_page, target_offset, init_location, inode_num);
//	}
//
//
//		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
//	  return inode_num;
//}

//JNIEXPORT jint JNICALL
//Java_org_apache_hadoop_io_nativeio_NativeIO_putChildrenInDirectoryFast(JNIEnv * env, jclass clazz, jlongArray addr, jint target_offset,
//		 jint directory_location, jlong inodeNum)
//{
//
//	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
//	  int quo = directory_location / NVM_GRAN;
//	  int cal_directory_location = directory_location % NVM_GRAN;
//
//	  void * dmiBuffer = (void *) nvram_addr[quo];
//	  void * dmiBuffer_init = (void *) nvram_addr[0];
//	  int child_num;
//
//		int next_dir;
//		int success = 1;
//		int inode_num = inodeNum;
//		int init_location= directory_location;
//		int last_page = 0;
//
//		/* pick first page's last page info*/
//		memcpy(&last_page, dmiBuffer + cal_directory_location + 4092 - 12, sizeof(last_page));
//
//		if (last_page == 0) {
//			memcpy(&child_num, dmiBuffer + cal_directory_location + 4092 - 4, sizeof(child_num));
//			if (child_num == 782) {
//				inode_num = inode_num + 1;
//				next_dir = 4096 + 4096 * (inode_num - 1);
//				memcpy(dmiBuffer + cal_directory_location + 4092 - 8, &next_dir,
//						sizeof(next_dir));
//				/* last page info update */
//				memcpy(dmiBuffer + cal_directory_location + 4092 - 12,
//						&next_dir, sizeof(next_dir));
//
//				inode_num = addChildrenInDirectoryFast(nvram_addr, next_dir, target_offset, init_location, inode_num);
//		}  else { // last page = 0 & child_num < 786
//			int next_location = 4 * (child_num);
//			memcpy(dmiBuffer + cal_directory_location + 952 + next_location,
//					&target_offset, sizeof(target_offset));
//			child_num = child_num + 1;
//			memcpy(dmiBuffer + cal_directory_location + 4092 - 4, &child_num,
//					sizeof(child_num));
//		}
//	} else {
//		inode_num = addChildrenInDirectoryFast(nvram_addr, last_page, target_offset, init_location, inode_num);
//	}
//
//		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
//	  return inode_num;
//}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putChildrenInDirectoryFast(JNIEnv * env, jclass clazz, jlongArray addr, jint target_offset,
		 jint directory_location, jlong inodeNum)
{

	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = directory_location / NVM_GRAN;
	  int cal_directory_location = directory_location % NVM_GRAN;

	  void * dmiBuffer = (void *) nvram_addr[quo];
	  int child_num;

		int next_dir;
		int success = 1;
		int inode_num = inodeNum;
		int last_page = 0;
		int children_num = 0;

		child_num = isPageFull(nvram_addr, directory_location);
		if ( child_num == 782 ) {
			memcpy(&last_page, dmiBuffer + cal_directory_location + 4092 - 12, sizeof(last_page));
			if (last_page == 0) {
				inode_num = inode_num + 1;
				next_dir = 4096 + 4096 * (inode_num - 1);
				memcpy(dmiBuffer + cal_directory_location + 4092 - 8, &next_dir, sizeof(next_dir));
				memcpy(dmiBuffer + cal_directory_location + 4092 - 12, &next_dir, sizeof(next_dir));
				addChildrenInDirectoryFast(nvram_addr, next_dir, target_offset, 0);
				return inode_num;
			}

			children_num = isPageFull(nvram_addr, last_page);
			if ( children_num == 1021 ) {
				inode_num = inode_num + 1;
				next_dir = 4096 + 4096 * (inode_num - 1);
				updateNextDir(nvram_addr, last_page, next_dir);
				memcpy(dmiBuffer + cal_directory_location + 4092 - 12, &next_dir, sizeof(next_dir));
				addChildrenInDirectoryFast(nvram_addr, next_dir, target_offset, 0);
				return inode_num;

			} else {
				addChildrenInDirectoryFast(nvram_addr, last_page, target_offset, children_num);
				return inode_num;
			}

		} else {
			//memcpy(&child_num, dmiBuffer + cal_directory_location + 4092 - 4, sizeof(child_num));
			int next_location = 4 * (child_num);
			memcpy(dmiBuffer + cal_directory_location + 952 + next_location,
					&target_offset, sizeof(target_offset));
			child_num = child_num + 1;
			memcpy(dmiBuffer + cal_directory_location + 4092 - 4, &child_num,
					sizeof(child_num));
		}

		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  return inode_num;
}


JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putLongLongTest(JNIEnv * env, jclass clazz, jlongArray addr, jlong id,
		 jlong rep,  jlong mod,  jlong access,  jlong prefer, jint index)
{
//	  void * dmiBuffer;
//	  dmiBuffer = (void *) addr;
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

//	  fprintf(fp, "success quo :%d\n" , quo);
//	  fprintf(fp, "success cal_index :%d\n" , cal_index);
//	  fprintf(fp, "success dmiBuffer :%ld\n" , nvram_addr[quo]);
//	  fprintf(fp, "success data : %ld -- %ld -- %ld -- %ld -- %ld\n" , id, rep, mod, access, prefer);
//	  fclose(fp);
	  memcpy(dmiBuffer + cal_index, &id, sizeof(id));
	  memcpy(dmiBuffer + cal_index + sizeof(id), &rep, sizeof(rep));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + sizeof(rep), &mod, sizeof(mod));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + sizeof(rep) + sizeof(mod), &access, sizeof(access));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + sizeof(rep) + sizeof(mod) + sizeof(access), &prefer, sizeof(prefer));

		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return (index + sizeof(id) + sizeof(rep) + sizeof(mod) + sizeof(access) + sizeof(prefer));
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putLongLongTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jlong id,
		 jlong rep,  jlong mod,  jlong access,  jlong prefer, jlong index)
{

	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = (int) (index / NVM_GRAN);
	  int cal_index = (int) (index % NVM_GRAN);
	  void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];

	  memcpy(dmiBuffer + cal_index, &id, sizeof(id));
	  memcpy(dmiBuffer + cal_index + sizeof(id), &rep, sizeof(rep));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + sizeof(rep), &mod, sizeof(mod));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + sizeof(rep) + sizeof(mod), &access, sizeof(access));
	  memcpy(dmiBuffer + cal_index + sizeof(id) + sizeof(rep) + sizeof(mod) + sizeof(access), &prefer, sizeof(prefer));

		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return (jlong)(index + sizeof(id) + sizeof(rep) + sizeof(mod) + sizeof(access) + sizeof(prefer));
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putmetaTest(JNIEnv * env, jclass clazz, jlongArray addr, jlongArray meta, jint index)
{
//	  void * dmiBuffer;
//	  dmiBuffer = (void *) addr;
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

		int remains = 40;
	  //int len = (*env)->GetArrayLength(env, meta);
	  jlong * nativeBytes = (*env)->GetLongArrayElements(env, meta, NULL);

		char * dest = (char *)( dmiBuffer + cal_index );
		char * src = (char *) nativeBytes;

		while(remains >= 16) {
			storeu_16bytes((void *)dest , (void *)src);
			dest += 16;
			src += 16;
			remains -= 16;
		}
		if(remains !=0) {
			memcpy(dest, src, remains);
		}
		(*env)->ReleaseLongArrayElements(env, meta, nativeBytes, 0);
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);

	  return (index + 40);
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntTest(JNIEnv * env, jclass clazz, jlongArray addr, jint data, jint index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

	  memcpy(dmiBuffer + cal_index, &data, sizeof(data));
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return (index + sizeof(data));
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jint data, jlong index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = (int) (index / NVM_GRAN);
	  int cal_index = (int) (index % NVM_GRAN);
	  void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];

	  memcpy(dmiBuffer + cal_index, &data, sizeof(data));
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return (jlong)(index + sizeof(data));
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntBATest(JNIEnv * env, jclass clazz, jlongArray addr, jint length, jbyteArray data, jint index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

//	  fprintf(fp, "putIntBATest quo :%d\n" , quo);
//	  fprintf(fp, "putIntBATest cal_index :%d\n" , cal_index);
//	  fprintf(fp, "putIntBATest dmiBuffer :%ld\n" , nvram_addr[quo]);
//	  fclose(fp);
	  char nativeStr[192] = {0, };
	  int return_index = 300;
		int remains = 192;
	  int len = (*env)->GetArrayLength(env, data);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, data, NULL);

		char * dest = (char *)(dmiBuffer + cal_index + sizeof(length));
	  memcpy(nativeStr, (char *) nativeBytes, len);

		char * src = (char *) nativeStr;
	  memcpy(dmiBuffer + cal_index, &length, sizeof(length));

		while(remains >= 32) {
			storeu_32bytes((void *)dest , (void *)src);
			dest += 32;
			src += 32;
			remains-=32;
		}

		while(remains >= 16) {
			storeu_16bytes((void *)dest , (void *)src);
			dest += 16;
			src += 16;
			remains -= 16;
		}
		if(remains !=0) {
			memcpy(dest, src, remains);
		}
	  //memcpy(dmiBuffer + cal_index + sizeof(length) , nativeStr, sizeof(nativeStr));
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return (index + sizeof(length) + return_index);
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntBATestLong(JNIEnv * env, jclass clazz, jlongArray addr, jint length, jbyteArray data, jlong index)
{

	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = (int) (index / NVM_GRAN);
	  int cal_index = (int) (index % NVM_GRAN);
	  void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];

	  char nativeStr[192] = {0, };
	  int return_index = 300;
		int remains = 192;
	  //int len = (*env)->GetArrayLength(env, data);
	  int len = (int)length;
	  //char *nativeStr = malloc(len);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, data, NULL);

		char * dest = (char *)(dmiBuffer + cal_index + sizeof(len));
	  memcpy(nativeStr, (char *) nativeBytes, len);
	  //nativeSTr[len] = '\0';
	  //int remains = len;

		char * src = (char *) nativeStr;
	  memcpy(dmiBuffer + cal_index, &len, sizeof(len));
//	  asm volatile ("clflush (%0)" :: "r"(dmiBuffer+cal_index));
//	  if(quo == 2) {
//	  FILE * fp;
//	  fp = fopen("/home/skt_test/length_check.txt", "at");
//	  fprintf(fp, "length = %d and %d and %s\n" , length, len, nativeStr);
//	  fprintf(fp, "additional quo addr = %ld === %ld === %ld\n", nvram_addr[0], nvram_addr[1], nvram_addr[2]);
//	  int *leng = (int*)(dmiBuffer + cal_index);
//	  fprintf(fp, "dmiBuffer + cal_index = %ld and %d\n", dmiBuffer + cal_index, *leng);
//	  int length_sec = 0;
//	  memcpy(&length_sec, dmiBuffer + cal_index, sizeof(length_sec));
//	  fprintf(fp, "length2 = %d\n" , length_sec);
//	  fclose(fp);
//	  }

		while(remains >= 32) {
			storeu_32bytes((void *)dest , (void *)src);
			dest += 32;
			src += 32;
			remains-=32;
		}

		while(remains >= 16) {
			storeu_16bytes((void *)dest , (void *)src);
			dest += 16;
			src += 16;
			remains -= 16;
		}
		if(remains !=0) {
			memcpy(dest, src, remains);
		}

		//free(nativeStr);
//	  memcpy(dmiBuffer + cal_index + sizeof(length) , nativeStr, sizeof(nativeStr));
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return (jlong)(index + sizeof(len) + return_index);
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntClientTest(JNIEnv * env, jclass clazz, jlongArray addr, jint length, jbyteArray data, jint index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

//	  fprintf(fp, "putIntBATest quo :%d\n" , quo);
//	  fprintf(fp, "putIntBATest cal_index :%d\n" , cal_index);
//	  fprintf(fp, "putIntBATest dmiBuffer :%ld\n" , nvram_addr[quo]);
//	  fclose(fp);
	  char nativeStr[64] = {0, };
	  int return_index = 300;
		int remains = 64;
	  int len = (*env)->GetArrayLength(env, data);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, data, NULL);

		char * dest = (char *)(dmiBuffer + cal_index + sizeof(length));
	  memcpy(nativeStr, (char *) nativeBytes, len);

		char * src = (char *) nativeStr;
	  memcpy(dmiBuffer + cal_index, &length, sizeof(length));

		while(remains >= 32) {
			storeu_32bytes((void *)dest , (void *)src);
			dest += 32;
			src += 32;
			remains-=32;
		}

		while(remains >= 16) {
			storeu_16bytes((void *)dest , (void *)src);
			dest += 16;
			src += 16;
			remains-= 16;
		}
		if(remains !=0) {
			memcpy(dest, src, remains);
		}
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return (index + sizeof(length) + return_index);
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntClientTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jint length, jbyteArray data, jlong index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = (int) (index / NVM_GRAN);
	  int cal_index = (int)(index % NVM_GRAN);
	  void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];

//	  fprintf(fp, "putIntBATest quo :%d\n" , quo);
//	  fprintf(fp, "putIntBATest cal_index :%d\n" , cal_index);
//	  fprintf(fp, "putIntBATest dmiBuffer :%ld\n" , nvram_addr[quo]);
//	  fclose(fp);
	  char nativeStr[64] = {0, };

	  int return_index = 300;
	  //int len = (*env)->GetArrayLength(env, data);
	  int len = (int)length;
	  //char *nativeStr = malloc(len);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, data, NULL);

		//int remains = len;
	  int remains = 64;
		char * dest = (char *)(dmiBuffer + cal_index + sizeof(len));
	  memcpy(nativeStr, (char *) nativeBytes, len);

		char * src = (char *) nativeStr;
	  memcpy(dmiBuffer + cal_index, &len, sizeof(len));

		while(remains >= 32) {
			storeu_32bytes((void *)dest , (void *)src);
			dest += 32;
			src += 32;
			remains-=32;
		}

		while(remains >= 16) {
			storeu_16bytes((void *)dest , (void *)src);
			dest += 16;
			src += 16;
			remains-= 16;
		}
		if(remains !=0) {
			memcpy(dest, src, remains);
		}
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		//free(nativeStr);
		  /*test*/
	  return (jlong)(index + sizeof(len) + return_index);
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntPermTest(JNIEnv * env, jclass clazz, jlongArray addr, jint length, jbyteArray data, jint index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

//	  fprintf(fp, "putIntBATest quo :%d\n" , quo);
//	  fprintf(fp, "putIntBATest cal_index :%d\n" , cal_index);
//	  fprintf(fp, "putIntBATest dmiBuffer :%ld\n" , nvram_addr[quo]);
//	  fclose(fp);
	  char nativeStr[16] = {0, };
	  int return_index = 300;
		int remains = 16;
	  int len = (*env)->GetArrayLength(env, data);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, data, NULL);

		char * dest = (char *)(dmiBuffer + cal_index + sizeof(length));
	  memcpy(nativeStr, (char *) nativeBytes, len);

		char * src = (char *) nativeStr;
	  memcpy(dmiBuffer + cal_index, &length, sizeof(length));

  	storeu_16bytes((void *)dest , (void *)src);
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	  return (index + sizeof(length) + return_index);
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putIntPermTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jint length, jbyteArray data, jlong index)
{

	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = (int) (index / NVM_GRAN);
	  int cal_index = (int) (index % NVM_GRAN);
	  void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];

	  char nativeStr[16] = {0, };

	  int return_index = 300;
	  //int len = (*env)->GetArrayLength(env, data);
	  int len = (int) length;

	  //char *nativeStr = malloc(len);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, data, NULL);
		int remains = 16;
	  //int remains = len;

		char * dest = (char *)(dmiBuffer + cal_index + sizeof(len));
	  memcpy(nativeStr, (char *) nativeBytes, len);

		char * src = (char *) nativeStr;
	  memcpy(dmiBuffer + cal_index, &len, sizeof(len));

		//memcpy(dest, src, remains);
  	storeu_16bytes((void *)dest , (void *)src);

		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		//free(nativeStr);
	  return (jlong)(index + sizeof(len) + return_index);
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_putBATest(JNIEnv * env, jclass clazz, jlongArray addr, jbyteArray data, jint index)
{

	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

	  //char ret[100] = {0, };
	  char nativeStr[300] = {0, };
	  int return_index = 300;

	  int len = (*env)->GetArrayLength(env, data);
	  jbyte * nativeBytes = (*env)->GetByteArrayElements(env, data, NULL);
	  strncpy(nativeStr, (char *) nativeBytes , len);

	  memcpy(dmiBuffer + cal_index , nativeStr, sizeof(nativeStr));
		(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
		  /*test*/
	  return index + return_index;
}


JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntTest(JNIEnv * env, jclass clazz, jlongArray addr, jint index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
//	fprintf(fp, "success1\n");

	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];
	int ret;
//	fprintf(fp, "success2 quo :%d\n" , quo);
//	fprintf(fp, "success2 cal_index :%d\n" , cal_index);
//	fprintf(fp, "success2 dmiBuffer :%ld\n" , nvram_addr[quo]);



	memcpy(&ret, dmiBuffer + cal_index, sizeof(int));
//	fprintf(fp, "success3\n");
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
//	fclose(fp);
	return (jint) ret;
}

JNIEXPORT jint JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jlong index)
{
//	FILE * fp;
//	fp = fopen("/home/skt_test/native_error_checking.txt", "at");
//	fprintf(fp, "success1\n");

	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = (int)(index / NVM_GRAN);
	int cal_index = (int)(index % NVM_GRAN);
	void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];
	int ret;
//	fprintf(fp, "success2 quo :%d\n" , quo);
//	fprintf(fp, "success2 cal_index :%d\n" , cal_index);
//	fprintf(fp, "success2 dmiBuffer :%ld\n" , nvram_addr[quo]);



	memcpy(&ret, dmiBuffer + cal_index, sizeof(int));
//	fprintf(fp, "success3\n");
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
//	fclose(fp);
	return (jint) ret;
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readLongTest(JNIEnv * env, jclass clazz, jlongArray addr, jint index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];
	long ret;

	memcpy(&ret, dmiBuffer + cal_index, sizeof(long));
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return (jlong) ret;
}

JNIEXPORT jlong JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readLongTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jlong index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = (int) (index / NVM_GRAN);
	int cal_index = (int) (index % NVM_GRAN);
	void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];
	long ret;

	memcpy(&ret, dmiBuffer + cal_index, sizeof(long));
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return (jlong) ret;
}

JNIEXPORT jintArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntArr(JNIEnv * env, 	jclass clazz, jlongArray addr, jint index, jint type)
{
	  jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	  int quo = index / NVM_GRAN;
	  int cal_index = index % NVM_GRAN;
	  void * dmiBuffer = (void *) nvram_addr[quo];

	  jintArray intAR;
	  int child_num = 0;
	  int i = 0;
	  int record_location = 0;
	  int mem = 0;
	  int total_num = 0;

	  jint loc[1024];
//	FILE * fp;
//  fp = fopen("/home/skt_test/native_error_checking.txt","at");
//  fprintf(fp, " offset = %d and type = %d\n", index, type);

	memcpy(&child_num, dmiBuffer + cal_index + 4092 - 4, sizeof(child_num));
	if (type == 0) { // first page
		memcpy(&loc, dmiBuffer + cal_index + 952, sizeof(int) * child_num);
//		for (i = 0; i < child_num; i++) {
//			record_location = 4 * i;
//			memcpy(&mem, dmiBuffer + cal_index + 936 + record_location, sizeof(mem));
//			loc[total_num] = mem;
//			total_num++;
//		}
	} else {
		memcpy(&loc, dmiBuffer + cal_index, sizeof(int) * child_num);
//		for (i = 0; i < child_num; i++) {
//			record_location = 4 * i;
//			memcpy(&mem, dmiBuffer + cal_index + record_location, sizeof(mem));
//			loc[total_num] = mem;
//			total_num++;
//		}
	}
	intAR = (*env)->NewIntArray(env, child_num);
	(*env)->SetIntArrayRegion(env, intAR, 0, child_num, loc);
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	//fclose(fp);
	return intAR;
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readBATest(JNIEnv * env,
		jclass clazz, jlongArray addr, jint index, jint sizeArray) {
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];

	jbyteArray byteAR;
	int length;
	char ret[200] = {0, };
	memcpy(ret, dmiBuffer + cal_index, sizeof(ret));
	length = (int) strlen(ret);

	byteAR = (*env)->NewByteArray(env, length);
	(*env)->SetByteArrayRegion(env, byteAR, 0, length, (jbyte *) ret);

//		  fp = fopen("/home/skt_test/native_error_checking.txt","at");
//		  fprintf(fp, "nativeStr read : %s and length : %d\n", ret, length);
//		  fprintf(fp, "nativeBytes read : %s and length : %d\n", (char *)byteAR, strlen(byteAR));
//
//		  fclose(fp);
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return byteAR;
}

JNIEXPORT jlongArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readmetaTest(JNIEnv * env,
		jclass clazz, jlongArray addr, jint index) {
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];

	jlongArray longAR;
	char ret[40] = {0, };
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index);
	int remains = 40;

	while(remains >= 16) {
		loadu_16bytes((void *)dest , (void *)src);
		dest += 16;
		src += 16;
		remains -= 16;
	}
	if(remains !=0) {
		memcpy(dest, src, remains);
	}

	longAR = (*env)->NewLongArray(env, 40);
	(*env)->SetLongArrayRegion(env, longAR, 0, 40, (jbyte *) ret);

	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return longAR;
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readBlockChunkTest(JNIEnv * env,
		jclass clazz, jlongArray addr, jint index) {
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];

	jbyteArray byteAR;
	char ret[24] = {0, };
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index);
	int remains = 24;

	while(remains >= 16) {
		loadu_16bytes((void *)dest , (void *)src);
		dest += 16;
		src += 16;
		remains -= 16;
	}
	if(remains !=0) {
		memcpy(dest, src, remains);
	}

	byteAR = (*env)->NewByteArray(env, 24);
	(*env)->SetByteArrayRegion(env, byteAR, 0, 24, (jbyte *) ret);

	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return byteAR;
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntBATest(JNIEnv * env, jclass clazz, jlongArray addr, jint index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];
	jbyteArray byteAR;
	int length;
	char ret[192] = {0, };
//	FILE * fp;

	int remains = 192;
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index + sizeof(length));
	memcpy(&length, dmiBuffer + cal_index, sizeof(length));

//  fp = fopen("/home/skt_test/native_error_checking.txt","at");
//  fprintf(fp, "1");
//
	while (remains >= 32) {
		loadu_32bytes((void *) dest, (void *) src);
		dest += 32;
		src += 32;
		remains -= 32;
	}

	while(remains >= 16) {
		loadu_16bytes((void *)dest , (void *)src);
		dest += 16;
		src += 16;
		remains -= 16;
	}

	if(remains !=0) {
		memcpy(dest, src, remains);
	}
	//memcpy(ret, dmiBuffer + cal_index + sizeof(length), sizeof(ret));
	byteAR = (*env)->NewByteArray(env, length);
	(*env)->SetByteArrayRegion(env, byteAR, 0, length, (jbyte *) ret);

	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
//  fclose(fp);
	return byteAR;
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntBATestLong(JNIEnv * env, jclass clazz, jlongArray addr, jlong index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = (int) (index / NVM_GRAN);
	int cal_index = (int) (index % NVM_GRAN);
	void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];
	jbyteArray byteAR;
	int length;
	char ret[192] = {0, };

	memcpy(&length, dmiBuffer + cal_index, sizeof(length));
	//char * ret = malloc(length);
	//int remains = length;
	int remains = 192;
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index + sizeof(length));

//	if(quo == 6) {
//		 FILE * fp;
//    fp = fopen("/home/skt_test/native_error_checking.txt","at");
//    fprintf(fp, "quo : %d, cal_index : %d, length : %d, index : %ld, buffer : %ld\n"
//		  , quo, cal_index, length, index, nvram_addr[quo]);
//	  fprintf(fp, "data : %s\n" , ret);
//	  fclose(fp);
//	}

	while (remains >= 32) {
		loadu_32bytes((void *) dest, (void *) src);
		dest += 32;
		src += 32;
		remains -= 32;
	}

	while(remains >= 16) {
		loadu_16bytes((void *)dest , (void *)src);
		dest += 16;
		src += 16;
		remains -= 16;
	}

	if(remains !=0) {
		memcpy(dest, src, remains);
	}
	//memcpy(ret, dmiBuffer + cal_index + sizeof(length), sizeof(ret));
	byteAR = (*env)->NewByteArray(env, length);
	(*env)->SetByteArrayRegion(env, byteAR, 0, length, (jbyte *) ret);
	//free(ret);
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return byteAR;
}


JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntClientTest(JNIEnv * env, jclass clazz, jlongArray addr, jint index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];
	jbyteArray byteAR;
	int length;
	char ret[64] = {0, };
	int remains = 64;
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index + sizeof(length));
	memcpy(&length, dmiBuffer + cal_index, sizeof(length));

	while (remains >= 32) {
		loadu_32bytes((void *) dest, (void *) src);
		dest += 32;
		src += 32;
		remains -= 32;
	}

	while(remains >= 16) {
		loadu_16bytes((void *)dest , (void *)src);
		dest += 16;
		src += 16;
		remains -= 16;
	}
	if(remains !=0) {
		memcpy(dest, src, remains);
	}
	byteAR = (*env)->NewByteArray(env, length);
	(*env)->SetByteArrayRegion(env, byteAR, 0, length, (jbyte *) ret);

	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return byteAR;
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntClientTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jlong index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = (int) (index / NVM_GRAN);
	int cal_index = (int) (index % NVM_GRAN);
	void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];
	jbyteArray byteAR;
	int length;
	memcpy(&length, dmiBuffer + cal_index, sizeof(length));

	char ret[64] = {0, };
	//char * ret = malloc(length);
	int remains = 64;
	//int remains = length;
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index + sizeof(length));

	while (remains >= 32) {
		loadu_32bytes((void *) dest, (void *) src);
		dest += 32;
		src += 32;
		remains -= 32;
	}

	while(remains >= 16) {
		loadu_16bytes((void *)dest , (void *)src);
		dest += 16;
		src += 16;
		remains -= 16;
	}
	if(remains !=0) {
		memcpy(dest, src, remains);
	}
	byteAR = (*env)->NewByteArray(env, length);
	(*env)->SetByteArrayRegion(env, byteAR, 0, length, (jbyte *) ret);
	//free(ret);
	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return byteAR;
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntPermTest(JNIEnv * env, jclass clazz, jlongArray addr, jint index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = index / NVM_GRAN;
	int cal_index = index % NVM_GRAN;
	void * dmiBuffer = (void *) nvram_addr[quo];
	jbyteArray byteAR;
	int length;
	char ret[16] = {0, };

	int remains = 16;
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index + sizeof(length));
	memcpy(&length, dmiBuffer + cal_index, sizeof(length));

	loadu_16bytes((void *)dest , (void *)src);

	byteAR = (*env)->NewByteArray(env, length);
	(*env)->SetByteArrayRegion(env, byteAR, 0, length, (jbyte *) ret);

	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return byteAR;
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_readIntPermTestLong(JNIEnv * env, jclass clazz, jlongArray addr, jlong index)
{
	jlong * nvram_addr = (*env)->GetLongArrayElements(env, addr, 0);
	int quo = (int) (index / NVM_GRAN);
	int cal_index = (int) (index % NVM_GRAN);
	void* dmiBuffer = (void *)(intptr_t) nvram_addr[quo];
	jbyteArray byteAR;
	int length;
	memcpy(&length, dmiBuffer + cal_index, sizeof(length));

	char ret[16] = {0, };
	//char * ret = malloc(length);
	int remains = 16;
	//int remains = length;
	char * dest = ret;
	char * src = (char *)(dmiBuffer + cal_index + sizeof(length));

	loadu_16bytes((void *)dest , (void *)src);
	//memcpy(dest, src, remains);

	byteAR = (*env)->NewByteArray(env, length);
	(*env)->SetByteArrayRegion(env, byteAR, 0, length, (jbyte *) ret);

	//free(ret);

	(*env)->ReleaseLongArrayElements(env, addr, nvram_addr, 0);
	return byteAR;
}

JNIEXPORT void JNICALL
Java_org_apache_hadoop_io_nativeio_NativeIO_clFlushFunction(JNIEnv *env, jclass clazz, jobject target, jlong size)
{
  int i=0;
  void * buffer = (*env)->GetDirectBufferAddress(env, target);
  for(i=0; i<size; i++)
    asm volatile("clflush (%0)" :: "r"(buffer+i));
  asm volatile("mfence" ::: "memory");
}




/**
 * vim: sw=2: ts=2: et:
 */
