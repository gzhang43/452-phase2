#include <stdio.h>
#include "phase2.h"

typedef struct PCB {
    int pid;
    char name[MAXNAME]
    int priority;
    int filled;
    int isBlocked;
} PCB;

struct PCB shadowProcessTable[MAXPROC+1];

void phase2_init(void) {

}

void phase2_start_service_processes(void) {

}

int phase2_check_io(void) {

}

void phase2_clockHandler(void) {

}

int MboxCreate(int slots, int slot_size) {

}

int MboxRelease(int mbox_id) {

}

int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {

}

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {

}

int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {

}

int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_max_size) {

}

void waitDevice(int type, int unit, int *status) {

}

void wakeupByDevice(int type, int unit, int status) {

}





