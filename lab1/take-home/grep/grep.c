#include "grep.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Destroy GrepOptions and free memory
void grep_options_destroy(GrepOptions* opts) {
    if (opts == NULL) {
        return;
    }
    // printf("hi1\n");
    if (opts->pattern != NULL) {
        // printf("hi1.1.1\n");
        free(opts->pattern);
    }
    // printf("hi1.1\n");
    
    if (opts->paths != NULL) {
        for (size_t i = 0; i < opts->path_count; ++i) {
            if (opts->paths[i] != NULL) {
                free(opts->paths[i]);
            }
        }
        free(opts->paths);
    }
    // printf("hi2\n");
    
    free(opts);
    // printf("hi3\n");
}

// Pattern matching function
bool grep_match_pattern(const char* pattern, const char* text, bool case_insensitive) {
    if (pattern == NULL || text == NULL) {
        return false;
    }
    
    if (case_insensitive) {
        // Case-insensitive matching: convert both strings to lowercase for comparison
        size_t pattern_len = strlen(pattern);
        size_t text_len = strlen(text);
        
        if (pattern_len > text_len) {
            return false;
        }
        
        for (size_t i = 0; i <= text_len - pattern_len; i++) {
            bool match = true;
            for (size_t j = 0; j < pattern_len; j++) {
                if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)pattern[j])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    } else {
        // Case-sensitive matching: use strstr
        return strstr(text, pattern) != NULL;
    }
}

// Search for pattern in a single file
GrepResult* grep_search_file(GrepOptions* opts, const char* filename) {
    // TODO: Implement file search
    // Read file line by line, match pattern against each line, and collect matches
    // Handle line numbers and invert match flags
    if(opts == NULL || filename == NULL) return NULL;
    FILE *fp = fopen(filename, "r");
    if(fp == NULL) return NULL;

    
    GrepResult* result = (GrepResult*)malloc(sizeof(GrepResult));
    if (result == NULL) {
        fclose(fp);
        return NULL;
    }
    
    result->capacity = 10;
    result->count = 0;
    result->matches = (GrepMatch*)malloc(sizeof(GrepMatch) * result->capacity);
    if(result->matches == NULL)
    {
        free(result);
        fclose(fp);
        return NULL;
    }

    char line[4096];
    int lineNumber = 0;
    while(fgets(line, sizeof(line), fp) != NULL)
    {
        lineNumber++;
        line[strcspn(line, "\r\n")] = '\0'; //remove trailing newlines
        // bool haha1 = (opts->pattern == NULL);
        // printf("printing haha1 = %d\n", haha1);
        bool isMatch = grep_match_pattern(opts->pattern, line, opts->case_insensitive);
        if(opts->invert_match)
        {
            isMatch = !isMatch;
        }
        if (isMatch)
        {
            if(result->count >= result->capacity)
            {
                result->capacity *= 2;
                GrepMatch* temp = (GrepMatch*) realloc(result->matches, result->capacity);
                if(temp == NULL) break;
                result->matches = temp;
            } //vector imnplementation type thing
            GrepMatch* match = &result->matches[result->count]; //latest match
            match->filename = strdup(filename);
            match->line_number = opts->line_number ? lineNumber : 0;
            match->line_content = strdup(line);
            result->count++;
        }
    }
    fclose(fp);
    return result;
}

// Print search results
void grep_print_results(GrepResult* result) {
    if (result == NULL || result->matches == NULL) {
        return;
    }
    
    for (size_t i = 0; i < result->count; ++i) {
        GrepMatch* match = &result->matches[i];
        
        if (match->filename != NULL) {
            printf("%s", match->filename);
        }
        
        if (match->line_number > 0) {
            printf(":%d", match->line_number);
        }
        
        if (match->line_content != NULL) {
            printf(":%s", match->line_content);
        }
        
        printf("\n");
    }
}

// Destroy GrepResult and free memory
void grep_result_destroy(GrepResult* result) {
    if (result == NULL) {
        return;
    }
    
    if (result->matches != NULL) {
        for (size_t i = 0; i < result->count; ++i) {
            if (result->matches[i].filename != NULL) {
                free(result->matches[i].filename);
            }
            if (result->matches[i].line_content != NULL) {
                free(result->matches[i].line_content);
            }
        }
        free(result->matches);
    }
    
    free(result);
}

/******************************************************************************
 * GREP UTILITY - EXHAUSTIVE TECHNICAL DOCUMENTATION
 * * This file implements the core logic for a C-based 'grep' utility. It handles
 * pattern matching, file scanning, dynamic memory management for results, 
 * and support for flags such as case-insensitivity and inverted matching.
 *****************************************************************************/

/**
 * STRUCTURE DEFINITIONS (Defined in grep.h)
 * ========================================
 * * 1. GrepOptions
 * - pattern: (char*) The string/substring to search for.
 * - recursive: (bool) Flag for -r; indicates if directories should be searched.
 * - case_insensitive: (bool) Flag for -i; if true, 'A' matches 'a'.
 * - line_number: (bool) Flag for -n; if true, records the 1-indexed line position.
 * - invert_match: (bool) Flag for -v; if true, returns lines NOT containing the pattern.
 * - paths: (char**) An array of strings representing file or directory paths to scan.
 * - path_count: (size_t) The total number of paths stored in the paths array.
 * * 2. GrepMatch
 * - filename: (char*) Deep copy of the name of the file where the match occurred.
 * - line_number: (int) The specific line number of the match (0 if -n is disabled).
 * - line_content: (char*) Deep copy of the actual text of the matching line.
 * * 3. GrepResult
 * - matches: (GrepMatch*) A dynamic array (heap-allocated) of individual match objects.
 * - count: (size_t) The current number of matches successfully stored.
 * - capacity: (size_t) The total allocated slots in the matches array (used for resizing).
 */

/**
 * FUNCTION: grep_options_destroy
 * ------------------------------
 * Deallocates all heap memory associated with the GrepOptions configuration.
 * * @param opts: A pointer to the GrepOptions struct to be destroyed.
 * * How it works:
 * 1. Safety Check: If opts is NULL, returns immediately to avoid segmentation faults.
 * 2. Pattern Cleanup: Frees the memory allocated for the search pattern string.
 * 3. Paths Cleanup: Iterates through the 'paths' array from 0 to path_count. It 
 * frees each individual string path before freeing the array pointer itself.
 * 4. Final Cleanup: Frees the actual GrepOptions structure pointer.
 */

/**
 * FUNCTION: grep_match_pattern
 * ----------------------------
 * The primary logic engine for determining if a line of text satisfies the 
 * search criteria.
 * * @param pattern: The substring sought by the user.
 * @param text: The line of text currently being scanned from a file.
 * @param case_insensitive: Boolean determining the comparison method.
 * * @return: Returns true if a match is found; otherwise false.
 * * How it works:
 * 1. Case-Sensitive: Directly utilizes the standard 'strstr()' function to 
 * find the first occurrence of pattern in text.
 * 2. Case-Insensitive: 
 * - Checks if pattern length is greater than text length (impossible match).
 * - Uses a nested loop (sliding window). The outer loop moves through the 
 * text, and the inner loop compares characters using 'tolower()'.
 * - If every character in the window matches the pattern when converted 
 * to lowercase, it returns true.
 */

/**
 * FUNCTION: grep_search_file
 * --------------------------
 * Opens a specific file and performs a line-by-line search based on user options.
 * * @param opts: Pointer to GrepOptions containing flags (-i, -v, -n) and the pattern.
 * @param filename: String representing the path to the file on disk.
 * * @return: A pointer to a GrepResult struct containing all matches found in the file.
 * * How it works:
 * 1. Initialization: Opens the file via 'fopen'. If it fails, returns NULL.
 * 2. Result Setup: Allocates a GrepResult struct on the heap and initializes 
 * an array of GrepMatch objects with an initial capacity of 10.
 * 3. Line Processing:
 * - Reads lines using 'fgets' with a 4096-byte buffer.
 * - Increments the line counter.
 * - Uses 'strcspn' to find and strip trailing newline characters (\r or \n).
 * 4. Matching Logic:
 * - Calls 'grep_match_pattern'.
 * - Inversion: If 'invert_match' (-v) is true, it flips the result of the match.
 * 5. Dynamic Storage:
 * - If a match is found, it checks if the 'matches' array is full.
 * - If full, it uses 'realloc' to double the capacity (Vector-like growth).
 * - Uses 'strdup' to create independent copies of the filename and line 
 * content to ensure the data survives after the file is closed.
 * 6. Finalization: Closes the file pointer and returns the result collection.
 */

/**
 * FUNCTION: grep_print_results
 * ----------------------------
 * Formats and outputs the discovered matches to the standard output (stdout).
 * * @param result: A pointer to the GrepResult struct containing the match list.
 * * How it works:
 * 1. Iterates through the 'matches' array from index 0 to 'count'.
 * 2. Prints the filename.
 * 3. Conditional Line Numbers: If 'line_number' is greater than 0 (set during 
 * search if -n was active), it prints ":[number]".
 * 4. Prints the content of the line after a colon separator.
 */

/**
 * FUNCTION: grep_result_destroy
 * -----------------------------
 * Performs a deep-free of the GrepResult structure and all its internal strings.
 * * @param result: A pointer to the GrepResult struct to be destroyed.
 * * How it works:
 * 1. Safety Check: If result is NULL, returns immediately.
 * 2. Match Cleanup: Iterates through the matches array. For every match, it 
 * must free 'filename' and 'line_content' because they were allocated 
 * using 'strdup' in the search function.
 * 3. Pointer Cleanup: Frees the 'matches' array pointer itself.
 * 4. Result Cleanup: Frees the main GrepResult structure pointer.
 */