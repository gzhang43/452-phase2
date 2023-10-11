/*
* Assignment: Phase 2
* Group: Grace Zhang and Ellie Martin
* Course: CSC 452 (Operating Systems)
* Instructors: Russell Lewis and Ben Dicken
* Due Date: 10/11/23
*
* Description: Code for our operating systems kernel that implements
* sending and receiving messages between processes thorugh the use of
* mailboxes.
*/

#include <stdio.h>
#include <string.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"

void diskHandler(int dev, void *arg);
void termHandler(int dev, void *arg);
void syscallHandler(int dev, void *arg);

typedef struct PCB {
    int pid;
    int isBlocked;
    struct PCB* nextInQueue;
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
    int consumerQueued;
    int producerQueued;
    int released;
    int filled;
} Mailbox;

void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);

struct Mailbox mailboxes[MAXMBOX];
struct Message mailSlots[MAXSLOTS]; 
struct PCB shadowProcessTable[MAXPROC+1];

int numMailboxes;
int numMailboxSlots;
int lastAssignedId;
int lastAssignedSlot;

int consumerAwake; // Use so only one consumer can be awake at a time
int producerAwake;

int timeOfLastClockMessage;

/*
Disables interrupts in the simulation by setting the corresponding bit
in the PSR to 0.
*/
unsigned int disableInterrupts() {
    unsigned int psr = USLOSS_PsrGet();
    int result = USLOSS_PsrSet(USLOSS_PsrGet() & ~2);
    if (result == 1) {
        USLOSS_Console("Error: invalid PSR value for set.\n");
        USLOSS_Halt(1);
    }
    return psr;
}

/*
Restores interrupts in the simulation by setting the PSR to the saved
PSR value.

Parameters:
    savedPsr - the saved PSR value
*/
void restoreInterrupts(int savedPsr) {
    int result = USLOSS_PsrSet(savedPsr);
    if (result == 1) {
        USLOSS_Console("Error: invalid PSR value for set.\n");
        USLOSS_Halt(1);
    } 
}

/*
Enables interrupts in the simulation by setting the corresponding
PSR bit to 1.
*/
void enableInterrupts() {
    int result = USLOSS_PsrSet(USLOSS_PsrGet() | 2);
    if (result == 1) {
        USLOSS_Console("Error: invalid PSR value for set.\n");
        USLOSS_Halt(1);
    } 
}

void nullsys(USLOSS_Sysargs *args) {
    unsigned int PSR = USLOSS_PsrGet();
    USLOSS_Console("nullsys(): Program called an unimplemented syscall.  ");
    USLOSS_Console("syscall no: %d   PSR: 0x0%d\n", args->number, PSR);
    USLOSS_Halt(1);
}

void phase2_init(void) {
    if (USLOSS_PsrGet() % 2 == 0) {
	USLOSS_Console("Process is not in kernel mode.\n");
	USLOSS_Halt(1);
    }

    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;    
    USLOSS_IntVec[USLOSS_TERM_INT] = termHandler;    
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;

    for (int i = 0; i < MAXPROC; i++) {
	shadowProcessTable[i].filled = 0;
    } 
    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].filled = 0;
    }
    for (int i = 0; i < MAXSLOTS; i++) {
	mailSlots[i].filled = 0;
    }
    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;   
    }

    numMailboxes = 0;
    lastAssignedId = -1;
    lastAssignedSlot = -1;
    consumerAwake = 0;
    producerAwake = 0;

    timeOfLastClockMessage = currentTime();
 
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
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    for (int i = 0; i < 7; i++) {
        if (mailboxes[i].consumers != NULL) {
            return 1;
        }
    }
    return 0;
}

void phase2_clockHandler(void) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }

    int status;

    if (currentTime() - timeOfLastClockMessage >= 100000) {
        timeOfLastClockMessage = currentTime();
        int ret = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &status); 
        MboxCondSend(0, (void*)(&status), 4);
    } 
}

void waitDevice(int type, int unit, int *status) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }

    if (type == USLOSS_CLOCK_DEV) {
        if (unit != 0) {
            USLOSS_Console("ERROR\n");
            USLOSS_Halt(1);
        }
    }
    else if (type == USLOSS_TERM_DEV) {
        if (unit < 0 || unit > 3) {
            USLOSS_Console("ERROR\n");
            USLOSS_Halt(1);
        }
        unit = unit + 1;
    }
    else if (type == USLOSS_DISK_DEV) {
        if (unit != 0 && unit != 1) {
            USLOSS_Console("ERROR\n");
            USLOSS_Halt(1);
        }
        unit = unit + 5;
    }
    MboxRecv(unit, status, sizeof(int));
}

void wakeupByDevice(int type, int unit, int status) {

}

void termHandler(int dev, void *arg) {
    int status;
    int unitNo = (int)(long)arg;
    
    int ret = USLOSS_DeviceInput(USLOSS_TERM_DEV, unitNo, &status);
    MboxCondSend(1 + unitNo, (void*)(&status), 4);
}

void diskHandler(int dev, void *arg) {
    int status;
    int unitNo = (int)(long)arg;
    
    int ret = USLOSS_DeviceInput(USLOSS_DISK_DEV, unitNo, &status);
    MboxCondSend(5 + unitNo, (void*)(&status), 4);
}

void syscallHandler(int dev, void *arg) {
    USLOSS_Sysargs* call = (USLOSS_Sysargs*)arg;
    int callNo = call->number;

    if (callNo >= MAXSYSCALLS || callNo < 0) {
        USLOSS_Console("syscallHandler(): Invalid syscall number %d\n", callNo);
        USLOSS_Halt(1);
    }
    
    (*(systemCallVec[callNo]))(call);
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
    while (!(mailboxes[nextId % MAXMBOX].filled == 0 || (
            mailboxes[nextId % MAXMBOX].filled == 1 && 
            mailboxes[nextId % MAXMBOX].released == 1 &&
            mailboxes[nextId % MAXMBOX].consumers == NULL &&
            mailboxes[nextId % MAXMBOX].producers == NULL))) {
        nextId = (nextId + 1) % MAXMBOX;
    }
    return nextId;
}

int getNextSlot() {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }              
    int nextSlot = lastAssignedSlot + 1;
    while (mailSlots[nextSlot % MAXSLOTS].filled == 1) {
        nextSlot = (nextSlot + 1) % MAXSLOTS;  
    }              
    return nextSlot;
} 

/*
Returns 1 if a mailbox can be created, and 0 if it cannot because the 
maximum number of mailboxes has been reached.
*/
int mailboxAvail() {
    return numMailboxes < MAXMBOX;
}

int MboxCreate(int slots, int slot_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (slots < 0 || slots > MAXSLOTS || slot_size < 0 || 
            slot_size > MAX_MESSAGE || !mailboxAvail()) {
        return -1;
    }
    int id = getNextMailboxId(); 
    Mailbox* mailbox = &mailboxes[id];

    mailbox->id = id;
    mailbox->numSlots = slots;
    mailbox->slotSize = slot_size; 
    mailbox->filled = 1;

    lastAssignedId = id;
    numMailboxes++;

    restoreInterrupts(savedPsr);
    return id;   
}

int MboxRelease(int mbox_id) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (mailboxes[mbox_id].filled == 0) {
        return -1;
    }
    mailboxes[mbox_id].released = 1;
    numMailboxes--;

    Message* messages = mailboxes[mbox_id].messages;
    while (messages != NULL) {
        messages->filled = 0;
        numMailboxSlots--;
        messages = messages->nextMessage;
    }

    if (mailboxes[mbox_id].producers != NULL) {
        unblockProc(mailboxes[mbox_id].producers->pid);
    }

    if (mailboxes[mbox_id].consumers != NULL) {
        unblockProc(mailboxes[mbox_id].consumers->pid);
    }

    restoreInterrupts(savedPsr);
    return 0;
}

void addMessageToMailbox(struct Message* slot, struct Message* messages) {
    struct Message* temp = messages;
    while (temp->nextMessage != NULL) {
        temp = temp->nextMessage;
    }
    temp->nextMessage = slot;
}

void addProcessToEndOfQueue(int pid, struct PCB* queue) {
    struct PCB *process = &shadowProcessTable[pid % MAXPROC];
   
    struct PCB* temp = queue;
    while (temp->nextInQueue != NULL) {
        temp = temp->nextInQueue;
    }
    temp->nextInQueue = process;
} 

void writeMessage(int mbox_id, void *msg_ptr) {
    Message* slot = &mailSlots[getNextSlot()];
       
    if (msg_ptr != NULL) {
        strcpy(slot->text, msg_ptr);
    }
    else {
        slot->text[0] = '\0';
    }
    slot->filled = 1;

    if (mailboxes[mbox_id].messages == NULL) {
        mailboxes[mbox_id].messages = slot;
    }
    else {
        addMessageToMailbox(slot, mailboxes[mbox_id].messages);
    }
    mailboxes[mbox_id].numSlotsUsed += 1;
    lastAssignedSlot++;
    numMailboxSlots++;
}

int Send(int mbox_id, void *msg_ptr, int msg_size, int isCond) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (numMailboxSlots >= MAXSLOTS) {
        return -2;
    }
    if (mailboxes[mbox_id].filled == 0 || (msg_size > 0 && msg_ptr == NULL) ||
            msg_size > mailboxes[mbox_id].slotSize ||
            mailboxes[mbox_id].released == 1) {
        return -1;
    }

    if ((mailboxes[mbox_id].numSlotsUsed < mailboxes[mbox_id].numSlots &&
            mailboxes[mbox_id].producers == NULL) || (
            mailboxes[mbox_id].numSlots == 0 && 
            mailboxes[mbox_id].consumers != NULL && 
            consumerAwake == 0)) {

        if (mailboxes[mbox_id].numSlots != 0) {
            writeMessage(mbox_id, msg_ptr);
        }

        // Unblock process at head of consumer queue
        if (mailboxes[mbox_id].consumers != NULL && consumerAwake == 0) {
            consumerAwake = 1;
            unblockProc(mailboxes[mbox_id].consumers->pid);
        }
        restoreInterrupts(savedPsr);
        return 0;
    }
    else if (!isCond) {
        shadowProcessTable[getpid() % MAXPROC].pid = getpid();
        if (mailboxes[mbox_id].producers == NULL) {
            mailboxes[mbox_id].producers = &shadowProcessTable[getpid() % 
                MAXPROC];
        }
	    else {
            addProcessToEndOfQueue(getpid(), mailboxes[mbox_id].producers);
        }
        blockMe(13);

        if (mailboxes[mbox_id].released == 1) {
            mailboxes[mbox_id].producers = mailboxes[mbox_id].producers->nextInQueue;
            if (mailboxes[mbox_id].producers != NULL) {
                unblockProc(mailboxes[mbox_id].producers->pid);
            }
            else {
                mailboxes[mbox_id].filled = 0;
            }
            return -3;
        }
        
        // Write message to slot once unblocked and unblock next producer if
        // applicable
        if (mailboxes[mbox_id].numSlots != 0) {
            writeMessage(mbox_id, msg_ptr);
        }

        mailboxes[mbox_id].producers = mailboxes[mbox_id].producers->nextInQueue;

        if (mailboxes[mbox_id].consumers != NULL && consumerAwake == 0) {
            consumerAwake = 1;
            unblockProc(mailboxes[mbox_id].consumers->pid);
        }        
        if (mailboxes[mbox_id].numSlotsUsed < mailboxes[mbox_id].numSlots &&
                mailboxes[mbox_id].producers != NULL) {
            unblockProc(mailboxes[mbox_id].producers->pid);
        }
        else {
            producerAwake = 0;
        }

        restoreInterrupts(savedPsr);
        return 0;
    } 
    else {
        restoreInterrupts(savedPsr);
        return -2;
    }
}

int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 
    int retVal = Send(mbox_id, msg_ptr, msg_size, 0);
    restoreInterrupts(savedPsr);
    return retVal;
}

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 
    int retVal = Send(mbox_id, msg_ptr, msg_size, 1);
    restoreInterrupts(savedPsr);
    return retVal;
}

int readMessage(int mbox_id, void *msg_ptr, int msg_max_size) {
    Message* slot = mailboxes[mbox_id].messages;

    if (strlen(slot->text) + 1 > msg_max_size && 
            !(mailboxes[mbox_id].slotSize == 0 && msg_max_size == 0)) {
        return -1;
    }  
    if (msg_max_size != 0) {
        memcpy(msg_ptr, slot->text, msg_max_size);
    }
    slot->filled = 0;
    
    mailboxes[mbox_id].messages = slot->nextMessage;
    mailboxes[mbox_id].numSlotsUsed -= 1;
    numMailboxSlots--;
    return 0;
}

int Recv(int mbox_id, void *msg_ptr, int msg_max_size, int isCond) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (mailboxes[mbox_id].filled == 0 ||
            mailboxes[mbox_id].released == 1) {
	return -1;
    }
    if ((mailboxes[mbox_id].numSlotsUsed > 0 && 
            mailboxes[mbox_id].consumerQueued == 0) || (
            mailboxes[mbox_id].numSlots == 0 &&
            mailboxes[mbox_id].producers != NULL && producerAwake == 0)) {

        if (mailboxes[mbox_id].numSlots != 0) {
            int ret = readMessage(mbox_id, msg_ptr, msg_max_size);
            if (ret == -1) {
                return -1;
            }
        }
        
        // Unblock process at the head of producer queue after receiving msg
        if (mailboxes[mbox_id].producers != NULL && producerAwake == 0) {
            producerAwake = 1;
            unblockProc(mailboxes[mbox_id].producers->pid);
        }
    }
    else if (!isCond) {
        shadowProcessTable[getpid() % MAXPROC].pid = getpid();
	if (mailboxes[mbox_id].consumers == NULL) {
            mailboxes[mbox_id].consumers = &shadowProcessTable[getpid() % 
                MAXPROC];
        }
	else {
            addProcessToEndOfQueue(getpid(), mailboxes[mbox_id].consumers);
        }
	blockMe(14);

        if (mailboxes[mbox_id].released == 1) {
            mailboxes[mbox_id].consumers = mailboxes[mbox_id].consumers->nextInQueue;
            if (mailboxes[mbox_id].consumers != NULL) {
                unblockProc(mailboxes[mbox_id].consumers->pid);
            }
            else {
                mailboxes[mbox_id].filled = 0;
            }
            return -3;
        }

        // Receive message and unblock next consumer if applicable	
        if (mailboxes[mbox_id].numSlots != 0) {
            int ret = readMessage(mbox_id, msg_ptr, msg_max_size);
            if (ret == -1) {
                return -1;
            }
        }

        mailboxes[mbox_id].consumers = mailboxes[mbox_id].consumers->nextInQueue;
	
        if (mailboxes[mbox_id].producers != NULL && producerAwake == 0) {
            producerAwake = 1;
            unblockProc(mailboxes[mbox_id].producers->pid);
        }
        if (mailboxes[mbox_id].consumers != NULL && 
                mailboxes[mbox_id].messages != NULL) {
	    unblockProc(mailboxes[mbox_id].consumers->pid);
	}
        else {
            consumerAwake = 0;
        }	
    }
    else {
        restoreInterrupts(savedPsr);
        return -2;
    }

    restoreInterrupts(savedPsr);
    if (mailboxes[mbox_id].numSlots != 0 && msg_max_size != 0 
            && strlen(msg_ptr) != 0) {        
        return strlen(msg_ptr) + 1;
    } else {
        return 0;
    }
}

int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 
    int retVal = Recv(mbox_id, msg_ptr, msg_max_size, 0);
    restoreInterrupts(savedPsr);
    return retVal;
}

int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 
    int retVal = Recv(mbox_id, msg_ptr, msg_max_size, 1);
    restoreInterrupts(savedPsr);
    return retVal;
}
