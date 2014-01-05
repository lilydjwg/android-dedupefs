/* ===================================================================== *
 *  
 * ===================================================================== */
#include<string.h>
#include<stdint.h>
#include<stdlib.h>
#include<stdio.h>

#define BUF_LEN 4096
/* --------------------------------------------------------------------- */
enum FileType { T_FILE, T_DIR, T_SYMLINK };

typedef struct fentry {
  enum FileType type;
  unsigned mode;
  int uid;
  int gid;
  uint64_t atime;
  uint64_t mtime;
  uint64_t ctime;
  char sha256[64];
  char name[1];
} fentry_t;

/*
 * returns:
 *    0: eof
 *   -1: error
 *    _: size of fentry used
 */
int fentry_read_one(FILE *fp, fentry_t **fentry){
  static char buf[BUF_LEN];
  static char buf2[BUF_LEN];
  int ret;
  char type;
  fentry_t *fe = *fentry;
  ret = fscanf(fp, "%c %o %d %d %lu %lu %lu %[^ \t\n] %[^\n]\n",
      &type,
      &fe->mode, &fe->uid, &fe->gid,
      &fe->atime, &fe->mtime, &fe->ctime,
      buf, buf2
  );
  if(ret == EOF){
    return 0;
  }else if(ret != 8){
    fprintf(stderr, "ERROR: bad entry at %ld\n", ftell(fp));
    return -1;
  }
  switch(type){
    case 'f':
      fe->type = T_FILE;
      break;
    case 'd':
      fe->type = T_DIR;
      break;
    case 'l':
      fe->type = T_SYMLINK;
      break;
    default:
      fprintf(stderr, "ERROR: unknown file type %c\n", type);
  }
  fprintf(stderr, "DEBUG: buf is %s\n", buf+2);
  strcpy(fe->name, buf+2);
  int l;
  l = strlen(buf) + sizeof(fentry_t) - sizeof(char);
  *fentry = realloc(fe, l);
  return l;
}

int main(int argc, char **argv){
  fentry_t *f = malloc(BUF_LEN);
  int l;
  fgets((char*)f, BUF_LEN, stdin);
  while(1){
    l = fentry_read_one(stdin, &f);
    if(l <= 0){
      break;
    }
    printf("TYPE: %d, SIZE: %d, NAME: %s.\n", f->type, l, f->name);
  }
  free(f);
  return 0;
}
/* ===================================================================== *
 * vim modeline                                                          *
 * vim:se fdm=expr foldexpr=getline(v\:lnum)=~'^\\S.*{'?'>1'\:1:         *
 * ===================================================================== */
