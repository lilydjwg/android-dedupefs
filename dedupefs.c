/*
 * dedupefs - A filesystem for Android dedupe backup.
 *
 * Most code is copied from rofs.
 * GPLv2.
 */


#define FUSE_USE_VERSION 26

static const char *dedupefsVersion = "0.1";

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<unistd.h>
#include<fuse.h>
#include<gdbm.h>
#include<pthread.h>
#include<sys/statvfs.h>

GDBM_FILE db;
pthread_mutex_t dblock = PTHREAD_MUTEX_INITIALIZER;
char *dbpath;
char *srcdir;
unsigned long blksize;

#define PATH_LEN 4096
#pragma pack(push, 1)
struct fentry {
  unsigned short mode;
  char type;
  char pad[5];
  uint32_t uid;
  uint32_t gid;
  uint64_t atime;
  uint64_t mtime;
  uint64_t ctime;
  uint64_t size;
  char extra[1];
};
#pragma pack(pop)

#define NONEXIST_RETURN(x) \
  if((x).dptr == NULL){ \
    return -ENOENT; \
  }
#define FENTRY(x) ((struct fentry*)(x).dptr)
#define MIN(x, y) ((x) < (y) ? (x) : (y))

static void db_fetch_for_path(const char* path, char type, datum* result){
  int plen = strlen(path) + 1;
  char *p = malloc(plen+1);
  p[0] = type;
  memcpy(p+1, path, plen);
  fwrite(p, plen, 1, stdout);
  printf(" fetched for path %s, plen=%d.\n", path, plen);

  datum key = {
    .dptr = p,
    .dsize = plen
  };

  pthread_mutex_lock(&dblock);
  *result = gdbm_fetch(db, key);
  pthread_mutex_unlock(&dblock);
  free(p);
}

static void fileinfo2stat(const datum *d, struct stat* st){
  struct fentry *f = FENTRY(*d);
  st->st_mode = f->mode;
  switch(f->type){
    case 'f':
      st->st_mode |= S_IFREG;
      break;
    case 'l':
      st->st_mode |= S_IFLNK;
      break;
    case 'd':
      st->st_mode |= S_IFDIR;
      break;
    default:
      fprintf(stderr, "bad file type %c.\n", f->type);
  }
  st->st_nlink = 1;
  st->st_uid = f->uid;
  st->st_gid = f->gid;
  st->st_size = f->size;
  st->st_atime = f->atime;
  st->st_mtime = f->mtime;
  st->st_ctime = f->ctime;
  st->st_blksize = blksize;
  st->st_blocks = f->size / 512;
  if(f->size % 512 != 0){
    st->st_blocks++;
  }
}

static int callback_getattr(const char *path, struct stat *st_data){
  datum result;
  db_fetch_for_path(path, 'f', &result);
  NONEXIST_RETURN(result);
  fileinfo2stat(&result, st_data);
  free(result.dptr);
  printf("done getattr with %s.\n", path);
  return 0;
}

static int callback_readlink(const char *path, char *buf, size_t size){
  datum result;
  db_fetch_for_path(path, 'f', &result);
  NONEXIST_RETURN(result);

  int res = 0;
  char *link = FENTRY(result)->extra;
  if(FENTRY(result)->type != 'l'){
    res = -EINVAL;
  }else{
    if(link[0] == '/'){
      /* force to use relative path */
      const char *p = path;
      int n;
      while(*p++){
        if(*p == '/'){
          n = MIN(size, 3);
          memcpy(buf, "../", n);
          buf += n;
          size -= n;
        }
      }
      /* strip data or system; this will be correct for the most of time */
      link = strchr(link+1, '/') + 1;
    }
    memcpy(buf, link, MIN(size, strlen(link)+1));
  }
  free(result.dptr);
  return res;
}

static int callback_readdir(const char *path, void *buf,
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
  (void)offset;
  (void)fi;

  datum result;
  db_fetch_for_path(path, 'd', &result);
  if(result.dptr == NULL){
    //TODO: ENOTDIR or ENOENT
    return -ENOTDIR;
  }

  char *name = result.dptr;
  filler(buf, "..", NULL, 0);
  printf("readdir: [%s]\n", name);
  while(name < result.dptr + result.dsize){
    printf("readdir: [%s]\n", name);
    filler(buf, name, NULL, 0);
    name += strlen(name) + 1;
  }
  free(result.dptr);

  return 0;
}

static int callback_mknod(const char *path, mode_t mode, dev_t rdev){
  (void)path;
  (void)mode;
  (void)rdev;
  return -EROFS;
}

static int callback_mkdir(const char *path, mode_t mode){
  (void)path;
  (void)mode;
  return -EROFS;
}

static int callback_unlink(const char *path){
  (void)path;
  return -EROFS;
}

static int callback_rmdir(const char *path){
  (void)path;
  return -EROFS;
}

static int callback_symlink(const char *from, const char *to){
  (void)from;
  (void)to;
  return -EROFS;
}

static int callback_rename(const char *from, const char *to){
  (void)from;
  (void)to;
  return -EROFS;
}

static int callback_link(const char *from, const char *to){
  (void)from;
  (void)to;
  return -EROFS;
}

static int callback_chmod(const char *path, mode_t mode){
  (void)path;
  (void)mode;
  return -EROFS;

}

static int callback_chown(const char *path, uid_t uid, gid_t gid){
  (void)path;
  (void)uid;
  (void)gid;
  return -EROFS;
}

static int callback_truncate(const char *path, off_t size){
  (void)path;
  (void)size;
  return -EROFS;
}

static int callback_utime(const char *path, struct utimbuf *buf){
  (void)path;
  (void)buf;
  return -EROFS;
}

static int callback_open(const char *path, struct fuse_file_info *finfo){
  int flags = finfo->flags;

  if((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_CREAT)
      || (flags & O_EXCL) || (flags & O_TRUNC) || (flags & O_APPEND)){
    return -EROFS;
  }

  char fpath[PATH_LEN];
  datum result;
  db_fetch_for_path(path, 'f', &result);
  NONEXIST_RETURN(result);

  strncpy(fpath, srcdir, PATH_LEN);
  fpath[MIN(strlen(srcdir), PATH_LEN-1)] = '/';
  fpath[MIN(strlen(srcdir)+1, PATH_LEN-1)] = '\0';
  strncat(fpath, FENTRY(result)->extra, PATH_LEN);
  free(result.dptr);
  printf("opening %s.\n", fpath);

  int fd;
  fd = open(fpath, flags);

  if(fd == -1){
    return -errno;
  }else{
    finfo->fh = fd;
    return 0;
  }
}

static int callback_read(const char *path, char *buf, size_t size,
    off_t offset, struct fuse_file_info *finfo){

  size_t res;
  lseek(finfo->fh, offset, SEEK_SET);
  res = read(finfo->fh, buf, size);
  if(res == -1){
    res = -errno;
  }
  return res;
}

static int callback_write(const char *path, const char *buf, size_t size,
    off_t offset, struct fuse_file_info *finfo){
  (void)path;
  (void)buf;
  (void)size;
  (void)offset;
  (void)finfo;
  return -EROFS;
}

static int callback_statfs(const char *path, struct statvfs *st_buf){
  int res;
  res = statvfs(srcdir, st_buf);

  if(res == -1){
    return -errno;
  }

  return 0;
}

static int callback_release(const char *path, struct fuse_file_info *finfo){
  (void)path;
  (void)finfo;
  return 0;
}

static int callback_fsync(const char *path, int crap,
    struct fuse_file_info *finfo){
  (void)path;
  (void)crap;
  close(finfo->fh);
  return 0;
}

static int callback_access(const char *path, int mode){
  if(mode & W_OK)
    return -EROFS;

  datum result;
  int res;

  db_fetch_for_path(path, 'f', &result);
  NONEXIST_RETURN(result);

  if(mode & X_OK && !(FENTRY(result)->mode & S_IXUSR)){
    res = -EACCES;
  }else{
    res = 0;
  }
  free(result.dptr);
  return res;
}

struct fuse_operations callback_oper = {
  .getattr = callback_getattr,
  .readlink = callback_readlink,
  .readdir = callback_readdir,
  .mknod = callback_mknod,
  .mkdir = callback_mkdir,
  .symlink = callback_symlink,
  .unlink = callback_unlink,
  .rmdir = callback_rmdir,
  .rename = callback_rename,
  .link = callback_link,
  .chmod = callback_chmod,
  .chown = callback_chown,
  .truncate = callback_truncate,
  .utime = callback_utime,
  .open = callback_open,
  .read = callback_read,
  .write = callback_write,
  .statfs = callback_statfs,
  .release = callback_release,
  .fsync = callback_fsync,
  .access = callback_access,
};

enum {
  KEY_HELP,
  KEY_VERSION,
};

static void usage(const char *progname){
  fprintf(stdout,
      "usage: %s dbpath srcdir mountpoint [options]\n"
      "\n"
      "   Mounts Android dedupe backup as a read-only mount at mountpoint\n"
      "\n"
      "general options:\n"
      "   -o opt[,opt...]     mount options\n"
      "   -h  --help          print help\n"
      "   -V  --version       print version\n" "\n", progname);
}

static int dedupefs_parse_opt(void *data, const char *arg, int key,
    struct fuse_args *outargs){
  (void)data;

  switch (key){
  case FUSE_OPT_KEY_NONOPT:
    if(dbpath == NULL){
      dbpath = strdup(arg);
      return 0;
    }else if(srcdir == NULL){
      srcdir = strdup(arg);
      return 0;
    }else{
      return 1;
    }
  case FUSE_OPT_KEY_OPT:
    return 1;
  case KEY_HELP:
    usage(outargs->argv[0]);
    exit(0);
  case KEY_VERSION:
    fprintf(stdout, "dedupefs version %s\n", dedupefsVersion);
    exit(0);
  default:
    fprintf(stderr, "see `%s -h' for usage\n", outargs->argv[0]);
    exit(1);
  }
  return 1;
}

static struct fuse_opt dedupefs_opts[] = {
  FUSE_OPT_KEY("-h", KEY_HELP),
  FUSE_OPT_KEY("--help", KEY_HELP),
  FUSE_OPT_KEY("-V", KEY_VERSION),
  FUSE_OPT_KEY("--version", KEY_VERSION),
  FUSE_OPT_END
};

int main(int argc, char *argv[]){
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  int res;

  res = fuse_opt_parse(&args, &dbpath, dedupefs_opts, dedupefs_parse_opt);
  if(res != 0){
    fprintf(stderr, "Invalid arguments\n");
    fprintf(stderr, "see `%s -h' for usage\n", argv[0]);
    exit(1);
  }
  if(dbpath == NULL || srcdir == NULL){
    fprintf(stderr, "Missing dbpath or srcdir\n");
    fprintf(stderr, "see `%s -h' for usage\n", argv[0]);
    exit(1);
  }

  db = gdbm_open(dbpath, 0, GDBM_READER, 0, NULL);
  if(!db){
    fprintf(stderr, "fatal: database \"%s\" can't be opened as GNU dbm.\n",
        dbpath);
    exit(2);
  }
  free(dbpath);
  dbpath = NULL;

  struct statvfs vfs;
  if(statvfs(srcdir, &vfs) != 0){
    perror("statvfs");
    exit(2);
  }
  blksize = vfs.f_bsize;

  char *p = NULL;
  p = realpath(srcdir, NULL);
  if(!p){
    perror("realpath for srcdir");
    exit(2);
  }
  free(srcdir);
  srcdir = p;

  res = fuse_main(args.argc, args.argv, &callback_oper, NULL);
  gdbm_close(db);
  free(srcdir);
  return res;
}
