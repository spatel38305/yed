void yed_init_log(void) {
    ys->log_name_stack = array_make(char*);

    LOG_FN_ENTER();
    yed_log("logging initialized");
    LOG_EXIT();
}

void yed__log_prints(char *s, int len) {
    yed_buffer *buff;
    int         row, i;
    yed_glyph   g;

    buff = yed_get_log_buffer();
    row  = yed_buff_n_lines(buff);

    buff->flags &= ~BUFF_RD_ONLY;

    for (i = 0; i < len; i += 1) {
        g.c = s[i];
        if (g.c == '\n') {
            row = yed_buffer_add_line_no_undo(buff);
        } else {
            yed_append_to_line_no_undo(buff, row, g);
        }
    }

    buff->flags |= BUFF_RD_ONLY;

    yed_mark_dirty_frames(buff);
}

const char *yed_top_log_name(void) {
    const char **top_p;

    top_p = (const char**)array_last(ys->log_name_stack);
    if (top_p == NULL) { return "???"; }

    return *top_p;
}

static int in_log;

int yed_vlog(char *fmt, va_list args) {
    char        tm_buff[128], nm_tm_buff[512], buff[1024];
    time_t      t;
    struct tm  *tm;
    const char *log_name, *header_fmt;
    int         len, new_header;

    if (in_log) { return 1; }

    in_log = 1;

    log_name   = yed_top_log_name();
    new_header = 0;

    /* If we don't have the names, do the new header. */
    if (!log_name || !ys->cur_log_name || strcmp(log_name, "???") == 0) {
        new_header = 1;
    }

    /* Or if there's a mismatch. */
    if (!new_header && strcmp(log_name, ys->cur_log_name) != 0) {
        new_header = 1;
    }

    if (new_header) {
        ys->cur_log_name = log_name;

        if (!log_name) { log_name = "???"; }

        t  = time(NULL);
        tm = localtime(&t);
        strftime(tm_buff, sizeof(tm_buff), "%D %I:%M:%S", tm);

        if (yed_buff_n_lines(yed_get_log_buffer()) == 1
        &&  yed_buff_get_line(yed_get_log_buffer(), 1)->visual_width == 0) {
            header_fmt = "[%s](%s) ";
        } else {
            header_fmt = "\n[%s](%s) ";
        }

        len = snprintf(nm_tm_buff, sizeof(nm_tm_buff), header_fmt, tm_buff, log_name);

        if (len > sizeof(nm_tm_buff) - 1) {
            len = sizeof(nm_tm_buff) - 1;
        }

        yed__log_prints(nm_tm_buff, len);
    }

    len = vsnprintf(buff, sizeof(buff), fmt, args);

    if (len > sizeof(buff) - 1) {
        len = sizeof(buff) - 1;
    }

    if (new_header && len && buff[0] == '\n') {
        yed__log_prints(buff + 1, len - 1);
    } else {
        yed__log_prints(buff, len);
    }

    in_log = 0;

    return new_header;
}

int yed_log(char *fmt, ...) {
    va_list va;
    int     r;

    va_start(va, fmt);
    r = yed_vlog(fmt, va);
    va_end(va);

    return r;
}

void yed_log_buff_mod_handler(yed_event *event) {
    yed_frame **fit;

    if (event->buffer != yed_get_log_buffer()) { return; }

    array_traverse(ys->frames, fit) {
        if ((*fit) != ys->active_frame
        &&  (*fit)->buffer == yed_get_log_buffer()) {
            yed_set_cursor_far_within_frame((*fit), yed_buff_n_lines(yed_get_log_buffer()), 1);
            (*fit)->dirty = 1;
        }
    }

}
