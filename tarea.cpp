#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <random>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h> // semaforo
#include <sys/msg.h> // cola mensajes
#include <sys/shm.h> // memoria compartida

#include <time.h> // para imprimir el tiempo
#include <unistd.h> // para esperar

#define MTYPE 1   // id del los mensajes
#define MSIZE 512 // longitud de la hilera del mensaje
#define PERM  0600  // permisos

// constantes para identificar los semaforos
#define SEMAFORO_MANDAR_IMPRIMIR 0
#define SEMAFORO_IMPRESORA_LIBRE 1
#define SEMAFORO_MUTEX_IMPRESORA 2
#define SEMAFORO_MUTEX_SHM       3
#define SEMAFORO_CONTADOR_PROCES 4

// duracion maxima del programa
#define MAX_SEGUNDOS 4
#define DORMIR 50
	
using namespace std;


/*
* Genera un numero aleatorio n y devuelve
* la enesima posicion en el vector de palabras
* enviado por parametro 
*/
string random_word(vector<string> v){
	random_device rd;
    mt19937 mt(rd());
    uniform_real_distribution<double> dist(0, v.size());

	return string(v.at( dist(mt) ));
}

/*
* Arma una consulta usando palabras aleatorias de una
* piscina de palabras previamente cargadas
*/
string generar_consulta(vector<string> x, vector<string> y, vector<string> z){
	
	string select("SELECT ");
	string from(" FROM ");
	string where(" WHERE ");
	string res = select + random_word(x) + from + random_word(y) + where + random_word(z);
	return res;
}

/*
* Reconoce los componentes de la consulta y los ama
* en otra hilera que se le va a mostrar al usuario
*/
string convertir_consulta(char c[MSIZE]){
	string consulta(c);

	string where(" WHERE ");
	string from(" FROM ");
	size_t pos_tabla = consulta.find(from);
	size_t pos_cond = consulta.find(where);
	
	string condicion(consulta, pos_cond+7);
	string columna(consulta, 7, pos_tabla-7);
	string tabla(consulta, pos_tabla+6, pos_cond-(pos_tabla+6));
	
	string res = string("Extraer de las tablas ")+tabla+", las columnas "+columna+" que cumplan con la condicion "+condicion;
	return res;
}

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
int sem_op(int sem_id, int nsem, struct sembuf oper[1], int sem_cont, int sflag=0){
    oper[0].sem_num = nsem;
    oper[0].sem_op = sem_cont;
    oper[0].sem_flg = sflag;
    int retval = semop(sem_id, oper, 1);
    return retval;
}

/*
* loop principal: en este main primero se va a inicializar
* las estructuras que necesitamos (semaforos, colas, shm etc)
* que van a necesitar los procesos.
*
* Luego va a ocurrir una bifurcacion principal: un proceso va a ser
* el generador de consultas. Este va a llevar el contador maximo de tiempo
* de ejecucion. El otro proceso va a ser la impresora.

* El proceso de consultas va a generar hileras de consultas y para cada
* una de ellas va a generar un nuevo proceso parser (el fork() interno que esta
* adentro se ocupa de esto). Este proceso parser va a tomar el mensaje de la cola
* de mensajes, lo va a reinterpretar y lo va a mandar a impresion por medio de
* semaforos y memoria compartida.
*
* Cuando al proceso generador de consultas se le acabe el tiempo este va esperar a
* que todos los procesos hijos hayan acabado de ejecutar las impresiones y luego
* procede a darle la señal a la impresora para que termine, luego libera todos los recursos
*/
int main(){

	ifstream archivox("columnas.txt");
	ifstream archivoy("tablas.txt");
	ifstream archivoz("condiciones.txt");
	string line;
	vector<string> palabrasx;
	vector<string> palabrasy;
	vector<string> palabrasz;

	// leo de archivos las palabras que se usaran
	// aleatoriamente
	while (getline(archivox, line))
		palabrasx.push_back(line);

	while (getline(archivoy, line))
		palabrasy.push_back(line);
	
	while (getline(archivoz, line))
		palabrasz.push_back(line);

	// inicializacion de los semaforos
	int sem_id = semget(IPC_PRIVATE, 5, PERM|IPC_CREAT|IPC_EXCL);
	if(sem_id < 0){
		cout <<  "No pude obtener los semaforos." << endl;
		perror("RAZON");
		return -1;
	}

	union semun {
		int val;
		struct semid_ds *buf;
		ushort * array;
	} argument;

	/*
	El semaforo de nueva impresion empieza en cero porque al menos un parser
	debe "despertar la impresora" por medio de un signal.

	El semaforo contador de procesos inicia en cero por razones obvias.

	El semaforo de impresora libre empieza en cero porque luego de la primera
	impresion la misma se libera
	*/
	argument.val = 0;

	if(semctl(sem_id, SEMAFORO_MANDAR_IMPRIMIR, SETVAL, argument) < 0){
		cout << "Error al inicializar el semaforo para nueva impresion." << endl;
		perror("RAZON");
		return -1;
	}

	if(semctl(sem_id, SEMAFORO_CONTADOR_PROCES, SETVAL, argument) < 0){
		cout << "Error al inicializar el semaforo que cuenta procesos." << endl;
		perror("RAZON");
		return -1;
	}

	if(semctl(sem_id, SEMAFORO_IMPRESORA_LIBRE, SETVAL, argument) < 0){
		cout << "Error al inicializar el semaforo de impresora libre." << endl;
		perror("RAZON");
		return -1;
	}

	/*
	El semaforo de impresora libre empieza con 1 recurso para que el primer
	proceso parser inicie sus labores.

	El semaforo de memoria compartido igual inicia con 1 recurso por el mismo
	argumento anterior.
	*/
	argument.val = 1;

	if(semctl(sem_id, SEMAFORO_MUTEX_IMPRESORA, SETVAL, argument) < 0){
		cout << "Error al inicializar el semaforo mutex impresora." << endl;
		perror("RAZON");
		return -1;
	}

	if(semctl(sem_id, SEMAFORO_MUTEX_SHM, SETVAL, argument) < 0){
		cout << "Error al inicializar el semaforo mutex memoria compartida." << endl;
		perror("RAZON");
		return -1;
	}

	else{
		cout << get_time() << " Iniciados los semaforos." << endl;
	}

	/*

	La primera cola de mensajes funciona para colocar la consulta que
	le voy a dar al nuevo proceso parser.

	La segunda solo sirve para indicarle al proceso impresor que ya no van
	a llegar nuevos parsers y que espere a que los parsers mueran para
	liberar los recursos.

	*/
	int msg_id = msgget(IPC_PRIVATE, PERM|IPC_CREAT|IPC_EXCL);
	if(msg_id < 0){
		cout << "No pude obtener la cola de mensajes." << endl;
		perror("RAZON");
		return -1;
	}else{
		cout << get_time() << " Iniciada la cola de mensajes " << msg_id << "." << endl;
	}

	int msg_fin_id = msgget(IPC_PRIVATE, PERM|IPC_CREAT|IPC_EXCL);
	if(msg_fin_id < 0){
		cout << "No pude obtener la cola de mensajes de fin de ejecucion." << endl;
		perror("RAZON");
		return -1;
	}else{
		cout << get_time() << " Iniciada la cola de mensajes de fin de ejecucion" << msg_fin_id << "." << endl;
	}

	// inicializacion de la memoria compartida
	int shm_id = shmget(IPC_PRIVATE, 512, PERM|IPC_CREAT|IPC_EXCL);
	if(shm_id < 0){
		cout << "No pude obtener la memoria compartida." << endl;
		perror("RAZON");
		return -1;
	}else{
		cout << get_time() << " Iniciada la memoria compartida " << shm_id << "." << endl << endl;
	}

	// struct para la cola de mensajes
	struct msgbuf {
		long mtype;
		char mtext[MSIZE] = {};
	} msg_buffer;

	// struct para las operaciones en los semaforos.
	struct sembuf oper_mandar[1], oper_shm[1], oper_contador[1], oper_libre[1],oper_impre[1];

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
			// Proceso generador de consultas

			// tiempo de espera entre consulta y consulta
			this_thread::sleep_for(chrono::milliseconds(DORMIR));

			string nuevaConsulta = generar_consulta(palabrasx, palabrasy, palabrasz);
			
			// copio el string al arreglo de caracteres del struct
			nuevaConsulta.copy( msg_buffer.mtext, nuevaConsulta.size() );
			
			// envio el mensaje para el hijo
			msg_buffer.mtype = MTYPE;
			retval = msgsnd(msg_id, &msg_buffer, sizeof(msg_buffer.mtext), 0);
			if (retval == -1) {
				cout << "Error al enviar el mensaje a la cola" << endl;
				perror("ERROR");
				return -1;
			}

			// aqui hago un signal para contar un nuevo proceso parser
			retval = sem_op(sem_id, SEMAFORO_CONTADOR_PROCES, oper_contador, 1);

			int f = fork();
			if(f == 0){

				// proceso parsers de mensajes

				// se conecta a la memoria compartida
				char *shared_memory =(char*)shmat(shm_id,0,0);
				if(shared_memory == (void*)-1){
					cout << "Error al conectarse a la memoria compartida (parser)." << endl;
					perror("RAZON");
					return -1;
				}

				// leo el mensaje en el tope de la cola que fue el que mi
				// proceso padre me dejo
				retval = msgrcv(msg_id, &msg_buffer, sizeof(msg_buffer.mtext), MTYPE, IPC_NOWAIT);
				if(retval == -1){
					cout << "Error al leer de la cola de mensajes." << endl;
					perror("ERROR");
					return -1;
				}else{

					// convierte la consulta
					string consulta = convertir_consulta(msg_buffer.mtext);

					// primero espero a la exclusividad de los procesos
					// de la impresora y luego ya puedo interactuar con ella
					retval = sem_op(sem_id, SEMAFORO_MUTEX_IMPRESORA, oper_impre, -1);
					
					retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, -1); // wait shm
					//zona critica
					consulta.copy(shared_memory, consulta.size());
					retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, 1); // signal shm

					// le doy un signal al semaforo de mandar a imprimir para que el
					// proceso que imprime imprima
					retval = sem_op(sem_id, SEMAFORO_MANDAR_IMPRIMIR, oper_mandar, 1);
					retval = sem_op(sem_id, SEMAFORO_IMPRESORA_LIBRE, oper_mandar, -1);

					// libero la exclusividad de la impresora
					retval = sem_op(sem_id, SEMAFORO_MUTEX_IMPRESORA, oper_impre, 1);

					// antes de irme le resto uno al contador de procesos
					retval = sem_op(sem_id, SEMAFORO_CONTADOR_PROCES, oper_contador, -1);

					return 0;
					// aqui muere el proceso parser
					// rip
				}
			}
		} //fin while

		// espero a que todos los procesos parser mueran
		retval = sem_op(sem_id, SEMAFORO_CONTADOR_PROCES, oper_contador, 0);

		// cuando no hayan mas procesos mato a la impresora
		msg_buffer.mtype = MTYPE;
		sprintf (msg_buffer.mtext, "%s", "fin");
		retval = msgsnd(msg_fin_id, &msg_buffer, sizeof(msg_buffer.mtext), 0);
		
		retval = sem_op(sem_id, SEMAFORO_IMPRESORA_LIBRE, oper_libre, -1);		
		
		// libero recursos
		retval = semctl(sem_id, 0, IPC_RMID);
		if(retval != -1){
			cout << get_time() << " Liberados los semaforos." << endl;
		}
		
		retval = msgctl(msg_id, 0, IPC_RMID);
		if(retval != -1){
			cout << get_time() << " Liberada la cola de mensajes de comandos." << endl;
		}

		retval = msgctl(msg_fin_id, 0, IPC_RMID);
		if(retval != -1){
			cout << get_time() << " Liberada la cola de mensajes de finalizacion." << endl;
		}

		retval = shmctl(shm_id, 0, IPC_RMID);
		if(retval != -1){
			cout << get_time() << " Liberada la memoria compartida." << endl;
		}

		cout << get_time() <<  " Termina ejecucion proceso generador de consultas." << endl;
		cout << "gg wp" << endl;
		return 0;
		// aqui muere el proceso generador de consultas
		// rip
	}else{
		// proceso que imprime la memo compartida

		int retval;
		bool terminar = false;

		char *shared_memory =(char*)shmat(shm_id,0,0);
		if(shared_memory == (void*)-1){
			cout << "Error al conectarse a la memoria compartida." << endl;
			perror("RAZON");
			return -1;
		}

		while(!terminar){

			// hago un wait hasta que tenga un solicitud nueva de impresion
			retval = sem_op(sem_id, SEMAFORO_MANDAR_IMPRIMIR, oper_mandar, -1, IPC_NOWAIT);
			if(retval != -1){
				retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, -1); // wait shm
		        //zona critica
		        cout << shared_memory << endl << endl;
		        shared_memory = "";
		        retval = sem_op(sem_id, SEMAFORO_MUTEX_SHM, oper_shm, 1); // signal shm

		        // aviso que la impresora esta libre para que otro proceso
		        // haga uso de esta y de la memoria compartida de forma segura
				retval = sem_op(sem_id, SEMAFORO_IMPRESORA_LIBRE, oper_libre, 1);
			}

			retval = msgrcv(msg_fin_id, &msg_buffer, sizeof(msg_buffer.mtext), MTYPE, IPC_NOWAIT);
			if(retval != -1){
				terminar = true;
				cout << get_time() << " Termina el proceso impresora." << endl;
				retval = sem_op(sem_id, SEMAFORO_IMPRESORA_LIBRE, oper_libre, 1);
				return 0;
			}
		}
	}
}
