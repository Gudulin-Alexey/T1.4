#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>



typedef struct SharedM {	// указатель на разделяемую память и обекты синхронизации для доступа к ней
	int *i;
	int shmid;
	sem_t *sem1, *sem2;
} SharedM;

typedef struct ThreadData {	// структуа данных для передачи в поток	
	SharedM *shm;
	int *buf;
	pthread_mutex_t *buf_mutex;
	pthread_cond_t *buf_cond;
} ThreadData;



int flag = 1; 		//флаг о необходимости продолжать работу



void myhandle(int mysignal)
{
	flag = 0;
}
int init_data(ThreadData *data, SharedM *shm, int *buf, pthread_mutex_t *buf_mutex, pthread_cond_t *buf_cond)
{
	data->shm = shm;
	data->buf = buf;
	data->buf_mutex = buf_mutex;
	data->buf_cond = buf_cond;
	if(pthread_mutex_init(buf_mutex, NULL) != 0)
	{
		return 1;
	}
	if (pthread_cond_init(buf_cond, NULL) != 0)
	{
		return 1;
	}
	return 0;
}

void init_shm (SharedM *shm)
{
	int shmkey;
	shmkey = ftok(".", 5);
	if ((shm->shmid = shmget(shmkey, sizeof(int), 0644 | IPC_CREAT)) == -1)
	{
		perror("shmget");
		exit(1);
	}
	shm->i = (int *) shmat(shm->shmid, 0, 0);
	shm->sem1 = sem_open("sem1", O_CREAT|O_EXCL, 0644, 1);
	shm->sem2 = sem_open("sem2", O_CREAT|O_EXCL, 0644, 0);
	sem_unlink("sem1");
	sem_unlink("sem2");
}

void *thread_C1(void *args)
{
	ThreadData *data;
	data = (ThreadData *) args;
	while(1)
	{
		sem_wait(data->shm->sem2);  		// ожидаем записи в shm
		pthread_mutex_lock(data->buf_mutex);	// захватываем ресурс buf
		*(data->buf) = *(data->shm->i);
		if (*(data->buf) == 100)		
			kill(getppid(), SIGUSR1);	// отправляем сигнал процессу B на завершение
		pthread_cond_signal(data->buf_cond);	// сообщаем второму потоку что запись в buf произведена
		pthread_mutex_unlock(data->buf_mutex);	//
		sem_post(data->shm->sem1);		// сообщаем процесу B о завершении чтения из shm

	}

}

void *thread_C2(void *args)
{
	ThreadData *data;
	data = (ThreadData *) args;
	time_t T;
	struct timespec t;
	while(1)
	{
		pthread_mutex_lock(data->buf_mutex);
		clock_gettime(CLOCK_REALTIME, &t);
		t.tv_sec += 1;
		if (pthread_cond_timedwait(data->buf_cond, data->buf_mutex, &t) == 0)	// ждём 1 секунду записи в buf, по завершению времени
		{									// выводим I am alive
			printf("value = %d\n",*(data->buf));
		}
		else
		{
			printf("I am alive\n");
		}
		pthread_mutex_unlock(data->buf_mutex);	
	}	

}



int main(int argc, char *argv[])
{
	signal(SIGTERM, myhandle);

	int fd[2];
	pid_t pidA;
	
	pipe(fd);
	if ((pidA = fork()) == -1)
	{
		perror("fork");
		exit(1);
	}
	if (pidA == 0)//A process
	{
		int i;
		close(fd[0]);
		while(flag)
		{
			scanf("%d",&i);
			write(fd[1], &i, sizeof(int));
		}		
		_exit(0);
	}
	else
	{
		pid_t pidC;
		SharedM shm;
		init_shm(&shm);
		if ((pidC = fork()) == -1)
		{
			perror("fork");
			exit(1);
		}
		if (pidC == 0)// C process
		{
			pthread_t C1,C2;
			static int buf;
			static pthread_mutex_t buf_mutex;
			static pthread_cond_t buf_cond;
			ThreadData data;
			if (init_data(&data, &shm, &buf, &buf_mutex, &buf_cond) != 0)
			{
				perror("pthread_cond_init");
				kill(getppid(), SIGUSR1);
				_exit(1);
			}
			if (pthread_create(&C1, NULL, &thread_C1, &data) != 0)
			{
				perror("pthread_create");
				kill(getppid(), SIGUSR1);
				_exit(1);
			}
			if (pthread_create(&C2, NULL, &thread_C2, &data) != 0)
			{
				perror("pthread_create");
				kill(getppid(), SIGUSR1);
				_exit(1);
			}
			while(flag)
			{
			}
			pthread_mutex_destroy(&buf_mutex);
			pthread_cond_destroy(&buf_cond);
			_exit(0);
			
		}
		else // B process
		{
			signal(SIGUSR1, myhandle);
			close(fd[1]);
			int n;
			while(flag)
			{	
				sem_wait(shm.sem1);//semaphore enter
				if (!flag)		//проверка условия чтобы не ждать ввода следующего числа от процесса А
					break;
				read(fd[0], &n, sizeof(int));
				n=n*n;
				*(shm.i) = n;
				sem_post(shm.sem2);//exit
				//printf("Recived string: %d \n", n);
			}
			shmdt(shm.i);
			shmctl(shm.shmid, IPC_RMID, 0);
			kill(pidA,SIGTERM);
			kill(pidC,SIGTERM);
			exit(0);
		}
		
	}
	return 0;
}
