#ifndef LCC_TASK_H
#define LCC_TASK_H

/*
 * FreeRTOS task glue for the LCC stack. Kept out of lcc_if.h so the
 * interface layer stays free of scheduler entry points.
 */

/* Initialize stack-level state: creates queues / mutexes inside lcc_if
 * and prepares any task-owned state. Call before vTaskStartScheduler. */
void lcc_task_init(void);

/* Main task body: drain the RX queue, run the alias tick, and service
 * backlog. Never returns. Intended to be invoked from task_protocol. */
void lcc_task_run(void);

#endif /* LCC_TASK_H */
