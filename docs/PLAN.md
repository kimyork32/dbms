Plan de Arquitectura e Implementación: DBMS Custom

Este documento contiene la teoría y el roadmap de implementación para un motor de base de datos relacional, basado en la arquitectura estándar descrita en *Database Internals* (Alex Petrov). El enfoque de desarrollo será **Bottom-Up**: construir desde la persistencia en disco hasta la capa de red.

Se recomienda utilizar C++ (estándar C++17 o superior) para la implementación, aprovechando el control de memoria y rendimiento.

Estructura del Proyecto

/dbms_project
├── src/
│   ├── storage/        # Gestor de disco, páginas, buffer pool, WAL
│   ├── execution/      # Modelo Volcano (Iteradores), operadores físicos
│   ├── optimizer/      # CBO, estimador de costos, generación de planes
│   ├── parser/         # Lexer, Parser (AST)
│   ├── transport/      # Sockets TCP, protocolo cliente-servidor
│   └── main.cpp        # Punto de entrada y orquestación
├── include/            # Cabeceras (.h / .hpp)
├── tests/              # Pruebas unitarias por módulo
└── CMakeLists.txt

Fase 1: Storage Engine (Motor de Almacenamiento)
Es la capa más baja. Su responsabilidad es la persistencia, concurrencia (ACID) y acceso eficiente a los datos crudos. No entiende de SQL.

Teoría
Disk Manager & Pages: Los datos no se leen byte a byte, sino en bloques fijos llamados Páginas (típicamente de 4KB u 8KB).
Buffer Pool Manager: Un caché en RAM para las páginas del disco. Minimiza el I/O. Utiliza políticas de reemplazo como LRU o Clock.
Access Methods: Estructuras de datos físicas. Heap Files (datos desordenados) y B+ Trees (índices para búsquedas $O(\log n)$).
WAL (Write-Ahead Logging) & Recovery: Antes de escribir datos en disco, se escribe la intención (log) en un archivo secuencial para garantizar durabilidad (Crash Recovery).
Concurrency Control (Lock/Txn Manager): Implementa 2PL (Two-Phase Locking) o MVCC (Multi-Version Concurrency Control) para aislamiento.

Plan de Acción (Agente CLI)
Ticket 1.1: Implementar DiskManager para leer/escribir bloques de 4KB en un archivo binario (.db).
Ticket 1.2: Implementar estructura Page (con cabecera y slots para tuplas).
Ticket 1.3: Implementar BufferPoolManager con algoritmo de reemplazo LRU.
Ticket 1.4: Implementar HeapFile para insertar, borrar y leer registros por su RecordID (PageID + SlotID).
Ticket 1.5: Implementar LogManager (WAL básico) y un TransactionManager simple.

Fase 2: Execution Engine (Motor de Ejecución)
Toma un plan de ejecución físico y lo ejecuta llamando a las APIs del Storage Engine.

Teoría
Volcano Model (Iterator Model): Cada operador relacional (Scan, Filter, Join) implementa una interfaz con los métodos Init(), Next() y Close(). Los datos fluyen de abajo hacia arriba en el árbol de ejecución.
Operadores Físicos:
SeqScan: Pide páginas al Buffer Pool y extrae tuplas una a una.
Filter: Evalúa predicados lógicos.
NestedLoopJoin / HashJoin: Cruza datos entre dos tablas.

Plan de Acción (Agente CLI)
Ticket 2.1: Definir la interfaz base AbstractExecutor (Init(), Next(&tuple)).
Ticket 2.2: Implementar SeqScanExecutor que lea del HeapFile.
Ticket 2.3: Implementar la clase Tuple y Value (para manejar tipos de datos int, varchar, etc.).
Ticket 2.4: Implementar FilterExecutor y InsertExecutor.

Fase 3: Query Processor (Procesador de Consultas)
Traduce el texto SQL a operaciones de ejecución óptimas.

Teoría
Parser: Analiza la cadena SQL. Usa un lexer para tokens y un parser para construir un AST (Abstract Syntax Tree).
Binder/Analyzer: Valida el AST contra el catálogo del sistema (verifica que las tablas y columnas existan). Convierte el AST a un Plan Lógico.
Optimizer: Transforma el Plan Lógico en un Plan Físico. Aplica reglas heurísticas (ej. Pushdown de predicados) o estimación de costos (CBO) para elegir el mejor Join o índice.

Plan de Acción (Agente CLI)
Ticket 3.1: Integrar una librería de parsing (ej. DuckDB parser, o crear uno básico usando Flex/Bison o regex para un subconjunto de SQL muy limitado como SELECT, INSERT, CREATE TABLE).
Ticket 3.2: Implementar el Catalog en memoria (hash map de nombres de tablas a metadata de archivos).
Ticket 3.3: Implementar un Planner rudimentario (Rule-based) que convierta un AST validado en un árbol de AbstractExecutor de la Fase 2.

Fase 4: Transport (Capa de Comunicación)
Expone el motor al mundo exterior para recibir consultas y devolver resultados.

Teoría
Arquitectura Cliente-Servidor: El servidor escucha en un puerto TCP.
Protocolo de Cable (Wire Protocol): Formato binario o de texto para enviar queries y recibir DataFrames de respuesta (similar al protocolo de PostgreSQL o MySQL).
Connection Handling: Creación de hilos (thread-per-connection) o I/O asíncrono (epoll/kqueue) para manejar múltiples clientes.

Plan de Acción (Agente CLI)
Ticket 4.1: Implementar un servidor TCP básico usando sockets POSIX (sys/socket.h).
Ticket 4.2: Definir un protocolo de mensajes simple: [Header: Length] [Payload: Query string].
Ticket 4.3: Conectar el hilo del socket con el pipeline completo: Socket -> Parser -> Planner -> Executor -> Storage -> Socket (Response).
Ticket 4.4: Crear un cliente CLI simple (estilo psql o sqlite3) para interactuar con el servidor de forma interactiva.
