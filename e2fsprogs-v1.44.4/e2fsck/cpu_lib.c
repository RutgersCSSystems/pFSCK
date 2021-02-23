/* CPU_Lib.c
 *
 * This library is needed to get specific CPU utilization 
 * information.
 */


/* Returns the total number of cores available 
 * in current CPUSET
 *
 */
int get_number_of_available_cores(){
	return 16;
}

/* Returns the total core utilization
 *
 * @return  utilization ranging from 0%-100.0%
 */
float get_total_core_utilization(){
	return 100.0;
}


/* Retturn the utilization of a specific core
 *
 * @arg  core  cpu id to sample
 *
 * @return   utilization from 0%-100.0%
 */
float get_core_utilization(int core){
	return 0;
}


/* Returns the core utilization of a calling process;
 *
 * @return  utilization ranging from 0%-100.0%
 */
float get_process_utilization(){
	return 100.0
}

/* Return the core utilization of calling thread
 *
 * @return  utilization ranging from 0%-100.0%
 */
float get_thread_utilization(){
	return 100.0;
}








