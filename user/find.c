//
// Created by yfb on 2022/4/12.
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void match(const char *path, const char *name) {
    int path_len = strlen(path);
    int name_len = strlen(name);
    if (path_len < name_len) {
        return;
    }

    int start;
    for (start=path_len-1;start>=0;start--) {
        if (path[start]=='/') {
            break;
        }
    }

    start = start + 1;
    if (path_len-start != name_len) {
        return;
    }

    int pa = 0;
    while (path[start] != 0) {

        if (name[pa] != path[start]) {
            return;
        }

        pa++;
        start++;
    }

    printf("%s\n", path);
}

void find(char *path, char *file_name) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_FILE:

            match(path, file_name);
            break;

        case T_DIR:
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("ls: path too long\n");
                break;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0)
                    continue;
                if (de.name[0] == '.' && de.name[1] == 0) continue;
                if (de.name[0] == '.' && de.name[1] == '.' && de.name[2] == 0) continue;
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                if (stat(buf, &st) < 0) {
                    printf("ls: cannot stat %s\n", buf);
                    continue;
                }
                find(buf, file_name);
            }
            break;
    }
    close(fd);
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(2, "Usage: find [path] [filename]\n");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}
