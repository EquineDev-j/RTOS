//*****************************************************************************
//
// ti rtos kernal
// 2/11/22
// Jannel Bennett
//
// For this lab;
// 1. init a2d (this can be in a function)
// 2. in the a2d you will need to set up interrupt like norm. you DO NOT need to do the NVIC reg stuff, the rtos will handle
// 3. create the HWI object and associate interrupt #40, which is the a2d with that object as well as w.e. function you write to
//    handle what happens when the a2d interrupt occurs
// 4. if you add other objects, hwi or swi , you need to #include 's for those added (BIOS module Headers)
// 5. print using RTOS     System_printf("SETPT: &d\n", setpoint_value);
// 6. need to change some lines in *The System.SupportProxy , system.h
//
/* Included is imported project from Resource Explorer 'Event' as a starting block
 * Copyright (c) 2015-2019, Texas Instruments Incorporated
 * All rights reserved.
 *
 */

/*
 *  ======== event_w_notes.c ========
 */
/*
 * The code is going to have two tasks, and they're going to work with each other to send some information out to
 *  the console. These are going to use READER and WRITER tasks. The WRITER task writes information into a mailbox and then
 *  the READER task reads that information from the mailbox and prints it to the console.
 */

/* XDC module Headers */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS module Headers */  // (this is all the rtos defines / structures / etc.)
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Mailbox.h>

#include <ti/drivers/Board.h> // driver is some code packaged up that let's you interface to some device in an easier manner
                              // hides detail and complexity for interfacing to different things

#define __MSP432P401R__
#include "msp.h"

#define NUMMSGS         3       /* Number of messages */ // how many messages are going to be done
#define TIMEOUT         12      /* Timeout value */ // these are clock ticks and each tick is 1ms by default

#define TASKSTACKSIZE   512 // this is used for creating stack sizes for the tasks

typedef struct MsgObj { // message object to use with the mailbox
    Int         id;             /* Writer task id */
    Char        val;            /* Message value */
} MsgObj, *Msg;

/* (FUNCTION PROTOTYPES) */
Void clk0Fxn(UArg arg0); // clock 0 function
Void clk1Fxn(UArg arg0); // clock 1 function
Void readertask(UArg arg0, UArg arg1);
Void writertask(UArg arg0, UArg arg1);

__extern xdc_Void xdc_runtime_System_flush__E(void); // helps to print right  away


/*(GLOBAL DECLARATIONS)*/
// task structures - separate stack for each task ( don't have stack overflow, know stack size needed)
//                 - dynamically scheduled based on priority level(32 levels) and current state
//                 - equal priority tasks get scheduled in order of creation
Task_Struct task0Struct, task1Struct; // structure that contains information about a given task
Char task0Stack[TASKSTACKSIZE], task1Stack[TASKSTACKSIZE]; // arrays defined for task0 and task1 stacks
                                                           // size of array -> TASKSTACKSIZE = 512
// semaphore structures - "flagger" on road construction
//                      - used to coordinate access to shared resources - global variables, buffer, etc.
Semaphore_Struct sem0Struct, sem1Struct;
Semaphore_Handle semHandle;

// event structures - similar to semaphore(binary-available or not - limit to 2 tasks)
//                  - only one task can pend on an event object at a time
//                  - only tasks can wait on events (not hwi / swi)
Event_Struct evtStruct;
Event_Handle evtHandle;

// clocks - periodic system tick - used for timeouts
//        - use clock objects to call functions to run on a timeout
Clock_Struct clk0Struct, clk1Struct;
Clock_Handle clk0Handle, clk1Handle;

// Mailboxes - pass buffers between tasks
//           - can be used for multiple tasks
Mailbox_Struct mbxStruct;
Mailbox_Handle mbxHandle;

/*
 *  ======== main ========
 */
int main()
{
// IN LAB6 - you'll set up A2D and some GPIO's //

/* (DECLARATIONS) */
/* Construct BIOS Objects */
    Task_Params taskParams;
    Semaphore_Params semParams;
    Clock_Params clkParams;
    Mailbox_Params mbxParams;

/* Call driver init functions */
    Board_init();

/* Construct writer/reader Task threads */
    // 1. put some default values into a parameter structure
    Task_Params_init(&taskParams);
    // 2. some specific values are now to be changed as follows
    taskParams.arg0 = (UArg)mbxHandle; // arg0 is the handle to the mailbox,
                                       // telling it what that mailbox is that's going to be used
    taskParams.stackSize = TASKSTACKSIZE;
    taskParams.priority = 1; // set task priority level
    taskParams.stack = &task0Stack; // address of the start of the stack
    // 3. structure gets passed in here to the task construct function
    //     this is similar to the create function we were looking at previously, (slightly different mechanism) 15:05m
    Task_construct(&task0Struct, (Task_FuncPtr)writertask, &taskParams, NULL);
    //      we pass in the address of the second stack here
    taskParams.stack = &task1Stack;
    Task_construct(&task1Struct, (Task_FuncPtr)readertask, &taskParams, NULL);
    // 4. create event object that will be used with the semaphore
    //    if you use construct functions instead of create, returns nothing
    //    so to get handle you have to call a separate function -> Event_handle(&evtStruct)
    Event_construct(&evtStruct, NULL);

/* Obtain event instance handle */
    evtHandle = Event_handle(&evtStruct);

    // update semaphore from the defaults
    Semaphore_Params_init(&semParams);
    // specifying binary mode
    semParams.mode = Semaphore_Mode_BINARY;
    // giving it the event handle, so this event that was set up is going to be associated here with the semaphore
    semParams.event = evtHandle;
    // event ID for the event
    semParams.eventId = Event_Id_01;
    // actually creating the semaphore object
    Semaphore_construct(&sem0Struct, 0, &semParams);

    // getting the handle to make use of it
    semHandle = Semaphore_handle(&sem0Struct);

    // similar thing with the clocks
    // (remember there are 2 places to specify what the timeout period is)
    // 1. the call to create the object contains the initial timeout
    // 2. the parameter structure contains the steady state timeout
    // without 2. the clocks here are set up as one shot timers


    // initialize the parameter structure
    Clock_Params_init(&clkParams);
    // start flag is changed
    clkParams.startFlag = TRUE;
    // 2. changing the steady state period to 1000 ticks / 1 sec
    clkParams.period = 1000;
    //1. initial timeout period is 5 ONLY for initial cycle, than it will be steady state period if set up, else one and done
    Clock_construct(&clk0Struct, (Clock_FuncPtr)clk0Fxn,
                    5, &clkParams);
    //1. initial time out period is 10 ONLY for initial cycle, then it will be SS period if set up, else one and done
    Clock_construct(&clk1Struct, (Clock_FuncPtr)clk1Fxn,
                    10, &clkParams);

    clk0Handle = Clock_handle(&clk0Struct);
    clk1Handle = Clock_handle(&clk1Struct);

/* Construct a Mailbox Instance */
    Mailbox_Params_init(&mbxParams);
    mbxParams.readerEvent = evtHandle;
    mbxParams.readerEventId = Event_Id_02;
    Mailbox_construct(&mbxStruct,sizeof(MsgObj), 2, &mbxParams, NULL);
    mbxHandle = Mailbox_handle(&mbxStruct);

/* GPIO init */ // PUT IN A FUNCTION //
    P1->DIR |= BIT0; // red LED
    P1->OUT &=~ BIT0; // start off
    P2->DIR |= BIT1; //green LED
    P2->DIR &=~ BIT1; // start off

    // after everything is all initialized and set up we call BIOS_start() to start up the RTOS
    //      and kick it off to run
    BIOS_start();    /* Does not return */ // under norm circumstances it will never return
                                          // from this function call. it will stay in there and keep running
    // if there are errors it will exit BIOS_start() and return 0 and be done
    return(0);
}

/*
 *  ======== clk0Fxn =======
 */
Void clk0Fxn(UArg arg0)
{
    /* Explicit posting of Event_Id_00 by calling Event_post() */
    Event_post(evtHandle, Event_Id_00); // posting is freeing it up
                                        // these happen if we didn't specify a period inside the structure
    P2->OUT ^= BIT1; // toggle LED
}

/*
 *  ======== clk1Fxn =======
 */
Void clk1Fxn(UArg arg0)
{
    /* Implicit posting of Event_Id_01 by Sempahore_post() */
    Semaphore_post(semHandle); // posting is freeing it up
                                // these happen once because we didn't specify a period inside the structure
}

/*
 *  ======== reader ======== // read messages from mailbox and then print them to the console
 */
Void readertask(UArg arg0, UArg arg1)
{
    MsgObj msg;
    UInt posted;

    for (;;) { //  infinite for loop - need because otherwise it is set for one and done cycle
        /* Wait for (Event_Id_00 & Event_Id_01) | Event_Id_02 */
                // // pending is requesting it,
        posted = Event_pend(evtHandle,  // we give handle of event so it knows which one
            Event_Id_00 + Event_Id_01,  /* andMask */ // this event is active when both event0 and event1
                                                      // are active. (i.e., something has posted both of these)
                                                     // Event_ID_00 = 0x01 Event_ID_01 = 0x02
            Event_Id_02,                /* orMask */ // OR this event is active when event2 is active.
            TIMEOUT); // this is how long to wait (this is 12ticks/12ms)
                      // if it doesn't happen in this time the patent function will return a 0 (timed out)
                      // otherwise it will return unsigned int, i.e., bits corresponding to which sub-event were active set
                      // example if Event_ID_00 + Event_ID_01 happened it will return 0x03

        if (posted == 0) { // check to see if timed out
            System_printf("Timeout expired for Event_pend()\n");
            break;
        }

        if ((posted & Event_Id_00) && (posted & Event_Id_01)) { // did Event_ID_00 + Event_ID_01 happen?
            /*
             * The following call to Semaphore_pend() will update the embedded
             * Event object to reflect the state of the semaphore's count after
             * the return from Semaphore_pend().
             * If the count is zero, then Event_Id_01 is cleared in the Event
             * object. If the count is non-zero, then Event_Id_01 is set in
             * the Event object.
             */
            if (Semaphore_pend(semHandle, BIOS_NO_WAIT)) { // pend on semaphore but ends with time of NO WAIT
                                                           // (i.e., it's not ready so we are moving on, not waiting for it)
                System_printf("Explicit posting of Event_Id_00 and Implicit posting of Event_Id_01\n");
            }
            else {
                System_printf("Semaphore not available. Test failed!\n");
            }
            break;
        }
        else if (posted & Event_Id_02) {
            System_printf("Implicit posting of Event_Id_02\n");
            /*
             * The following call to Mailbox_pend() will update the embedded
             * Event object to reflect whether messages are available in the
             * Mailbox after the current message is removed.
             * If there are no more messages available, then Event_Id_02 is
             * cleared in the Event object. If more messages are available,
             * then Event_Id_02 is set in the Event object.
             */
            if (Mailbox_pend(mbxHandle, &msg, BIOS_NO_WAIT)) { // mailbox returns 1? also pend on mailbox, want to get info from it
                                                              // do you have something for me?
                                                              // again we NO WAIT, so if nothing there we are moving on, not waiting
                /* Print value */
                System_printf("read id = %d and val = '%c'.\n",msg.id, msg.val);
            }
            else {
                System_printf("Mailbox not available. Test failed!\n");
            }
        }
        else {
            System_printf("Unknown Event\n");
            break;
        }
    }

    while(1){
        Event_pend(evtHandle,// use event to toggle
             Event_Id_00 + Event_Id_01,  /* andMask */
             Event_Id_02,                /* orMask */
             TIMEOUT*200); // 2400ticks
        P1->OUT^= BIT0; // toggle RED LED
    }

    // BIOS_exit(0); // after all that it will exit (FOR LAB6 YOU DO NOT WANT TO EXIT! you want to go on 4evr
}

/*
 *  ======== writer ======== // puts information into the mailbox
 */
Void writertask(UArg arg0, UArg arg1)
{
    MsgObj      msg;
    Int i;

    for (i=0; i < NUMMSGS; i++) { // NUMMSGS was set to 3, so loop 3 times
        /* Fill in value */
        msg.id = i; // populate message structure, id will get current value of i (0,1, or 2)
        msg.val = i + 'a'; // val member would get single character (i + 'a')
                           // 'a' is ASCII value hex61
                           // i=1 we get 'a' , i=2 we get 'b', i=3 we get 'c'

        System_printf("writing message id = %d val = '%c' ...\n",
        msg.id, msg.val);

        /* Enqueue message */
            // post to mailbox // posting is freeing it up
        Mailbox_post(mbxHandle, &msg, TIMEOUT);  // passing in the address of the message structure, handle the mailbox, timeout
    }

    System_printf("writer done.\n"); // then it will be done and terminate ( no for(;;) loop )
}

