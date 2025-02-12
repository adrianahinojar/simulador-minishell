# Simulador-Minishell
Minishell en C, diseñada para ejecutarse en **Linux**, que gestiona comandos en primer y segundo plano, soporta pipes, redirección de entrada/salida y manejo de señales.

## Características principales

- Soporte para ejecutar comandos en **foreground** y **background** (`&`).
- Manejo de **pipes (`|`)** para conectar múltiples comandos.
- Implementación de comandos internos:
  - `cd [directorio]` → Cambia el directorio actual.
  - `jobs` → Muestra los procesos en segundo plano.
  - `fg [job_id]` → Trae un proceso en segundo plano al primer plano.
  - `umask [valor]` → Muestra o cambia la máscara de permisos.
- Manejo de **señales (`SIGINT`)** para evitar que la shell se cierre con `Ctrl + C`.
- Gestión dinámica de procesos en segundo plano.

## Compilación

Para compilar la mini shell, usa el siguiente comando:

```bash
gcc minishell.c libparser_64.a -o minishell
