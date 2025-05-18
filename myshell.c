#include <unistd.h>     // for fork, execvp, chdir, getcwd, dup2, close
#include <linux/limits.h>     // for PATH_MAX
#include <stdio.h>      // for printf, perror, fgets
#include <stdlib.h>     // for exit, malloc, free, atoi
#include <string.h>     // for strcmp, strncmp, strlen, strcpy
#include <sys/types.h>  // for pid_t
#include <sys/wait.h>   // for waitpid
#include <fcntl.h>      // for open
#include <signal.h>     // for kill, signal
#include "LineParser.h" // for parseCmdLines and cmdLine struct
#include <errno.h>

int debug_mode = 0;

#define TERMINATED -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 20

typedef struct process{
    cmdLine* cmd;                         /* the parsed command line*/
    pid_t pid; 		                  /* the process id that is running the command*/
    int status;                           /* status of the process: RUNNING/SUSPENDED/TERMINATED */
    struct process *next;	                  /* next process in chain */
} process;

// Global variable
process *process_list = NULL;

typedef struct terminal_command{
    char * input_command;  
    struct terminal_command *next;                          	                  
} terminal_command;

terminal_command *history_head = NULL;
terminal_command *history_tail = NULL;
int history_size = 0;

void addProcess(process** process_list, cmdLine* cmd, pid_t pid,int status);
void freeProcessList(process* process_list);
void updateProcessList(process **process_list);
void updateProcessStatus(process* process_list, int pid, int status);
void printProcessList(process** process_list);
void executePipeCommand(cmdLine *pCmd);
void execute(cmdLine *pCmdLine);
void addHistory(const char *terminal_cmd);
void printHistory();
const char *print_n_Command(int n);




// Receive a process list (process_list), a command (cmd), and the process id (pid) of the process running the command
void addProcess(process** process_list, cmdLine* cmd, pid_t pid, int status){
    process *new_process = (process*) malloc(sizeof(process));
    new_process -> cmd = cmd;
    new_process -> pid = pid;
    new_process -> status = status;
    new_process -> next = *process_list;
    *process_list = new_process;
}

// <index in process list> <process id> <process status> <the command together with its arguments>
void printProcessList(process** process_list){
    int index = 0;
    char *status;

    updateProcessList(process_list);
    // Print format
    printf("Index\tPID\tSTATUS\tCOMMAND\n");

    process *current = *process_list;
    process *prev = NULL;
    process *remove_process = NULL;

    while(current != NULL){
        status = current->status == RUNNING ? "Running":
                current->status == SUSPENDED ? "Suspended":
                "Terminated";
        printf("%d\t%d\t%s\t",index++,current->pid,status);
        // Print command together with its arguments
        for(int i=0; i < current->cmd->argCount; i++){
            printf("%s ", current->cmd->arguments[i]);
        }
        printf("\n"); // For next process
        if (current->status == TERMINATED){
            remove_process = current;
            // First process in process list; 
            if(prev == NULL){
                *process_list = current->next;
            }
            else{
                prev->next = current->next;
            }
            current = current -> next;
            if (remove_process -> cmd != NULL){
                remove_process -> cmd -> next = NULL;
            }
            remove_process -> next = NULL;
            freeCmdLines(remove_process->cmd);
            free(remove_process);
        }
        else{
            prev = current;
            current = current -> next;
        }
    }
}

//  Free all memory allocated for the process list.
void freeProcessList(process* process_list){
    process *current = process_list;
    process *next = NULL;

    while(current != NULL){
        next = current->next;
        freeCmdLines(current->cmd);
        current->next = NULL;
        free(current);
        current = next;
    }
}

// Go over the process list, and for each process check if it is done.
void updateProcessList(process **process_list){
    int status;
    process *current = *process_list;
    pid_t pid;

    while(current != NULL){
        // If the child pid changed status waitpid returns its PID.
        // If the child has not exited yet, it returns 0 immediately.
        // If there’s an error (e.g., no such child), it returns -1.

        // WNOHANG - Don't block (wait) if no child process has exited. Just return immediately.
        //  &status saves details on termination of the child 
        pid = waitpid(current->pid, &status, WNOHANG);

        // If the child pid changed status waitpid returns its PID
        if (pid > 0){
            //  WIFEXITED &  WIFSIGNALED - returns true if the child terminated
            if (WIFEXITED(status) || WIFSIGNALED(status)){
                current->status = TERMINATED;
            }
            // WIFSTOPPED - returns  true  if the child process was stopped by delivery of a signal;
            else if (WIFSTOPPED(status)){
                current->status = SUSPENDED;
            }
            // WIFCONTINUED - return if a stopped child has been resumed by delivery of SIGCONT
            else if (WIFCONTINUED(status)){
                current->status = RUNNING;
            }
        }
        // If there’s an error (e.g., no such child), it returns -1.
        else if (pid == -1){
            if (errno == ECHILD){
                current->status = TERMINATED;
            }
        }
        current = current -> next;
    }

}

// Find the process with the given id in the process_list and change its status to the received status.
void updateProcessStatus(process* process_list, int pid, int status){
    process *current = process_list;
    
    while(current != NULL){
        if (current->pid == pid){
            current ->status = status;
            return;
        }
        current = current -> next;
    }
}

void wakeupProcess(pid_t process_id){
    if(kill(process_id, SIGCONT) == -1){
        fprintf(stderr, "%d wakeup failed\n", process_id);
    }
    else {
        fprintf(stderr, "Send signal wakeup to process %d\n", process_id);
        updateProcessStatus(process_list, process_id, RUNNING);
    }
}

void haltProcess(pid_t process_id){
    if(kill(process_id,SIGSTOP) == -1){
        fprintf(stderr, "%d halt failed\n", process_id);
    }
    else {
        fprintf(stderr, "Send signal halt to process %d\n", process_id);
        updateProcessStatus(process_list, process_id, SUSPENDED);
    }
}

void iceProcess(pid_t process_id){
    if(kill(process_id, SIGINT) == -1){
        fprintf(stderr, "%d ice failed", process_id);
    }
    else {
        fprintf(stderr, "Send signal ice to process %d\n", process_id);
        updateProcessStatus(process_list, process_id, TERMINATED);
    }
}

void executePipeCommand(cmdLine *pCmd){
    int pipe_fd[2]; // pipe_fd[0] - read end, pipe_fd[1] - write end;
    int in_fd, out_fd;
    char* const* execute_first_cmd = pCmd->arguments;
    char* const* execute_second_cmd = pCmd->next->arguments;

    pid_t pid1;
    pid_t pid2;

    if(pCmd -> outputRedirect != NULL || pCmd-> next -> inputRedirect != NULL){
        perror("Error: cannot redirect left-side output or right-side input in a pipe\n");
        freeCmdLines(pCmd);
        return;
    }

    // Create a pipe for communication between parent and child
    if(pipe(pipe_fd) == -1){
        perror("pipe failed\n");
        freeCmdLines(pCmd);
        exit(EXIT_FAILURE);
    }

    // Create a new process (child_1)
    pid1 = fork();

    // Child process 
    if(pid1 == 0){

        // Closes stdout
        close(1);
        dup(pipe_fd[1]);  // Redirect write_end to stdout 
        close(pipe_fd[1]); // Close duplicated 
        close(pipe_fd[0]); // Close read_end

        if(pCmd->inputRedirect != NULL){
            in_fd = open(pCmd-> inputRedirect,O_RDONLY);
            if (in_fd < 0){ // Error in opening file
                fprintf(stderr, "Could not input Redirect from %s\n", pCmd->inputRedirect);
                freeCmdLines(pCmd);
               _exit(1); 
            }
            dup2(in_fd,0); // Replace stdin with in_fd
            close(in_fd);
        }

        if(execvp((const char*)execute_first_cmd[0],(char* const*)execute_first_cmd) == -1){
            perror ("first command failed\n");
            freeCmdLines(pCmd);
            _exit(1); // Exit immediately and safely - prevent duplicate or unintended output due to may sharr\ed buffers with parent 
        }
    }

    // Parent process
    else if (pid1 > 0){

        close(pipe_fd[1]); // Close write_end of pipe;

        pid2 = fork();

        // Child 2 fork faild
        if(pid2 < 0){
            perror("fork faild for child 2\n");
            freeCmdLines(pCmd);
            exit(EXIT_FAILURE);
        }

        // Child 2
        if(pid2 == 0){

            close(0); // close stdin
            dup(pipe_fd[0]); // Redirect read_end to stdin 
            close(pipe_fd[0]); // Close duplicated 

            if(pCmd->next -> outputRedirect != NULL){
            // 0 - for base octal, 6 - rw user, 4 - read group, 4 - read other
            out_fd = open(pCmd-> next -> outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644); // O_WRONLY - write only, O_CREAT - create file if not exist, O_TRUNC - delete current input given file 
            if (out_fd < 0){ // Error in opening file
                fprintf(stderr, "Could not output Redirect from %s\n",pCmd-> next ->outputRedirect);
                freeCmdLines(pCmd);
               _exit(1); 
            }
            dup2(out_fd,1); // Replace stdout with in_fd
            close(out_fd);
        }

            if(execvp((const char*)execute_second_cmd[0],(char* const*)execute_second_cmd) == -1){
                perror ("second command failed\n");
                freeCmdLines(pCmd);
                _exit(1); // Exit immediately and safely - prevent duplicate or unintended output due to may sharr\ed buffers with parent 
            }
        }

        // Parent here

        close(pipe_fd[0]);

        if(pCmd->blocking){
            addProcess(&process_list, pCmd, pid1 ,RUNNING); // Terminated  
            waitpid(pid1,NULL,0); // Wait for child1 process to finish 
        }
        else{
            addProcess(&process_list, pCmd, pid1 ,RUNNING); // Terminated  
        }
        
        if(pCmd->next->blocking){
            addProcess(&process_list, pCmd->next, pid2 ,TERMINATED); // Terminated 
            waitpid(pid2,NULL,0); // Wait for child2 process to finish  
        }
        else{
            addProcess(&process_list, pCmd->next, pid2 ,RUNNING); // Terminated  
        }

    }
    // fork failed 
    else{
        perror("fork failed for child 1\n");
        exit(EXIT_FAILURE);
    }
}

void execute(cmdLine *pCmdLine){

    pid_t pid;
    int process_id, in_fd, out_fd;
    char * command = pCmdLine->arguments[0]; 

    // Build in 'cd' command 
    if (strcmp(command, "cd") == 0) {
        // No directory was given
        if (pCmdLine->arguments[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else if (chdir(pCmdLine->arguments[1]) != 0) { // Change current working directory - 0 is for success 
            perror("cd failed");
        }
        freeCmdLines(pCmdLine);
        return; // Command was executed
    }

    if (strcmp(command, "procs") == 0) {
        printProcessList(&process_list);
        freeCmdLines(pCmdLine);
        return;
    }

    // Process killing commands
    else if(strcmp(command,"halt") == 0 || strcmp(command,"wakeup") == 0 || strcmp(command,"ice") == 0){
        if(pCmdLine->arguments[1] == NULL){
            fprintf(stderr, "%s: missing process-id\n",command);
            freeCmdLines(pCmdLine);
            return;
        }

        process_id = atoi(pCmdLine->arguments[1]);
        if(process_id != 0){

            if(strcmp(command,"halt") == 0){
                haltProcess((pid_t)process_id);
            }

            else if(strcmp(command,"wakeup") == 0){
                wakeupProcess((pid_t)process_id);
            }

            else{ 
                iceProcess((pid_t)process_id);
            }
        }
        else{
            fprintf(stderr, "%s: process-id is not valid\n",command);
        }
        freeCmdLines(pCmdLine);
        return;

    }

    pid = fork(); // Create a child process 

    // Child process
    if(pid == 0){

        if(pCmdLine->inputRedirect != NULL){
            in_fd = open(pCmdLine-> inputRedirect,O_RDONLY);
            if (in_fd < 0){ // Error in opening file
                fprintf(stderr, "Could not input Redirect from %s\n", pCmdLine->inputRedirect);
                freeCmdLines(pCmdLine);
               _exit(1); 
            }
            dup2(in_fd,0); // Replace stdin with in_fd
            close(in_fd);
        }

        if(pCmdLine->outputRedirect != NULL){
            // 0 - for base octal, 6 - rw user, 4 - read group, 4 - read other
            out_fd = open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644); // O_WRONLY - write only, O_CREAT - create file if not exist, O_TRUNC - delete current input given file 
            if (out_fd < 0){ // Error in opening file
                fprintf(stderr, "Could not output Redirect from %s\n",pCmdLine->outputRedirect);
                freeCmdLines(pCmdLine);
               _exit(1); 
            }
            dup2(out_fd,1); // Replace stdout with out_fd
            close(out_fd);
        }

        if(execvp(command,pCmdLine->arguments)){ // Replace program with new process 
            fprintf(stderr, "Operation failed\n");
            freeCmdLines(pCmdLine);
            _exit(1); // Exit immediately and safely - prevent duplicate or unintended output due to may sharr\ed buffers with parent 
        }
    }
    //Parent process 
    else if (pid > 0){ 
        if(debug_mode){
            fprintf(stderr, "PID: %d\n", pid);
            fprintf(stderr, "Executing command: %s\n", command);
        }
        
        if(pCmdLine->blocking){
            waitpid(pid,NULL,0); // Wait for child process to finish 
            addProcess(&process_list, pCmdLine, pid,TERMINATED); // Terminated  
        }
        else{
            addProcess(&process_list, pCmdLine, pid, RUNNING);
        }
    }
    // Fork failed
    else{
        perror("fork failed");
        exit(1);
    }
}

void addHistory(const char *terminal_cmd){
    terminal_command *hist = (terminal_command *)malloc(sizeof(terminal_command));
    // Memory allocation failed
    if (hist == NULL) {
        perror("malloc failed");
        exit(1);
    }

    hist->input_command = strdup(terminal_cmd);// Duplicate to keep command in history list;
    hist->next = NULL;
    if(history_size == 0){
        history_head = hist;
        history_size = 1;
    }
    else {
        if(history_size == HISTLEN){
            terminal_command *to_delete = history_head;
            history_head = history_head -> next;
            free(to_delete->input_command);
            free(to_delete);
        }
        else{
            history_size++;
        }
        history_tail->next = hist;
    }
    history_tail = hist;
}

void printHistory(){
    terminal_command *current = history_head;
    int count = 1;
    while(current != NULL){
        printf("%d: %s",count,current->input_command);
        count++;
        current = current->next;
    }
}

const char *print_n_Command(int n){
    terminal_command *current = history_head;

    if( n > history_size + 1 || n < 1){
        printf("history number %d does not exist", n);
        return 0;
    }
    else{
        while(n > 1){
            current = current -> next;
            n--;
        }
        return current->input_command;
    }
}

void free_historyList(){
    terminal_command *current = history_head;
    terminal_command *next = NULL;

    while(current != NULL){
        next = current->next;
        current->next = NULL;
        free(current->input_command); // Free memory allocation of strdup
        free(current);
        current = next;
    }
}

void quit(){
    freeProcessList(process_list);
    free_historyList();
}


int main(int argc, char**argv) {
    char cwd[PATH_MAX];  // Current working directory buffer
    char input[2048];    // User input buffer
    cmdLine *cmd = NULL;

    // Turn ON debug mode
    for(int i=0; i< argc; i++){
        if(strcmp(argv[i], "-d") == 0){
            debug_mode = 1; 
            break; // Stop after finding the -d flag
        }
    }

    while (1) {

        if (getcwd(cwd, sizeof(cwd)) == NULL) { // Get current working directory
            perror("getcwd");
            exit(EXIT_FAILURE);
        }

        printf("%s> ", cwd); // Display prompt of current working directory
 
        // Read user input
        if (fgets(input, sizeof(input), stdin) == NULL) { 
            printf("\n");
            break;  // Exit on Ctrl+D
        }

        if(strncmp(input,"!!",2) == 0){
            if (history_size != 0){
                printf("%s",history_tail->input_command);
                strcpy(input, history_tail->input_command);  // Copy the command
            }
            else{
                perror("No history is available");
            }
        }

        else if (input[0] == '!' && input[1] != '\0') {
            const char* cmd = print_n_Command(atoi(input + 1));
            if (cmd != NULL) {
                strcpy(input, cmd); // Copy command into input
            }
        }
        addHistory(input);

        input[strcspn(input, "\n")] = '\0';

        if(strncmp(input,"hist",4) == 0){
            printHistory();
            continue;
        }

        // User entered quit, exit program 
        if(strncmp(input,"quit",4) == 0){
            quit();
            return 0;
        }

        cmd = parseCmdLines(input);

        if(cmd != NULL){
            if(cmd->next != NULL){
                executePipeCommand(cmd);
            }
            else{
                execute(cmd);
            }
        }
    }

    return 0;
}