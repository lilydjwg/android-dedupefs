/*
 * dedupefs - The filesystem for Android dedupe backup.
 *
 * Most code is copied from rofs.
 * GPLv2.
 */


#define FUSE_USE_VERSION 26

static const char *dedupefsVersion = "0.1";

#include<sys/types.h>
#include<sys/stat.h>
#include<sys/statvfs.h>
#include<stdio.h>
#include<strings.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/xattr.h>
#include<dirent.h>
#include<unistd.h>
#include<fuse.h>
#include<gdbm.h>

GDBM_FILE db;
char *dbpath;

#pragma pack(push, 1)
struct fentry {
  char type;
  char pad;
  unsigned short mode;
  uint32_t uid;
  uint32_t gid;
  uint64_t atime;
  uint64_t mtime;
  uint64_t ctime;
  uint64_t size;
  char* path;
  char* extra;
};
#pragma pack(pop)

/* Translate an dedupefs path into it's underlying filesystem path */
static char *translate_path(const char *path){

  char *rPath = malloc(sizeof(char) * (strlen(path) + strlen(dbpath) + 1));

  strcpy(rPath, dbpath);
  if(rPath[strlen(rPath) - 1] == '/'){
    rPath[strlen(rPath) - 1] = '\0';
  }
  strcat(rPath, path);

  return rPath;
}

static void db_fetch_for_path(const char* path, char type, datum* result){
  int plen = strlen(path) + 1;
  char *p = malloc(plen);
  p[0] = type;
  memcpy(p+1, path, plen);
  fwrite(p, plen, 1, stdout);
  printf(" fetched for path %s, plen=%d.\n", path, plen);

  datum key = {
    .dptr = p,
    .dsize = plen
  };

  *result = gdbm_fetch(db, key);
  free(p);
}

static void parse_file_info(const datum *d, struct stat* st){
  struct fentry *f = (struct fentry*) d->dptr;
  //FIXME
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
}

static int callback_getattr(const char *path, struct stat *st_data){
  datum result;
  db_fetch_for_path(path, 'f', &result);
  if(result.dptr == NULL){
    return -ENOENT;
  }
  parse_file_info(&result, st_data);
  printf("done getattr with %s.\n", path);
  return 0;
}

static int callback_readlink(const char *path, char *buf, size_t size){
  int res;
  char *ipath;
  ipath = translate_path(path);

  res = readlink(ipath, buf, size - 1);
  free(ipath);
  if(res == -1){
    return -errno;
  }
  buf[res] = '\0';
  return 0;
}

static int callback_readdir(const char *path, void *buf,
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
  (void)offset;
  (void)fi;

  datum result;
  db_fetch_for_path(path, 'd', &result);
  if(result.dptr == NULL){
    return -ENOTDIR;
  }

  char *name = result.dptr;
  while(*name){
    filler(buf, name, NULL, 0);
    name += strlen(name) + 1;
  }

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
  int res;

  /* We allow opens, unless they're tring to write, sneaky
   * people.
   */
  int flags = finfo->flags;

  if((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_CREAT)
      || (flags & O_EXCL) || (flags & O_TRUNC) || (flags & O_APPEND)){
    return -EROFS;
  }
  char *ipath;
  ipath = translate_path(path);

  res = open(ipath, flags);

  free(ipath);
  if(res == -1){
    return -errno;
  }
  close(res);
  return 0;
}

static int callback_read(const char *path, char *buf, size_t size,
    off_t offset, struct fuse_file_info *finfo){
  int fd;
  int res;
  (void)finfo;
  char *ipath;

  ipath = translate_path(path);
  fd = open(ipath, O_RDONLY);
  free(ipath);
  if(fd == -1){
    res = -errno;
    return res;
  }
  res = pread(fd, buf, size, offset);

  if(res == -1){
    res = -errno;
  }
  close(fd);
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
  char *ipath;
  ipath = translate_path(path);

  res = statvfs(path, st_buf);
  free(ipath);
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
  (void)finfo;
  return 0;
}

static int callback_access(const char *path, int mode){
  int res;
  char *ipath;
  ipath = translate_path(path);

  /* Don't pretend that we allow writing
   * Chris AtLee <chris@atlee.ca>
   */
  if(mode & W_OK)
    return -EROFS;

  res = access(ipath, mode);
  free(ipath);
  if(res == -1){
    return -errno;
  }
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
      "usage: %s dbpath mountpoint [options]\n"
      "\n"
      "   Mounts Android dedupe backup as a read-only mount at mountpoint\n"
      "\n"
      "general options:\n"
      "   -o opt,[opt...]     mount options\n"
      "   -h  --help          print help\n"
      "   -V  --version       print version\n" "\n", progname);
}

static int dedupefs_parse_opt(void *data, const char *arg, int key,
    struct fuse_args *outargs){
  (void)data;

  switch (key){
  case FUSE_OPT_KEY_NONOPT:
    if(dbpath == 0){
      dbpath = strdup(arg);
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
  if(dbpath == 0){
    fprintf(stderr, "Missing dbpath\n");
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

  res = fuse_main(args.argc, args.argv, &callback_oper, NULL);
  gdbm_close(db);
  return res;
}
