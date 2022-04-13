//
// Created by yfb on 2022/4/12.
//
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void csp(int rd) {
    int p;
    read(rd, &p, 4);
    printf("prime %d\n", p);

    int fd[2];
    pipe(fd);

    if (fork() == 0) {
        // child
        close(fd[1]);
        csp(fd[0]);
    } else {
        // father
        int num;
        while (read(rd, &num, 4) != 0) {
            if (num % p!=0) {
                write(fd[1], &num, 4);
            }
        }
        close(fd[1]);
        wait(0);
    }

    close(fd[0]);

    exit(0);
}


int main() {
    int fd[2];
    pipe(fd);

    if (fork() == 0) {
        // child
        close(fd[1]);
        csp(fd[0]);
    } else {
        // father
        for (int num = 2; num <= 35; num++) {
            write(fd[1], &num, 4);
        }
        close(fd[1]);
        wait(0);
    }

    close(fd[0]);

    exit(0);
}
