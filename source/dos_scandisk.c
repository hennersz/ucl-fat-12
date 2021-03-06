#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

typedef struct ClusterNode {
  uint16_t clusterNum;
  struct ClusterNode* nextNode; 
} ClusterNode;

typedef struct FileHeader {
  uint16_t fileSize;
  uint16_t noOfClusters;
  char name[15];
  struct FileHeader* nextFile;
  ClusterNode* startCluster;
} FileHeader;

typedef struct FileList {
  FileHeader *first;
  FileHeader *last;
} FileList;

void addCluster(FileHeader *header, uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb, ClusterNode *prev){
  ClusterNode *newNode = malloc(sizeof(ClusterNode));
  newNode->clusterNum = cluster;
  newNode->nextNode = NULL;
  if(prev == NULL){
    header->startCluster = newNode;
  } else {
    prev->nextNode = newNode;
  }
  header->noOfClusters += 1;
  uint16_t nextCluster = get_fat_entry(cluster, image_buf, bpb);
  if(is_end_of_file(nextCluster)){
    return;
  } else {
    addCluster(header, nextCluster, image_buf, bpb, newNode);
  }
}

void createFile(FileList *files, struct direntry* dirent, uint8_t *image_buf, struct bpb33* bpb){
  FileHeader *header = malloc(sizeof(FileHeader));
  header->fileSize = getulong(dirent->deFileSize);
  header->noOfClusters = 0;
  char name[9];
  char extension[4];
  name[8] = ' ';
  extension[3] = ' ';
  memcpy(name, &(dirent->deName[0]), 8);
  memcpy(extension, dirent->deExtension, 3);
  int i;
  for (i = 8; i > 0; i--) {
    if (name[i] == ' ')
      name[i] = '\0';
    else
      break;
  }

  /* remove the spaces from extensions */
  for (i = 3; i > 0; i--) {
    if (extension[i] == ' ')
      extension[i] = '\0';
    else
      break;
  }

  strcpy(header->name, name);
  strcat(header->name, ".");
  strcat(header->name, extension);
  header->nextFile = NULL;
  header->startCluster = NULL; 
  
  if(files->first == NULL){
    files->first = header;
    files->last = header;
  } else {
    files->last->nextFile = header;
    files->last = header;
  }
  uint16_t file_cluster = getushort(dirent->deStartCluster);
  addCluster(header, file_cluster, image_buf, bpb, NULL);
}

void followFileClusters(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb, uint16_t* seenClusters){
  seenClusters[cluster] = 1;
  uint16_t nextCluster = get_fat_entry(cluster, image_buf, bpb);
  if(is_end_of_file(nextCluster)){
    return;
  } else {
    followFileClusters(nextCluster, image_buf, bpb, seenClusters);
  }
}

void scanFiles(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb, uint16_t* seenClusters, FileList *files){
  struct direntry *dirent;
  int d, i;
  dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);
  while(1){
    for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; d += sizeof(struct direntry)) {
      char name[9];
      uint16_t file_cluster;
      name[8] = ' ';
      memcpy(name, &(dirent->deName[0]), 8);
      if (name[0] == SLOT_EMPTY)
        return;

      /* skip over deleted entries */
      if (((uint8_t)name[0]) == SLOT_DELETED)
        continue;
     
      /* namMaes are space padded - remove the spaces */
      for (i = 8; i > 0; i--) {
        if (name[i] == ' ')
          name[i] = '\0';
        else
          break;
      }

      /* don't print "." or ".." directories */
      if (strcmp(name, ".")==0) {
        dirent++;
        continue;
      }
      if (strcmp(name, "..")==0) {
        dirent++;
        continue;
      }

      if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
        continue;
      } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
        printf("Directory size: %hhu\n", *dirent->deFileSize);
        file_cluster = getushort(dirent->deStartCluster);
        followFileClusters(file_cluster, image_buf, bpb, seenClusters);
        scanFiles(file_cluster, image_buf, bpb, seenClusters, files);
      } else {
        file_cluster = getushort(dirent->deStartCluster);
        followFileClusters(file_cluster, image_buf, bpb, seenClusters);
        createFile(files, dirent, image_buf, bpb);
      }
      dirent++;
    }
    dirent++;
    /* if (cluster == 0) { */
    /*   // root dirMa is special */
    /*   dirent++; */
    /* } else { */
    /*   cluster = get_fat_entry(cluster, image_buf, bpb); */
    /*   dirent = (struct direntry*)cluster_to_addr(cluster, */
    /*     image_buf, bpb); */
    /* } */
  }
}
void printUnrefedClusters(uint16_t *seenClusters, uint8_t *image_buf, struct bpb33* bpb){
  uint16_t i;
  printf("Unreferenced: ");
  for(i = 2; i < 2848; i++){
    if(seenClusters[i] == 0){
      uint16_t entry = get_fat_entry(i, image_buf, bpb);
      if(entry >= CLUST_FIRST && entry <= (CLUST_EOFE & FAT12_MASK)){
        printf("%u ", i);
      }
    }
  }
  printf("\n");
}

void write_dirent(struct direntry *dirent, char *filename,
  uint16_t start_cluster, uint32_t size)
{
  char *p, *p2;
  char *uppername;
  int len, i;

  /* clean out anything old that used to be here */
  memset(dirent, 0, sizeof(struct direntry));

  /* extract just the filename part */
  uppername = strdup(filename);
  p2 = uppername;
  for (i = 0; i < strlen(filename); i++) {
    if (p2[i] == '/' || p2[i] == '\\') {
      uppername = p2+i+1;
    }
  }

  /* convert filename to upper case */
  for (i = 0; i < strlen(uppername); i++) {
    uppername[i] = toupper(uppername[i]);
  }

  /* set the file name and extension */
  memset(dirent->deName, ' ', 8);
  p = strchr(uppername, '.');
  memcpy(dirent->deExtension, "___", 3);
  if (p == NULL) {
    fprintf(stderr, "No filename extension given - defaulting to .___\n");
  } else {
    *p = '\0';
    p++;
    len = strlen(p);
    if (len > 3) len = 3;
    memcpy(dirent->deExtension, p, len);
  }
  if (strlen(uppername) > 8) {
    uppername[8]='\0';
  }
  memcpy(dirent->deName, uppername, strlen(uppername));
  free(p2);

  /* set the attributes and file size */
  dirent->deAttributes = ATTR_NORMAL;
  putushort(dirent->deStartCluster, start_cluster);
  putulong(dirent->deFileSize, size);

  /* a real filesystem would set the time and date here, but it's
     not necessary for this coursework */
}


/* create_dirent finds a free slot in the directory, and write the
   directory entry */

void create_dirent(struct direntry *dirent, char *filename,
  uint16_t start_cluster, uint32_t size, uint8_t *image_buf, struct bpb33* bpb)
{
  while(1) {
    if (dirent->deName[0] == SLOT_EMPTY) {
      /* we found an empty slot at the end of the directory */
      write_dirent(dirent, filename, start_cluster, size);
      dirent++;

      /* make sure the next dirent is set to be empty, just in
         case it wasn't before */
      memset((uint8_t*)dirent, 0, sizeof(struct direntry));
      dirent->deName[0] = SLOT_EMPTY;
      return;
    }
    if (dirent->deName[0] == SLOT_DELETED) {
      /* we found a deleted entry - we can just overwrite it */
      write_dirent(dirent, filename, start_cluster, size);
      return;
    }
    dirent++;
  }
}

void countClusters(uint16_t cluster, int* count, uint16_t* seenClusters, uint8_t *image_buf, struct bpb33* bpb){
  *count += 1;
  seenClusters[cluster] = 1;
  uint16_t entry = get_fat_entry(cluster, image_buf, bpb);
  if(is_end_of_file(entry)){
    return;
  } else {
    countClusters(entry, count, seenClusters, image_buf, bpb);
  }
}

void printUnreffedFiles(uint16_t *seenClusters, uint8_t *image_buf, struct bpb33* bpb){
  uint16_t i;
  int foundFiles = 0;
  struct direntry *dirent;
  dirent = (struct direntry*) root_dir_addr(image_buf, bpb);
  for(i = 2; i < 2848; i++){
    if(seenClusters[i] == 0){
      uint16_t entry = get_fat_entry(i, image_buf, bpb);
      if(entry >= CLUST_FIRST && entry <= (CLUST_EOFE & FAT12_MASK)){
        int count = 0;
        countClusters(i, &count, seenClusters, image_buf, bpb);
        printf("Lost File: %u %i\n", i, count);
        foundFiles++;
        char *fileName = malloc(sizeof(char) * 14);
        sprintf(fileName, "found%i.dat", foundFiles);
        create_dirent(dirent, fileName, i, count * 512, image_buf, bpb);
      }
    }
  }
}

int main(int argc, char** argv){
  uint8_t *image_buf;
  int fd;
  struct bpb33* bpb;
  image_buf = mmap_file(argv[1], &fd);
  bpb = check_bootsector(image_buf);
  uint16_t seenClusters[2848] = {0}; 
  FileList *files = malloc(sizeof(FileList));
  files->last = NULL;
  files->first = NULL;
  scanFiles(0, image_buf, bpb, seenClusters, files);
  printUnrefedClusters(seenClusters, image_buf, bpb);
  printUnreffedFiles(seenClusters, image_buf, bpb);
  FileHeader *file = files->first;
  while(file != NULL){
    /* printf("%s %u %u\n", file->name, file->fileSize, file->noOfClusters * 512); */
    if(file->fileSize < (file->noOfClusters - 1) * 512){
      printf("%s %u %u\n", file->name, file->fileSize, file->noOfClusters * 512);
      int count;
      ClusterNode *node = file->startCluster;
      for(count = 1; count<= file->noOfClusters; count++){
        if(file->fileSize > (count - 1) * 512)
          break;
        node = node->nextNode;
      }
      node = node->nextNode;
      set_fat_entry(node->clusterNum, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
      node = node->nextNode;
      while(node != NULL){
        set_fat_entry(node->clusterNum, CLUST_FREE, image_buf, bpb);
        node = node->nextNode;
      } 
    }
    file = file->nextFile;
  }
  close(fd);
  exit(0);
}
