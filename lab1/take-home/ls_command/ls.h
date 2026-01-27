#ifndef LS_H
#define LS_H

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

// Entry structure
typedef struct {
    char* name;
    char type;
    ino_t inode;    
} LsEntry;

// Matrix structure
typedef struct {
    char*** data;
    size_t rows;
    size_t* cols;
} StringMatrix;

// Ls structure
typedef struct {
    bool a_;
    bool l_;
    bool h_;
} Ls;

Ls* ls_create(void);
Ls* ls_create_with_flags(bool a, bool l, bool h);
void ls_destroy(Ls* ls);

StringMatrix* ls_run(Ls* ls, const char* path);

LsEntry* ls_scan_directory(Ls* ls, const char* path, size_t* count);
StringMatrix* ls_process_entries(Ls* ls, LsEntry* entries, size_t* count);

void ls_print(const StringMatrix* matrix);

void ls_entry_destroy(LsEntry* entry, size_t count);
void string_matrix_destroy(StringMatrix* matrix);

#endif  // LS_H