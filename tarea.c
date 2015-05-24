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

#define MTYPE 1   // id del los mensajes
#define MSIZE 512 // longitud de la hilera del mensaje
#define PERM 0600  // permisos 

// constantes para identificar los semaforos
#define SEMAFORO_MANDAR_IMPRIMIR 0
#define SEMAFORO_IMPRESORA_LIBRE 1
#define SEMAFORO_MUTEX_SHM       2
#define SEMAFORO_CONTADOR_PROCES 3

// duracion maxima del programa
#define MAX_SEGUNDOS 5

/*
* Este metodo simplemente devuelve la hora actual, esto
* para controlar el tiempo en que se lleva a cabo una 
* accion en algun proceso
*/
char time_buffer [80];
char* get_time(){
	time_t rawtime;
	struct tm * timeinfo;	
	time (&rawtime);
	timeinfo = localtime (&rawtime);
	strftime (time_buffer, sizeof(time_buffer),"%I:%M:%S%p",timeinfo);
	return &time_buffer[0];
}

/*
* forma corta de llamar a semop
* se sabe que si sem_cont es positivo es un signal y en caso
* de que sea negativo es un wait
*/
int sem_op(int sem_id, int nsem, struct sembuf oper[1], int sem_cont){
    oper[0].sem_num = nsem;
    oper[0].sem_op = sem_cont;
    oper[0].sem_flg = 0;
    int retval = semop(sem_id, oper, 1);
    return retval;
}

/*
* genera una hilera de caracteres aleatorios
*/
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

/*
* loop principal: en este main primero se va a inicializar
* las estructuras que necesitamos (semaforos, colas, shm etc)
* que van a necesitar los procesos.
* 
* Luego va a ocurrir una bifurcacion principal: un proceso va a ser
* el generador de consultas. Este va a llevar el contador maximo de tiempo
* de ejecucion. El otro proceso va a ser la impresora.

* El procesos de consultas va a generar hileras de consultas y para cada
* una de ellas va a generar un nuevo proceso parser (el fork() interno que esta
* adentro se ocupa de esto). Este proceso parser va a tomar el mensaje de la cola
* de mensajes, lo va a reinterpretar y lo va a mandar a impresion por medio de
* semaforos y memoria compartida.
* 
* Cuando al proceso generador de consultas se le acabe el tiempo este va a indicarle
* a la impresora que espere a que los procesos parsers acaben y luego cierre los recursos
* que hemos abierto (sem, colas, shm) y con esto termina la ejecucion del programa
*/
void main(){
	
	// inicializacion de los semaforos
	int sem_id = semget(IPC_PRIVATE, 4, PERM|IPC_CREAT|IPC_EXCL);
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

	/*
	El semaforo de nueva impresion empieza en cero porque al menos un parser
	debe "despertar la impresora" por medio de un signal.

	El semaforo contador de procesos inicia el cero por razones obvias.
	*/
	argument.val = 0;  
	
	if(semctl(sem_id, SEMAFORO_MANDAR_IMPRIMIR, SETVAL, argument) < 0){
		printf("Error al inicializar el semaforo para nueva impresion.\n");
		perror("RAZON");
		exit(-1);
	}

	if(semctl(sem_id, SEMAFORO_CONTADOR_PROCES, SETVAL, argument) < 0){
		printf("Error al inicializar el semaforo que cuenta procesor.\n");
		perror("RAZON");
		exit(-1);
	}

	/*
	El semaforo de impresora libre empieza con 1 recurso para que el primer
	proceso parser inicie sus labores.

	El semaforo de memoria compartido igual inicia con 1 recurso por el mismo
	argumento anterior.
	*/
	argument.val = 1; 

	if(semctl(sem_id, SEMAFORO_IMPRESORA_LIBRE, SETVAL, argument) < 0){
		printf("Error al inicializar el semaforo de impresora libre.\n");
		perror("RAZON");
		exit(-1);
	}

	if(semctl(sem_id, SEMAFORO_MUTEX_SHM, SETVAL, argument) < 0){
		printf("Error al inicializar el semaforo de la memoria compartida.\n");
		perror("RAZON");
		exit(-1);
	}

	else{
		printf("%s Iniciados los semaforos.\n", get_time());
	}

	/*
	La primera cola de mensajes funciona para colocar la consulta que
	le voy a dar al nuevo proceso parser. 

	La segunda solo sirve pa indicarle al proceso impresor que ya no van
	a llegar nuevos parsers y que espere a que los parsers mueran para
	liberar los recursos.
	*/
	int msg_id = msgget(IPC_PRIVATE, PERM|IPC_CREAT|IPC_EXCL);
	if(msg_id < 0){
		printf("No pude obtener la cola de mensajes.\n");
		perror("RAZON");
		exit(-1);
	}else{
		printf("%s Iniciada la cola de mensajes %d.\n", get_time(), msg_id);
	}

	int msg_fin_id = msgget(IPC_PRIVATE, PERM|IPC_CREAT|IPC_EXCL);
	if(msg_fin_id < 0){
		printf("No pude obtener la cola de mensajes de fin de ejecucion.\n");
		perror("RAZON");
		exit(-1);
	}else{
		printf("%s Iniciada la cola de mensajes de fin de ejecucion %d.\n", get_time(), msg_fin_id);
	}

	// inicializacion de la memoria compartida
	int shm_id = shmget(IPC_PRIVATE, 512, PERM|IPC_CREAT|IPC_EXCL);
	if(shm_id < 0){
		printf("No pude obtener la memoria compartida.\n");
		perror("RAZON");
		exit(-1);
	}else{
		printf("%s Iniciada la memoria compartida %d.\n\n", get_time(), shm_id);
	}

	// struct para la cola de mensajes
	struct msgbuf {
		long mtype; 
		char mtext[MSIZE];
	} msg_buffer;

	// struct para las operaciones en los semaforos.
	struct sembuf oper_mandar[1], oper_shm[1], oper_contador[1], oper_libre[1];

	int bifurcacionPrimaria = fork();
	if(bifurcacionPrimaria){
		// proceso padre
		// este es el generador de consultas

		// tiempo maximo de ejecucion, una vez que termine
		// envio un mensaje al proceso impresor
		time_t tiempoInicial = time(0);
		long int limite = tiempoInicial + MAX_SEGUNDOS;

		// retval simplemente lo voy a usar para guardar las
		// respuestas de las operaciones que haga
		int retval;

		while(time(0) < limite){
			// tiempo de espera entre consulta y consulta
			sleep(1);

			// mae daniel aqui en vez de generar una hilera random
			// tenemos que generar una conulta aleatoria, como la de ejemplo
			// en la tarea, la longitud no importa xq tiene de maximo 512 bytes
			// ejm
			// SELECT x FROM y, z WHERE condicion
			// x, y, z, condicion podrian bien ser hileras aleatorias para no jodernos la vida

			msg_buffer.mtype = MTYPE;
			sprintf (msg_buffer.mtext, "%s\n", rand_string(10));
			retval = msgsnd(msg_id, &msg_buffer, sizeof(msg_buffer.mtext), 0); 

			if (retval == -1) {
				printf("Error al enviar el mensaje a la cola\n");
				perror("ERROR");
				exit(-1);
			}

			int f = fork();
			if(f == 0){
				// tipo procesos 2: parsers de mensajes

				// aqui hago un signal para contar un nuevo proceso parser
				retval = sem_op(sem_id, SEMAFORO_CONTADOR_PROCES, oper_contador, 1);

				char *shared_memory =(char*)shmat(shm_id,0,0);
				if(shared_memory == (void*)-1){
					printf("Error al conectarse a la memoria compartida.\n");
					perror("RAZON");
					exit(-1);
				}

				// leo el mensaje en el tope de la cola que fue el que mi
				// proceso padre me dejo
				retval = msgrcv(msg_id, &msg_buffer, sizeof(msg_buffer.mtext), MTYPE, IPC_NOWAIT);
				if(retval == -1){
					printf("Error al leer de la cola de mensajes.\n");
					perror("ERROR");
					exit(-1);
				}else{

					// hago una wait a ver si la impresora esta libre
					// pd. el semaforo comienza con 1 permiso porque 
					// al inicio nadie la esta usando
					// con esto nos aseguramos que el uso de la impresora
					// sea exclusivo
					retval = sem_op(sem_id, SEMAFORO_IMPRESORA_LIBRE, oper_libre, -1); 

					retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, -1); // wait shm
					//zona critica

					// mae daniel aqui habria que modificar msg_buffer.mtext
					// antes de colocarlo en la memo compartida para hacer la vara esa
					// de parsearlo osea que quede 
					// "EXTRAER de las tablas y,z las columnas x que cumplan con condición"
					// el sprinf es la forma de copiar hileras en la memo compartida

					sprintf (shared_memory, "%s", msg_buffer.mtext);
					retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, 1); // signal shm

					// le doy un signal al semaforo de mandar a imprimir para que el
					// proceso que imprime imprima
					retval = sem_op(sem_id, SEMAFORO_MANDAR_IMPRIMIR, oper_mandar, 1); 

					// antes de salir, le resto uno al contador de procesos
					retval = sem_op(sem_id, SEMAFORO_CONTADOR_PROCES, oper_contador, -1);

					exit(0);
					// aqui muere el proceso parser
					// rip
				}
			}
		} //fin while

		// entonces cuando salga del while fue porque ya se me acabo el tiempo
		// de ejecucion. Envio un mensaje a la cola de fin para que el impresor
		// sepa que es momento de finalizar y cerrar todo
		msg_buffer.mtype = MTYPE;
		sprintf (msg_buffer.mtext, "%s", "fin");
		retval = msgsnd(msg_fin_id, &msg_buffer, sizeof(msg_buffer.mtext), 0);
		if (retval == -1) {
			printf("Error al enviar el mensaje a la cola\n");
			perror("ERROR");
			exit(-1);
		}else
			exit(0);
			// aqui muere el proceso generador de consultas
			// rip
	
	}else{
		// proceso que imprime la memo compartida

		int retval, finEjecucion = 0;

		char *shared_memory =(char*)shmat(shm_id,0,0);
		if(shared_memory == (void*)-1){
			printf("Error al conectarse a la memoria compartida.\n");
			perror("RAZON");
			exit(-1);
		}

		while(!finEjecucion){

			// hago un wait hasta que tenga un solicitud nueva de impresion
			retval = sem_op(sem_id, SEMAFORO_MANDAR_IMPRIMIR, oper_mandar, -1);

			retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, -1); // wait shm
	        //zona critica
	        printf ("Me llega: %s\n", shared_memory);
	        retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, 1); // signal shm

	        // aviso que la impresora esta libre para que otro proceso
	        // haga uso de esta y de la memoria compartida de forma segura
			retval = sem_op(sem_id, SEMAFORO_IMPRESORA_LIBRE, oper_libre, 1); 

			// pregunto si hay un mensaje de terminar la ejecucion
			retval = msgrcv(msg_fin_id, &msg_buffer, sizeof(msg_buffer.mtext), MTYPE, IPC_NOWAIT);
			if(retval != -1){

				// si entro aqui es porque recibi el mensaje de finalizacion
				finEjecucion = 1;

				// espero a que hayan cero procesos esperando a imprimir
				// segun la documentacion un wait(0) hacer que el proceso se
				// duerma hasta que el contador del semaforo este en cero
				retval = sem_op(sem_id, SEMAFORO_CONTADOR_PROCES, oper_contador, 0);

				// salgo (debo cerrar cosas aqui)
				retval = semctl(sem_id, 0, IPC_RMID);
				if(retval != -1){
					printf("%s Liberados los semaforos.\n", get_time());
				}

				retval = shmctl(shm_id, 0, IPC_RMID);
				if(retval != -1){
					printf("%s Liberada la memoria compartida.\n", get_time());
				}

				retval = msgctl(msg_id, 0, IPC_RMID);
				if(retval != -1){
					printf("%s Liberada la cola de mensajes de comandos.\n", get_time());
				}

				retval = msgctl(msg_fin_id, 0, IPC_RMID);
				if(retval != -1){
					printf("%s Liberada la cola de mensajes de finalizacion.\n", get_time());
				}
				printf("gg wp\n");
			}

		}		
		exit(0);
		// muere la impresora
		// rip
	}
}