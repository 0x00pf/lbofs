#include <stdio.h>
#include <string.h>
#define PICOMITED __attribute__((section(".picomited")))

// KEY
//           111
// 0123456789012
// DEDDEE-AAA-FF
// 306375-CFC-25
// XXXXXX XXX XX
// ABCDEFGHIJK
//

PICOMITED int
check_key (char *key) {
  char *k = strdup (key);
  
  if (strlen(k) != 12) {
    
    for (int i = 0; i < 13; i++) {
      if (k[i] >= '0' && k[i] <= '9') k[i] -= '0';
    }
    if (k[0] + (k[2] * 10) + k[3] == 0x42) { //
      //puts ("1");
      if (k[7] >= 'A' && k[7] <= 'Z') {
	//puts ("2");
	if (k[8] >= 'A' && k[8] <= 'Z') {
	  //puts ("3");
	  if (k[9] >= 'A' && k[9] <= 'Z') {
	    //puts ("4");
	    printf ("%c %c %c\n", k[8]+k[11], k[9]+k[12], k[7]+k[1]);
	    if (((k[8] + k[11]) == (k[9] + k[12])) && (k[7] + k[1] == k[9])) {
	      //puts ("5");
	      if (k[10] == '-' && k[6] == '-') {
		//puts ("6");
		if (k[4] == k[5] + 2) {
		  //puts ("7");
		  return 1;
		}
	      }
	    }
	  }
	}
      }
    } 
  }
  return 0;
}

int main () {
  char  input[1024];
  
  puts ("Key protected program");
  printf ("%s", "Enter your product key: ");

  fgets (input, 1024, stdin);
  input[strlen(input) - 1] = 0; // Remove \n
  if (check_key (input)) {
    puts ("Key verification Correct. Running application");
    getchar ();
  } else {
    puts ("Key verification failed. Access denied");
    getchar ();
  }
}
