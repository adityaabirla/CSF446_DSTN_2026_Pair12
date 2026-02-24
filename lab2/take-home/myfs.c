/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/

#include "params.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- data_block init/free --- */
void data_block_init(struct data_block *b, int size) {
  b->data = (char *)malloc((size_t)size);
  if (b->data)
    memset(b->data, 0, (size_t)size);
}

void data_block_free(struct data_block *b) {
  free(b->data);
  b->data = NULL;
}

/* --- path_to_inode helpers --- */
void path_to_inode_add(struct myfs_state *s, const char *path,
                       int inode_index) {
  if (s->path_count >= s->NUM_INODES)
    return;
  strncpy(s->path_to_inode[s->path_count].path, path, PATH_MAX - 1);
  s->path_to_inode[s->path_count].path[PATH_MAX - 1] = '\0';
  s->path_to_inode[s->path_count].inode = inode_index;
  s->path_count++;
}

void path_to_inode_remove(struct myfs_state *s, const char *path) {
  int i;
  for (i = 0; i < s->path_count; i++) {
    if (strcmp(s->path_to_inode[i].path, path) == 0) {
      /* swap with last */
      if (i != s->path_count - 1) {
        s->path_to_inode[i] = s->path_to_inode[s->path_count - 1];
      }
      s->path_count--;
      return;
    }
  }
}

int path_to_inode_lookup(struct myfs_state *s, const char *path) {
  int i;
  for (i = 0; i < s->path_count; i++) {
    if (strcmp(s->path_to_inode[i].path, path) == 0)
      return s->path_to_inode[i].inode;
  }
  return -1;
}

/* --- myfs_state create/destroy --- */
struct myfs_state *myfs_state_create(FILE *log, const char *root,
                                     int num_inodes, int num_data_blocks,
                                     int data_block_size) {
  struct myfs_state *s;
  int i;
  char *rootpath;

  s = (struct myfs_state *)malloc(sizeof(struct myfs_state));
  if (!s)
    return NULL;
  s->NUM_INODES = num_inodes;
  s->NUM_DATA_BLOCKS = num_data_blocks;
  s->DATA_BLOCK_SIZE = data_block_size;
  s->logfile = log;
  s->path_count = 0;

  rootpath = realpath(root, NULL);
  if (!rootpath) {
    free(s);
    return NULL;
  }
  s->rootdir = strdup(rootpath);
  free(rootpath);
  if (!s->rootdir) {
    free(s);
    return NULL;
  }

  s->data_blocks = (struct data_block **)malloc((size_t)num_data_blocks *
                                                sizeof(struct data_block *));
  if (!s->data_blocks) {
    free(s->rootdir);
    free(s);
    return NULL;
  }
  for (i = 0; i < num_data_blocks; i++) {
    s->data_blocks[i] = (struct data_block *)malloc(sizeof(struct data_block));
    if (!s->data_blocks[i]) {
      while (i--)
        data_block_free(s->data_blocks[i]), free(s->data_blocks[i]);
      free(s->data_blocks);
      free(s->rootdir);
      free(s);
      return NULL;
    }
    data_block_init(s->data_blocks[i], data_block_size);
  }

  s->inodes =
      (struct inode **)malloc((size_t)num_inodes * sizeof(struct inode *));
  if (!s->inodes) {
    for (i = 0; i < num_data_blocks; i++)
      data_block_free(s->data_blocks[i]), free(s->data_blocks[i]);
    free(s->data_blocks);
    free(s->rootdir);
    free(s);
    return NULL;
  }
  for (i = 0; i < num_inodes; i++) {
    s->inodes[i] = (struct inode *)malloc(sizeof(struct inode));
    if (!s->inodes[i]) {
      while (i--)
        free(s->inodes[i]->blocks), free(s->inodes[i]);
      free(s->inodes);
      for (i = 0; i < num_data_blocks; i++)
        data_block_free(s->data_blocks[i]), free(s->data_blocks[i]);
      free(s->data_blocks);
      free(s->rootdir);
      free(s);
      return NULL;
    }
    s->inodes[i]->num_blocks = 0;
    s->inodes[i]->blocks = (int *)malloc((size_t)num_data_blocks * sizeof(int));
    if (!s->inodes[i]->blocks) {
      free(s->inodes[i]);
      while (i--)
        free(s->inodes[i]->blocks), free(s->inodes[i]);
      free(s->inodes);
      for (i = 0; i < num_data_blocks; i++)
        data_block_free(s->data_blocks[i]), free(s->data_blocks[i]);
      free(s->data_blocks);
      free(s->rootdir);
      free(s);
      return NULL;
    }
  }

  s->inode_bitmap = (int *)calloc((size_t)num_inodes, sizeof(int));
  s->data_block_bitmap = (int *)calloc((size_t)num_data_blocks, sizeof(int));
  if (!s->inode_bitmap || !s->data_block_bitmap) {
    if (s->inode_bitmap)
      free(s->inode_bitmap);
    if (s->data_block_bitmap)
      free(s->data_block_bitmap);
    for (i = 0; i < num_inodes; i++)
      free(s->inodes[i]->blocks), free(s->inodes[i]);
    free(s->inodes);
    for (i = 0; i < num_data_blocks; i++)
      data_block_free(s->data_blocks[i]), free(s->data_blocks[i]);
    free(s->data_blocks);
    free(s->rootdir);
    free(s);
    return NULL;
  }

  s->path_to_inode = (struct path_inode *)malloc((size_t)num_inodes *
                                                 sizeof(struct path_inode));
  if (!s->path_to_inode) {
    free(s->data_block_bitmap);
    free(s->inode_bitmap);
    for (i = 0; i < num_inodes; i++)
      free(s->inodes[i]->blocks), free(s->inodes[i]);
    free(s->inodes);
    for (i = 0; i < num_data_blocks; i++)
      data_block_free(s->data_blocks[i]), free(s->data_blocks[i]);
    free(s->data_blocks);
    free(s->rootdir);
    free(s);
    return NULL;
  }

  return s;
}

void myfs_state_destroy(struct myfs_state *s) {
  int i;
  if (!s)
    return;
  if (s->logfile)
    fclose(s->logfile);
  free(s->rootdir);
  for (i = 0; i < s->NUM_DATA_BLOCKS; i++) {
    data_block_free(s->data_blocks[i]);
    free(s->data_blocks[i]);
  }
  free(s->data_blocks);
  for (i = 0; i < s->NUM_INODES; i++) {
    free(s->inodes[i]->blocks);
    free(s->inodes[i]);
  }
  free(s->inodes);
  free(s->inode_bitmap);
  free(s->data_block_bitmap);
  free(s->path_to_inode);
  free(s);
}

/* --- logging (DO NOT CHANGE) --- */
FILE *log_open(char *file_name) {
  FILE *logfile;

  logfile = fopen(file_name, "w");
  if (logfile == NULL) {
    perror("logfile");
    exit(EXIT_FAILURE);
  }
  setvbuf(logfile, NULL, _IOLBF, 0);
  return logfile;
}

void log_char(char c) {
  FILE *log_file = MYFS_DATA->logfile;
  if (c == '\n')
    fprintf(log_file, "\\n");
  else
    fprintf(log_file, "%c", c);
}

static int path_inode_cmp(const void *a, const void *b) {
  const struct path_inode *pa = (const struct path_inode *)a;
  const struct path_inode *pb = (const struct path_inode *)b;
  return strcmp(pa->path, pb->path);
}

void log_fuse_context(void) {
  struct myfs_state *myfs_data = MYFS_DATA;
  FILE *log_file = myfs_data->logfile;
  int i, j, k, num_blocks, block_index;

  if (myfs_data->path_count > 1) {
    qsort(myfs_data->path_to_inode, (size_t)myfs_data->path_count,
          sizeof(struct path_inode), path_inode_cmp);
  }

  fprintf(log_file, "PATH_TO_INODE_MAP:\n");
  for (i = 0; i < myfs_data->path_count; i++)
    fprintf(log_file, "%s: %d\n", myfs_data->path_to_inode[i].path,
            myfs_data->path_to_inode[i].inode);

  fprintf(log_file, "INODE_BITMAP: [");
  for (i = 0; i < myfs_data->NUM_INODES; i++) {
    fprintf(log_file, "%d", myfs_data->inode_bitmap[i]);
    if (i != myfs_data->NUM_INODES - 1)
      fprintf(log_file, ", ");
  }
  fprintf(log_file, "]\n");

  fprintf(log_file, "DATA_BLOCK_BITMAP: [");
  for (i = 0; i < myfs_data->NUM_DATA_BLOCKS; i++) {
    fprintf(log_file, "%d", myfs_data->data_block_bitmap[i]);
    if (i != myfs_data->NUM_DATA_BLOCKS - 1)
      fprintf(log_file, ", ");
  }
  fprintf(log_file, "]\n");

  for (i = 0; i < myfs_data->NUM_INODES; i++) {
    fprintf(log_file, "inode%d: ", i);
    num_blocks = myfs_data->inodes[i]->num_blocks;
    for (j = 0; j < num_blocks; j++) {
      block_index = myfs_data->inodes[i]->blocks[j];
      for (k = 0; k < myfs_data->DATA_BLOCK_SIZE; k++)
        log_char(myfs_data->data_blocks[block_index]->data[k]);
    }
    fprintf(log_file, "\n");
  }
}

void log_msg(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(MYFS_DATA->logfile, format, ap);
  va_end(ap);
}

/* --- FUSE operations --- */
static void myfs_fullpath(char fpath[PATH_MAX], const char *path) {
  strcpy(fpath, MYFS_DATA->rootdir);
  strncat(fpath, path, PATH_MAX - 1);
  fpath[PATH_MAX - 1] = '\0';
}

/* Global logical size array (indexed by inode number) */
static off_t *g_inode_logical_size = NULL;

/* Helper: find lowest-index free inode. Returns -1 if none. */
static int find_free_inode(void) {
  struct myfs_state *s = MYFS_DATA;
  int i;
  for (i = 0; i < s->NUM_INODES; i++) {
    if (s->inode_bitmap[i] == 0)
      return i;
  }
  return -1;
}

/* Helper: find lowest-index free data block. Returns -1 if none. */
static int find_free_data_block(void) {
  struct myfs_state *s = MYFS_DATA;
  int i;
  for (i = 0; i < s->NUM_DATA_BLOCKS; i++) {
    if (s->data_block_bitmap[i] == 0)
      return i;
  }
  return -1;
}

/* Helper: count free data blocks */
static int count_free_data_blocks(void) {
  struct myfs_state *s = MYFS_DATA;
  int i, count = 0;
  for (i = 0; i < s->NUM_DATA_BLOCKS; i++) {
    if (s->data_block_bitmap[i] == 0)
      count++;
  }
  return count;
}

/* Helper: ensure inode has at least blocks_needed blocks.
 * Allocates additional blocks (lowest index first). Returns 0 on success, -1 if
 * not enough. */
static int allocate_blocks_for_append(struct inode *ino, int blocks_needed) {
  struct myfs_state *s = MYFS_DATA;
  int additional = blocks_needed - ino->num_blocks;
  int i;

  if (additional <= 0)
    return 0;
  if (additional > count_free_data_blocks())
    return -1;

  for (i = 0; i < additional; i++) {
    int blk = find_free_data_block();
    s->data_block_bitmap[blk] = 1;
    ino->blocks[ino->num_blocks] = blk;
    ino->num_blocks++;
  }
  return 0;
}

static int myfs_unlink(const char *path) {
  int res;
  char fpath[PATH_MAX];
  myfs_fullpath(fpath, path);

  log_msg("DELETE %s\n", path);

  /* Lookup inode, free its data blocks, clear inode and path map, reset logical
   * size. */
  {
    struct myfs_state *s = MYFS_DATA;
    int inode_idx = path_to_inode_lookup(s, path);
    if (inode_idx >= 0) {
      struct inode *ino = s->inodes[inode_idx];
      int i;
      for (i = 0; i < ino->num_blocks; i++) {
        int blk = ino->blocks[i];
        s->data_block_bitmap[blk] = 0;
        memset(s->data_blocks[blk]->data, 0, (size_t)s->DATA_BLOCK_SIZE);
      }
      ino->num_blocks = 0;
      s->inode_bitmap[inode_idx] = 0;
      path_to_inode_remove(s, path);
      g_inode_logical_size[inode_idx] = 0;
    }
  }

  res = unlink(fpath);
  if (res == -1) {
    log_msg("ERROR: DELETE %s\n", path);
    log_fuse_context();
    return -errno;
  }

  log_fuse_context();
  return 0;
}

static int myfs_create(const char *path, mode_t mode,
                       struct fuse_file_info *fi) {
  int res;                    // will store host open() result / fd
  char fpath[PATH_MAX];       // absolute path inside mirrored root
  myfs_fullpath(fpath, path); // convert FUSE path ("/a.txt") to real root path

  log_msg("CREATE %s\n", path); // required operation log line

  /* Find free inode; fail with INODES FULL if none. */
  {
    int inode_idx = find_free_inode(); // pick lowest free inode index
    if (inode_idx == -1) {             // no free inode available
      log_msg("ERROR: INODES FULL\n"); // required error log
      log_fuse_context();              // always log context after logs
      return -1;                       // assignment-specific failure code
    }
    MYFS_DATA->inode_bitmap[inode_idx] = 1;        // mark inode as allocated
    path_to_inode_add(MYFS_DATA, path, inode_idx); // bind path -> inode
    g_inode_logical_size[inode_idx] = 0; // new file has logical size 0
  }

  res = open(fpath, fi->flags, mode);    // create/open real mirrored file
  if (res == -1) {                       // host create/open failed
    log_msg("ERROR: CREATE %s\n", path); // operation-specific error log
    log_fuse_context();                  // required context log on failure
    return -errno;                       // propagate POSIX error to caller
  }

  fi->fh = (uint64_t)(unsigned long)res; // cache host fd in FUSE file handle
  log_fuse_context();                    // required context log on success
  return 0;                              // success
}

static int myfs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi) {
  int fd;                     // host fd for fallback read path
  ssize_t res;                // host pread result
  char fpath[PATH_MAX];       // mirrored absolute path
  myfs_fullpath(fpath, path); // build host path

  log_msg("READ %s\n", path); // required operation log

  /* Lookup inode, use logical size for total_size, log each block, copy from
   * data blocks to buf. */
  {
    struct myfs_state *s = MYFS_DATA; // shortcut to global FS state
    int inode_idx =
        path_to_inode_lookup(s, path);          // find inode owning this path
    if (inode_idx >= 0) {                       // tracked file found
      struct inode *ino = s->inodes[inode_idx]; // inode metadata pointer
      off_t total_size =
          g_inode_logical_size[inode_idx]; // logical EOF for this file
      int block_size = s->DATA_BLOCK_SIZE; // fixed block size
      off_t bytes_to_read = 0;             // result size from simulated storage

      if (offset < total_size) {     // only read if start before EOF
        bytes_to_read = (off_t)size; // initial request size
        if (bytes_to_read > total_size - offset) // clamp at EOF
          bytes_to_read = total_size - offset;
      }

      if (bytes_to_read > 0) { // any data actually requested/available
        off_t bytes_done = 0;  // bytes copied so far
        off_t cur = offset;    // current logical read cursor
        while (bytes_done < bytes_to_read) {        // consume requested range
          int bi = (int)(cur / block_size);         // inode block list index
          int off_in_blk = (int)(cur % block_size); // offset inside that block
          int actual_blk = ino->blocks[bi];         // physical data block index
          int chunk =
              block_size - off_in_blk; // max readable bytes in current block
          int k;
          if (chunk > (int)(bytes_to_read - bytes_done)) // trim last chunk
            chunk = (int)(bytes_to_read - bytes_done);

          fprintf(s->logfile,
                  "DATA BLOCK %d: ", actual_blk); // required block log prefix
          for (k = 0; k < chunk; k++)             // log exactly read bytes
            log_char(s->data_blocks[actual_blk]
                         ->data[off_in_blk + k]); // escaped-safe char log
          fprintf(s->logfile, "\n");              // end one block log line

          memcpy(buf + bytes_done, // destination in FUSE read buffer
                 s->data_blocks[actual_blk]->data +
                     off_in_blk, // source inside simulated block
                 (size_t)chunk); // byte count

          bytes_done += chunk; // advance copied count
          cur += chunk;        // advance logical cursor
        }
      }

      log_fuse_context();        // required context log for success path
      return (int)bytes_to_read; // return bytes served from simulated storage
    }
  }

  /* Fallback for files without inodes (e.g. directories) */
  if (fi == NULL)
    fd = open(fpath, O_RDONLY); // open host file directly if no cached fd
  else
    fd = (int)(unsigned long)fi->fh; // reuse cached fd from open/create

  if (fd == -1) { // host open failed
    log_msg("ERROR: READ %s\n", path);
    log_fuse_context();
    return -errno;
  }

  res = pread(fd, buf, size, offset); // positional host read
  if (res == -1) {                    // host read failure
    log_msg("ERROR: READ %s\n", path);
    if (fi == NULL)
      close(fd); // close only if we opened it here
    return -errno;
  }

  if (fi == NULL)
    close(fd); // cleanup temporary fd

  log_fuse_context(); // context log on fallback success
  return (int)res;    // bytes read from host fallback
}

static int myfs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi) {
  int fd;                     // host fd for mirror write
  ssize_t res;                // host pwrite result
  char fpath[PATH_MAX];       // absolute mirrored path
  myfs_fullpath(fpath, path); // build full path

  log_msg("WRITE %s\n", path); // required operation log

  /* Lookup inode; allocate blocks, copy data, update logical size. */
  {
    struct myfs_state *s = MYFS_DATA;              // FS state
    int inode_idx = path_to_inode_lookup(s, path); // find inode for file
    if (inode_idx >= 0) {                          // if tracked inode exists
      struct inode *ino = s->inodes[inode_idx];    // inode metadata
      int block_size = s->DATA_BLOCK_SIZE;         // block size constant
      off_t logical_size =
          g_inode_logical_size[inode_idx];  // current logical file size
      off_t new_end = offset + (off_t)size; // end position after this write
      off_t max_end = logical_size > new_end
                          ? logical_size
                          : new_end; // preserve holes/overwrite
      int blocks_needed = (int)((max_end + block_size - 1) /
                                block_size); // ceil(max_end / block_size)
      int additional =
          blocks_needed - ino->num_blocks; // extra blocks required now

      if (additional > 0 &&
          additional > count_free_data_blocks()) {  // capacity check
        log_msg("ERROR: NOT ENOUGH DATA BLOCKS\n"); // required error log
        log_fuse_context();                         // required context log
        return -1;                                  // abort write
      }

      if (additional > 0)
        allocate_blocks_for_append(
            ino, blocks_needed); // allocate lowest-index free blocks

      /* Write data into data blocks */
      {
        size_t bw = 0;                      // bytes written from input buffer
        off_t cur = offset;                 // logical cursor in file
        while (bw < size) {                 // consume entire input `buf`
          int bi = (int)(cur / block_size); // index into inode->blocks
          int off_in_blk = (int)(cur % block_size); // intra-block offset
          int chunk =
              block_size - off_in_blk;   // max bytes this block can take now
          if ((size_t)chunk > size - bw) // trim for final partial chunk
            chunk = (int)(size - bw);
          memcpy(s->data_blocks[ino->blocks[bi]]->data +
                     off_in_blk, // dst in simulated block
                 buf + bw,       // src from user buffer
                 (size_t)chunk); // bytes to copy
          bw += chunk;           // advance source buffer position
          cur += chunk;          // advance logical file position
        }
      }

      if (new_end > logical_size)                  // file grew beyond prior EOF
        g_inode_logical_size[inode_idx] = new_end; // update logical size
    }
  }

  (void)fi; // silence unused warning in some compilers
  if (fi == NULL)
    fd = open(fpath, O_WRONLY); // open host file if no cached handle
  else
    fd = (int)(unsigned long)fi->fh; // reuse file handle from FUSE open/create

  if (fd == -1) { // host open failed
    log_msg("ERROR: WRITE %s\n", path);
    log_fuse_context();
    return -errno;
  }

  res = pwrite(fd, buf, size, offset); // mirror write to backing file
  if (res == -1) {                     // host write failed
    log_msg("ERROR: WRITE %s\n", path);
    log_fuse_context();
    if (fi == NULL)
      close(fd);
    return -errno;
  }

  if (fi == NULL)
    close(fd); // close temporary host fd

  log_fuse_context(); // required context log on success
  return (int)res;    // bytes written
}

static void *myfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
  (void)conn;
  cfg->use_ino = 1;
  cfg->entry_timeout = 0;
  cfg->attr_timeout = 0;
  cfg->negative_timeout = 0;
  cfg->direct_io = 1;

  {
    struct myfs_state *s = MYFS_DATA;
    g_inode_logical_size =
        (off_t *)calloc((size_t)s->NUM_INODES, sizeof(off_t));
  }

  return MYFS_DATA;
}

static int myfs_getattr(const char *path, struct stat *stbuf,
                        struct fuse_file_info *fi) {
  int res;
  char fpath[PATH_MAX];
  (void)fi;
  myfs_fullpath(fpath, path);

  res = lstat(fpath, stbuf);
  if (res == -1)
    return -errno;
  return 0;
}

static int myfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags) {
  DIR *dp;
  struct dirent *de;
  char fpath[PATH_MAX];

  (void)offset;
  (void)fi;
  (void)flags;
  myfs_fullpath(fpath, path);

  dp = opendir(fpath);
  if (dp == NULL)
    return -errno;

  while ((de = readdir(dp)) != NULL) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;
    if (filler(buf, de->d_name, &st, 0, (enum fuse_fill_dir_flags)0))
      break;
  }

  closedir(dp);
  return 0;
}

static int myfs_mkdir(const char *path, mode_t mode) {
  int res;
  char fpath[PATH_MAX];
  myfs_fullpath(fpath, path);

  res = mkdir(fpath, mode);
  if (res == -1)
    return -errno;
  return 0;
}

static int myfs_rmdir(const char *path) {
  int res;
  char fpath[PATH_MAX];
  myfs_fullpath(fpath, path);

  res = rmdir(fpath);
  if (res == -1)
    return -errno;
  return 0;
}

static int myfs_open(const char *path, struct fuse_file_info *fi) {
  int res;
  char fpath[PATH_MAX];
  myfs_fullpath(fpath, path);

  res = open(fpath, fi->flags);
  if (res == -1)
    return -errno;
  fi->fh = (uint64_t)(unsigned long)res;
  return 0;
}

static int myfs_release(const char *path, struct fuse_file_info *fi) {
  (void)path;
  close((int)(unsigned long)fi->fh);
  return 0;
}

static const struct fuse_operations myfs_oper = {
    .getattr = myfs_getattr,
    .mkdir = myfs_mkdir,
    .unlink = myfs_unlink,
    .rmdir = myfs_rmdir,
    .open = myfs_open,
    .read = myfs_read,
    .write = myfs_write,
    .release = myfs_release,
    .readdir = myfs_readdir,
    .init = myfs_init,
    .create = myfs_create,
};

void myfs_usage(void) {
  fprintf(stderr, "usage:  myfs [FUSE and mount options] mount_point log_file "
                  "root_dir num_inodes num_data_blocks data_block_size\n");
  abort();
}

int main(int argc, char *argv[]) {
  int fuse_stat;
  struct myfs_state *myfs_data;
  FILE *logf;

  if ((getuid() == 0) || (geteuid() == 0)) {
    fprintf(stderr,
            "Running BBFS as root opens unnacceptable security holes\n");
    return 1;
  }

  fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION,
          FUSE_MINOR_VERSION);

  if ((argc < 6) || (argv[argc - 6][0] == '-') || (argv[argc - 5][0] == '-') ||
      (argv[argc - 4][0] == '-'))
    myfs_usage();

  logf = log_open(argv[argc - 5]);
  myfs_data = myfs_state_create(logf, argv[argc - 4], atoi(argv[argc - 3]),
                                atoi(argv[argc - 2]), atoi(argv[argc - 1]));
  if (!myfs_data) {
    fclose(logf);
    fprintf(stderr, "myfs_state_create failed\n");
    return 1;
  }

  argc -= 5;

  fprintf(stderr, "about to call fuse_main\n");
  fuse_stat = fuse_main(argc, argv, &myfs_oper, myfs_data);
  fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

  myfs_state_destroy(myfs_data);
  return fuse_stat;
}