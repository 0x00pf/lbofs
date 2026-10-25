#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define DIE(s) {perror(s); exit(1);}

char *cond[] = {"ne", "le", "mp", "g", "e", NULL};

int main (int argc, char *argv[]) {
  FILE *fi = fopen (argv[1], "rt");
  int   f = open (argv[2], O_WRONLY);
  
  if (fi == NULL || f < 0) DIE ("cannot open files");
  printf ("struct picomite_t pm[] = {\n");
  while (!feof (fi)) {
    char buf[1024], trash[1024], flags[16];
    uint64_t offset, target;
    
    if (!fgets (buf, 1024, fi)) break;
    char *aux = strchr (buf, ':');
    if (!aux) DIE ("malformed string:");
    *aux = 0;
    aux++;
    sscanf (buf, "%x", &offset);

    
    char *aux1 = strchr (aux, 'j');
    if (!aux1) DIE ("malformed string");

    *aux1 = 0;
    while (*aux == ' ' || *aux == '\t') aux++;;
    int cnt = 0;
    for (int i = 0; i < strlen(aux); i++) if (aux[i] != ' ' && aux[i+1] == ' ') cnt++;
    
    aux1++;
    
    sscanf (aux1, "%s %x %s", flags, &target, trash);
    int i;
    for (i = 0; cond[i] != NULL; i++) if (!strcmp(cond[i], flags)) break;
    printf ("{0x%x, 0x%x, 0x%x, %d, \"%s\"},\n", offset + 1, target, offset + cnt,
	    cond[i] == NULL ? -1 : i, flags);
    uint8_t int3[1] = {0xcc};
    lseek (f, offset, SEEK_SET);
    write (f, &int3, 1);
  }
  printf ("{0,0,0,0,NULL}};\n");
  fclose (fi);
  close (f);
}
