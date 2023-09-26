#include <stdio.h>
#include <usloss.h>
#include "phase2.h"

typedef struct PCB {
    int pid;
    char name[MAXNAME]
    int priority;
    int filled;
    int isBlocked;
} PCB;

typedef struct Message {
    int mailboxId;
    int text[MAX_MESSAGE];
    int filled;
} Message;

typedef struct Mailbox {
    int id;
    int numSlots;
    int slotSize;
    struct Message* messages;
    int filled;
} Mailbox;

struct Mailbox mailboxes[MAXMBOX];
struct Message mailSlots[MAXSLOTS]; 
struct PCB shadowProcessTable[MAXPROC+1];

int numMailboxes;
int lastAssignedId;

void phase2_init(void) {
    if (USLOSS_PsrGet() % 2 == 0){
	USLOSS_Console("Process is not in kernel mode.\n");
	USLOSS_Halt(1);
    }

    for (int i = 0; i < MAXPROC; i++){
	shadowProcessTable[i].filled = 0;
    } 
    for (int i = 0; i < MAXMBOX; i++){
	mailboxes[i].filled = 0;
    }
     for (int i = 0; i < MAXSLOTS; i++){
	mailSlots[i].filled = 0;
    }

    numMailboxes = 0;
    lastAssignedId = 0;
}

void phase2_start_service_processes(void) {

}

int phase2_check_io(void) {

}

void phase2_clockHandler(void) {

}

/*
Returns the next available Mailbox id. The id is the index of the mailbox
in the array of mailboxes, and ids can be reused once a mailbox is destroyed.
*/
int getNextMailboxId() {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int nextId = lastAssignedId + 1;
    while (mailboxes[nextId % MAXMBOX].filled == 1) {
        nextId++;
    }
    return nextId % MAXMBOX;
}

/*
Returns 1 if a mailbox can be created, and 0 if it cannot because the 
maximum number of mailboxes has been reached.
*/
int mailboxAvail() {
    return numMailboxes < MAXMBOX;
}

int MboxCreate(int slots, int slot_size) {
    if (slots < 0 || slots > MAXSLOTS || !mailboxAvail()) {
        return -1;
    }
    int id = getNextMailboxId(); 
    Mailbox* mailbox = &mailboxes[id];

    mailbox->id = id;
    mailbox->numSlots = slots;
    mailbox->slotSize = slot_size; 

    return id;   
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





