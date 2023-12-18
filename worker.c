// worker.c
// Ahmed Ahmed CS4760 12/17/23
// Project 2 (REDO)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>

#define SHM_KEY 12345

typedef struct {
    int seconds;
    int nanoseconds;
} Clock;

int main(int argc, char *argv[]) {
    // Ensure correct number of command-line arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <durationSeconds> <durationNanoseconds>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse command-line arguments for duration
    int durationSeconds = atoi(argv[1]);
    int durationNanoseconds = atoi(argv[2]);

    // Attach to shared memory for clock
    int clockShmId = shmget(SHM_KEY, sizeof(Clock), 0666);
    if (clockShmId == -1) {
        perror("shmget failed in worker");
        exit(EXIT_FAILURE);
    }
    Clock *clock = (Clock *)shmat(clockShmId, NULL, 0);
    if (clock == (void *)-1) {
        perror("shmat failed in worker");
        exit(EXIT_FAILURE);
    }

    // Calculate termination time based on duration and current clock time
    int terminationSeconds = clock->seconds + durationSeconds;
    int terminationNanoseconds = clock->nanoseconds + durationNanoseconds;
    if (terminationNanoseconds >= 1000000000) {
        terminationSeconds++;
        terminationNanoseconds -= 1000000000;
    }

    printf("WORKER PID:%d PPID:%d SysClocks: %d SysclockNano: %d\n--Just Starting\n",
           getpid(), getppid(), clock->seconds, clock->nanoseconds);

    // Main loop to check termination condition
    while (1) {
        // Check if termination time has passed
        int currentSeconds = clock->seconds;
        int currentNanoseconds = clock->nanoseconds;
        if (currentSeconds > terminationSeconds ||
            (currentSeconds == terminationSeconds && currentNanoseconds >= terminationNanoseconds)) {
            // Output termination information and exit
            printf("WORKER PID:%d PPID:%d SysClocks: %d SysclockNano: %d\n--Terminating\n",
                   getpid(), getppid(), currentSeconds, currentNanoseconds);
            break; // Exit the loop
        }
    }

    // Detach from shared memory
    shmdt(clock);

    return 0;
}
