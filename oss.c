// oss.c
// Ahmed Ahmed CS4760 9/28/23

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <stdbool.h>

#define MAX_CHILDREN 20
#define SHM_KEY 12345
#define TIMEOUT_SECONDS 60 // 60 seconds real-life timeout

typedef struct {
    int occupied;       // Flag to indicate if the PCB entry is occupied
    pid_t pid;          // Process ID of the child process
    int startSeconds;   // Start time in seconds of the child process
    int startNano;      // Start time in nanoseconds of the child process
} PCB;

typedef struct {
    int seconds;        // Simulated time in seconds
    int nanoseconds;    // Simulated time in nanoseconds
} Clock;

Clock *sharedClock;  // Pointer to shared clock
PCB *processTable;   // Pointer to process table
int clockShmId, tableShmId;  // Shared memory IDs

void incrementClock(Clock *clock) {
    clock->nanoseconds += 10000000; // Increment by 10 milliseconds (10,000,000 nanoseconds)
    while (clock->nanoseconds >= 1000000000) {
        clock->seconds++;
        clock->nanoseconds -= 1000000000;
    }
}

void cleanupSharedMemory() {
    shmctl(clockShmId, IPC_RMID, NULL);
    shmctl(tableShmId, IPC_RMID, NULL);
    shmdt(sharedClock);
    shmdt(processTable);
}

void handleTimeout(int signum) {
    printf("Program timed out. Cleaning up...\n");
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
            waitpid(processTable[i].pid, NULL, 0);
        }
    }
    cleanupSharedMemory();
    exit(1);
}

void handleSigint(int sig) {
    printf("SIGINT received. Terminating all child processes and cleaning up...\n");
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
            waitpid(processTable[i].pid, NULL, 0);
        }
    }
    cleanupSharedMemory();
    exit(0);
}

pid_t launchWorker(int workerId, int timeLimit) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("Failed to fork");
        exit(EXIT_FAILURE);
    }
    if (pid == 0) { // Child process
        char secArg[30], nanoArg[30];
        sprintf(secArg, "%d", rand() % timeLimit + 1);
        sprintf(nanoArg, "%d", rand() % 1000000000);
        execl("./worker", "worker", secArg, nanoArg, (char *)NULL);
        perror("execl failed");
        exit(EXIT_FAILURE);
    }
    return pid; // Return PID of the forked child process
}

int main(int argc, char *argv[]) {
    int maxChildren = MAX_CHILDREN;
    int simul = -1;
    int timeLimit = -1;
    int opt;

    while ((opt = getopt(argc, argv, "hn:s:t:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-n proc] [-s simul] [-t timelimit]\n", argv[0]);
                exit(EXIT_SUCCESS);
            case 'n':
                maxChildren = atoi(optarg);
                break;
            case 's':
                simul = atoi(optarg);
                break;
            case 't':
                timeLimit = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-n proc] [-s simul] [-t timelimit]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

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

    sharedClock->seconds = 0;
    sharedClock->nanoseconds = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
    }

    signal(SIGALRM, handleTimeout);
    signal(SIGINT, handleSigint);
    alarm(TIMEOUT_SECONDS);

    srand(time(NULL));

    int activeChildren = 0;
    int childrenLaunched = 0;
    int lastOutputSeconds = -1; // Initialize to -1 to ensure initial output
    int lastOutputNano = 0;
    time_t startRealTime = time(NULL);

    while (true) {
        if (difftime(time(NULL), startRealTime) >= TIMEOUT_SECONDS) {
            break;
        }

        for (int i = 0; i < maxChildren && activeChildren < simul; i++) {
            if (!processTable[i].occupied && childrenLaunched < maxChildren) {
                if ((sharedClock->seconds < timeLimit) || 
                    (sharedClock->seconds == timeLimit && sharedClock->nanoseconds == 0)) {
                    pid_t childPid = launchWorker(i, timeLimit);
                    processTable[i].occupied = 1;
                    processTable[i].pid = childPid;
                    // Set start time in the next cycle after clock increment
                }
            }
        }

        int status;
        pid_t terminatedPid;
        while ((terminatedPid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < maxChildren; i++) {
                if (processTable[i].pid == terminatedPid) {
                    processTable[i].occupied = 0;
                    activeChildren--;
                    break;
                }
            }
        }

        incrementClock(sharedClock);

        // Check if half a second has passed in simulated time
        if ((sharedClock->seconds > lastOutputSeconds) || 
            (sharedClock->seconds == lastOutputSeconds && sharedClock->nanoseconds - lastOutputNano >= 500000000)) {
            
            printf("OSS PID:%d SysClockS: %d SysclockNano: %d\n", getpid(), sharedClock->seconds, sharedClock->nanoseconds);
            printf("Process Table:\n");
            printf("Entry Occupied  PID    StartS    StartN\n");
            for (int i = 0; i < maxChildren; i++) {
                printf("%5d %8d %6d %8d %8d\n", 
                       i, processTable[i].occupied, processTable[i].pid, 
                       processTable[i].startSeconds, processTable[i].startNano);
            }

            lastOutputSeconds = sharedClock->seconds;
            lastOutputNano = sharedClock->nanoseconds;
        }

        if (activeChildren == 0 && childrenLaunched >= maxChildren) {
            break;
        }
    }

    cleanupSharedMemory();
    return 0;
}
