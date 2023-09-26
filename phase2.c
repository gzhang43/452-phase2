#include <stdio.h>
#include <string.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"

void diskHandler(int dev, void *arg);
void termHandler(int dev, void *arg);

typedef struct PCB {
    int pid;
    char name[MAXNAME];
    int priority;
    int isBlocked;
    int filled;
} PCB;

typedef struct Message {
    int mailboxId;
    char text[MAX_MESSAGE];
    struct Message* nextMessage;
    int filled;
} Message;

typedef struct Mailbox {
    int id;
    int numSlots;
    int slotSize;
    int numSlotsUsed;
    struct Message* messages;
    struct PCB* consumers;
    struct PCB* producers;
    int filled;
} Mailbox;

struct Mailbox mailboxes[MAXMBOX];
struct Message mailSlots[MAXSLOTS]; 
struct PCB shadowProcessTable[MAXPROC+1];

int numMailboxes;
int numMailboxSlots;
int lastAssignedId;
int lastAssignedSlot;

void phase2_init(void) {
    if (USLOSS_PsrGet() % 2 == 0) {
	USLOSS_Console("Process is not in kernel mode.\n");
	USLOSS_Halt(1);
    }

    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;    
    USLOSS_IntVec[USLOSS_TERM_INT] = termHandler;    

    for (int i = 0; i < MAXPROC; i++) {
	shadowProcessTable[i].filled = 0;
    } 
    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].filled = 0;
    }
    for (int i = 0; i < MAXSLOTS; i++) {
	mailSlots[i].filled = 0;
    }

    numMailboxes = 0;
    lastAssignedId = -1;
    lastAssignedSlot = -1;
    
    MboxCreate(1, 4); 
    MboxCreate(1, 4); 
    MboxCreate(1, 4); 
    MboxCreate(1, 4); 
    MboxCreate(1, 4); 
    MboxCreate(1, 4); 
    MboxCreate(1, 4); 
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

int getNextSlot() {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }              
    int nextSlot = lastAssignedSlot + 1;
    while (mailboxes[nextSlot % MAXMBOX].filled == 1) {
        nextSlot++;  
    }              
    return nextSlot % MAXSLOTS;
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

    lastAssignedId = id;
    numMailboxes++;
    return id;   
}

int MboxRelease(int mbox_id) {

}

int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (numMailboxSlots >= MAXSLOTS) {
        return -2;
    }
    // TODO: Check illegal argument values and if mailvox was released
    // TODO: Block if there is no space in the mailbox
    // TODO: Add message slots to linked list

    if (mailboxes[mbox_id].numSlotsUsed < mailboxes[mbox_id].numSlots) {
        Message* slot = &mailSlots[getNextSlot()];
        strcpy(slot->text, msg_ptr);
        mailboxes[mbox_id].messages = slot;
        return 0;
    }
}

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {

}

int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    //if (mailboxes[mbox_id].numSlotsUsed > 0) {
        Message* slot = mailboxes[mbox_id].messages;
        strcpy(msg_ptr, slot->text);
    //}
    return strlen(msg_ptr) + 1;
}

int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_max_size) {

}

void waitDevice(int type, int unit, int *status) {

}

void wakeupByDevice(int type, int unit, int status) {

}

void diskHandler(int dev, void *arg) {
    int unitNo = (int)(long)arg;
}

void termHandler(int dev, void *arg) {
    int unitNo = (int)(long)arg;
}



