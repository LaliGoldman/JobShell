#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char**argv) {
    int pipe_fd[2]; // pipe_fd[0] - read end, pipe_fd[1] - write end;

    char *execute_ls[] = {"ls", "-ls", NULL};
    char *execute_wc[] = {"wc", NULL};

    pid_t pid1;
    pid_t pid2;

    // Create a pipe for communication between parent and child
    if(pipe(pipe_fd) == -1){
        perror("pipe failed");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr,"(parent_process>forking...)\n");

    // Create a new process (child_1)
    pid1 = fork();

    // Child process 
    if(pid1 == 0){

        fprintf(stderr,"(child1>redirecting stdout to the write end of the pipe...)\n");


        // Closes stdout
        close(1);
        dup(pipe_fd[1]);  // Redirect write_end to stdout 
        close(pipe_fd[1]); // Close duplicated 
        close(pipe_fd[0]); // Close read_end

        fprintf(stderr,"(child1>going to execute cmd: ls -ls)\n");

        if(execvp(execute_ls[0],execute_ls) == -1){
            perror ("ls -ls failed");
            _exit(1); // Exit immediately and safely - prevent duplicate or unintended output due to may sharr\ed buffers with parent 
        }
    }
    // Parent process
    else if (pid1 > 0){
        fprintf(stderr,"(parent_process>created process with id: %d)\n", pid1);
        fprintf(stderr,"(parent_process>closing the write end of the pipe...)\n");

        close(pipe_fd[1]); // Close write_end of pipe;

        fprintf(stderr,"(parent_process>forking...)\n");

        pid2 = fork();

        // Child 2 fork faild
        if(pid2 < 0){
            perror("fork faild for child 2");
            exit(EXIT_FAILURE);
        }

        // Child 2
        if(pid2 == 0){

            fprintf(stderr,"(child2>redirecting stdin to the read end of the pipe...)\n");

            close(0); // close stdin
            dup(pipe_fd[0]); // Redirect read_end to stdin 
            close(pipe_fd[0]); // Close duplicated 

            fprintf(stderr,"(child2>going to execute cmd: wc)\n");

            if(execvp(execute_wc[0],execute_wc) == -1){
                perror ("wc failed");
                _exit(1); // Exit immediately and safely - prevent duplicate or unintended output due to may sharr\ed buffers with parent 
            }
        }

        // Parent here

        fprintf(stderr, "(parent_process>created process with id: %d)\n",pid2);
        fprintf(stderr,"(parent_process>closing the read end of the pipe...)\n");

        close(pipe_fd[0]);

        fprintf(stderr, "(parent_process>waiting for child processes to terminate...)\n");

        waitpid(pid1,NULL,0); // Wait for child1 process to finish 
        waitpid(pid2,NULL,0); // Wait for child2 process to finish 

        fprintf(stderr, "(parent_process>exiting...)\n");
    }
    // fork failed 
    else{
        perror("fork failed for child 1");
        exit(EXIT_FAILURE);
    }
    return 0;
}