// includes regulares
#include <stdio.h>
#include <stdlib.h>

// includes de semaforo
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <time.h> // para imprimir el tiempo
#include <unistd.h> // para esperar

#define PERM 0600  // permisos 

char* get_time(){
  time_t current_time = time(NULL);
  char* c_time_string = ctime(&current_time);
  return c_time_string;
}

int main(){

	int sem_id = semget(IPC_PRIVATE, 1, PERM|IPC_CREAT|IPC_EXCL);

	if(sem_id < 0){
		printf("No pude obtener el semaforo.\n");
   	exit(-1);
   }

   union semun {
   	int val;
   	struct semid_ds *buf;
   	ushort * array;
   } argument;

   // inicializo el contador del semaforo en 0
   argument.val = 0;  

   if(semctl(sem_id, 0, SETVAL, argument) < 0){
      printf("Error al inicializar el contador del semaforo.\n");
   }else{
      printf("Semaforo %d inicializado. %s\n", sem_id, get_time());
   }

   // struct para las operaciones
   struct sembuf operations[1];

   int f = fork();
   if(f == -1)
   	exit(1);

   if(f == 0){
   	//printf("I'm Luke\n");
    sleep(2); // lo pongo a dormir para notar el bloqueo del wait
   	printf("El proceso hijo da signal al semaforo. %s\n", get_time());

   	operations[0].sem_num = 0;
    operations[0].sem_op = 1;
    operations[0].sem_flg = 0;
    int retval = semop(sem_id, operations, 1);

    if(retval == 0){
    	printf("Termine de dar la señal signal. %s\n", get_time());
    }else{
    	printf("Error al realizar el signal.\n");
    	perror("RAZON");
    }

   }else{
   	//printf("Luke, I'm your father\n");
   	printf("El proceso da wait al semaforo. %s\n", get_time());

   	operations[0].sem_num = 0;
    operations[0].sem_op = -1;
    operations[0].sem_flg = 0;
    int retval = semop(sem_id, operations, 1);
    if(retval == 0){
      printf("El tiempo de wait termino. %s\n", get_time());
      printf("Aqui el padre ya puede hacer las operaciones en la region critica\n\n");
    }else{
    	printf("Error al realizar el wait.\n");
    	perror("RAZON");
    }

    retval = semctl(sem_id, 0, IPC_RMID);
    if(retval == 0){
      printf("Semaforo %d destruido.\n", sem_id);
    }else{
      printf("Error al destruir semaforo.\n");
      perror("RAZON");
    }

   }

   return 0;
}

