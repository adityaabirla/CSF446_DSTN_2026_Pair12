#include "ls.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

//forward declarations -->
static char* combine_paths(const char* base, const char* leaf);
static int sort_by_name(const void* p1, const void* p2);
static int sort_by_id_then_name(const void* p1, const void* p2);
static int sort_matrix_rows(const void* p1, const void* p2);
static ino_t fetch_inode_id(const char* target_path);

struct RowWrapper
{
    char** row_ptr;
    size_t columnCount;
};

static bool is_hidden(const char* name) {
    if (name == NULL || strlen(name) == 0) {
        return false;
    }
    return name[0] == '.';
}

static char* strdup_safe(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str) + 1;
    char* copy = (char*)malloc(len);
    if (copy != NULL) {
        strcpy(copy, str);
    }
    return copy;
}

// Helper function to determine file type
static char get_file_type(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return 'f';  // Default to file if stat fails
    }
    
    if (S_ISDIR(st.st_mode)) {
        return 'd';  // Directory
    } else if (S_ISLNK(st.st_mode)) {
        return 'l';  // Symbolic link
    } else {
        return 'f';  // Regular file
    }
}

// Helper function to get file type string for -l flag
static const char* get_file_type_string(char type) {
    switch (type) {
        case 'd':
            return "DIRECTORY";
        case 'l':
            return "SOFTLINK";
        case 'f':
        default:
            return "FILE";
    }
}

static ino_t fetch_inode_id(const char* targetpath)
{
    struct stat info;
    int x = lstat(targetpath, &info);
    if(x == 0) return info.st_ino;
    else return 0;
}


/* Function: Joins two path strings with a '/' separator safely */
static char* combine_paths(const char* s1, const char* s2) {
    if (!s1 || !s2) return NULL;
    size_t s2len = strlen(s2);
    size_t s1len = strlen(s1);
    char* result = malloc(s1len + s2len + 2);
    if (!result) return NULL;

    strcpy(result, s1);
    if (s1len > 0 && result[s1len - 1] != '/') {
        result[s1len + 1] = '\0';
        result[s1len] = '/';
    }
    strcat(result, s2);
    return result;
}



/*
Ls* ls:

This is the configuration structure containing the user's flags (like -a for hidden files).

The function uses ls->a_ to decide whether to skip files starting with a dot.

const char* current:

The path of the directory currently being scanned (e.g., ./build or ./build/subdir).

LsEntry** list:

This is a pointer to a dynamic array of LsEntry structures.

It is passed as a double pointer (**) because if the array runs out of space, the function needs to call realloc and update the original pointer to the new memory location.

size_t* total:

A pointer to a counter that tracks exactly how many files have been found across the entire recursive process.

Using a pointer ensures that when the function calls itself for a subdirectory, the "count" continues to grow rather than resetting.

size_t* cap:

A pointer to the current "capacity" (maximum size) of the allocated list array.

The function checks *total >= *cap to determine if it needs to resize the array to prevent a memory overflow.
*/
static void deep_crawl(Ls* ls, const char* current, LsEntry** list, size_t* total, size_t* capacity)
{
    DIR* stream = opendir(current);
    if(stream == NULL) return;

    struct dirent* entry;
    while((entry = readdir(stream)) != NULL)
    {
        bool aflag = ls->a_;
        bool hiddenflag = is_hidden(entry->d_name);
        if(!aflag && hiddenflag) continue; //if a flag is not set then ignore hidden files
        bool flag1 = (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0); //is it a . or ..??
        char* abspath = combine_paths(current, entry->d_name);
        if(abspath == NULL)         continue;
        char filetype = get_file_type(abspath);
        if(*total >= *capacity)
        {
            if(*capacity == 0) *capacity = 16;
            else *capacity = 2 * (*capacity);
            *list = realloc(*list, (*capacity) * sizeof(LsEntry));
        }

        (*list)[*total].type = filetype;
        (*list)[*total].name = abspath;
        (*list)[*total].inode = fetch_inode_id(abspath);
        (*total)++; //count badha do

        if(filetype == 'd' && !flag1) //dont want infinite loop
        {
            deep_crawl(ls, abspath, list, total, capacity);
        }
    }
    closedir(stream);
}

Ls* ls_create(void) {
    Ls* ls = (Ls*)malloc(sizeof(Ls));
    if (ls == NULL) {
        return NULL;
    }
    ls->a_ = false;
    ls->l_ = false;
    ls->h_ = false;
    return ls;
}

Ls* ls_create_with_flags(bool a, bool l, bool h) {
    Ls* ls = (Ls*)malloc(sizeof(Ls));
    if (ls == NULL) {
        return NULL;
    }
    ls->a_ = a;
    ls->l_ = l;
    ls->h_ = h;
    return ls;
}

void ls_destroy(Ls* ls) {
    if (ls != NULL) {
        free(ls);
    }
}

void ls_print(const StringMatrix* matrix) {
    if (matrix == NULL || matrix->data == NULL) {
        return;
    }
    
    for (size_t i = 0; i < matrix->rows; ++i) {
        if (matrix->data[i] == NULL) {
            continue;
        }
        for (size_t j = 0; j < matrix->cols[i]; ++j) {
            if (matrix->data[i][j] != NULL) {
                printf("%s", matrix->data[i][j]);
                if (j != matrix->cols[i] - 1) {
                    printf(" ");
                }
            }
        }
        printf("\n");
    }
}





//The ls_scan_directory function serves as the primary data-gathering stage of the ls command. Its job is to look at the target path and return a list of LsEntry structures containing the name, type, and inode of every relevant file.
LsEntry* ls_scan_directory(Ls* ls, const char* path, size_t* count) {
    // TODO: Implement ls_scan_directory
    if (count == NULL) {
        return NULL;
    }
    if(path == NULL) return NULL;
    *count = 0;
    bool hh = ls->h_;
    if(!hh)
    {
        struct dirent **names;
        int n = scandir(path, &names, NULL, alphasort);
        if(n<0) return NULL;
        int mSize = sizeof(LsEntry)*n;
        LsEntry* res = malloc(mSize);
        size_t validindex = 0;
        for(int i = 0; i<n; i++)
        {
            if(!ls->a_ && is_hidden(names[i]->d_name))
            {
                free(names[i]);
                continue; //dont want . and ..
            }
            char* fullpoath = combine_paths(path, names[i]->d_name);
            res[validindex].name = fullpoath;
            res[validindex].type = get_file_type(fullpoath);
            res[validindex].inode = 0;
            validindex++;
            free(names[i]);
        }
        free(names);
        *count = validindex;
        return res;
    }
    else
    {
        size_t capacty = 20;
        int msze = sizeof(LsEntry) * capacty;
        LsEntry* haha = malloc(msze);
        deep_crawl(ls, path, &haha, count, &capacty);
        return haha;
    }
    
    // *count = 0;
    // return NULL;
}

// Sorting logic for Inodes and Names
static int sort_by_id_then_name(const void* p1, const void* p2) {
    const LsEntry *e1 = p1, *e2 = p2;
    if (e1->inode != e2->inode) return (e1->inode < e2->inode) ? -1 : 1;
    return strcmp(e1->name, e2->name);
}

static int sort_matrix_rows(const void* p1, const void* p2) {
    const struct RowWrapper *w1 = p1, *w2 = p2;
    return strcmp(w1->row_ptr[0], w2->row_ptr[0]);
}

//The ls_process_entries function is the "formatting engine" of the program. Its primary responsibility is to transform the raw data stored in the LsEntry array (names, types, and inodes) into the final 2D StringMatrix structure used for output.
/*
The function follows two entirely different logic paths based on whether the -h (hardlink) flag is active.

1. Normal Mode (No -h flag)
If the hardlink flag is not set, the process is straightforward:

Row Allocation: One row is created in the matrix for every entry in the entries array.

Column Allocation: Each row is allocated either 1 column (if just names are needed) or 2 columns (if the -l long format flag is active).

Data Copying:

Column 0: Stores a heap-allocated copy of the filename.

Column 1: If -l is active, it stores the string representation of the file type (e.g., "FILE", "DIRECTORY", or "SOFTLINK").

2. Hardlink Mode (With -h flag)
When searching for hardlinks, the function performs a more complex multi-step process to group files together:

A. Sorting and Grouping
Initial Sort: The entries array is sorted by Inode ID using qsort. This ensures that all files sharing the same physical data are positioned next to each other in the array.

Group Counting: The function loops through the sorted array to find "clumps" of identical Inodes. It only counts a group if it contains two or more files, as a single file is not considered "hardlinked" to anything else in this context.

B. Matrix Population
Row Creation: Each hardlink group is assigned exactly one row in the StringMatrix.

Column Layout:

The number of columns in the row equals the number of files in that group.

If the -l flag is also active (-hl), one additional column is added at the end for the FILE_TYPE.

Wrapping for Final Sort: To satisfy the requirement that "rows must be sorted lexicographically," the function creates a list of RowWrapper structs. These "wrappers" hold the pointers to each row so they can be easily reordered.

C. Final Lexicographical Sort
Sorting the Rows: The function calls qsort on the RowWrapper list. This sorts the entire rows alphabetically based on the string in the first column (the first filename of each group).
*/
StringMatrix* ls_process_entries(Ls* ls, LsEntry* entries, size_t* count) {
    // TODO: Implement ls_process_entries
    StringMatrix* matrix = (StringMatrix*)malloc(sizeof(StringMatrix));
    if (matrix == NULL) {
        return NULL;
    }
    if(count == NULL) return NULL;

    bool hflag = ls->h_;
    if(!hflag)
    {
        //easy wala, no h flag lesser BT.
        matrix->rows = *count;
        int msz1 = sizeof(size_t) * matrix->rows;
        int msz2 = sizeof(char**) * matrix->rows;
        matrix->cols = malloc(msz1);
        matrix->data = malloc(msz2);
        for(int i = 0; i<matrix->rows; i++)
        {
            bool lflag = ls->l_;
            size_t colsize = (lflag) ? 2 : 1; //one extra column for file type and shi
            matrix->cols[i] = colsize;
            int msz3 = sizeof(char*) * colsize;
            matrix->data[i] = malloc(msz3);
            char* temp2 = entries[i].name;
            matrix->data[i][0] = strdup_safe(temp2);
            char *temp = get_file_type_string(entries[i].type);
            if(lflag) matrix->data[i][1] = strdup_safe(temp);
        }
    }
    else
    {
        //now hardlinks wala case -> BTBT

        //grouping logic for hardlinks ->>
        size_t numOfGroups = 0;
        qsort(entries, *count, sizeof(LsEntry), sort_by_id_then_name);
        for(int i = 0; i< (*count);)
        {
            int j = i+1;
            while(j < (*count) && entries[j].inode == entries[i].inode) j++;
            int cnt = j-i;
            if(cnt >= 2) numOfGroups++;
            i = j;
        }

        matrix->rows = numOfGroups;
        matrix->data = malloc(sizeof(char**) * numOfGroups);
        matrix->cols = malloc(sizeof(size_t) * numOfGroups);
        int mallocSize = sizeof(struct RowWrapper) * numOfGroups;
        struct RowWrapper* wrapperss = malloc(mallocSize);
        size_t groupIndex = 0;
        for(int i = 0; i<*count; i)
        {
            size_t j = 1+i;
            while(j < *count && entries[j].inode == entries[i].inode) j++;
            int members = j-i;
            if(members >= 2)
            {
                int totallColumns = members + (ls->l_ ? 1 : 0); //for type
                matrix->cols[groupIndex] = totallColumns;
                int mSize = sizeof(char*) * totallColumns;
                matrix->data[groupIndex] = malloc(mSize);

                for(int k = 0; k<members; k++)
                {
                    matrix->data[groupIndex][k] = strdup_safe(entries[i+k].name);
                }
                bool lflag = ls->l_;
                if(lflag)
                {
                    char *str = get_file_type_string(entries[i].type);
                    matrix->data[groupIndex][members] = strdup_safe(str);
                }
                wrapperss[groupIndex].row_ptr = matrix->data[groupIndex];
                wrapperss[groupIndex].columnCount = totallColumns;
                groupIndex++;
            }
            i = j;
        }
        qsort(wrapperss, numOfGroups, sizeof(struct RowWrapper), sort_matrix_rows);
        for(int r = 0; r < numOfGroups; r++)
        {
            matrix->data[r] = wrapperss[r].row_ptr;
            matrix->cols[r] = wrapperss[r].columnCount;
        }
        free(wrapperss);
    }
    
    // matrix->data = NULL;
    // matrix->rows = 0;
    // matrix->cols = NULL;
    
    return matrix;
}

StringMatrix* ls_run(Ls* ls, const char* path) {
    // TODO: Implement ls_run
    if (ls == NULL || path == NULL) {
        return NULL;
    }
    size_t found = 0;
    LsEntry* rawList = ls_scan_directory(ls, path, &found);
    StringMatrix* ans = ls_process_entries(ls, rawList, &found);
    if(ans) ls_entry_destroy(rawList, found);
    return ans;
    // StringMatrix* result = (StringMatrix*)malloc(sizeof(StringMatrix));
    // if (result == NULL) {
    //     return NULL;
    // }
    
    // result->data = NULL;
    // result->rows = 0;
    // result->cols = NULL;
    
    // ls_print(result);
    // return result;
}

void ls_entry_destroy(LsEntry* entry, size_t count) {
    if (entry == NULL) {
        return;
    }
    
    for (size_t i = 0; i < count; ++i) {
        if (entry[i].name != NULL) {
            free(entry[i].name);
        }
    }
    free(entry);
}

void string_matrix_destroy(StringMatrix* matrix) {
    if (matrix == NULL) {
        return;
    }
    
    if (matrix->data != NULL) {
        for (size_t i = 0; i < matrix->rows; ++i) {
            if (matrix->data[i] != NULL) {
                for (size_t j = 0; j < matrix->cols[i]; ++j) {
                    if (matrix->data[i][j] != NULL) {
                        free(matrix->data[i][j]);
                    }
                }
                free(matrix->data[i]);
            }
        }
        free(matrix->data);
    }
    
    if (matrix->cols != NULL) {
        free(matrix->cols);
    }
    
    free(matrix);
}



/******************************************************************************
 * LS COMMAND IMPLEMENTATION - DOCUMENTATION & ARCHITECTURAL OVERVIEW
 * * This file implements a simplified version of the Linux 'ls' command. 
 * It supports directory traversal, long formatting (-l), hidden files (-a), 
 * and hardlink grouping (-h) across recursive directory structures.
 ******************************************************************************/

/* * === DATA STRUCTURES ===
 * * 1. Ls: 
 * The configuration heart. Stores boolean flags (a_, l_, h_) that 
 * dictate the behavior of every other function in the program.
 * * 2. LsEntry: 
 * A temporary container for raw file data. It holds the relative path (name), 
 * the type symbol (type), and the Inode number (inode) which is essential 
 * for identifying hardlinks.
 * * 3. StringMatrix: 
 * The final output format. A 2D grid of strings. 
 * - data: Triple pointer (char***) representing rows and columns.
 * - rows: Total number of rows in the matrix.
 * - cols: An array storing the number of columns for each specific row.
 * * 4. RowWrapper: 
 * A helper structure used during the hardlink (-h) phase. It allows us to 
 * sort entire rows of the StringMatrix lexicographically using qsort.
 */

/* * === CORE FUNCTION DOCUMENTATION ===
 */

/**
 * combine_paths(base, leaf)
 * Purpose: Safely joins a directory path and a filename with a '/' separator.
 * Parameters:
 * - base: The parent directory string.
 * - leaf: The file or subdirectory name.
 * Returns: A newly allocated string (e.g., "dir/file"). 
 * Logic: It checks if the base string already ends in '/' to avoid double slashes.
 */

/**
 * fetch_inode_id(targetpath)
 * Purpose: Retrieves the unique Inode number for a file from the filesystem.
 * Parameters:
 * - targetpath: The path to the file.
 * Returns: An ino_t value (Inode). Returns 0 if lstat fails.
 * Logic: Hardlinked files share the same Inode; this is the key for the -h flag.
 */

/**
 * deep_crawl(ls, current, list, total, capacity)
 * Purpose: Recursively explores every directory and sub-directory to find files.
 * Parameters:
 * - ls: Configuration flags.
 * - current: The directory currently being explored.
 * - list: A double pointer to the LsEntry array (may be reallocated).
 * - total: Tracks the total count of files found across recursion.
 * - capacity: Tracks the current size limit of the list array.
 * Logic: 
 * - Uses opendir/readdir to find entries.
 * - Filters hidden files based on the -a flag.
 * - Skips "." and ".." to prevent infinite loops.
 * - If it finds a directory, it calls itself (Recursion).
 * - Dynamically grows the LsEntry array using realloc ($2 \times$ growth).
 */

/**
 * ls_scan_directory(ls, path, count)
 * Purpose: The "Data Collection" phase.
 * Parameters:
 * - ls: Configuration flags.
 * - path: The user-provided starting directory.
 * - count: Output parameter to store how many entries were found.
 * Logic:
 * - IF NO -h: Uses scandir() with alphasort to get a sorted list of the immediate folder.
 * - IF -h: Calls deep_crawl() to gather files from the entire tree.
 */

/**
 * ls_process_entries(ls, entries, count)
 * Purpose: The "Formatting Engine." Converts raw LsEntry data into a StringMatrix.
 * Parameters:
 * - ls: Configuration flags.
 * - entries: The raw data array from the scan phase.
 * - count: The number of items in the entries array.
 * Logic:
 * - NORMAL MODE: Creates one row per file. Column 1 is the name, Column 2 (if -l) is the type.
 * - HARDLINK MODE: 
 * 1. Sorts entries by Inode ID so hardlinked files are adjacent.
 * 2. Counts groups where $count \geq 2$.
 * 3. Creates one row per group, containing all paths sharing that Inode.
 * 4. Uses RowWrapper and qsort to sort the final rows alphabetically by the first filename.
 */

/**
 * ls_run(ls, path)
 * Purpose: The "Orchestrator."
 * Parameters:
 * - ls: The configuration struct.
 * - path: The target directory.
 * Logic: Coordinates the workflow: Scan -> Process -> Cleanup raw entries -> Return Matrix.
 */

/**
 * string_matrix_destroy(matrix)
 * Purpose: Deep memory cleanup.
 * Logic: It must free strings, then columns, then the rows array, then the struct itself.
 * Failing to do this in the correct order results in memory leaks.
 */

/* * === FLAG LOGIC SUMMARY ===
 * * -a: "All" - If NOT set, functions like deep_crawl and ls_scan skip files starting with '.'.
 * -l: "Long" - Adds an extra column to the matrix containing "FILE", "DIRECTORY", or "SOFTLINK".
 * -h: "Hardlink" - Triggers recursive scanning and complex grouping logic in the processing phase.
 */

 // ________________________________________________________________________________________________________________________

 /******************************************************************************
 * LS COMMAND IMPLEMENTATION - FULL FUNCTIONAL DOCUMENTATION
 * * This file implements a subset of the POSIX 'ls' utility. It handles 
 * directory traversal, file metadata retrieval, and complex grouping logic 
 * for hardlinks.
 ******************************************************************************/

/**
 * FUNCTION: is_hidden
 * Purpose: Determines if a file should be considered "hidden" by system standards.
 * Parameters:
 * - name: The filename to check.
 * Logic: In Unix-like systems, any file or directory starting with a '.' 
 * (dot) is hidden. This function is used by the scan functions to respect 
 * the presence or absence of the '-a' flag.
 */

/**
 * FUNCTION: strdup_safe
 * Purpose: A memory-safe wrapper for duplicating strings on the heap.
 * Parameters:
 * - str: The source string to copy.
 * Logic: Standard string assignments only copy pointers. To ensure each 
 * StringMatrix cell owns its memory (allowing for safe cleanup), we 
 * allocate a fresh buffer and copy the content.
 */

/**
 * FUNCTION: get_file_type
 * Purpose: Identifies the filesystem nature of an object (File, Directory, or Link).
 * Parameters:
 * - path: The full relative path to the object.
 * Logic: Uses the 'lstat' system call to fetch the 'st_mode' bitmask. 
 * Unlike 'stat', 'lstat' does not follow symbolic links, which is 
 * critical for the '-l' flag to correctly identify 'SOFTLINK' types.
 */

/**
 * FUNCTION: get_file_type_string
 * Purpose: Translates internal type codes into the user-facing labels required by the README.
 * Parameters:
 * - type: A character code ('d', 'l', or 'f').
 * Returns: A static string ("DIRECTORY", "SOFTLINK", or "FILE").
 */

/**
 * FUNCTION: combine_paths
 * Purpose: Joins a parent directory and a filename into a single path string.
 * Parameters:
 * - s1: The base directory.
 * - s2: The file or subdirectory name.
 * Logic: Safely handles the separator by checking if the base already 
 * ends in a '/'. This prevents malformed paths like "dir//file".
 */

/**
 * FUNCTION: fetch_inode_id
 * Purpose: Retrieves the unique Inode (Index Node) identifier for a file.
 * Parameters:
 * - targetpath: The file path.
 * Logic: The Inode is the "DNA" of a file. If two paths have the same 
 * Inode, they point to the same physical data (Hardlinks). This is the 
 * core metric used for the '-h' grouping logic.
 */

/**
 * FUNCTION: deep_crawl (RECURSIVE)
 * Purpose: Performs a full-tree traversal to find all files in subdirectories.
 * Parameters:
 * - ls: The flag configuration (used to check for '-a').
 * - current: The current directory being scanned.
 * - list: A pointer to the LsEntry array (updated as files are found).
 * - total: A pointer to the global counter of discovered files.
 * - capacity: The current size of the allocated 'list' memory.
 * Logic: 
 * - Opens the directory and iterates through entries.
 * - Skips "." and ".." to avoid infinite recursion loops.
 * - If it finds a directory, it calls itself recursively.
 * - It dynamically resizes the 'list' array using 'realloc' when full.
 */

/**
 * FUNCTION: ls_create & ls_create_with_flags
 * Purpose: Constructors for the 'Ls' configuration structure.
 * Parameters: Booleans representing -a, -l, and -h.
 * Logic: Allocates memory for the flag container and initializes state.
 */

/**
 * FUNCTION: ls_print
 * Purpose: Standardized output function for the StringMatrix.
 * Logic: Iterates through rows and columns, printing strings separated 
 * by spaces and rows separated by newlines.
 */

/**
 * FUNCTION: ls_scan_directory
 * Purpose: The "Discovery Phase" of the command.
 * Parameters:
 * - ls: Configuration flags.
 * - path: The target directory.
 * - count: Output parameter for the total number of items found.
 * Logic:
 * - If '-h' is OFF: Uses 'scandir' with 'alphasort' to get a sorted 
 * list of the immediate directory only.
 * - If '-h' is ON: Initializes a large array and calls 'deep_crawl' 
 * to find every file in the entire subdirectory tree.
 */

/**
 * FUNCTION: sort_by_id_then_name & sort_matrix_rows
 * Purpose: Comparators for the 'qsort' function.
 * Logic: 
 * - 'sort_by_id_then_name' clumps files with the same Inode together.
 * - 'sort_matrix_rows' ensures that the final groups in the 
 * StringMatrix are ordered alphabetically by their first filename.
 */

/**
 * FUNCTION: ls_process_entries
 * Purpose: The "Engine" that formats raw data into the final 2D Matrix.
 * Parameters:
 * - entries: The raw array of paths, types, and inodes.
 * - count: Number of raw entries.
 * Logic:
 * - NORMAL: Maps each entry to its own row (1-2 columns).
 * - HARDLINK (-h):
 * 1. Sorts all entries by Inode.
 * 2. Identifies "clumps" of 2+ files with the same Inode.
 * 3. Creates one wide row for each group.
 * 4. Uses 'RowWrapper' to sort these groups alphabetically.
 */

/**
 * FUNCTION: ls_run
 * Purpose: The high-level orchestrator.
 * Logic: Manages the sequence: Scan -> Process -> Cleanup Raw Data -> Return Final Result.
 */

/**
 * FUNCTION: ls_entry_destroy & string_matrix_destroy
 * Purpose: Deep memory cleanup.
 * Logic: Because we use nested pointers (Matrix -> Rows -> Strings), 
 * we must free from the "inside out" (strings first, then row pointers, 
 * then the matrix itself) to prevent memory leaks.
 */