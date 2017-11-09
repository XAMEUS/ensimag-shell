/*****************************************************
* Copyright Grégory Mounié 2008-2015                *
*           Simon Nieuviarts 2002-2009              *
* This code is distributed under the GLPv3 licence. *
* Ce code est distribué sous la licence GPLv3+.     *
*****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

typedef struct list_proc {
    pid_t pid;
    char* cmd;
    struct list_proc * next;
} list_proc;

static list_proc *l_bg = NULL;
static list_proc *l_fg = NULL;

void add_list_proc(list_proc **l, char* cmd, pid_t pid) {
    list_proc *e;
    e = malloc(sizeof(list_proc));
    e->cmd = malloc(sizeof(char) * (strlen(&cmd[0]) + 1));
    strncpy(e->cmd, &cmd[0], strlen(&cmd[0]) + 1);
    e->pid = pid;
    e->next = *l;
    *l = e;
}

void rm_list_proc(list_proc *l, pid_t pid) {
    if (l == NULL) return;
    if (l->pid == pid) {
        list_proc *e = l;
        *l = *l->next;
        free(e);
        return;
    }
    list_proc * current = l;
    while (current->next != NULL && current->next->pid != pid) {
        current = current->next;
    }
    if (current->next->pid == pid) {
        list_proc *e = current;
        e->next = e->next->next;
        free(e);
    }
}

void print_list_proc(list_proc *bg) {
    list_proc * current = bg;
    while (current != NULL) {
        printf("%d %s\n", current->pid, current->cmd);
        current = current->next;
    }
}

void refresh_list_proc(list_proc **bg, pid_t r) {
    struct list_proc *current = *bg;
    struct list_proc *prev = NULL;
    while(current != NULL) {
        if (r == current->pid) {
            printf("+ %s : done [%d]\n", current->cmd, current->pid);
            if(prev == NULL) {
                *bg = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
}


/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
* following lines.  You may also have to comment related pkg-config
* lines in CMakeLists.txt.
*/

#if USE_GUILE == 1
#include <libguile.h>

int question6_executer(char *line)
{
    /* Question 6: Insert your code to execute the command line
    * identically to the standard execution scheme:
    * parsecmd, then fork+execvp, for a single command.
    * pipe and i/o redirection are not required.
    */
    printf("Not implemented yet: can not execute %s\n", line);
    int pipefd[2][2];
    executecommand(parsecmd(line), 1, 1, 0, pipefd, NULL, NULL);
    /* Remove this line when using parsecmd as it will free it */
    // free(line);

    return 0;
}

SCM executer_wrapper(SCM x)
{
    return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif

void handler_child_exit(int sig) {
    int pid = wait(NULL);
    refresh_list_proc(&l_bg, pid);
    refresh_list_proc(&l_fg, pid);
    signal(SIGCHLD, handler_child_exit);
}

void terminate(char *line) {
    #if USE_GNU_READLINE == 1
    /* rl_clear_history() does not exist yet in centOS 6 */
    clear_history();
    #endif
    if (line)
    free(line);
    printf("exit\n");
    exit(0);
}

void executecommand(char **cmd, int i, int len_l, int bg, int pipefd[2][2], int *f_in, int *f_out) {
    pid_t pid;
    switch(pid = fork()) {
        case -1:
        perror("fork");
        break;
        case 0:
        if(len_l > 1) {
            if(i > 0) { //stdin
                dup2(pipefd[0][0], 0);
                close(pipefd[0][0]);
                close(pipefd[0][1]);
            }
            //stdout
            if(i < len_l - 1) dup2(pipefd[1][1], 1);
        }
        if(!i && f_in) dup2(*f_in, 0);
        if(i == len_l - 1 && f_out) dup2(*f_out, 1);
        if(execvp(*cmd, (char * const*) cmd) == -1 ) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
        default:
        {
            if(!bg) add_list_proc(&l_fg, *cmd,pid);
            if (len_l > 1 && i > 0) {
                close(pipefd[0][0]);
                close(pipefd[0][1]);
            }
            if(!bg && i == len_l - 1) while(l_fg) pause();
            if(bg) add_list_proc(&l_bg, *cmd, pid);
        }
    }
}

int main() {
    printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

    signal(SIGCHLD, handler_child_exit);

    #if USE_GUILE == 1
    scm_init_guile();
    /* register "executer" function in scheme */
    scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
    #endif
    int *f_in = NULL, *f_out = NULL;
    while (1) {
        struct cmdline *l;
        char *line=0;
        int i;
        char *prompt = "ensishell> ";

        /* Readline use some internal memory structure that
        can not be cleaned at the end of the program. Thus
        one memory leak per command seems unavoidable yet */
        line = readline(prompt);
        if (line == 0 || ! strncmp(line,"exit", 4)) {
            terminate(line);
        }

        #if USE_GNU_READLINE == 1
        add_history(line);
        #endif


        #if USE_GUILE == 1
        /* The line is a scheme command */
        if (line[0] == '(') {
            char catchligne[strlen(line) + 256];
            sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
            scm_eval_string(scm_from_locale_string(catchligne));
            free(line);
            continue;
        }
        #endif

        /* parsecmd free line and set it up to 0 */
        l = parsecmd( & line);

        /* If input stream closed, normal termination */
        if (!l) {

            terminate(0);
        }



        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }
        int len_l = 0;
        while(l->seq[len_l]!=0) len_l++;

        int pipefd[2][2];
        if(f_in) {
            fsync(*f_in);
            close(*f_in);
            *f_in = 0;
        }
        if(f_out) {
            fsync(*f_out);
            close(*f_out);
            *f_out = 0;
        }
        if(l->in) {
            f_in = malloc(sizeof(*f_in));
            *f_in = open(l->in, O_RDONLY);
        }
        if(l->out) {
            f_out = malloc(sizeof(*f_out));
            *f_out = open(l->out, O_WRONLY | O_CREAT, 0644);
        }

        for (i=0; l->seq[i]!=0; i++) {
            if (strcmp(*l->seq[i], "jobs") == 0) {
                print_list_proc(l_bg);
                break;
            }
            if(len_l > 1) {
                if(i > 0) {
                    pipefd[0][0] = pipefd[1][0];
                    pipefd[0][1] = pipefd[1][1];
                }
                if(i < len_l - 1 && pipe(pipefd[1]) == -1) {
                    perror("pipe");
                    break;
                }
            }
            executecommand(l->seq[i], i, len_l, l->bg, pipefd, f_in, f_out);
        }
        if(f_in) {
            free(f_in);
            f_in = NULL;
        }
        if(f_out) {
            free(f_out);
            f_out = NULL;
        }
    }
}
