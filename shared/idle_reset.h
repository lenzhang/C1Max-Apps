#pragma once
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>
extern char **environ;

/* The stock PowerManager consumes this one-shot flag and clears its elapsed
 * backlight counter. Preserve the user's timer and all global lock settings.
 * Call only on activity/session boundaries, or while an app has active work.
 */
static inline int c1_reset_idle(void) {
    pid_t pid;
    char *argv[]={(char *)"setprop",(char *)"sys.backlight.timer.reset",(char *)"1",NULL};
    if(posix_spawn(&pid,"/usr/bin/setprop",NULL,NULL,argv,environ))return -1;
    int status;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return -1;}
    return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;
}
