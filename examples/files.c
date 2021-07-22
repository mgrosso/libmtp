/**
 * \file files.c
 * Example program that lists all files on a device.
 *
 * Copyright (C) 2005-2012 Linus Walleij <triad@df.lth.se>
 * Copyright (C) 2007 Ted Bullock <tbullock@canada.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "common.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>

typedef struct path_segment path_segment_t;
struct path_segment {
    char *segment;
    path_segment_t *prev;
    path_segment_t *next;
};
static path_segment_t *path_stack_first = NULL;
static path_segment_t *path_stack_last = NULL;

static void
push_segment(const char *name) {
    path_segment_t *seg = (path_segment_t *)malloc(sizeof(path_segment_t));
    seg->segment = strdup(name);
    seg->prev = path_stack_last;
    seg->next = NULL;
    path_stack_last = seg;
    if(path_stack_first == NULL) {
        path_stack_first = seg;
    } else {
        seg->prev->next = seg;
    }
}

static void
pop_segment() {
    if(path_stack_first == path_stack_last) {
        path_stack_first = NULL;
    }
    path_segment_t *seg_to_free = path_stack_last;
    path_stack_last = path_stack_last->prev;
    if(path_stack_last != NULL) {
        path_stack_last->next = NULL;
    }
    free(seg_to_free->segment);
    free(seg_to_free);
}

static int
current_path_len() {
    int ret = 0;
    for(path_segment_t *seg=path_stack_first; seg; seg = seg->next) {
        ++ret;
    }
    return ret;
}

static char *
current_path() {
    const size_t max_write = PATH_MAX + 1;
    char *buf = (char *)malloc(max_write);
    char *last_char = buf + PATH_MAX;
    char *b = buf;
    memset(buf, 0, max_write);
    fprintf(stdout, "PATHLEN %i\n", current_path_len());
    for(path_segment_t *seg=path_stack_first; seg; seg = seg->next) {
        char *s = seg->segment;
        while(*s && b < last_char) {
            *b++ = *s++;
        }
        *b++ = '/';
        fprintf(stdout, "BUF %s segment %s\n", buf, seg->segment);
    }
    b = b < last_char ? b : last_char;
    *b = '\0';
    return buf;
}

static int
should_copy(LIBMTP_file_t *file) {
    char *path = current_path();
    size_t dir_path_len = strlen(path);
    size_t full_path_len = dir_path_len + strlen(file->filename) + 1;
    char *full_path = (char *)malloc(full_path_len);
    strcpy(full_path, path);
    strcpy(full_path + dir_path_len, file->filename);
    struct stat statbuf;
    int retval = 0;
    if(-1 == stat(file->filename, &statbuf)) {
        if(errno == ENOENT) {
            fprintf(stdout, "STAT(%s)->ENOENT\n", file->filename);
            retval = 1;
        } else {
            fprintf(stdout,
                    "couldn't stat %s (%s) and errno is not ENOENT: %i\n",
                    file->filename, full_path, errno);
            exit(errno);
        }
    } else {
        fprintf(stdout, "compare: %s (%s) : %li ==  %lu\n",
                file->filename, full_path,
                statbuf.st_size, file->filesize);
        retval = statbuf.st_size != file->filesize;
    }
    free(path);
    free(full_path);
    return retval;
}


static void dump_fileinfo(LIBMTP_file_t *file)
{
  printf("File ID: %u\n", file->item_id);
  if (file->filename != NULL)
    printf("   Filename: %s\n", file->filename);

  // This is sort of special...
  if (file->filesize == (uint32_t) -1) {
    printf("   None. (abstract file, size = -1)\n");
  } else {
#ifdef __WIN32__
    printf("   File size %llu (0x%016I64X) bytes\n", file->filesize, file->filesize);
#else
    printf("   File size %llu (0x%016llX) bytes\n",
	   (long long unsigned int) file->filesize,
	   (long long unsigned int) file->filesize);
#endif
  }
  printf("   Parent ID: %u\n", file->parent_id);
  printf("   Storage ID: 0x%08X\n", file->storage_id);
  printf("   Filetype: %s\n", LIBMTP_Get_Filetype_Description(file->filetype));
}

static void
pushdir(const char *dirname)
{
    if(mkdir(dirname, 0755) != 0 && errno != EEXIST) {
        fprintf(stdout, "couldn't mkdir %s, errno: %i\n", dirname, errno);
        exit(1);
    }
    if(chdir(dirname) != 0) {
        fprintf(stdout, "couldn't chdir %s, errno: %i\n", dirname, errno);
        exit(1);
    }
    push_segment(dirname);
}

static void
popdir()
{
    if(chdir("..") != 0) {
        fprintf(stdout, "wow, couldn't chdir(\"..\") errno: %i\n", errno);
        exit(1);
    }
    pop_segment();
}


static void
exit_if_too_many_fails()
{
    static int fails = 0;
    fails = fails + 1;
    fprintf(stdout, "total fails so far: %i", fails);
    if(fails > 10) {
        exit(1);
    }
}

static void
dump_files(LIBMTP_mtpdevice_t *device, uint32_t storageid, int leaf)
{
  LIBMTP_file_t *files;

  /* Get file listing. */
  files = LIBMTP_Get_Files_And_Folders(device,
				       storageid,
				       leaf);
  if (files == NULL) {
    LIBMTP_Dump_Errorstack(device);
    LIBMTP_Clear_Errorstack(device);
  } else {
    LIBMTP_file_t *file, *tmp;
    file = files;
    while (file != NULL) {
      /* Please don't print these */
      if (file->filetype == LIBMTP_FILETYPE_FOLDER) {
        printf("ENTER DIRECTORY:%s\n", file->filename);
        pushdir(file->filename);
        char *s = current_path();
        printf("CURRENT_PATH after entering %s is %s\n", file->filename, s);
        free(s);
        dump_files(device, storageid, file->item_id);
        printf("LEAVE DIRECTORY:%s\n", file->filename);
        popdir(file->filename);
        s = current_path();
        printf("CURRENT_PATH after leaving %s is %s\n", file->filename, s);
        free(s);
      } else {
        dump_fileinfo(file);
        int will_copy = should_copy(file);
        fprintf(stdout, "should_copy(%s):%i\n", file->filename, will_copy);
        if(will_copy) {
            if(LIBMTP_Get_File_To_File(device, file->item_id, file->filename, NULL, NULL) != 0 ) {
                char wd[1024];
                getcwd(&wd[0], 1024);
                fprintf(stdout, "couldn't write %s in dir %s, errno:%i\n", file->filename, wd, errno);
                exit_if_too_many_fails();
            }
        }
      }
      tmp = file;
      file = file->next;
      LIBMTP_destroy_file_t(tmp);
    }
  }
}

int main(int argc, char **argv)
{
  LIBMTP_raw_device_t *rawdevices;
  int numrawdevices;
  LIBMTP_error_number_t err;
  int i;

  fprintf(stdout, "libmtp version: " LIBMTP_VERSION_STRING "\n\n");

  LIBMTP_Init();

  err = LIBMTP_Detect_Raw_Devices(&rawdevices, &numrawdevices);
  switch(err)
  {
  case LIBMTP_ERROR_NO_DEVICE_ATTACHED:
    fprintf(stdout, "mtp-files: No Devices have been found\n");
    return 0;
  case LIBMTP_ERROR_CONNECTING:
    fprintf(stdout, "mtp-files: There has been an error connecting. Exit\n");
    return 1;
  case LIBMTP_ERROR_MEMORY_ALLOCATION:
    fprintf(stdout, "mtp-files: Memory Allocation Error. Exit\n");
    return 1;

  /* Unknown general errors - This should never execute */
  case LIBMTP_ERROR_GENERAL:
  default:
    fprintf(stdout, "mtp-files: Unknown error, please report "
                    "this to the libmtp developers\n");
    return 1;

  /* Successfully connected at least one device, so continue */
  case LIBMTP_ERROR_NONE:
    fprintf(stdout, "mtp-files: Successfully connected\n");
    fflush(stdout);
    break;
  }

  /* iterate through connected MTP devices */
  for (i = 0; i < numrawdevices; i++) {
    LIBMTP_mtpdevice_t *device;
    LIBMTP_devicestorage_t *storage;
    char *friendlyname;

    device = LIBMTP_Open_Raw_Device_Uncached(&rawdevices[i]);
    if (device == NULL) {
      fprintf(stdout, "Unable to open raw device %d\n", i);
      continue;
    }

    /* Echo the friendly name so we know which device we are working with */
    friendlyname = LIBMTP_Get_Friendlyname(device);
    if (friendlyname == NULL) {
      printf("Listing File Information on Device with name: (NULL)\n");
    } else {
      printf("Listing File Information on Device with name: %s\n", friendlyname);
      free(friendlyname);
    }

    LIBMTP_Dump_Errorstack(device);
    LIBMTP_Clear_Errorstack(device);

    /* Loop over storages */
    for (storage = device->storage; storage != 0; storage = storage->next) {
      dump_files(device, storage->id, LIBMTP_FILES_AND_FOLDERS_ROOT);
    }
    LIBMTP_Release_Device(device);
  }

  free(rawdevices);

  printf("OK.\n");
  exit (0);
}
