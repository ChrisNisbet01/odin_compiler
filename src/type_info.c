#include "type_descriptors.h"
#include "hash.h"
#include <llvm-c/Core.h>
#include <string.h>

#define TYPE_INFO_TABLE_SIZE 1024

typedef struct TypeInfoEntry {
    int64_t type_id;
    LLVMValueRef type_info_global;
    struct TypeInfoEntry *next;
} TypeInfoEntry;

typedef struct {
    TypeInfoEntry *buckets[TYPE_INFO_TABLE_SIZE];
} TypeInfoTable;

static TypeInfoTable *type_info_table = NULL;

static size_t type_info_hash(int64_t type_id) {
    return (size_t)type_id % TYPE_INFO_TABLE_SIZE;
}

void type_info_table_init(void) {
    if (type_info_table != NULL) return;
    type_info_table = calloc(1, sizeof(TypeInfoTable));
}

void type_info_table_insert(int64_t type_id, LLVMValueRef type_info_global) {
    type_info_table_init();
    
    size_t bucket = type_info_hash(type_id);
    TypeInfoEntry *entry = calloc(1, sizeof(TypeInfoEntry));
    entry->type_id = type_id;
    entry->type_info_global = type_info_global;
    entry->next = type_info_table->buckets[bucket];
    type_info_table->buckets[bucket] = entry;
}

LLVMValueRef type_info_table_lookup(int64_t type_id) {
    if (type_info_table == NULL) return NULL;

    size_t bucket = type_info_hash(type_id);
    TypeInfoEntry *entry = type_info_table->buckets[bucket];
    while (entry != NULL) {
        if (entry->type_id == type_id) {
            return entry->type_info_global;
        }
        entry = entry->next;
    }
    return NULL;
}

void type_info_destroy(void) {
    if (type_info_table == NULL) return;
    for (int i = 0; i < TYPE_INFO_TABLE_SIZE; i++) {
        TypeInfoEntry *entry = type_info_table->buckets[i];
        while (entry != NULL) {
            TypeInfoEntry *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(type_info_table);
    type_info_table = NULL;
}