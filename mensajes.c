// includes regulares
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h> // semaforo
#include <sys/msg.h> // cola mensajes

#include <time.h> // para imprimir el tiempo
#include <unistd.h> // para esperar
#include <string.h> // strcpy

#define MTYPE 1
#define MSIZE 128
#define PERM 0600  // permisos 

char* get_time(){
  time_t current_time = time(NULL);
  char* c_time_string = ctime(&current_time);
  return c_time_string;
}

// forma corta de llamar a semop
// se sabe que si sem_cont es positivo es un signal y en caso
// de que sea negativo es un wait
int sem_op(int sem_id, int nsem, struct sembuf operations[1], int sem_cont){
    operations[0].sem_num = 0;
    operations[0].sem_op = sem_cont;
    operations[0].sem_flg = 0;
    int retval = semop(sem_id, operations, nsem);
    return retval;
}

int main(){

  int sem_id = semget(IPC_PRIVATE, 1, PERM|IPC_CREAT|IPC_EXCL);
  if(sem_id < 0){
    printf("No pude obtener el semaforo.\n");
    perror("RAZON");
    exit(-1);
   }

   int msg_id = msgget(IPC_PRIVATE, PERM|IPC_CREAT|IPC_EXCL);
   if(msg_id < 0){
    printf("No pude obtener la cola de mensajes.\n");
    perror("RAZON");
    exit(-1);
   }else{
    printf("Inicializada la cola de mensajes %d. %s", msg_id, get_time());
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
      perror("RAZON");
   }else{
      printf("Inicializado el semaforo %d. %s\n", sem_id, get_time());
   }

   // struct para las operaciones
   struct sembuf operations[1];

   // struct para la cola de mensajes 
   struct msgbuf {
   long mtype; 
   char mtext[MSIZE];
   } msg_buffer;

   int f = fork();
   if(f == -1)
    exit(1);

   if(f == 0){
    //printf("I'm Luke\n");
    int retval;
    while(1){
      sleep(1); // 1 segundo entre cada envio para notar los cambios
      retval = sem_op(sem_id, 1, operations, 1);
      if(retval == 0){

        msg_buffer.mtype = MTYPE;
        sprintf (msg_buffer.mtext, "%s\n", "Hola Mundo! :)");
        retval = msgsnd(msg_id, &msg_buffer, sizeof(msg_buffer.mtext), 0); 
        if (retval == -1) {
          perror("ERROR");
        }

      }else{
        printf("Error al realizar el signal.\n");
        perror("RAZON");
      }
    }

  }else{
    //printf("Luke, I'm your father\n");
    int retval;
    while(1){
      retval = sem_op(sem_id, 1, operations, -1);
      if(retval == 0){

        retval = msgrcv(msg_id, &msg_buffer, sizeof(msg_buffer.mtext), MTYPE, IPC_NOWAIT);
        if(retval == -1){
          perror("ERROR");
        }else{
          printf("Recibi: %s\n", msg_buffer.mtext);
        }

      }else{
        printf("Error al realizar el wait.\n");
        perror("RAZON");
      }
    }

   }

   return 0;
}

