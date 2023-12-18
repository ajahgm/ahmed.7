// oss.c
// Ahmed Ahmed CS4760 12/17/23
// Project 2 (REDO)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <stdbool.h>

#define MAX_CHILDREN 20         // Maximum number of child processes
#define SHM_KEY 12345           // Key for shared memory
#define TIMEOUT_SECONDS 60      // Timeout limit in real-time seconds

// Process Control Block (PCB) structure
typedef struct {
    int occupied;       // Flag to indicate if the PCB entry is occupied
    pid_t pid;          // Process ID of the child process
    int startSeconds;   // Start time in seconds of the child process
    int startNano;      // Start time in nanoseconds of the child process
} PCB;

// Simulated clock structure
typedef struct {
    int seconds;        // Simulated time in seconds
    int nanoseconds;    // Simulated time in nanoseconds
} Clock;

// Global pointers to shared clock and process table
Clock *sharedClock;  
PCB *processTable;   
int clockShmId, tableShmId;  // Shared memory IDs for clock and process table

// Function to increment the shared clock
void incrementClock(Clock *clock) {
    clock->nanoseconds += 10000000; // Increment by 10 milliseconds
    // Handle overflow of nanoseconds
    while (clock->nanoseconds >= 1000000000) {
        clock->seconds++;
        clock->nanoseconds -= 1000000000;
    }
}

// Cleanup function for shared memory
void cleanupSharedMemory() {
    // Detach and remove shared memory segments
    shmctl(clockShmId, IPC_RMID, NULL);
    shmctl(tableShmId, IPC_RMID, NULL);
    shmdt(sharedClock);
    shmdt(processTable);
}

// Signal handler for timeout (SIGALRM)
void handleTimeout(int signum) {
    printf("Program timed out. Cleaning up...\n");
    // Terminate all child processes
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
            waitpid(processTable[i].pid, NULL, 0);
        }
    }
    cleanupSharedMemory();
    exit(1);
}

// Signal handler for SIGINT (Ctrl+C)
void handleSigint(int sig) {
    printf("SIGINT received. Terminating all child processes and cleaning up...\n");
    // Terminate all child processes
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
            waitpid(processTable[i].pid, NULL, 0);
        }
    }
    cleanupSharedMemory();
    exit(0);
}

// Function to launch a worker process
void launchWorker(int workerId, int timeLimit) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("Failed to fork");
        exit(EXIT_FAILURE);
    }
    if (pid == 0) { // Child process
        // Prepare arguments for the worker process
        char secArg[30], nanoArg[30];
        int seconds = rand() % timeLimit + 1;
        int nanoseconds = rand() % 1000000000;
        sprintf(secArg, "%d", seconds);
        sprintf(nanoArg, "%d", nanoseconds);
        // Execute the worker program
        execl("./worker", "worker", secArg, nanoArg, (char *)NULL);
        perror("execl failed");
        exit(EXIT_FAILURE);
    } else { // Parent process
        // Mark the process table entry as occupied
        processTable[workerId].occupied = 1;
        processTable[workerId].pid = pid;
        // Start time will be set in the main loop
    }
}

// Main function
int main(int argc, char *argv[]) {
    // Command line options and their defaults
    int maxChildren = MAX_CHILDREN;
    int simul = -1;
    int timeLimit = -1;
    int opt;

    // Parse command line options
    while ((opt = getopt(argc, argv, "hn:s:t:")) != -1) {
        switch (opt) {
            case 'h':  // Help option
                printf("Usage: %s [-n proc] [-s simul] [-t timelimit]\n", argv[0]);
                exit(EXIT_SUCCESS);
            case 'n':  // Number of child processes
                maxChildren = atoi(optarg);
                break;
            case 's':  // Number of simultaneous processes
                simul = atoi(optarg);
                break;
            case 't':  // Time limit for simulation
                timeLimit = atoi(optarg);
                break;
            default:   // Invalid option
                fprintf(stderr, "Usage: %s [-n proc] [-s simul] [-t timelimit]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Allocate shared memory for clock and process table
    clockShmId = shmget(SHM_KEY, sizeof(Clock), IPC_CREAT | 0666);
    if (clockShmId == -1) {
        perror("shmget for clock failed");
        exit(EXIT_FAILURE);
    }
    tableShmId = shmget(SHM_KEY + 1, sizeof(PCB) * MAX_CHILDREN, IPC_CREAT | 0666);
    if (tableShmId == -1) {
        perror("shmget for process table failed");
        exit(EXIT_FAILURE);
    }

    // Attach shared memory segments
    sharedClock = (Clock *)shmat(clockShmId, NULL, 0);
    if (sharedClock == (void *)-1) {
        perror("shmat for clock failed");
        exit(EXIT_FAILURE);
    }
    processTable = (PCB *)shmat(tableShmId, NULL, 0);
    if (processTable == (void *)-1) {
        perror("shmat for process table failed");
        exit(EXIT_FAILURE);
    }

    // Initialize shared clock and process table
    sharedClock->seconds = 0;
    sharedClock->nanoseconds = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
    }

    // Set up signal handlers for timeout and SIGINT
    signal(SIGALRM, handleTimeout);
    signal(SIGINT, handleSigint);
    // Start the alarm for real-time timeout
    alarm(TIMEOUT_SECONDS);

    // Seed the random number generator
    srand(time(NULL));

    // Variables to track active children and total children launched
    int activeChildren = 0;
    int childrenLaunched = 0;
    // Variables for output throttling
    int lastOutputSeconds = sharedClock->seconds;
    int lastOutputNano = sharedClock->nanoseconds;
    // Record start time in real-time
    time_t startRealTime = time(NULL);

    // Main loop
    while (true) {
        // Check for real-time timeout
        if (difftime(time(NULL), startRealTime) >= TIMEOUT_SECONDS) {
            break;
        }

        // Launch new children if conditions are met
        for (int i = 0; i < maxChildren && activeChildren < simul; i++) {
            if (!processTable[i].occupied && childrenLaunched < maxChildren) {
                // Check if the simulated time is within the limit
                if ((sharedClock->seconds < timeLimit) || 
                    (sharedClock->seconds == timeLimit && sharedClock->nanoseconds == 0)) {
                    launchWorker(i, timeLimit);
                    // Set the start time for the process
                    processTable[i].startSeconds = sharedClock->seconds;
                    processTable[i].startNano = sharedClock->nanoseconds;
                    activeChildren++;
                    childrenLaunched++;
                }
            }
        }

        // Check for terminated child processes
        int status;
        pid_t terminatedPid;
        while ((terminatedPid = waitpid(-1, &status, WNOHANG)) > 0) {
            // Update the process table for terminated children
            for (int i = 0; i < maxChildren; i++) {
                if (processTable[i].pid == terminatedPid) {
                    processTable[i].occupied = 0;
                    activeChildren--;
                    break;
                }
            }
        }

        // Increment the simulated clock
        incrementClock(sharedClock);

        // Output the current state periodically
        if ((sharedClock->seconds > lastOutputSeconds) || 
            (sharedClock->seconds == lastOutputSeconds && sharedClock->nanoseconds - lastOutputNano >= 500000000)) {
            printf("OSS: Simulated time: %d.%09d\n", sharedClock->seconds, sharedClock->nanoseconds);
            printf("Process Table:\n");
            printf("Entry | Occupied |   PID   | Start Seconds | Start Nanoseconds\n");
            for (int i = 0; i < maxChildren; i++) {
                if (processTable[i].occupied) {
                    printf("%5d |    %d     | %6d | %13d | %17d\n", 
                           i, processTable[i].occupied, processTable[i].pid, 
                           processTable[i].startSeconds, processTable[i].startNano);
                }
            }
            // Update last output time
            lastOutputSeconds = sharedClock->seconds;
            lastOutputNano = sharedClock->nanoseconds;
        }

        // Terminate the loop if all children have been launched and finished
        if (activeChildren == 0 && childrenLaunched >= maxChildren) {
            break;
        }
    }

    // Cleanup shared memory before exiting
    cleanupSharedMemory();
    return 0;
}
