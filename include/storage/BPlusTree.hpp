/**
 * @file BPlusTree.h
 * @brief Implementación de un Árbol B+ persistente en disco.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <stdexcept>

#define PAGE_SIZE 4096
#define B 50  // Grado Mínimo. Max hijos = 100, Max claves = 99.

/**
 * @struct MetaPage
 * @brief Estructura de la página de metadatos (página 0) del archivo.
 * Almacena información crítica para la persistencia del árbol.
 */
struct MetaPage {
    uint32_t magic;             /// 4 < Número mágico para identificar el formato del archivo.
    int64_t root_offset;        /// 8 < Offset (en bytes) de la página raíz.
    int64_t free_list_head;     /// 8 < Cabeza de la lista de páginas libres para reutilización.
    int64_t next_append_offset; /// 8 < Offset para la próxima página nueva al final del archivo.
    char padding[PAGE_SIZE - 28]; /// 4072(PAGE_SIZE=4096)< Relleno para completar el tamaño de página.
                                  /// total: 4096
};

/**
 * @struct NodeHeader
 * @brief Cabecera común para todos los nodos del árbol.
 */
struct NodeHeader {
    uint8_t is_leaf;    /// 1 < Indica si el nodo es una hoja (1) o interno (0).
    uint16_t num_keys;  /// 2 < Número actual de claves almacenadas en el nodo.
                        /// total: 3
};

/**
 * @struct BPlusLeafNode
 * @brief Representación de un nodo hoja en el Árbol B+.
 * @tparam K Tipo de la clave.
 * @tparam V Tipo del valor (payload).
 */
template <typename K, typename V>
struct BPlusLeafNode {
    NodeHeader header;      /// 3 < Cabecera del nodo.
    int64_t prev_leaf;      /// 8 < Offset a la hoja anterior (lista doblemente enlazada).
    int64_t next_leaf;      /// 8 < Offset a la hoja siguiente (lista doblemente enlazada).
    K keys[2 * B - 1];      /// 99(B=50) < Arreglo de claves.
    V payloads[2 * B - 1];  /// 99(B=50) < Arreglo de valores asociados a las claves.
                            /// total: 19 + 99K 99V
};

/**
 * @struct BPlusInternalNode
 * @brief Representación de un nodo interno (no hoja) en el Árbol B+.
 * @tparam K Tipo de la clave.
 */
template <typename K>
struct BPlusInternalNode {
    NodeHeader header;               /// 3 < Cabecera del nodo.
    K keys[2 * B - 1];               /// 198 < Arreglo de claves que sirven como separadores.
    int64_t children_offsets[2 * B]; /// 200 < Offsets a los nodos hijos.
};

/**
 * @class BPlusTreeDisk
 * @brief Clase principal para gestionar un Árbol B+ almacenado en disco.
 * Soporta operaciones de inserción, búsqueda y eliminación con persistencia.
 */
class BPlusTreeDisk {
private:
    int fd;            /// 4 < Descriptor de archivo para operaciones de E/S.
    MetaPage meta;     /// 4096 < Estructura en memoria de la página de metadatos.

    /**
     * @brief Lee una página completa desde el disco.
     * @param offset Posición en bytes desde el inicio del archivo.
     * @param buffer Puntero al destino de la lectura.
     */
    void read_page(int64_t offset, void* buffer) {
        if (pread(fd, buffer, PAGE_SIZE, offset) != PAGE_SIZE) {
            throw std::runtime_error("error in read page (I/O)");
        }
    }

    /**
     * @brief Escribe una página completa al disco.
     * @param offset Posición en bytes donde escribir.
     * @param buffer Datos a escribir.
     */
    void write_page(int64_t offset, const void* buffer) {
        if (pwrite(fd, buffer, PAGE_SIZE, offset) != PAGE_SIZE) {
            throw std::runtime_error("error in write page (I/O)");
        }
    }

    /**
     * @brief Sincroniza la página de metadatos actual con el disco.
     */
    void save_meta() {
        write_page(0, &meta);
    }

    /**
     * @brief Reserva una nueva página, reutilizando de la free list si es posible.
     * @return Offset de la página reservada.
     */
    int64_t allocate_page() {
        if (meta.free_list_head != -1) {
            int64_t recycled_off = meta.free_list_head;
            
            // Leer el siguiente en la free list
            char buf[PAGE_SIZE];
            read_page(recycled_off, buf);
            int64_t next_free = *reinterpret_cast<int64_t*>(buf);
            
            meta.free_list_head = next_free;
            save_meta();
            
            // Limpiar la página reciclada
            char empty[PAGE_SIZE] = {0};
            write_page(recycled_off, empty);
            return recycled_off;
        }

        int64_t new_offset = meta.next_append_offset;
        meta.next_append_offset += PAGE_SIZE;
        save_meta();
        
        // Inicializar bloque a 0 en disco
        char empty[PAGE_SIZE] = {0};
        write_page(new_offset, empty);
        return new_offset;
    }

    /**
     * @brief Libera una página y la añade a la free list.
     * @param offset Offset de la página a liberar.
     */
    void deallocate_page(int64_t offset) {
        if (offset <= 0) return; // No liberar la meta-página u offsets invalidos

        char buf[PAGE_SIZE] = {0};
        int64_t* next_ptr = reinterpret_cast<int64_t*>(buf);
        *next_ptr = meta.free_list_head;
        
        write_page(offset, buf);
        
        meta.free_list_head = offset;
        save_meta();
    }

    /**
     * @brief Divide un nodo hijo que ha alcanzado su capacidad máxima.
     * @param parent_off Offset del nodo padre.
     * @param index Índice del hijo en el padre.
     * @param child_off Offset del nodo hijo a dividir.
     */
    void split_child(int64_t parent_off, int index, int64_t child_off) {
        char p_buf[PAGE_SIZE], c_buf[PAGE_SIZE], z_buf[PAGE_SIZE] = {0};
        
        read_page(parent_off, p_buf);
        read_page(child_off, c_buf);
        
        auto* parent = reinterpret_cast<BPlusInternalNode<int>*>(p_buf);
        auto* child_hdr = reinterpret_cast<NodeHeader*>(c_buf);
        
        int64_t z_off = allocate_page();
        int k_up;

        if (child_hdr->is_leaf) {
            auto* y = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(c_buf);
            auto* z = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(z_buf);
            
            z->header.is_leaf = 1;
            z->header.num_keys = B;
            y->header.num_keys = B - 1;
            
            for (int j = 0; j < B; j++) {
                z->keys[j] = y->keys[j + B - 1];
                z->payloads[j] = y->payloads[j + B - 1];
            }
            
            z->next_leaf = y->next_leaf;
            z->prev_leaf = child_off;
            y->next_leaf = z_off;
            
            if (z->next_leaf != -1) {
                char right_buf[PAGE_SIZE];
                read_page(z->next_leaf, right_buf);
                auto* right_sibling = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(right_buf);
                right_sibling->prev_leaf = z_off;
                write_page(z->next_leaf, right_buf);
            }
            
            k_up = z->keys[0]; 
        } else {
            auto* y = reinterpret_cast<BPlusInternalNode<int>*>(c_buf);
            auto* z = reinterpret_cast<BPlusInternalNode<int>*>(z_buf);
            
            z->header.is_leaf = 0;
            z->header.num_keys = B - 1;
            y->header.num_keys = B - 1;
            
            for (int j = 0; j < B - 1; j++) {
                z->keys[j] = y->keys[j + B];
            }
            for (int j = 0; j < B; j++) {
                z->children_offsets[j] = y->children_offsets[j + B];
            }
            
            k_up = y->keys[B - 1]; 
        }

        for (int j = parent->header.num_keys; j >= index + 1; j--) {
            parent->children_offsets[j + 1] = parent->children_offsets[j];
        }
        parent->children_offsets[index + 1] = z_off;

        for (int j = parent->header.num_keys - 1; j >= index; j--) {
            parent->keys[j + 1] = parent->keys[j];
        }
        parent->keys[index] = k_up;
        parent->header.num_keys++;

        write_page(parent_off, p_buf);
        write_page(child_off, c_buf);
        write_page(z_off, z_buf);
    }

    /**
     * @brief Inserta una clave en un nodo que se garantiza no está lleno.
     * @param node_off Offset del nodo.
     * @param k Clave a insertar.
     * @param v Valor asociado.
     */
    void insert_non_full(int64_t node_off, int k, int64_t v) {
        char buf[PAGE_SIZE];
        read_page(node_off, buf);
        NodeHeader* hdr = reinterpret_cast<NodeHeader*>(buf);
        
        int i = hdr->num_keys - 1;
        
        if (hdr->is_leaf) {
            auto* leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(buf);
            while (i >= 0 && k < leaf->keys[i]) {
                leaf->keys[i + 1] = leaf->keys[i];
                leaf->payloads[i + 1] = leaf->payloads[i];
                i--;
            }
            leaf->keys[i + 1] = k;
            leaf->payloads[i + 1] = v;
            leaf->header.num_keys++;
            write_page(node_off, buf);
        } else {
            auto* internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
            while (i >= 0 && k < internal->keys[i]) i--;
            i++;
            
            int64_t child_off = internal->children_offsets[i];
            char c_buf[PAGE_SIZE];
            read_page(child_off, c_buf);
            NodeHeader* child_hdr = reinterpret_cast<NodeHeader*>(c_buf);
            
            if (child_hdr->num_keys == 2 * B - 1) {
                split_child(node_off, i, child_off);
                read_page(node_off, buf); // Refrescar parent post-split
                internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
                if (k >= internal->keys[i]) i++;
            }
            
            insert_non_full(internal->children_offsets[i], k, v);
        }
    }

    /**
     * @brief Elimina una clave de forma recursiva.
     * @param node_off Offset del nodo actual.
     * @param k Clave a eliminar.
     */
    void delete_recursive(int64_t node_off, int k) {
        char buf[PAGE_SIZE];
        read_page(node_off, buf);
        NodeHeader* hdr = reinterpret_cast<NodeHeader*>(buf);

        if (hdr->is_leaf) {
            auto* leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(buf);
            int idx = -1;
            for (int i = 0; i < leaf->header.num_keys; i++) {
                if (leaf->keys[i] == k) { idx = i; break; }
            }
            if (idx != -1) {
                for (int i = idx; i < leaf->header.num_keys - 1; i++) {
                    leaf->keys[i] = leaf->keys[i + 1];
                    leaf->payloads[i] = leaf->payloads[i + 1];
                }
                leaf->header.num_keys--;
                write_page(node_off, buf);
            }
            return;
        }

        auto* internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
        int i = 0;
        while (i < internal->header.num_keys && k >= internal->keys[i]) i++;

        int64_t child_off = internal->children_offsets[i];
        char c_buf[PAGE_SIZE];
        read_page(child_off, c_buf);
        NodeHeader* c_hdr = reinterpret_cast<NodeHeader*>(c_buf);

        if (c_hdr->num_keys == B - 1) {
            fix_underflow(node_off, buf, internal, i, child_off, c_buf, c_hdr);
            
            // Recalcular índice de enrutamiento tras posible reestructuración del padre
            i = 0;
            while (i < internal->header.num_keys && k >= internal->keys[i]) i++;
            child_off = internal->children_offsets[i];
        }

        delete_recursive(child_off, k);
    }

    /**
     * @brief Soluciona el subflujo (underflow) de un nodo hijo.
     * @param p_off Offset del padre.
     * @param p_buf Buffer del padre.
     * @param parent Estructura del padre.
     * @param c_idx Índice del hijo en el padre.
     * @param c_off Offset del hijo.
     * @param c_buf Buffer del hijo.
     * @param c_hdr Cabecera del hijo.
     */
    void fix_underflow(int64_t p_off, char* p_buf, BPlusInternalNode<int>* parent, int c_idx, int64_t c_off, char* c_buf, NodeHeader* c_hdr) {
        int64_t left_off = (c_idx > 0) ? parent->children_offsets[c_idx - 1] : -1;
        int64_t right_off = (c_idx < parent->header.num_keys) ? parent->children_offsets[c_idx + 1] : -1;

        char s_buf[PAGE_SIZE]; // Sibling buffer

        // Intentar préstamo desde hermano izquierdo
        if (left_off != -1) {
            read_page(left_off, s_buf);
            NodeHeader* left_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            if (left_hdr->num_keys >= B) {
                borrow_from_left(parent, c_idx, c_buf, c_hdr, s_buf, left_hdr);
                write_page(p_off, p_buf);
                write_page(c_off, c_buf);
                write_page(left_off, s_buf);
                return;
            }
        }

        // Intentar préstamo desde hermano derecho
        if (right_off != -1) {
            read_page(right_off, s_buf);
            NodeHeader* right_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            if (right_hdr->num_keys >= B) {
                borrow_from_right(parent, c_idx, c_buf, c_hdr, s_buf, right_hdr);
                write_page(p_off, p_buf);
                write_page(c_off, c_buf);
                write_page(right_off, s_buf);
                return;
            }
        }

        // Fusión obligatoria (Merge)
        if (left_off != -1) {
            read_page(left_off, s_buf);
            NodeHeader* left_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            merge_nodes(parent, c_idx - 1, s_buf, left_hdr, c_buf, c_hdr);
            write_page(p_off, p_buf);
            write_page(left_off, s_buf);
            deallocate_page(c_off);
        } else {
            read_page(right_off, s_buf); // Forzar carga del derecho para fusionar hacia la derecha
            NodeHeader* right_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            merge_nodes(parent, c_idx, c_buf, c_hdr, s_buf, right_hdr);
            write_page(p_off, p_buf);
            write_page(c_off, c_buf);
            deallocate_page(right_off);
        }
    }

    /**
     * @brief Toma una clave prestada del hermano izquierdo.
     */
    void borrow_from_left(BPlusInternalNode<int>* parent, int c_idx, char* c_buf, NodeHeader* c_hdr, char* l_buf, NodeHeader* l_hdr) {
        if (c_hdr->is_leaf) {
            auto* child = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(c_buf);
            auto* left = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(l_buf);

            // Desplazar child a la derecha
            for (int i = child->header.num_keys; i > 0; i--) {
                child->keys[i] = child->keys[i - 1];
                child->payloads[i] = child->payloads[i - 1];
            }
            child->keys[0] = left->keys[left->header.num_keys - 1];
            child->payloads[0] = left->payloads[left->header.num_keys - 1];
            
            parent->keys[c_idx - 1] = child->keys[0]; // Actualizar enrutador
        } else {
            auto* child = reinterpret_cast<BPlusInternalNode<int>*>(c_buf);
            auto* left = reinterpret_cast<BPlusInternalNode<int>*>(l_buf);

            for (int i = child->header.num_keys; i > 0; i--) child->keys[i] = child->keys[i - 1];
            for (int i = child->header.num_keys + 1; i > 0; i--) child->children_offsets[i] = child->children_offsets[i - 1];

            child->keys[0] = parent->keys[c_idx - 1]; // Bajar separador
            child->children_offsets[0] = left->children_offsets[left->header.num_keys]; // Mover hijo
            parent->keys[c_idx - 1] = left->keys[left->header.num_keys - 1]; // Subir nuevo separador
        }
        c_hdr->num_keys++;
        l_hdr->num_keys--;
    }

    /**
     * @brief Toma una clave prestada del hermano derecho.
     */
    void borrow_from_right(BPlusInternalNode<int>* parent, int c_idx, char* c_buf, NodeHeader* c_hdr, char* r_buf, NodeHeader* r_hdr) {
        if (c_hdr->is_leaf) {
            auto* child = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(c_buf);
            auto* right = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(r_buf);

            child->keys[child->header.num_keys] = right->keys[0];
            child->payloads[child->header.num_keys] = right->payloads[0];

            for (int i = 0; i < right->header.num_keys - 1; i++) {
                right->keys[i] = right->keys[i + 1];
                right->payloads[i] = right->payloads[i + 1];
            }
            parent->keys[c_idx] = right->keys[0];
        } else {
            auto* child = reinterpret_cast<BPlusInternalNode<int>*>(c_buf);
            auto* right = reinterpret_cast<BPlusInternalNode<int>*>(r_buf);

            child->keys[child->header.num_keys] = parent->keys[c_idx];
            child->children_offsets[child->header.num_keys + 1] = right->children_offsets[0];
            parent->keys[c_idx] = right->keys[0];

            for (int i = 0; i < right->header.num_keys - 1; i++) right->keys[i] = right->keys[i + 1];
            for (int i = 0; i < right->header.num_keys; i++) right->children_offsets[i] = right->children_offsets[i + 1];
        }
        c_hdr->num_keys++;
        r_hdr->num_keys--;
    }

    /**
     * @brief Fusiona dos nodos hermanos.
     */
    void merge_nodes(BPlusInternalNode<int>* parent, int p_idx, char* l_buf, NodeHeader* l_hdr, char* r_buf, NodeHeader* r_hdr) {
        if (l_hdr->is_leaf) {
            auto* left = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(l_buf);
            auto* right = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(r_buf);

            for (int i = 0; i < right->header.num_keys; i++) {
                left->keys[left->header.num_keys + i] = right->keys[i];
                left->payloads[left->header.num_keys + i] = right->payloads[i];
            }
            left->header.num_keys += right->header.num_keys;
            
            // Reenlazar lista horizontal
            left->next_leaf = right->next_leaf;
            if (right->next_leaf != -1) {
                char next_buf[PAGE_SIZE];
                read_page(right->next_leaf, next_buf);
                auto* next_leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(next_buf);
                next_leaf->prev_leaf = parent->children_offsets[p_idx]; // offset izquierdo
                write_page(right->next_leaf, next_buf);
            }
        } else {
            auto* left = reinterpret_cast<BPlusInternalNode<int>*>(l_buf);
            auto* right = reinterpret_cast<BPlusInternalNode<int>*>(r_buf);

            left->keys[left->header.num_keys] = parent->keys[p_idx]; // Empujar separador
            left->header.num_keys++;

            for (int i = 0; i < right->header.num_keys; i++) {
                left->keys[left->header.num_keys + i] = right->keys[i];
            }
            for (int i = 0; i <= right->header.num_keys; i++) {
                left->children_offsets[left->header.num_keys + i] = right->children_offsets[i];
            }
            left->header.num_keys += right->header.num_keys;
        }

        // Eliminar clave enrutadora y puntero al hijo derecho en el nodo padre
        for (int i = p_idx; i < parent->header.num_keys - 1; i++) {
            parent->keys[i] = parent->keys[i + 1];
        }
        for (int i = p_idx + 1; i < parent->header.num_keys; i++) {
            parent->children_offsets[i] = parent->children_offsets[i + 1];
        }
        parent->header.num_keys--;
    }

public:
    /**
     * @brief constructor. open the file and load or initialize the tree structure
     * @param filename File name of the database
     */
    BPlusTreeDisk(const char* filename) {
        fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd < 0) throw std::runtime_error("error opening db file");

        struct stat st;
        fstat(fd, &st);
        if (st.st_size == 0) {
            meta.magic = 0x42545245; // 'BTRE'
            meta.root_offset = PAGE_SIZE;
            meta.free_list_head = -1;
            meta.next_append_offset = PAGE_SIZE * 2;
            save_meta();

            // creating root 
            char root_buf[PAGE_SIZE] = {0};
            auto* root = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(root_buf);
            root->header.is_leaf = 1;
            root->header.num_keys = 0;
            root->prev_leaf = -1;
            root->next_leaf = -1;
            write_page(PAGE_SIZE, root_buf);
        } else {
            read_page(0, &meta);
        }
    }

    /**
     * @brief Destructor. close descritor of the file
     */
    ~BPlusTreeDisk() {
        close(fd);
    }

    /**
     * @brief Busca un valor asociado a una clave.
     * @param k Clave a buscar.
     * @return El valor (payload) si se encuentra, o -1 si no existe.
     */
    int64_t search(int k) {
        int64_t curr_off = meta.root_offset;
        char buf[PAGE_SIZE];

        while (true) {
            read_page(curr_off, buf);
            NodeHeader* hdr = reinterpret_cast<NodeHeader*>(buf);

            if (hdr->is_leaf) {
                auto* leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(buf);
                for (int i = 0; i < leaf->header.num_keys; i++) {
                    if (leaf->keys[i] == k) return leaf->payloads[i];
                }
                return -1; // No encontrado
            } else {
                auto* internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
                int i = 0;
                while (i < internal->header.num_keys && k >= internal->keys[i]) i++;
                curr_off = internal->children_offsets[i];
            }
        }
    }

    /**
     * @brief Inserta un par clave-valor en el árbol.
     * @param k Clave a insertar.
     * @param v Valor asociado.
     *
     * FIX: overload types
     */
    void insert(int k, int64_t v) {
        char root_buf[PAGE_SIZE];
        read_page(meta.root_offset, root_buf);
        NodeHeader* root_hdr = reinterpret_cast<NodeHeader*>(root_buf);

        if (root_hdr->num_keys == 2 * B - 1) {
            int64_t new_root_off = allocate_page();
            char new_root_buf[PAGE_SIZE] = {0};
            auto* new_root = reinterpret_cast<BPlusInternalNode<int>*>(new_root_buf);
            
            new_root->header.is_leaf = 0;
            new_root->header.num_keys = 0;
            new_root->children_offsets[0] = meta.root_offset;
            write_page(new_root_off, new_root_buf);
            
            split_child(new_root_off, 0, meta.root_offset);
            
            meta.root_offset = new_root_off;
            save_meta();
            
            insert_non_full(new_root_off, k, v);
        } else {
            insert_non_full(meta.root_offset, k, v);
        }
    }

    /**
     * @brief Elimina una clave del árbol.
     * @param k Clave a eliminar.
     */
    void remove(int k) {
        if (meta.root_offset == -1) return;

        delete_recursive(meta.root_offset, k);

        // Comprobación de colapso de la raíz
        char root_buf[PAGE_SIZE];
        read_page(meta.root_offset, root_buf);
        NodeHeader* root_hdr = reinterpret_cast<NodeHeader*>(root_buf);

        if (root_hdr->num_keys == 0 && !root_hdr->is_leaf) {
            auto* root_int = reinterpret_cast<BPlusInternalNode<int>*>(root_buf);
            int64_t old_root = meta.root_offset;
            meta.root_offset = root_int->children_offsets[0];
            save_meta();
            deallocate_page(old_root);
        }
    }
};

