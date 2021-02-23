/* Scheduler.c 
 *
 * Scheduler.c implements scheduler function
 *
 * The scheduler can be used two ways:
 *  1. Manually invoked via schedule()
 *  2. Via a scheduler background thread
 *
 * The scheduler relies on a generic policy which must be 
 * set as callback. The policy must specify given the current
 * core utilization and the number core utilization of the 
 * process, how to modify thread contexts and schedule threads.
 *
 * Using as a background scheduler thread:
 * In order to use the background scheduler thread,
 * a trigger callback must be set to trigger the
 * scheduler. This callback can be either a simple timer
 * function that puts the scheduler to sleep for periods
 * at a time, or even a semphaphore, allowing other threads
 * to post the the scheduler thread and activate scheduling. 
 */


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


/* Main Scheduler Struct
 *
 * Holds pointers to contexts and callbacks
 * in order tto facilitate scheduling
 *
 */
struct scheduler{
	int running;
	int core_quota;
	void* trigger;
	void* policy;
	void* context;
	pthread_t id;

	// Core aware 
	int pid; // if pid is non zero, then core aware
	char result[2048];
	char command[30];
};



////////////////////////////////////
// Scheduler Management Functions //
////////////////////////////////////

/* Inittializes a new scheduler
 *
 * Allocates a new scheduler struct and sets the default scheduler
 * values
 *
 * @arg  sched_pointer  address to scheduler pointer
 */
void initialize_scheduler(struct scheduler** sched_pointer){
	
	// Allocate Schedulers
	struct scheduler* s = (struct scheduler*) malloc(sizeof(struct scheduler));
	
	// Set the defaults
	s->running = 0;
	s->trigger = NULL;
	s->policy = NULL;
	s->context = NULL;

	// Set the scheduler pointer
	*sched_pointer = s;
}


/* Destroys the scheduler
 *
 * Stops any background scheduler thread as well as
 * frees the main scheduler struct
 *
 * @arg  sched_pointer  address to scheduler pointer
 */
void destroy_scheduler(struct scheduler** sched_pointer){
	
	// Terminate thread if background thread is being used
	free(*sched_pointer);
}



///////////////////////
// Scheduler Setters //
///////////////////////

/* Sets the scheduler policy to be used
 *
 * @arg  sched   pointer to scheduler struct
 * @arg  policy  pointer to policy callback function 
 */
void set_policy(struct scheduler* sched, void* policy){
	sched->policy = policy;
}

/* Set the context which is tto be used with the policy
 *
 * @arg  sched    pointer to scheduler struct
 * @arg  context  pointer to context to be used with policy
 */
void set_context(struct scheduler* sched, void* context){
	sched->context = context;
}

/* Sets the scheduler trigger 
 *
 * @arg  sched    pointer to scheduler struct
 * @arg  trigger  pointer to trigger callback function
 */
void set_trigger(struct scheduler* sched, void* trigger){
	sched->trigger = trigger;
}



////////////////////
// Schedule Logic //
////////////////////

/* Main schedule logic
 * 
 * Gather cpu utilization information and calls the policy callback.
 * Can be invoked by background scheduler thread of called explicity.
 *
 * @arg  sched  pointer to scheduler struct
 */
void schedule(struct scheduler* sched){

	// Variables to hold CPU utilization samples
	// MAKE SURE TO MATCH TYPES or else seg fault
	float cores;
	float total_cpu_utilization;
	int curr_threads;
	float  process_utilization;

	// Get current utilization 
	FILE *cmd;
	char *res_ptr;
	char *start;
	char *end;
	char number[10];
	float sample_delay = 0.2;

	if (sched->pid){
		sprintf(sched->command, "top -p %d -n 2 -b -d %f", sched->pid, sample_delay);
		cmd = popen(sched->command,"r");
		res_ptr = sched->result;
    		while (fgets(res_ptr, sizeof(sched->result), cmd) !=NULL)
			res_ptr += strlen(res_ptr);	
		pclose(cmd);
		//printf("%s", sched->result);


		// Parse total cpu utilization by looking for last occurence of 
		// the  substring "id" for idle CPU percentage. CPU utilization 
		// would then be 100% - idle %.
		res_ptr = sched->result + 1;
		while(*res_ptr != '\0'){
			if(res_ptr[0] == 'd' && res_ptr[-1] == 'i'){
				end = &res_ptr[-1];
			}
			res_ptr++;
		}
		start = &end[-1];
		while(start[-1] != ','){
			start--;
		}	
		memcpy(number, start, end-start);
		total_cpu_utilization = 100.0 - atof(number);
		printf("[sched] Total CPU Utilization: %f\n", total_cpu_utilization);

		// Get process cpu utilization by looking backwards for SCHEDULER status
		// 'S' = schedules or 'R' = running. Process Cpu utilization is right 
		// after the scheduler status 
		while(*res_ptr != 'S' && *res_ptr != 'R'){
			res_ptr--;	
		}
		start = res_ptr + 1;
		end = start;
		while(*end != '.'){
			end++;
		}
		while(*end != ' ' && *end != '\t'){
			end++;
		}
		memset(number, 0, 10);
		memcpy(number, start, end-start);
        	process_utilization = atof(number);
		printf("[sched] Process CPU Utilization: %f\n", process_utilization);

	}

	void (*policy_func)(float cpu_util, float process_util, void* args);
	// Run policy callback
	policy_func = sched->policy;

	policy_func(total_cpu_utilization, process_utilization, sched->context);

}



////////////////////////////////////
// Background Scheduler Functions //
////////////////////////////////////

/* Background scheduler thread function
 *
 * @arg  args  the scheduler pointer
 *
 */
void scheduler_thread(void* args){

	// Cast pointer to scheduler struct
	struct scheduler* sched = (struct scheduler*) args;

	void (*trigger_func)();
	trigger_func = sched->trigger;
	// Keep running scheduler policy 
	
	while(sched->running){
		schedule(sched);  // run scheduler
		trigger_func(); // wait on scheduler trigger
	}

}

/* Starts the background scheduler thread
 *
 * @arg  sched  scheduler pointer
 */
void run_scheduler_thread(struct scheduler* sched){
	
	if(sched->running == 1){
		printf("Scheduler already running");
		return; 
	}
	
	if(sched->policy == NULL){
		printf("Cannot run scheduler, no policy set");
		return;
	}

	if(sched->trigger == NULL){
		printf("WARNING: No scheduler trigger set. Will constantly spin.");
	}

	// Run Scheduler
	
    	int ret;

	sched->running = 1;	

	ret = pthread_create(&sched->id, NULL, &scheduler_thread, (void*) sched);
    	
	if(ret==0){
        	printf("Scheduler started successfully.\n");
    	}else{
		sched->running = 0;
        	printf("Scheduler Unable to start.\n");
	}
}

void kill_scheduler_thread(struct scheduler* sched){
	sched->running = 0;
}

void set_pid(struct scheduler* sched, int pid){
	sched->pid = pid;
}

