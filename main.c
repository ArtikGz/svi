#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <string.h>
#include "vec.h"

#define CURRENT_LINE (editor->lines.elements[editor->cur_y])

static struct termios termios_original, termios_new;
void termios_raw_mode(void) {
    tcgetattr(0, &termios_original);
    termios_new = termios_original;
    termios_new.c_lflag &= ~(ICANON | ECHO); 
    tcsetattr(0, TCSANOW, &termios_new); 
}

void termios_reset(void) {
    tcsetattr(0, TCSANOW, &termios_original);
}

typedef enum {
    MODE_NORMAL,
    MODE_VISUAL,
    MODE_INSERT
} Mode;

typedef enum {
    DIRECTION_UP,
    DIRECTION_DOWN,
    DIRECTION_RIGHT,
    DIRECTION_LEFT
} Direction;

typedef struct {
    size_t start, end;
} Line;
DEFINE_VEC(Lines, Line);
DEFINE_VEC(Text, char);

typedef struct {
    const char* filename;
    const char* filepath;

    Lines lines;
    Text text;

    size_t cur_x, cur_y;
    size_t display_line;

    bool marked;
    size_t mark_x, mark_y;

    Mode mode;
} Editor;

void set_mark(Editor* editor)  {
    editor->marked = true;
    editor->mark_x = editor->cur_x;
    editor->mark_y = editor->cur_y;
}

typedef enum {
    AR_QUIT_EDITOR,
    AR_NOTHING
} ActionResult;

int last_index_of(const char* string, char c) {
    int found = -1;

    size_t i = 0;
    while (string[i] != '\0') {
        if (string[i] == c) {
            found = i;
        }

        i += 1;
    }

    return found;
}

char* get_filename(char* filepath) {
    int found = last_index_of(filepath, '/');
    
    if (found > 0) {
        return filepath + found + 1;
    } else {
        return filepath;
    }
}

ActionResult editor_save_file(Editor* editor) {
    FILE* file = fopen(editor->filepath, "w");
    
    fwrite(editor->text.elements, sizeof(char), editor->text.count, file);
    
    fclose(file);

    return AR_NOTHING;
}

struct winsize window_size;

void compute_lines(Editor* editor) {
    Lines lines = (Lines){0};
    size_t init = 0;
    for (size_t i = 0; i < editor->text.count; ++i) {
        if (editor->text.elements[i] == '\n' || (editor->text.count - i <= 0)) {
            VEC_APPEND(lines, ((Line) {
                .start = init,
                .end = i
            }));

            init = i + 1;
        }   
    }

    if (lines.count == 0) {
        VEC_APPEND(lines, ((Line) {
            .start = 0,
            .end = 0
        }));
    }

    editor->lines = lines;
}

int editor_read_file(Editor* editor, char* filepath) {
    if (editor == NULL) return -1;

    editor->mode = MODE_NORMAL;
    editor->display_line = 0;

    if (filepath == NULL) {
        editor->lines = (Lines) {0};
        editor->text = (Text) {0};
    } else {
        FILE* file = fopen(filepath, "r");
        if (file == NULL) {
            return -1;
        }
        if (fseek(file, 0, SEEK_END) != 0) {
            return -1;
        }

        long length = ftell(file);
        if (length < 0) {
            return -1;
        }

        if (fseek(file, 0, SEEK_SET) != 0) {
            return -1;
        }

        char* buffer = malloc(length);
        fread(buffer, 1, length, file);
        if (ferror(file)) {
            return -1;
        }

        editor->filepath = filepath;
        editor->filename = get_filename(filepath);
        editor->text.elements = buffer;
        editor->text.count = length;
        editor->text.capacity = length;

        fclose(file);
    }

    compute_lines(editor);

    return 0;
}

void clear_screen() {
    printf("\033[H\033[J");
}

const char* mode_to_string(Mode mode) {
    switch (mode) {
        case MODE_NORMAL: return "-- NORMAL --";
        case MODE_VISUAL: return "-- VISUAL --";
        case MODE_INSERT: return "-- INSERT --";
    }

    return "-- NULL --";
}

bool is_in_selection_range(Editor* editor, size_t current) {
    if (!editor->marked) return false;

    size_t mark = editor->lines.elements[editor->mark_y].start + editor->mark_x;
    size_t cursor = CURRENT_LINE.start + editor->cur_x;
    if (mark >= cursor) {
        return current >= cursor && current <= mark;
    } else {
        return current >= mark && current <= cursor;
    }
}

void editor_render(Editor* editor) {
    size_t from = editor->display_line;
    size_t to = editor->display_line + window_size.ws_row - 2;

    clear_screen();
    bool in_selection = false;
    for (size_t i = from; i < to; ++i) {
        if (i < editor->lines.count) {
            Line line = editor->lines.elements[i];

            for (size_t j = line.start; j < line.end; ++j) {
                if (is_in_selection_range(editor, j) && !in_selection) {
                    in_selection = true;
                    printf("\033[47m\033[30m"); 
                } else if (!is_in_selection_range(editor, j) && in_selection) {
                    in_selection = false; 
                    printf("\033[0m");
                }

                printf("%c", editor->text.elements[j]);
            }
        } else {
            printf("~");
        }
        printf("\n");
    }
    printf("%s [%li;%li] %s\n", mode_to_string(editor->mode), editor->cur_x, editor->cur_y, editor->filename);
        
    printf("\033[%li;%liH", (editor->cur_y - editor->display_line)+1, editor->cur_x+1);

    fflush(stdout);
}

ActionResult editor_move(Editor* editor, Direction direction) {
    switch (direction) {
        case DIRECTION_UP: {
            if (editor->cur_y > 0) editor->cur_y -= 1;
            if (editor->cur_y < editor->display_line) editor->display_line -= 1;
        }; break;
        case DIRECTION_DOWN: {
            if (editor->cur_y < editor->lines.count - 1) editor->cur_y += 1;
            if (editor->cur_y > editor->display_line + window_size.ws_row - 3) editor->display_line += 1;
        }; break;
        case DIRECTION_LEFT: {
            if (editor->cur_x > 0) editor->cur_x -= 1;
        }; break;
        case DIRECTION_RIGHT: {
            if (editor->cur_x < CURRENT_LINE.end - CURRENT_LINE.start) editor->cur_x += 1;
        }; break;
    }

    size_t line_size = CURRENT_LINE.end - CURRENT_LINE.start;
    if (editor->cur_x >= line_size) editor->cur_x = line_size;

    return AR_NOTHING;
}

bool is_alphanumeric(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_');
}

bool is_whitespace(char c) {
    return c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

void editor_advance_char(Editor* editor) {
    size_t line_size = CURRENT_LINE.end - CURRENT_LINE.start;
    if (editor->cur_x == line_size) {
        if (editor->lines.count > editor->cur_y) {
            editor->cur_x = 0;
            editor->cur_y += 1;
        }
    } else {
        editor->cur_x += 1;
    }
}

void editor_backward_char(Editor* editor) {
    if (editor->cur_x == 0) {
        if (editor->cur_y > 0) {
            editor->cur_y -= 1;
            editor->cur_x = CURRENT_LINE.end - CURRENT_LINE.start;
        }
    } else {
        editor->cur_x -= 1;
    }
}

void skip_whitelines(Editor* editor) {
    while (is_whitespace(editor->text.elements[CURRENT_LINE.start + editor->cur_x])) {
        editor_advance_char(editor);
    }
}

void advance_while_alpha(Editor* editor) {
    while (is_alphanumeric(editor->text.elements[CURRENT_LINE.start + editor->cur_x])) {
        editor_advance_char(editor);
    }
}

void skip_whitelines_backward(Editor* editor) {
    while (is_whitespace(editor->text.elements[CURRENT_LINE.start + editor->cur_x - 1])) {
        editor_backward_char(editor);
    }
}

void backward_while_alpha(Editor* editor) {
    while (is_alphanumeric(editor->text.elements[CURRENT_LINE.start + editor->cur_x - 1])) {
        editor_backward_char(editor);
    }
}

ActionResult editor_move_end_word(Editor* editor) {
    skip_whitelines(editor);
    if (is_alphanumeric(editor->text.elements[CURRENT_LINE.start + editor->cur_x])) {
        advance_while_alpha(editor);
    } else {
        editor_advance_char(editor);
    }

    return AR_NOTHING;
}

ActionResult editor_move_begin_word(Editor* editor) {
    skip_whitelines_backward(editor);
    if (is_alphanumeric(editor->text.elements[CURRENT_LINE.start + editor->cur_x - 1])) {
        backward_while_alpha(editor);
    } else {
        editor_backward_char(editor);
    }

    return AR_NOTHING;
}

ActionResult editor_handle_insert(Editor* editor, char action) {
    if (action == 27) {
        editor->mode = MODE_NORMAL;
    } else if (action == '\x7f') {
        if (CURRENT_LINE.start + editor->cur_x > 0) {
            size_t index = CURRENT_LINE.start + editor->cur_x - 1;
            VEC_REMOVE(editor->text, index);
            compute_lines(editor);
            
            if (editor->cur_x == 0) {
                if (editor->cur_y > 0) {
                    Line prev_line = editor->lines.elements[editor->cur_y - 1];
                    editor->cur_x = prev_line.end - prev_line.start;
                }

                editor_move(editor, DIRECTION_UP);
            } else {
                editor_move(editor, DIRECTION_LEFT);
            }
        }
    } else {
        size_t index = CURRENT_LINE.start + editor->cur_x;
        VEC_INSERT(editor->text, index, action);

        compute_lines(editor);
        if (action == '\n') {
            editor_move(editor, DIRECTION_DOWN);
            editor->cur_x = 0;
        } else {
            editor_move(editor, DIRECTION_RIGHT);
        }
    }

    return AR_NOTHING;
}

ActionResult editor_handle_normal(Editor* editor, char action) {
    switch (action) {
        case 'j': return editor_move(editor, DIRECTION_DOWN); 
        case 'k': return editor_move(editor, DIRECTION_UP); 
        case 'l': return editor_move(editor, DIRECTION_RIGHT);
        case 'h': return editor_move(editor, DIRECTION_LEFT); 
        case 'e': return editor_move_end_word(editor);
        case 'b': return editor_move_begin_word(editor);
        case 'w': return editor_save_file(editor);
        case 'o': {
            size_t index = CURRENT_LINE.end;
            VEC_INSERT(editor->text, index, '\n');

            editor->cur_x = 0;
            editor->cur_y += 1;

            editor->mode = MODE_INSERT;
            compute_lines(editor);
        }; break;
        case 'O': {
            size_t index = CURRENT_LINE.start;
            VEC_INSERT(editor->text, index, '\n');

            editor->cur_x = 0;

            editor->mode = MODE_INSERT;
            compute_lines(editor);
        }; break;
        case 'i': {
            editor->mode = MODE_INSERT;
        }; break;
        case 'I': {
            size_t index = CURRENT_LINE.start;
            while (index < editor->text.count && index < CURRENT_LINE.end && is_whitespace(editor->text.elements[index])) {
                index += 1;
            }

            editor->cur_x = index - CURRENT_LINE.start;
            editor->mode = MODE_INSERT;
        }; break;
        case 'A': {
            editor->cur_x = CURRENT_LINE.end - CURRENT_LINE.start;
            editor->mode = MODE_INSERT;
        }; break;
        case 'v': {
            set_mark(editor);
            editor->mode = MODE_VISUAL;
        }; break;
        case 'q': return AR_QUIT_EDITOR; 
    }

    return AR_NOTHING;
}

void editor_remove_selection(Editor* editor) {
    size_t cursor = CURRENT_LINE.start + editor->cur_x;
    size_t mark = editor->lines.elements[editor->mark_y].start + editor->mark_x;
    if (cursor != mark) {
        if (mark > cursor) {
            for (size_t i = mark - 1; i >= cursor; --i) {
                VEC_REMOVE(editor->text, i);

                if (i == 0) break;
            }
        } else {
            for (size_t i = cursor - 1; i >= mark; --i) {
                VEC_REMOVE(editor->text, i);

                if (i == 0) break;
            }
            
            editor->cur_x = editor->mark_x;
            editor->cur_y = editor->mark_y;
        }

        compute_lines(editor);
    }

    editor->marked = false;
}

ActionResult editor_handle_visual(Editor* editor, char action) {
    switch (action) {
        case 'j': return editor_handle_normal(editor, action); 
        case 'k': return editor_handle_normal(editor, action);
        case 'l': return editor_handle_normal(editor, action);
        case 'h': return editor_handle_normal(editor, action); 
        case 'e': return editor_handle_normal(editor, action);
        case 'b': return editor_handle_normal(editor, action);
        case 'd': {
            editor_remove_selection(editor);
            editor->mode = MODE_NORMAL;
        }; break;
        case 'c': {
            editor_remove_selection(editor);
            editor->mode = MODE_INSERT;
        }; break;
        case 27: {
            editor->marked = false;
            editor->mode = MODE_NORMAL;
        }; break;
    }

    return AR_NOTHING;
}

ActionResult editor_handle_action(Editor* editor, char action) {
    switch (editor->mode) {
        case MODE_NORMAL: return editor_handle_normal(editor, action); 
        case MODE_VISUAL: return editor_handle_visual(editor, action);
        case MODE_INSERT: return editor_handle_insert(editor, action);
    }

    return AR_NOTHING;
}


int main(int argc, char** argv) {
    char* filepath;
    if (argc < 2) {
        fprintf(stderr, "Creating new files is not supported yet.\n");
        return 1;
    } else {
        filepath = argv[1];
    }

    Editor editor = {0};
    if (editor_read_file(&editor, filepath) < 0) {
        fprintf(stderr, "Couldn't read file %s\n", filepath);
        return 1;
    }

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size);

    char current;
    int quit = 0;
    termios_raw_mode();
        while (!quit) {
            editor_render(&editor);
            read(0, &current, 1);

            ActionResult result = editor_handle_action(&editor, current);
            switch (result) {
                case AR_QUIT_EDITOR: {
                    quit = 1;
                }; break;
                case AR_NOTHING: break;
            }
        }
    clear_screen();
    termios_reset();
}
