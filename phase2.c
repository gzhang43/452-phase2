#include <stdio.h>
#include <string.h>
#include <usloss.h>
#include "phase1.h"
#include "phase2.h"

void diskHandler(int dev, void *arg);
void termHandler(int dev, void *arg);

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

struct Mailbox mailboxes[MAXMBOX];
struct Message mailSlots[MAXSLOTS]; 
struct PCB shadowProcessTable[MAXPROC+1];

int numMailboxes;
int numMailboxSlots;
int lastAssignedId;
int lastAssignedSlot;

int consumerAwake; // Use so only one consumer can be awake at a time
int producerAwake;

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
    consumerAwake = 0;
    producerAwake = 0;
 
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
    while (mailSlots[nextSlot % MAXSLOTS].filled == 1) {
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

int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (mailboxes[mbox_id].released == 1 && mailboxes[mbox_id].filled == 1) {
        return -3;
    } 
    if (numMailboxSlots >= MAXSLOTS) {
        return -2;
    }
    if (mailboxes[mbox_id].filled == 0 || (msg_size > 0 && msg_ptr == NULL) ||
            msg_size > mailboxes[mbox_id].slotSize) {
        return -1;
    }

    if (mailboxes[mbox_id].numSlotsUsed < mailboxes[mbox_id].numSlots &&
            mailboxes[mbox_id].producers == NULL) {
        Message* slot = &mailSlots[getNextSlot()];
        
        if (msg_ptr != NULL) {
            strcpy(slot->text, msg_ptr);
        }
        else {
            slot->text[0] = '\0';
        }
        slot->filled = 1;
        lastAssignedSlot++;

        if (mailboxes[mbox_id].messages == NULL) {
            mailboxes[mbox_id].messages = slot;
        }
        else {
            addMessageToMailbox(slot, mailboxes[mbox_id].messages);
        }
        mailboxes[mbox_id].numSlotsUsed += 1;
        numMailboxSlots++;

        // Unblock process at head of consumer queue
        if (mailboxes[mbox_id].consumers != NULL && consumerAwake == 0) {
            consumerAwake = 1;
            unblockProc(mailboxes[mbox_id].consumers->pid);
        }
        restoreInterrupts(savedPsr);
        return 0;
    }
    else if (mailboxes[mbox_id].numSlots == 0) {
        if (mailboxes[mbox_id].consumers != NULL && consumerAwake == 0) {
            consumerAwake = 1;
            unblockProc(mailboxes[mbox_id].consumers->pid);      
        }
        else {
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

            mailboxes[mbox_id].producers = mailboxes[mbox_id].producers->nextInQueue;
            if (mailboxes[mbox_id].numSlotsUsed < mailboxes[mbox_id].numSlots &&
                    mailboxes[mbox_id].producerQueued == 1) {
                unblockProc(mailboxes[mbox_id].producers->pid);
            }
            else {
                producerAwake = 0;
            }
        }
        restoreInterrupts(savedPsr);
        return 0;
    }
    else {
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
        Message* slot = &mailSlots[getNextSlot()];
        
        if (msg_ptr != NULL) {
            strcpy(slot->text, msg_ptr);
        }
        else {
            slot->text[0] = '\0';
        }
        slot->filled = 1;
        lastAssignedSlot++;

        if (mailboxes[mbox_id].messages == NULL) {
            mailboxes[mbox_id].messages = slot;
        }
        else {
            addMessageToMailbox(slot, mailboxes[mbox_id].messages);
        }
        mailboxes[mbox_id].numSlotsUsed += 1;
        numMailboxSlots++;
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
}

int MboxSendHelper(int mbox_id, void *msg_ptr) {

}

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (mailboxes[mbox_id].released == 1 && mailboxes[mbox_id].filled == 1) {
        return -3;
    }
    if (numMailboxSlots >= MAXSLOTS) {
        return -2;
    }
    if (mailboxes[mbox_id].filled == 0 || (msg_size > 0 && msg_ptr == NULL) ||
        msg_size > mailboxes[mbox_id].slotSize) {
        return -1;
    }

    if (mailboxes[mbox_id].numSlotsUsed < mailboxes[mbox_id].numSlots) {
        Message* slot = &mailSlots[getNextSlot()];
        
        if (msg_ptr != NULL) {
            strcpy(slot->text, msg_ptr);
        }
        else {
            slot->text[0] = '\0';
        }
        slot->filled = 1;
        lastAssignedSlot++;

        if (mailboxes[mbox_id].messages == NULL) {
            mailboxes[mbox_id].messages = slot;
        }
        else {
            addMessageToMailbox(slot, mailboxes[mbox_id].messages);
        }
        mailboxes[mbox_id].numSlotsUsed += 1;
        numMailboxSlots++;

        // Unblock process at head of consumer queue
        if (mailboxes[mbox_id].consumers != NULL && consumerAwake == 0) {
            consumerAwake = 1;
            unblockProc(mailboxes[mbox_id].consumers->pid);
        }
        restoreInterrupts(savedPsr);
        return 0;
    }
    else {
        restoreInterrupts(savedPsr);
        return -2;
    }
}

int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (mailboxes[mbox_id].released == 1 && mailboxes[mbox_id].filled == 1) {
	return -3;
    }
    if (mailboxes[mbox_id].filled == 0) {
	return -1;
    }
    if (mailboxes[mbox_id].numSlotsUsed > 0 && 
            mailboxes[mbox_id].consumerQueued == 0) {
        Message* slot = mailboxes[mbox_id].messages;

	if (strlen(slot->text) > msg_max_size) {
	    return -1;
	}

        strcpy(msg_ptr, slot->text);
        
        slot->filled = 0;
	mailboxes[mbox_id].messages = slot->nextMessage;
	mailboxes[mbox_id].numSlotsUsed -= 1;
        numMailboxSlots--;

        // Unblock process at the head of producer queue after receiving msg
        if (mailboxes[mbox_id].producers != NULL && producerAwake == 0) {
            producerAwake = 1;
            unblockProc(mailboxes[mbox_id].producers->pid);
        }
    }
    else if (mailboxes[mbox_id].numSlots == 0) {
        if (mailboxes[mbox_id].producers != NULL && producerAwake == 0) {
            producerAwake = 1;
            unblockProc(mailboxes[mbox_id].producers->pid);
        }
        else {
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
            mailboxes[mbox_id].consumers = mailboxes[mbox_id].consumers->nextInQueue;
	    if (mailboxes[mbox_id].consumers != NULL && 
                    mailboxes[mbox_id].messages != NULL) {
	        unblockProc(mailboxes[mbox_id].consumers->pid);
	    }
            else {
                consumerAwake = 0;
            }	
        }
        return 0;
    }
    else {
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

	Message* slot = mailboxes[mbox_id].messages;

	if (strlen(slot->text) > msg_max_size) {
	    return -1;
	}

        // Receive message and unblock next consumer if applicable	
        strcpy(msg_ptr, slot->text);
        
        slot->filled = 0;
	mailboxes[mbox_id].messages = slot->nextMessage;
	mailboxes[mbox_id].numSlotsUsed -= 1;
        numMailboxSlots--;

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
    restoreInterrupts(savedPsr);
    return strlen(msg_ptr) + 1;
}

int MboxRecvHelper(int mbox_id, void *msg_ptr) {

}

int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }
    int savedPsr = disableInterrupts(); 

    if (mailboxes[mbox_id].released == 1 && mailboxes[mbox_id].filled == 1) {
        return -3;
    }
    if (mailboxes[mbox_id].filled == 0) {
        return -1;
    }
    if (mailboxes[mbox_id].numSlotsUsed > 0) {
        Message* slot = mailboxes[mbox_id].messages;

        if (strlen(slot->text) > msg_max_size) {
            return -1;
        }

        strcpy(msg_ptr, slot->text);
        
        slot->filled = 0;
        mailboxes[mbox_id].messages = slot->nextMessage;
        mailboxes[mbox_id].numSlotsUsed -= 1;
        numMailboxSlots--;

        // Unblock process at the head of producer queue after receiving msg
        if (mailboxes[mbox_id].producers != NULL && producerAwake == 0) {
            producerAwake = 1;
            unblockProc(mailboxes[mbox_id].producers->pid);
        }
        restoreInterrupts(savedPsr);
        return strlen(msg_ptr) + 1;
    }
    else {
        restoreInterrupts(savedPsr);
        return -2;
    }
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



