#include "internal.h"

static yed_cell yed_new_cell(char c) {
    yed_cell cell;

    cell.__data = 0;
    cell.c      = c;

    return cell;
}

static yed_line yed_new_line(void) {
    yed_line line;

    memset(&line, 0, sizeof(line));

    line.cells = array_make(yed_cell);

    return line;
}

static void yed_free_line(yed_line *line) {
    array_free(line->cells);
}

static void yed_line_add_cell(yed_line *line, yed_cell *cell, int idx) {
    array_insert(line->cells, idx, *cell);
    line->visual_width += 1;
}

static void yed_line_append_cell(yed_line *line, yed_cell *cell) {
    array_push(line->cells, *cell);
    line->visual_width += 1;
}

static void yed_line_delete_cell(yed_line *line, int idx) {
    line->visual_width -= 1;
    array_delete(line->cells, idx);
}

static void yed_line_pop_cell(yed_line *line) {
    yed_line_delete_cell(line, array_len(line->cells) - 1);
}

static yed_line * yed_buffer_add_line(yed_buffer *buff) {
    yed_line new_line,
             *line;

    new_line = yed_new_line();

    line = bucket_array_push(buff->lines, new_line);

    yed_mark_dirty_frames(buff);

    return line;
}

static yed_buffer yed_new_buff(void) {
    yed_buffer  buff;

    buff.lines = bucket_array_make(yed_line);
	buff.path   = NULL;

    yed_buffer_add_line(&buff);

    return buff;
}

static void yed_append_to_line(yed_line *line, char c) {
    yed_cell cell;

    cell = yed_new_cell(c);
    yed_line_append_cell(line, &cell);
}

static void yed_append_to_buff(yed_buffer *buff, char c) {
    yed_line *line;

    if (c == '\n') {
        yed_buffer_add_line(buff);
    } else {
        if (bucket_array_len(buff->lines) == 0) {
            line = yed_buffer_add_line(buff);
        } else {
            line = bucket_array_last(buff->lines);
        }

        yed_append_to_line(line, c);
    }
}


static int yed_line_col_to_cell_idx(yed_line *line, int col) {
    int       found;
    int       cell_idx;
    yed_cell *cell_it;

    if (col == array_len(line->cells) + 1) {
        return col - 1;
    } else if (col == 1 && array_len(line->cells) == 0) {
        return 0;
    }

    cell_idx = 0;
    found    = 0;

    array_traverse(line->cells, cell_it) {
        if (col - 1 <= 0) {
            found = 1;
            break;
        }
        col      -= 1;
        cell_idx += 1;
    }

    if (!found) {
        ASSERT(0, "didn't compute a good cell idx");
        return -1;
    }

    return cell_idx;
}

static yed_cell * yed_line_col_to_cell(yed_line *line, int col) {
    int idx;

    idx = yed_line_col_to_cell_idx(line, col);

    if (idx == -1) {
        return NULL;
    }

    return array_item(line->cells, idx);
}

static void yed_line_clear(yed_line *line) {
    array_clear(line->cells);
    line->visual_width = 0;
}

static yed_line * yed_buff_get_line(yed_buffer *buff, int row) {
    int idx;

    idx = row - 1;

    if (idx < 0 || idx >= bucket_array_len(buff->lines)) {
        return NULL;
    }

    return bucket_array_item(buff->lines, idx);
}

static yed_line * yed_buff_insert_line(yed_buffer *buff, int row) {
    int      idx;
    yed_line new_line, *line;

    idx = row - 1;

    if (idx < 0 || idx > bucket_array_len(buff->lines)) {
        return NULL;
    }

    new_line = yed_new_line();
    line     = bucket_array_insert(buff->lines, idx, new_line);

    yed_mark_dirty_frames(buff);

    return line;
}

static void yed_buff_delete_line(yed_buffer *buff, int row) {
    int       idx;
    yed_line *line;

    idx = row - 1;

    LIMIT(idx, 0, bucket_array_len(buff->lines));

    line = yed_buff_get_line(buff, row);
    yed_free_line(line);
    bucket_array_delete(buff->lines, idx);

    yed_mark_dirty_frames(buff);
}

static void yed_insert_into_line(yed_buffer *buff, int row, int col, char c) {
    int       idx;
    yed_line *line;
    yed_cell  cell;

    line = yed_buff_get_line(buff, row);

    idx = col - 1;

    LIMIT(idx, 0, line->visual_width);

    cell = yed_new_cell(c);
    idx  = yed_line_col_to_cell_idx(line, col);
    yed_line_add_cell(line, &cell, idx);

    yed_mark_dirty_frames_line(buff, row);
}

static void yed_delete_from_line(yed_buffer *buff, int row, int col) {
    int       idx;
    yed_line *line;

    line = yed_buff_get_line(buff, row);

    idx = col - 1;

    LIMIT(idx, 0, line->visual_width);

    idx = yed_line_col_to_cell_idx(line, col);
    yed_line_delete_cell(line, idx);

    yed_mark_dirty_frames_line(buff, row);
}


static void yed_fill_buff_from_file(yed_buffer *buff, const char *path) {
    FILE        *f;
    int          fd, i, file_size;
    struct stat  fs;
    char        *file_data;
    yed_line    *last_line;

    f = fopen(path, "r");
    if (!f) {
        ERR("unable to open file");
    }

    fd = fileno(f);

    if (fstat(fd, &fs) != 0) {
        ERR("unable to stat file");
    }

    file_size = fs.st_size;
    file_data = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);

    if (file_data == MAP_FAILED) {
        ERR("mmap failed");
    }

    for (i = 0; i < file_size; i += 1) {
        yed_append_to_buff(buff, file_data[i]);
    }

    munmap(file_data, file_size);

    if (bucket_array_len(buff->lines) > 1) {
        last_line = bucket_array_last(buff->lines);
        if (array_len(last_line->cells) == 0) {
            bucket_array_pop(buff->lines);
        }
    }

	buff->path = strdup(path);

    fclose(f);

    yed_mark_dirty_frames(buff);
}

static void yed_write_buff_to_file(yed_buffer *buff, const char *path) {
    FILE     *f;
    yed_line *line;
    yed_cell *cell;

    f = fopen(path, "w");
    if (!f) {
        ERR("unable to open file");
        return;
    }

    bucket_array_traverse(buff->lines, line) {
        array_traverse(line->cells, cell) {
            fwrite(&cell->c, 1, 1, f);
        }
        fprintf(f, "\n");
    }

    fclose(f);
}
