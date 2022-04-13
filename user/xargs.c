#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

void copy(char **p1, char *p2){
    *p1 = malloc(strlen(p2) + 1);
    strcpy(*p1, p2);
}

char* readline() {
    char* buf = malloc(100);
    char* p = buf;
    while(read(0, p, 1) != 0){
        if(*p == '\n' || *p == '\0'){
            *p = '\0';
            return buf;
        }
        p++;
    }
    if(p != buf) return buf;
    free(buf);
    return 0;
}

int main(int argc, char *argv[]){
    if (argc < 2){
        printf("Please enter more parameters!\n");
        exit(1);
    }else{
        int i;
        char *pars[MAXARG];
        for (i = 1; i < argc; i++){
            copy(&pars[i - 1], argv[i]);
        }

        char *a;
        while ((a = readline()) !=0){
            pars[argc-1] = a;
            pars[argc]=0;
            if (fork() == 0){
                exec(pars[0], pars);
                exit(1);
            }else{
                wait(0);
            }
        }
        exit(0);
    }
}
