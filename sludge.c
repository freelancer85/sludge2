#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

//sludge magic bytes, found in start of a sludge archive
#define SLUDGE_MAGIC "Sludge0.1"

//maximum length of the name in record
#define SLUDGE_NAME_LEN 250

struct sludge_record {
  char   name[SLUDGE_NAME_LEN + 1];
  size_t size;
};

static int read_record(FILE *fp, struct sludge_record * rec){
  size_t n;
  //read record from file
  if( ((n = fread(rec->name, 1, SLUDGE_NAME_LEN, fp)) != SLUDGE_NAME_LEN) ||
      ((n = fread(&rec->size, sizeof(size_t), 1, fp)) != 1)   ){
    if(n != 0){
      perror("read");
    }
    return -1;
  }

  //termiante the name string
  rec->name[SLUDGE_NAME_LEN] = '\0';

  return 0;
}

static int sludge_check_magick(FILE *fp){
  char buf[sizeof(SLUDGE_MAGIC)];

  //read the magic bytes
  if(fread(buf, 1, sizeof(buf), fp) < sizeof(buf)){
    perror("read");
    return -1;
  }

  //check the magic bytes are correct
  if(strncmp(buf, SLUDGE_MAGIC, sizeof(buf)) != 0){
    fprintf(stderr, "Error: Not a sludge archive\n");
    return -1;
  }

  return 0;
}

//Open a sludge archive for reading
static FILE* sludge_open(const char *filename){

  //open the file
  FILE * fp = fopen(filename, "r");
  if(fp == NULL){
    perror("fopen");
    return NULL;
  }

  if(sludge_check_magick(fp) < 0){
    fclose(fp);
    return NULL;
  }

  return fp;
}

static FILE* sludge_creat(const char *filename){

  FILE* fp;

  //check if file exists
  const int exists = (access(filename, R_OK) == 0);
  if(exists){  //if file exists
    fp = fopen(filename, "r+");
  }else{
    fp = fopen(filename, "w");
  }

  if(fp == NULL){
    perror("fopen");
    return NULL;
  }

  if(exists){
    //check magic of existing file
    if(sludge_check_magick(fp) < 0){
      fclose(fp);
      return NULL;
    }

    //seek to the end of file, since we are appending
    if(fseek(fp, 0L, SEEK_END) == -1){
      perror("fseek");
      fclose(fp);
      return NULL;
    }
  }else{
    //save magic to a new file
    if(fwrite(SLUDGE_MAGIC, 1, sizeof(SLUDGE_MAGIC), fp) != sizeof(SLUDGE_MAGIC)){
      perror("fwrite");
      fclose(fp);
      return NULL;
    }
  }

  return fp;
}

//List contents of a sludge archive
static int sludge_list(const char *filename){
  struct sludge_record rec;

  //open the sludge archive for reading only
  FILE* fp = sludge_open(filename);
  if(fp == NULL){
    return -1;
  }

  //iterate over each record in file
  while(read_record(fp, &rec) == 0){
    //print record to screen, static length fields first
    printf("Size=%-10lu\tName=%s\n", rec.size, rec.name);

    //skip the contents
    if(fseek(fp, rec.size, SEEK_CUR) == -1){
      perror("fseek");
      break;
    }
  }
  fclose(fp);

  return 0;
}

//Copy all data between two file descriptors
static int copy_all(FILE* from, FILE* to, const size_t size){
  char buf[100];
  ssize_t n;        //bytes read/write
  size_t total = 0; //total bytes written

  size_t left = (size > sizeof(buf)) ? sizeof(buf) : size;

  //read from and write to
  while((n = fread(buf, 1, left, from)) > 0){
    if(fwrite(buf, 1, n, to) != n){
      perror("fwrite");
    }
    total += n;
    if(total >= size){
      break;
    }
    left = ((size - total)> sizeof(buf)) ? sizeof(buf) : (size - total);
  }

  if(n < 0){  //if read had an error
    perror("fread");
    return -1;
  }

  return total;
}

//Append the file to archive
static int sludge_append(FILE* afp, const char * filename){
  struct stat st;
  struct sludge_record rec;
  FILE* ffd;

  //get file statistics
  if(stat(filename, &st) == -1){
    perror("stat");
    return -1;
  }

  ffd = fopen(filename, "r");
  if(ffd == NULL){
    perror("fopen");
    return -1;
  }

  //fill in the record
  strncpy(rec.name, filename, SLUDGE_NAME_LEN);
  rec.size = st.st_size;

  //save record to file
  if( (fwrite(rec.name, 1, SLUDGE_NAME_LEN, afp)    != SLUDGE_NAME_LEN) ||
      (fwrite(&rec.size, sizeof(size_t), 1, afp)  != 1)  ){
    perror("fwrite");
    return -1;
  }

  //save file contents to archive
  if(copy_all(ffd, afp, rec.size) < 0){
    fclose(ffd);
    return -1;
  }

  fclose(ffd);
  return 0;
}

//Find a record by name
static int sludge_find(FILE* fp, struct sludge_record * rec, const char * filename){
  int rv = 0;

  //move to file beginning, saving the current file position
  const int old_pos = ftell(fp);
  if(old_pos == -1){
    perror("fseek");
    return 0;
  }

  if(fseek(fp, sizeof(SLUDGE_MAGIC), SEEK_SET) == -1){
    perror("fseek");
    return 0;
  }

  //iterate over each record in file
  while(read_record(fp, rec) == 0){
    //if filenames match
    if(strcmp(rec->name, filename) == 0){
      //at this point file records is parsed, and offset is at file contents!
      rv = ftell(fp); //return file content offset
      break;
    }

    //skip file contents
    if(fseek(fp, rec->size, SEEK_CUR) == -1){
      perror("fseek");
      break;
    }
  }

  //restore file position, as it was in beginning
  if(fseek(fp, old_pos, SEEK_SET) == (off_t)-1){
    perror("fseek");
    return 0;
  }

  return rv;
}

static int sludge_add(const char *filename, const int num_files, char * const files[]){
  int i, rv = 0;
  struct sludge_record rec;

  //create a a new archive, or open existing one (in append mode)
  FILE * fp = sludge_creat(filename);
  if(fp == NULL){
    return -1;
  }

  //for each file in array of filenames
  for(i=0; i < num_files; i++){
    // before we add file, we must check if its not a duplicate
    if(sludge_find(fp, &rec, files[i]) > 0){  //if file exists
      fprintf(stderr, "Error: File %s already in archive\n", files[i]);
      rv = -1;  //return error code
      break;
    }

    if(sludge_append(fp, files[i]) < 0){  //if append failed
      fprintf(stderr, "Error: Failed to append file %s\n", files[i]);
      rv = -1;  //return error code
      break;
    }
  }
  fclose(fp);

  return rv;
}

//Extract a single file from archive
static int sludge_extract_file(FILE * afp, const char *filename){
  struct sludge_record rec;
  off_t off = 0;

  if(access(filename, F_OK) == 0){
    printf("Error: Can't overwrite %s\n", filename);
    return -1;
  }

  //try to create the file, if it doesn't exist
  FILE * fp = fopen(filename, "w");
  if(fp == NULL){
    perror("open");
    return -1;
  }
  printf("extracting file: %s\n", filename);

  //find the record in file
  if((off = sludge_find(afp, &rec, filename)) > 0){

    //file was found, seek to content position
    if(fseek(afp, off, SEEK_SET) == -1){
      perror("fseek");
    }
    //copy contents to output file
    copy_all(afp, fp, rec.size);
  }
  fclose(fp);
  return 0;
}

static int sludge_extract_each(FILE * afp){
  struct sludge_record rec;

  //iterate over each record in file
  while(read_record(afp, &rec) == 0){

    if(access(rec.name, F_OK) == 0){
      printf("Error: Can't overwrite %s\n", rec.name);
      return -1;
    }

    printf("extracting file: %s\n", rec.name);

    //try to create the file, if it doesn't exist
    FILE * fp = fopen(rec.name, "w");
    if(fp == NULL){
      perror("fopen");
      return -1;
    }

    //copy contents to output file
    copy_all(afp, fp, rec.size);
    fclose(fp);
  }
  return 0;
}

//Extract a group of files from archive
static int sludge_extract(const char *filename, const int num_files, char * const files[]){
  int i, rv = 0;

  //create a a new archive, or open existing one (in append mode)
  FILE * fp = sludge_open(filename);
  if(fp == NULL){
    return -1;
  }

  if(num_files == 0){
    rv = sludge_extract_each(fp);
  }else{
    //for each file in array of filenames
    for(i=0; i < num_files; i++){
      if(sludge_extract_file(fp, files[i]) < 0){
        rv = -1;
        break;
      }
    }
  }
  fclose(fp);

  return rv;
}

int main(const int argc, char * const argv[]){
  int rv = EXIT_SUCCESS;

  //check if we have needed arguments
  if(argc < 3){
    fprintf(stderr, "Usage: sludge {-lae} {archive.sludge} [files..]\n");
    return EXIT_FAILURE;
  }
  const char * mode    = argv[1];
  const char * archive = argv[2];

  if(strcmp(mode, "-l") == 0){      // list mode
    if(sludge_list(archive) < 0){
      rv = EXIT_FAILURE;
    }
  }else if(strcmp(mode, "-a") == 0){  // append/create mode
    if(sludge_add(archive, argc - 3, &argv[3]) < 0){
      rv = EXIT_FAILURE;
    }

  }else if(strcmp(mode, "-e") == 0){  // extract mode
    if(sludge_extract(archive, argc - 3, &argv[3]) < 0){
      rv = EXIT_FAILURE;
    }
  }

  return rv;
}
