#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define __NR_BARRIER_INIT 351
#define __NR_BARRIER_WAIT 352
#define __NR_BARRIER_DESTROY 353

#define barrier_init(x,y) syscall(__NR_BARRIER_INIT, x, y)

#define barrier_wait(x) syscall(__NR_BARRIER_WAIT, x)

#define barrier_destroy(x) syscall(__NR_BARRIER_DESTROY, x)

#define THREAD_COUNT_1 5
#define THREAD_COUNT_2 20

struct thread_data{
	int thread_no;
	int barrier_id;
	FILE *f;
};

int avg_sleep;
void childProcess();

void* threadFunction(void *p){
	int i,ret;
	struct thread_data *data = (struct thread_data *)p;
	
	for(i=0; i<100;i++){
		
		fprintf(data->f,"Thread going %d sleep, pid = %d barrier = %d i = %d \n",data->thread_no, getpid(), data->barrier_id, i);
		fflush(data->f);
		
		ret = barrier_wait(data->barrier_id);  
		if(ret < 0){
			
			fprintf(data->f,"Thread %d wait failed\n", data->thread_no);
			perror("wait error thread ");
			pthread_exit(NULL);
		}
		fprintf(data->f,"thread %d woke up\n", data->thread_no);
		fflush(data->f);

		// Barrier 2 thread 5 calls barrier destroy on 50th iteration
		if(data->thread_no == THREAD_COUNT_1 && i == 50){
			fprintf(data->f,"destroyed by thread %d\n",data->thread_no);
			ret = barrier_destroy(data->barrier_id);
			if(ret < 0)
				perror("barrier destroy failed");
		}
		usleep(rand()%(2*avg_sleep));
	}
	return 0;
}

int main(int argc, char **argv){
	int child_pid1, child_pid2, pid;
	int child_identifier = 1;
	int status;
	
	printf("Please Enter average sleep time in usecs\n");
	scanf("%d",&avg_sleep);

	child_pid1 = fork();
	if(child_pid1 == 0)
		childProcess(child_identifier);  // 1st child
	else if(child_pid1 < 0){
		perror("fork 1 failed :");
	}else{
		child_identifier++;
		child_pid2 = fork();			// 2nd child
		if(child_pid2 == 0)
			childProcess(child_identifier);
		else if(child_pid2 < 0){
			perror("fork 2 failed :");
		}else{
			// parent (main) waits for child processes to end
			pid = wait(&status);
			printf("process :%d ended\n ", pid);

			pid = wait(&status);
			printf("process :%d ended\n ", pid);
		}
	}

	return 0;
}


void childProcess(int child){
	struct thread_data data[THREAD_COUNT_1 + THREAD_COUNT_2]; 
	pthread_t threads[THREAD_COUNT_1 + THREAD_COUNT_2];
	FILE *f1, *f2;
	int ret, i;
	int id1, id2;
	time_t t;

	srand((unsigned) time(&t));

	if(child == 1){
		f1 = fopen("P1_T5.txt", "w+");
		if(f1 == NULL)
			perror("f1open error :");
		f2 = fopen("P1_T20.txt", "w+");
		if(f2 == NULL)
			perror("f2open error :");
	}
	else{
		f1 = fopen("P2_T5.txt", "w+");
		if(f1 == NULL)
			perror("f1 open error :");
		f2 = fopen("P2_T20.txt", "w+");
		if(f2 == NULL)
			perror("f2 open error :");
	}

	// Initializing 2 barriers
	barrier_init(THREAD_COUNT_1, &id1);
	barrier_init(THREAD_COUNT_2, &id2);


	printf("Process %d barrier 1 id :%d\n", getpid(), id1);
	printf("Process %d barrier 2 id :%d\n", getpid(), id2);

	for(i=0; i< (THREAD_COUNT_1 + THREAD_COUNT_2) ; i++){
		
		data[i].thread_no = i;
		if(i < THREAD_COUNT_1){
			// Thread 0...4 belong to 1st barrier
			data[i].barrier_id = id1;  
			data[i].f = f1;
			fprintf(f1,"created thread %d, barrier_id %d\n", data[i].thread_no, data[i].barrier_id);
			fflush(f1);
		}
		else{
			// Thread 5...20 belong to 2nd barrier
			data[i].barrier_id = id2;
			data[i].f = f2;
			fprintf(f2,"created thread %d, barrier_id %d\n", data[i].thread_no, data[i].barrier_id);
			fflush(f2);
		}

		ret = pthread_create( &threads[i], NULL, threadFunction, &data[i]);
		if(ret){
			printf("Error - pthread_create() return code: %d\n",ret);
			perror("Error: ");
		}
	}

	for(i=0; i< THREAD_COUNT_1 + THREAD_COUNT_2; i++){
		ret = pthread_join(threads[i], NULL);
		if(ret != 0){
			perror("pthread join error: ");
		}
	}
	ret = fclose(f1);
	if(ret < 0)
		perror("fclose 1 failed");
	ret = fclose(f2);
	if(ret < 0)
		perror("fclose 1 failed");

	ret = barrier_destroy(id1);
	if(ret < 0)
		perror("barrier destroy failed");

	// This barrier was destroyed earlier would throw error
	ret = barrier_destroy(id2);
	if(ret < 0)
		perror("barrier destroy failed");
	exit(0);
}