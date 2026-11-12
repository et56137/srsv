#include <stdio.h>

#define WHOAMI	"CONTROLLER"

#include "interface.h"

static struct input *inputs;
static struct input_interface *ii;
struct timespec t0; //for PRINTF & LOG

static void sigint_handler(int sig __attribute__((unused)))
{
	PRINTF("SIGINT: ending simulation\n");
	ii->simulation_stop = 1;
}

int main(int argc, char** argv)
{
	clock_gettime(CLOCK_MONOTONIC, &t0);

	int nr_inputs;
	if(argc == 2){
		nr_inputs = load_inputs(&inputs, argv[1]);
	} else{
		nr_inputs = load_inputs(&inputs, "inputs.txt");
	}


	ii = connect_to_input_interface(nr_inputs, 0);
	if (!ii) {
		PRINTF("ERROR: Controller failed to connect to interface! Is input-simulator started?\n");
		exit(1);
	}
	ii->pid_contr = getpid();

	struct sigaction act;
	act.sa_handler = sigint_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	pthread_mutex_lock(&ii->lock);
	ii->controller_ready = 1; //input simulator ready
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Controller ready\n");
	
	//wait for input simulator to be ready
	if (ii->input_simulator_ready) {
		//input simulator is ready and waiting for signal
		if (kill(ii->pid_input, SIGUSR1)) {
			perror("kill error");
			exit(1);
		}
	}
	else {
		pthread_mutex_unlock(&ii->lock);
		//wait for input simulator to signal us
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGUSR1);
		if (pthread_sigmask(SIG_BLOCK, &set, NULL)) {
			perror("pthread_sigmask error");
			exit(1);
		}
		int sig;
		if (sigwait(&set, &sig)) {
			perror("sigwait error");
			exit(1);
		}
		pthread_mutex_lock(&ii->lock);
	}
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Controller starting\n");

	//main loop
	while (ii->simulation_stop == 0) {
		int inputs_handled = 0;
		int prev_requests[nr_inputs];
		for(int i=0; i<nr_inputs; i++){
			prev_requests[i] = (&ii->input[i])->request;
		}
		for (int i = 0; i < nr_inputs && ii->simulation_stop == 0; i++) {
			//check/update i-th input
			
			//TODO
			struct sim_input *s = &ii->input[i];

			/* pending request for input i */
			if (s->request != s->response) {
				inputs_handled++;

				struct timespec start, end, cur, pause;
				clock_gettime(CLOCK_MONOTONIC, &start);
				s->t_started = start;
				PRINTF("Task %d %s started, req: %d\n", i, inputs[i].name, s->request);

				end = start;
				pause = end;
				TIMESPEC_ADD_MS(end, inputs[i].C);

				/* Wait 1ms, check updated information, if fail, stop running */
				do{
					TIMESPEC_ADD_MS(pause, 10);
					PRINTF("Doing %d %s, req: %d\n", i, inputs[i].name, s->request);
					for (int k=0; k<nr_inputs; k++){
						if(prev_requests[k] != (&ii->input[k])->request){
							PRINTF("New req: %d %s, req: %d\n", k, inputs[k].name, s->request);
							prev_requests[k] = (&ii->input[k])->request;
						};
					}
					pthread_mutex_unlock(&ii->lock);
					do {
						clock_gettime(CLOCK_MONOTONIC, &cur);
					} while (TIMESPEC_LE(cur, pause));
					pthread_mutex_lock(&ii->lock);
				} while(TIMESPEC_LE(pause, end));

				s->t_resp = cur;
				s->response = s->request; /* can also be += 1, the other file drops extras either way*/
			}

		}

		if (inputs_handled == 0 && ii->simulation_stop == 0) { //if no requests, sleep 1 msec
			pthread_mutex_unlock(&ii->lock);
			msleep(10);
			pthread_mutex_lock(&ii->lock);
		}
	}

	PRINTF("Controller ending\n");
	pthread_mutex_unlock(&ii->lock);

	return 0;
}
