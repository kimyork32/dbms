# Megatron DBMS

Megatron DBMS es un sistema de gestión de bases de datos relacional (RDBMS) diseñado con fines educativos y de investigación. Se ha construido utilizando una arquitectura modular que separa claramente las responsabilidades del procesamiento de consultas, la ejecución y el almacenamiento en disco, todo ello implementado en C++ moderno.

---

## Arquitectura del Sistema

La arquitectura de Megatron sigue el modelo clásico de los sistemas de bases de datos relacionales, dividiéndose en las siguientes capas principales:

1. **Parser (Analizador Sintáctico):** Toma una consulta SQL cruda y genera un Árbol de Sintaxis Abstracta (AST).
2. **Binder (Analizador Semántico):** Valida el AST contra el Catálogo de la base de datos, asegurándose de que las tablas y columnas referenciadas existan y los tipos de datos sean compatibles.
3. **Optimizer (Optimizador de Consultas):** Toma el AST validado y genera un plan de ejecución físico (`PlanNode`). Durante esta fase, el optimizador también inyecta *hints* de memoria (`BufferHint`) basados en reglas heurísticas (por ejemplo, mantener tablas hash en RAM o descartar páginas tras un escaneo masivo secuencial).
4. **Execution Engine (Motor de Ejecución):** Sigue el modelo Volcano (iterador). Cada nodo del plan es procesado por un `Executor` que va "jalando" tuplas (pull) mediante el método `Next()`.
5. **Storage Engine (Motor de Almacenamiento):** Gestiona la persistencia de datos en disco. Incluye el manejo de páginas (Slotted Pages), un gestor de buffer global (Buffer Pool Manager) y estructuras de acceso e indexación.

---

## Estructura de Directorios

El código fuente está organizado lógicamente separando cabeceras (`include/`) e implementaciones (`src/`). 

```text
megatron_db/
├── CMakeLists.txt          # Configuración de compilación para CMake
├── docs/                   # Documentación y tareas del proyecto
├── include/                # Archivos de cabecera (.hpp)
│   ├── binder/             # Lógica y definiciones del analizador semántico (Binder)
│   ├── catalog/            # Catálogo del sistema (esquemas y metadatos)
│   ├── execution/          # Clases para los nodos del plan (PlanNode) y ejecutores
│   ├── optimizer/          # Optimizador de consultas heurístico
│   ├── parser/             # Lexer, Parser y definiciones del AST
│   └── storage/            # Submódulos de almacenamiento:
│       ├── engine/         # Interfaz y motor principal de almacenamiento en disco
│       ├── index/          # Métodos de acceso a datos (Árboles B+, Tablas Hash)
│       ├── page/           # Manejo de páginas en disco (SlottedPage) y Buffer Pool
│       └── record/         # Definiciones de tuplas, tipos de datos y esquemas
└── src/                    # Código fuente (.cpp)
    ├── benchmarks/         # Batería de pruebas unitarias, de estrés y rendimiento (Tests)
    ├── binder/             # Implementación del Binder
    ├── catalog/            # Implementación del Catálogo
    ├── execution/          # Implementación de los Executors (SeqScan, HashJoin, etc.)
    ├── optimizer/          # Implementación de las reglas de optimización
    ├── parser/             # Implementación del analizador sintáctico
    ├── storage/            # Implementación profunda de almacenamiento (Buffer Pool, I/O)
    └── main.cpp            # Punto de entrada de la aplicación
```

### Detalle de Componentes Clave

- **`storage/page/buffer_pool_manager`:** Administra qué páginas del disco se encuentran en memoria caché RAM. Implementa algoritmos de reemplazo y además es **Plan-Aware**; es decir, utiliza indicaciones semánticas del optimizador (`BufferHint`) para decidir cuándo mantener una página en memoria (`KEEP_HOT`) o cuándo descartarla rápidamente (`DISCARD_QUICKLY`).
- **`execution/executor`:** Implementa los operadores relacionales básicos como `SeqScanExecutor`, `IndexScanExecutor`, `HashJoinExecutor`, `AggregationExecutor` y `FilterExecutor`.
- **`storage/index/b_plus_tree`:** Estructura de árbol B+ persistente en disco, responsable del escaneo por índice para agilizar búsquedas.
- **`benchmarks/`:** Todas las pruebas modulares se realizan aquí (Módulos M1, M2, M3), validando desde la concurrencia en el buffer pool hasta pruebas completas que engranan Binder, Optimizador, y Ejecutores.

---

## Compilación y Ejecución

Megatron DBMS utiliza CMake para la construcción del sistema.

### Requisitos
- CMake (>= 3.10)
- Compilador de C++ con soporte para C++17 (GCC, Clang o MSVC)

### Construcción

Desde la raíz del proyecto, ejecuta:

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

### Correr Pruebas (Tests)

Para garantizar la integridad y el correcto funcionamiento de los distintos módulos:

```bash
cd build
ctest --output-on-failure
```

También puedes ejecutar ejecutables de prueba específicos directamente desde `build/`:
```bash
./megatron_m3_test
./megatron_binder_stress_test
```
