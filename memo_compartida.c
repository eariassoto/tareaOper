// includes regulares
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h> // semaforo
#include <sys/msg.h> // cola mensajes
#include <sys/shm.h> // memoria compartida

#include <time.h> // para imprimir el tiempo
#include <unistd.h> // para esperar
#include <string.h> // strcpy

#define MTYPE 1
#define MSIZE 128
#define PERM 0600  // permisos 

#define SEMAFORO_PRODUCTOR 0
#define SEMAFORO_CONSUMIDOR 1
#define SEMAFORO_SHM   2

char* get_time(){
  time_t current_time = time(NULL);
  char* c_time_string = ctime(&current_time);
  return c_time_string;
}

// forma corta de llamar a semop
// se sabe que si sem_cont es positivo es un signal y en caso
// de que sea negativo es un wait
int sem_op(int sem_id, int nsem, struct sembuf oper[1], int sem_cont){
    oper[0].sem_num = nsem;
    oper[0].sem_op = sem_cont;
    oper[0].sem_flg = 0;
    int retval = semop(sem_id, oper, 1);
    return retval;
}

char *rand_string(size_t l) {
  char *randomString = NULL;
  if (l) {
    randomString = malloc(sizeof(char) * (l+1));
    if (randomString) {
      int n = 0;
      for (n = 0;n < l;n++) {
        randomString[n] = '0' + rand()%72;
      }
      randomString[l] = '\0';
    }
  }
  return randomString;
}

int main(){
  printf("En este ejemplo un proceso va a poner en memoria una hilera de caracteres aleatorios. Se usan tres semaforos, uno para el area de memoria compartida y dos para que ambos procesos se avisen cuando se terminan de interactuar con la operacion de produccion/consumo.\n\n");

  // inicializacíon de los semaforos
  int sem_id = semget(IPC_PRIVATE, 3, PERM|IPC_CREAT|IPC_EXCL);
  if(sem_id < 0){
    printf("No pude obtener el semaforo.\n");
    perror("RAZON");
    exit(-1);
   }

   union semun {
    int val;
    struct semid_ds *buf;
    ushort * array;
   } argument;

   argument.val = 0;  

   if(semctl(sem_id, SEMAFORO_PRODUCTOR, SETVAL, argument) < 0){
      printf("Error al inicializar el contador del semaforo del productor.\n");
      perror("RAZON");
      exit(-1);
   }else{
      printf("Inicializado el semaforo del productor. %s", get_time());
   }

   if(semctl(sem_id, SEMAFORO_CONSUMIDOR, SETVAL, argument) < 0){
      printf("Error al inicializar el contador del semaforo del consumidor.\n");
      perror("RAZON");
      exit(-1);
   }else{
      printf("Inicializado el semaforo del consumidor. %s", get_time());
   }

   argument.val = 1; // le doy permiso para que el hijo ponga el primer texto
   if(semctl(sem_id, SEMAFORO_SHM, SETVAL, argument) < 0){
      printf("Error al inicializar el contador del semaforo de memoria.\n");
      perror("RAZON");
      exit(-1);
   }else{
      printf("Inicializado el semaforo de memoria compartida. %s", get_time());
   }

   // inicializacion de la memoria compartida
   int shm_id = shmget(IPC_PRIVATE, 512, PERM|IPC_CREAT|IPC_EXCL);
   if(shm_id < 0){
    printf("No pude obtener la memoria compartida.\n");
    perror("RAZON");
    exit(-1);
   }else{
    printf("Inicializada la memoria compartida %d. %s\n", shm_id, get_time());
   }

   // struct para las operaciones
   struct sembuf oper_productor[1], oper_shm[1], oper_consumidor[1];

   int f = fork();
   if(f == -1)
    exit(1);

   if(f == 0){
    //printf("I'm Luke\n");
    int retval;
    char *shared_memory =(char*)shmat(shm_id,0,0);
    if(shared_memory == (void*)-1){
      printf("Error al conectarse a la memoria compartida.\n");
      perror("RAZON");
      exit(-1);
    }

    while(1){
      sleep(1); // 1 segundo entre cada envio para notar los cambios

      retval = sem_op(sem_id, SEMAFORO_SHM, oper_shm, -1); // wait shm
      //zona critica
      sprintf (shared_memory, "%s\n", rand_string(12));
      retval = sem_op(sem_id, SEMAFORO_SHM, oper_shm, 1); // signal shm
      
      retval = sem_op(sem_id, SEMAFORO_PRODUCTOR, oper_productor, 1); // signal productor
      if(retval < 0){
        printf("Error al realizar el signal de productor.\n");
        perror("RAZON");
        exit(-1); // hago esto por miedo a inanicion
      }else{
        retval = sem_op(sem_id, SEMAFORO_CONSUMIDOR, oper_consumidor, -1); // wait consumidor
        if(retval < 0){
          printf("Error al realizar el wait de consumidor.\n");
          perror("RAZON");
          exit(-1); // hago esto por miedo a inanicion
        }
      }
    }

  }else{
    //printf("Luke, I'm your father\n");
    int retval;
    char *shared_memory =(char*)shmat(shm_id,0,0);
    if(shared_memory == (void*)-1){
      printf("Error al conectarse a la memoria compartida.\n");
      perror("RAZON");
      exit(-1);
    }

    while(1){
      retval = sem_op(sem_id, SEMAFORO_PRODUCTOR, oper_productor, -1); // wait productor
      if(retval == 0){

        retval = sem_op(sem_id, SEMAFORO_SHM, oper_shm, -1); // wait shm
        //zona critica
        printf ("En la memoria compartida hay: %s\n", shared_memory);
        retval = sem_op(sem_id, SEMAFORO_SHM, oper_shm, 1); // signal shm

        retval = sem_op(sem_id, SEMAFORO_CONSUMIDOR, oper_consumidor, 1); // signal consumidor
        if(retval < 0){
          printf("Error al realizar el signal de consumidor.\n");
          perror("RAZON");
          exit(-1); // hago esto por miedo a inanicion
        }
      }else{
        printf("Error al realizar el wait del productor.\n");
        perror("RAZON");
        exit(-1); // hago esto por miedo a inanicion
      }
    }

   }
   return 0;
}

