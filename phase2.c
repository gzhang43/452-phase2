/*
Assignment: Phase 2
Group: Grace Zhang and Ellie Martin
Course: CSC 452 (Operating Systems)
Instructors: Russell Lewis and Ben Dicken
Due Date: 10/11/23

Description: Code for Phase 2 of our operating systems kernel that implements
sending and receiving messages between processes thorugh the use of mailboxes. 
Contains methods to create and destroy mailboxes, and for sending and receiving 
messages from mailboxes that can also be used by processes to block, receive 
interrupts, and as mutexes. The program also has mailboxes created for handling 
interrupts from disks, terminals, and the clock handler, and different processes 
can block on these mailboxes to wait for interrupts in the form of a message being 
sent. A vector for syscalls is also created, and the syscall handler indexes into 
this to call the correct syscall.

To compile with testcases, run the Makefile. 
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

int numMailboxes;     // The number of mailboxes being used currently
int numMailboxSlots;  // The number of slots being used currently
int lastAssignedId;   // The last assigned index for mailboxes
int lastAssignedSlot; // The last assigned index for slots

int consumerAwake; // Use so only one consumer can be awake at a time
int producerAwake;

int timeOfLastClockMessage; // The time the last clock msg was sent

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

/*
A function called when a syscall is not implemented yet. The syscall
vector is initialized to contain nullsys function pointers.

Parameters:
    args - argument containing the syscall number and other fields
*/
void nullsys(USLOSS_Sysargs *args) {
    unsigned int PSR = USLOSS_PsrGet();
    USLOSS_Console("nullsys(): Program called an unimplemented syscall.  ");
    USLOSS_Console("syscall no: %d   PSR: 0x0%d\n", args->number, PSR);
    USLOSS_Halt(1);
}

/*
Initializes the data structures for phase2, such as the mailbox and
slot arrays and the shadow process table. Also initializes the
interrupt handlers for disks, terminals, and syscalls, and creates
the mailboxes for the clock and devices.
*/
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

/*
Starts the service processes for phase2.
*/
void phase2_start_service_processes(void) {

}

/*
Checks if any processes are blocked on the device or clock mailboxes.

If yes, then return 1 because processes are waiting on I/O. If not,
then return 0.
*/
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

/*
Clock handler called by phase 1. Checks if the last message sent to the
clock mailbox was over 100 ms ago, and sends another message if yes.
*/
void phase2_clockHandler(void) {
    if (USLOSS_PsrGet() % 2 == 0) {
        USLOSS_Console("Process is not in kernel mode.\n");
        USLOSS_Halt(1);
    }

    int status;
    int currTime = currentTime();
    if (currTime - timeOfLastClockMessage >= 100000) {
        int ret = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &status); 
        timeOfLastClockMessage = currTime;
        MboxCondSend(0, (void*)(&status), 4);
    } 
}

/*
Has the current process wait for a device to send an interrupt by
calling recv on its mailbox.

Parameters:
    type - the type of device (disk, terminal, or clock)
    unit - the unit number of the device
    status - out pointer to deliver the device status once an interrupt
             arrives
*/
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

/*
Interrupt handler for terminals that sends the status of the terminal
to its mailbox.

Parameters:
    arg - the unit number of the terminal
*/
void termHandler(int dev, void *arg) {
    int status;
    int unitNo = (int)(long)arg;
    
    int ret = USLOSS_DeviceInput(USLOSS_TERM_DEV, unitNo, &status);
    MboxCondSend(1 + unitNo, (void*)(&status), 4);
}

/*
The interrupt handler for disks that sends the status of a terminal to
its mailbox. 

Parameters:
    arg - the unit number of the disk
*/
void diskHandler(int dev, void *arg) {
    int status;
    int unitNo = (int)(long)arg;
    
    int ret = USLOSS_DeviceInput(USLOSS_DISK_DEV, unitNo, &status);
    MboxCondSend(5 + unitNo, (void*)(&status), 4);
}

/*
An interrupt handler for syscalls that indexes into the syscall vector and
calls the function located at the index.

Parameters:
    arg - contains the arguments for the syscall
*/
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

/*
Returns the next available slot id. The id is the index into the array of
mailbox slots, and can be reused.
*/
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

/*
Creates a mailbox with the given number of slots and slot size.

Parameters:
    slots - the number of slots to hold messages the mailbox should have
    slot_size - the largest message size that can be sent through this
                mailbox

Returns: the id of the allocated mailbox, or -1 in case of an error.
*/
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

/*
Destroys the mailbox with the given mbox_id. Frees all the slots of the
mailbox and starts process to flush all producers and consumers of the 
mailbox.

Parameters:
    mbox_id - the id of the mailbox to release

Returns: 0 if release was successful, and -1 if the id is not currently
in use.
*/
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

/*
Adds a message to the end of the given list of messages.

Parameters:
    slot - the Message instance to add
    messages - the list of messages to add to
*/
void addMessageToMailbox(struct Message* slot, struct Message* messages) {
    struct Message* temp = messages;
    while (temp->nextMessage != NULL) {
        temp = temp->nextMessage;
    }
    temp->nextMessage = slot;
}

/*
Adds the process with the given pid to the end of the given queue.

Parameters:
    pid - the pid of the process to add
    queue - the queue to add the process to
*/
void addProcessToEndOfQueue(int pid, struct PCB* queue) {
    struct PCB *process = &shadowProcessTable[pid % MAXPROC];
   
    struct PCB* temp = queue;
    while (temp->nextInQueue != NULL) {
        temp = temp->nextInQueue;
    }
    temp->nextInQueue = process;
} 

/*
Writes a message to the given mailbox. Requires that the mailbox has
sufficient space for a message.

Parameters:
    mbox_id - the id of the mailbox to write to
    msg_ptr - pointer to the message to write
*/
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

/*
Helper function for sending a message to a mailbox. Will block if mailbox
does not have sufficient space depending on the value of isCond.

Parameters:
    mbox_id - the id of the mailbox to send a message to
    msg_ptr - pointer to the message to send
    msg_size - the length of the message to send
    isCond - 0 if function should block, and 1 if send is conditional

Returns: -3 if the mailbox was released, -2 if the mailbox has run out of
slots, -1 if illegal argument values were given, and 0 if send was
successful.
*/
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

/*
Sends a message to the given mailbox, and blocks the process if the mailbox 
is full. The message will be sent eventually once the mailbox has open slots,
depending on where the process is in the producer queue.

Parameters:
    mbox_id - the id of the mailbox to send a message to
    msg_ptr - pointer to the message to send
    msg_size - the length of the message to send

Returns: -3 if the mailbox was released, -2 if the system has run out of
slots, -1 if illegal argument values were given, and 0 if send was
successful.
*/
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

/*
Sends a message to the given mailbox, but does not block in the case the
mailbox is full.

Parameters:
    mbox_id - the id of the mailbox to send a message to
    msg_ptr - pointer to the message to send
    msg_size - the length of the message to send

Returns: -3 if the mailbox was released, -2 if the system has run out of
slots, -1 if illegal argument values were given, and 0 if send was
successful.
*/
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

/*
Reads the first message from the given mailbox.

Parameters:
    mbox_id - the id of the mailbox to read from
    msg_ptr - the out pointer to hold the message read
    msg_max_size - the size of the buffer; can receive up to this size

Returns: -1 if illegal argument values were given, and 0 otherwise.
*/
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

/*
Helper function to receive message from a mailbox. Blocks upon encountering an 
empty mailbox depending on value of isCond. If consumer is blocked, then the
process is added to the consumer queue of the mailbox, and a message is received
when available.

Parameters:
    mbox_id - the id of the mailbox to receive from
    msg_ptr - pointer to buffer to hold received message
    msg_max_size - the size of the buffer; can receive up to this size

Returns: -3 if mailbox was released, -1 if illegal values were given as arguments,
and the size of the message received otherwise.
*/
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

/*
Function to receive message from a mailbox. Blocks upon encountering an 
empty mailbox. If consumer is blocked, then the process is added to the 
consumer queue of the mailbox, and a message is received when available.

Parameters:
    mbox_id - the id of the mailbox to receive from
    msg_ptr - pointer to buffer to hold received message
    msg_max_size - the size of the buffer; can receive up to this size

Returns: -3 if mailbox was released, -1 if illegal values were given as arguments,
and the size of the message received otherwise.
*/
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

/*
Function to receive message from a mailbox that does not block when a message is
not available.

Parameters:
    mbox_id - the id of the mailbox to receive from
    msg_ptr - pointer to buffer to hold received message
    msg_max_size - the size of the buffer; can receive up to this size

Returns: -3 if mailbox was released, -1 if illegal values were given as arguments,
and the size of the message received otherwise.
*/
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
