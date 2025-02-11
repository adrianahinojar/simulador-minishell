#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include "parser.h"

// Definimos una estructura para almacenar los trabajos
typedef struct Job {
    pid_t *pid;            // Array de pids con los pids de la linea de comandos &
    int num_pids;           // numero de pids dentro del array de pids
    int job_id;             // ID del trabajo (número)
    int background;       // 1 si está en segundo plano, 0 si está en primer plano
    int status;           // Estado del proceso: 0 - en ejecución, 1 - terminado
    char *command;          // Comando que se ejecutó 
} job_t;

job_t *jobs = NULL;        // Array dinámico de trabajos
int num_jobs = 0;        // Número de trabajos en segundo plano


// Función para agregar un trabajo en segundo plano a la lista de trabajos
void agregar_job_background(pid_t *pid, int num_pids, char *comando) {
    job_t *temp = realloc(jobs, sizeof(job_t) * (num_jobs + 1));
    if (temp == NULL) {
        perror("Error al agregar trabajo en segundo plano");
        free(jobs); //// Liberar memoria previamente asignada en caso de error
        exit(1);
    }
    jobs = temp;  // Asigna el nuevo puntero


    jobs[num_jobs].num_pids = num_pids; // Asignar el número de PIDs

    // Guardar la información del trabajo en el array
    jobs[num_jobs].pid = pid;
    jobs[num_jobs].job_id = num_jobs + 1;  // ID del trabajo, empezamos desde 1
    jobs[num_jobs].background = 1;  // Marcar como trabajo en segundo plano
    jobs[num_jobs].status = 0;  // Inicialmente, está en ejecución
    jobs[num_jobs].command = strdup(comando);  // Almacenar el comando que se ejecutó, strdup rea una nueva cadena de caracteres que tiene el mismo contenido que la original usando memoria dinamica (malloc)
    num_jobs++;  // Incrementar el contador de trabajos
}



void mostrar_jobs() {
    if (num_jobs == 0) {
        printf("No hay trabajos en segundo plano.\n");
        return;
    }


    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].background == 1) {  // Solo mostrar trabajos en segundo plano
            if (jobs[i].status == 0) {
                printf("[%d]+  Running    %s \n", i+1, jobs[i].command);
            }//los trabajos que hayan terminado se borran del array de trabajo en el actualizar_estado_trabajo() y no se imprimiran
        }
    }
}

job_t* encontrar_trabajo(int job_id) { //para encontrar un trabajo en background y pasarlo a foreground
    int i = 0;
    while (i < num_jobs) {
        if (jobs[i].job_id == job_id) {
            return &jobs[i]; // Devuelve un puntero al trabajo encontrado
        }
        i++;
    }
    return NULL; // Devuelve NULL si no se encuentra el trabajo
}

void fg(tline *linea) {
    int job_id;  // ID del trabajo a traer al primer plano
    job_t *job;
    

    // Si se pasa un número de trabajo, intentar usarlo
    if (linea->ncommands > 1 && linea->commands[0].argc > 1) {
        job_id = atoi(linea->commands[0].argv[1]);
    } else {
        // Si no se pasa ningún ID, traer el último trabajo en segundo plano
        if (num_jobs > 0) {
            job_id = jobs[num_jobs - 1].job_id;
        } else {
            fprintf(stderr, "No hay trabajos en segundo plano.\n");
            return;
        }
    }

    // Buscar el trabajo en el array de trabajos
        job = encontrar_trabajo(job_id);

    // Si no se encuentra el trabajo
    if (job == NULL) {
        printf("Trabajo con PID %d no encontrado.\n", job_id);
        return;
    }
        // Si el proceso está en segundo plano y ha terminado, mostrar un error
        if (job->status == 1) {
            fprintf(stderr, "El proceso con PID %d ya ha terminado.\n", job->pid[linea->ncommands-1]);
            return;
        }

        // Esperar a todos los procesos asociados al trabajo
        printf("%s\n", job->command);
        for (int i = 0; i < job->num_pids; i++) {
            if (waitpid(job->pid[i], NULL, 0) < 0) {
                perror("Error al esperar por el proceso");
            }
        }
        
        job->background = 0;  // Marcar el trabajo como no en segundo plano

        // Eliminar trabajos que han terminado
    
        free(job->command);  // Liberar la memoria dinamica del comando (porque se uso strup)
        free(jobs->pid); //liberar memoria dinamica del array de pids
        
        // Si el trabajo está terminado, eliminarlo de la lista
        for (int j = job_id; j < num_jobs - 1; j++) {
            jobs[j] = jobs[j + 1];  // Desplazar los trabajos hacia la izquierda
        }
        num_jobs--;  // Reducir el número de trabajos
        
        return;
}
    

void ejecutar_comandos(tline *linea, char *buf) {
    int i;
    int pipefd[2];
    pid_t pid;
    int prev_pipe_fd = -1;  // Variable para almacenar el pipe anterior
    pid_t *pid_todos=NULL;

    // Reservar memoria para los PIDs solo si la línea está en background
    if (linea->background) {
        pid_todos = malloc(sizeof(pid_t) * linea->ncommands);
        if (pid_todos == NULL) {
            perror("Error al asignar memoria para pid_todos");
            exit(1);
        }
    }
    
    for (i = 0; i < linea->ncommands; i++) {
        if (i < linea->ncommands - 1) {//el ultimo comando no tiene ningun pipe
            // Crear un pipe para el siguiente comando
            if (pipe(pipefd) == -1) {
                perror("Error al crear el pipe");
                exit(1);
            }
        }

        pid = fork();
        if (pid < 0) {
            perror("Error al crear el proceso hijo");
            exit(1);
        } else if (pid == 0) { // Proceso hijo

            if(linea->background){//PARA QUE LOS PROCESOS EN EL BACKGROUND NO MUERAN AL HACER CNTROL + C
                signal(SIGINT, SIG_IGN);//si el proceso esta en background ignore el SIG_INT(Cntrl + C)
            }
            // Redirección de entrada si es necesario, unicamente en el primer comando
            if ((i == 0) && linea->redirect_input != NULL) {
                if(freopen(linea->redirect_input, "r", stdin) == NULL){
                    fprintf(stderr, "%s: Error. %s\n", linea->redirect_input, strerror(errno));
                    exit(1);
                } 
            }

            // Si no es el primer comando, redirigir la entrada desde el pipe anterior
            if (prev_pipe_fd != -1) {
                dup2(prev_pipe_fd, STDIN_FILENO);
                close(prev_pipe_fd);
            }

            // Si no es el último comando, redirigir la salida al siguiente pipe
            if (i < linea->ncommands - 1) {
                close(pipefd[0]);  // No necesitamos la parte de lectura del pipe
                dup2(pipefd[1], STDOUT_FILENO);  // Redirigir salida al pipe
                close(pipefd[1]);
            }

            // Redirección de salida si existe, unicamente en el ultimo comando
            if ((i == linea->ncommands - 1) && linea->redirect_output != NULL) {
                if(freopen(linea->redirect_output, "w", stdout) == NULL){
                    fprintf(stderr, "%s: Error. %s\n", linea->redirect_output, strerror(errno));
                    exit(1);
                }
            }

            // Redirección de error si existe, solo en el último comando
            if ((i == linea->ncommands - 1) && linea->redirect_error != NULL) {
                if(freopen(linea->redirect_error, "w", stderr) == NULL){
                    fprintf(stderr, "%s: Error. %s\n", linea->redirect_error, strerror(errno));
                    exit(1);
                }
            }
            if(linea->commands[i].filename == NULL){//PARA CUANDO ESTE MAL ESCRITO ALGUN COMANDO O NO EXISTA MUESTRE EL ERROR
                fprintf(stderr,"Error: Comando '%s' no encontrado.\n",linea->commands[i].argv[0]);
                fflush(stdout);
                exit(1);
            }
             
            // Ejecutar el comando actual, el exe siempre al final 
            if (execvp(linea->commands[i].filename, linea->commands[i].argv) == -1) {
                fprintf(stderr, "%s: No se encuentra el mandato\n", linea->commands[i].filename);
                fflush(stdout);
                exit(1);
            }
            
        } else { // Proceso padre

            // Si la linea de comando esta en segundo plano, añadimos los pids de cada comando en el array de pids para pasarselos al job
            if (linea->background) {
                pid_todos[i]=pid;
            }

            // Cerrar los extremos del pipe que ya no se necesitan
            if (prev_pipe_fd != -1) {
                close(prev_pipe_fd);
            }
            if (i < linea->ncommands) {
                close(pipefd[1]);  // No necesitamos la parte de escritura del pipe, porque el siguiente comando solo leerá del pipe
                prev_pipe_fd = pipefd[0];  // Guardar el extremo de lectura para el siguiente comando
            }

            // Si el comando no es en segundo plano, esperar al hijo
            if (!linea->background) {
                waitpid(pid, NULL, 0);  // Esperamos a que el proceso hijo termine
            }
        }
    }

    if(linea->background){
        //un job para todos los mandatos de la linea

        // Llamar a la función auxiliar para agregar el trabajo en segundo plano
        agregar_job_background(pid_todos, linea->ncommands, buf);  // Guardar la línea completa de comandos
        // Informar que el trabajo se ha ejecutado en segundo plano
        printf("[%d] %d\n", num_jobs, pid_todos[linea->ncommands-1]); //mostrar el ultimo pid de la linea de comandos 
        
    }else{
        free(pid_todos);
    }
    

    // No esperar a que todos los procesos hijos terminen en segundo plano
    if (!linea->background) {
        for (i = 0; i < linea->ncommands; i++) {
            wait(NULL);  // Esperamos a que todos los procesos hijos terminen, los de en primer plano
        }
    }
}


void cambiar_directorio(tline *linea) {
    // Verificamos que no haya más de un comando en la línea
    if (linea->ncommands == 1) {
        // Si el comando 'cd' tiene 0 o 1 argumento, cambia al directorio HOME o al especificado
        if (linea->commands[0].argc == 1) {
            // No tiene argumentos, se cambia al directorio HOME
            char *home = getenv("HOME");
            if (home == NULL) {
                fprintf(stderr, "Error: La variable HOME no está definida\n");
                return;
            }
            if (chdir(home) == -1) {
                perror("Error al cambiar de directorio");
            } else {
                // Imprimir la nueva ruta del directorio actual
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    printf("Directorio actual: %s\n", cwd);
                }
            }
        } else if (linea->commands[0].argc == 2) {
            // Si tiene un argumento, usarlo como la ruta del directorio
            if (chdir(linea->commands[0].argv[1]) == -1) {
                perror("Error al cambiar de directorio");
            } else {
                // Imprimir la nueva ruta del directorio actual
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    printf("Directorio actual: %s\n", cwd);
                }
            }
        } else {
            // Si hay más de un argumento, se muestra un error
            perror("Error: El comando cd solo debe tener uno o ningún argumento.\n");
        }
    } else {
        perror("Error: El comando cd no debe tener tuberías ni más de un comando.\n");
    }
}

// Manejador de la señal SIGINT (Ctrl + C) para la minishell
void manejar_SIGINT(int sig) {
    // Ignoramos la señal SIGINT para que la shell no termine con Ctrl + C
    printf("\nCtrl + C no cierra la minishell, se debe utilizar exit.\n");
    printf("msh> ");
    fflush(stdout); // Asegura que el prompt se imprima correctamente
}

void ejecutar_umask(tline *linea) {//usa un numero octal para especificar los permisos que NO se establecerán
    // Si no se pasa ningún argumento, mostramos el valor actual de umask
    if (linea->ncommands == 1 && linea->commands[0].argc == 1) {
        mode_t actual_umask = umask(0); // Usamos umask(0) para obtener el valor actual
        printf("Máscara actual: %03o\n", actual_umask); // Mostramos la máscara en formato octal
        umask(actual_umask);  // Restauramos la umask original
    }
    // Si se pasa un argumento, intentamos establecer una nueva umask
    else if (linea->commands[0].argc == 2) {
        // Obtenemos el argumento (el valor de la nueva umask)
        char *argumento = linea->commands[0].argv[1];
        char *p; // después de la conversión apuntará al primer carácter no convertido de la cadena.
        // Verificamos si el argumento es válido (debe ser octal con tres dígitos, se expresa empezando por 0)
        if (argumento[0] == '0' && (strlen(argumento) == 4 || strlen(argumento) == 3)) {
            // Intentamos convertir el argumento a un número octal
            mode_t nueva_umask = strtol(argumento, &p, 8);//puede ponerse en vez de p NULL pero en este caso queremos asegurarnos de que se ha convertido correctamente a octal
            
            //Verificamos que la conversión haya sido exitosa
            if(*p == '\0'){//si p equivale a la cadena vacia es que no ha habido error
                // Establecemos la nueva umask
                umask(nueva_umask);
                printf("Nueva máscara establecida: %03o\n", nueva_umask); // Mostramos la nueva máscara en formato octal

            }else{//la conversión a octal no fue exitosa
                perror("Error: El valor de umask ser un número octal válido.\n");
            }
            
        } else {
            // Si el formato no es válido, mostramos un error
            perror("Error: El valor de umask debe estar en formato octal (por ejemplo, 022).\n");
        }
    }
}

 
int main() {
	char buf[1024];
	tline * line;
	int i,j;

    // Capturamos la señal SIGINT para la minishell (proceso principal) para que no se cierre
    signal(SIGINT, manejar_SIGINT);

	printf("msh> ");	
	while (fgets(buf, 1024, stdin)) {//lee lineas por entrada estandar, el buf es la linea entera sin tokenizar
		line = tokenize(buf);
        
		if (line == NULL)
			continue;
        
        // Si el comando es 'exit', cerramos la shell
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "exit") == 0) {
            // Liberar memoria para los trabajos en segundo plano
            for (int i = 0; i < num_jobs; i++) {
                free(jobs[i].command);  // Liberar cada comando
                free(jobs[i].pid); //Liberar cada array de pids
            }
            free(jobs);  // Liberar el array de trabajos
            exit(0); // Salimos del shell

        }else if(line->ncommands == 1 && strcmp(line->commands[0].argv[0], "umask") == 0) {
            ejecutar_umask(line);

        }else if(strcmp(line->commands[0].argv[0], "cd") == 0) {
            cambiar_directorio(line);
        
        }// Si el comando es 'jobs', mostramos los trabajos en segundo plano
        else if (strcmp(line->commands[0].argv[0], "jobs") == 0) {
            mostrar_jobs();
        }
        // Si el comando es 'fg', traemos un trabajo al primer plano
        else if (strcmp(line->commands[0].argv[0], "fg") == 0) {
            fg(line);

        }else {//Aqui se ejecutan el resto de comandos, que nos son los creados por nosotras, con o sin pipes, tanto en background como en foreground
            ejecutar_comandos(line,buf);    
        }
        // Imprimir la siguiente línea de comando
        printf("msh> ");  
    }
    return 0;
}
 