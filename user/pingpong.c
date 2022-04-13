//
// Created by yfb on 2022/4/12.
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    int pid;

    int p2c[2];
    int c2p[2];

    pipe(p2c);
    pipe(c2p);

    pid = fork();
    if (pid == 0){
        // child
        char buf[1];
        read(p2c[0], buf,1);
        int cpid = getpid();
        printf("%d: received ping\n", cpid);
        write(c2p[1], "c", 1);
    } else {
        // father
        write(p2c[1], "p", 1);
        char buf[1];
        read(c2p[0], buf,1);
        int ppid = getpid();
        printf("%d: received pong\n", ppid);
    }

    close(p2c[0]);
    close(p2c[1]);
    close(c2p[0]);
    close(c2p[1]);

    exit(0);
}
