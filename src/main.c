/**
 * Copyright (C) 2024 Devin Rockwell
 *
 * This file is part of ocean.
 *
 * ocean is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ocean is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ocean.  If not, see <https://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define VERSION "0.1"
#define TABSTOP 2

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorInsertRow(int at, char *s, size_t len);

enum EditorHiglightType
{
    HL_NORMAL,
    HL_MATCH,
    HL_SELECT = 1 << 7
};

typedef struct
{
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
} Erow;

typedef enum
{
    NORMAL,
    INSERT,
    VISUAL_CHAR
} Mode;

typedef struct
{
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int numrows;
    Erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    Mode mode;
    int selection_x, selection_y;
    int buffer_size;
    char *copy_buffer;
} Editor;

Editor E;

void die(const char *s)
{
    perror(s);
    exit(1);
}

void editorUpdateSyntax(Erow *row)
{
    int i;

    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    for (i = 0; i < row->rsize; i++)
    {
    }
}

int editorRowCxToRx(Erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
        {
            rx += (TABSTOP - 1) - (rx % TABSTOP);
        }
        rx++;
    }
    return rx;
}

int editorRowRxToCx(Erow *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)
    {
        if (row->chars[cx] == '\t')
        {
            cur_rx += (TABSTOP - 1) - (cur_rx % TABSTOP);
        }
        cur_rx++;
        if (cur_rx > rx)
        {
            return cx;
        }
    }
    return cx;
}

void editorUpdateRow(Erow *row)
{
    int tabs = 0;
    int j;
    int idx;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            tabs++;
        }
    }
    free(row->render);
    row->render = malloc(row->size + tabs * (TABSTOP - 1) + 1);
    idx = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % TABSTOP != 0)
            {
                row->render[idx++] = ' ';
            }
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorRowInsertChar(Erow *row, int at, int c)
{
    if (at < 0 || at > row->size)
    {
        at = row->size;
    }
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(Erow *row, char *s, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(Erow *row, int at)
{
    if (at < 0 || at >= row->size)
    {
        return;
    }
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorInsertChar(int c)
{
    if (E.cy == E.numrows)
    {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertRow(int at, char *s, size_t len)
{
    if (at < 0 || at > E.numrows)
    {
        return;
    }

    E.row = realloc(E.row, sizeof(Erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(Erow) * (E.numrows - at));
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorFreeRow(Erow *row)
{
    free(row->render);
    free(row->chars);
    free(row->hl);
}
void editorDelRow(int at)
{
    if (at < 0 || at >= E.numrows)
    {
        return;
    }
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(Erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorDelChar(void)
{
    Erow *row;

    if (E.cy == E.numrows)
    {
        return;
    }
    if (E.cx == 0 && E.cy == 0)
    {
        return;
    }
    row = &E.row[E.cy];
    if (E.cx > 0)
    {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    }
    else
    {
        E.cx = E.row[E.cy].size ? E.row[E.cy].size - 1 : 0;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

void editorInsertNewline(void)
{
    if (E.cx == 0)
    {
        editorInsertRow(E.cy, "", 0);
    }
    else
    {
        Erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

char *editorRowsToString(unsigned int *buflen)
{
    char *buf;
    char *p;
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
    {
        totlen += E.row[j].size + 1;
    }
    *buflen = totlen;
    buf = malloc(totlen);
    p = buf;
    for (j = 0; j < E.numrows; j++)
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorOpen(char *filename)
{
    FILE *fp = fopen(filename, "r");
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    free(E.filename);
    E.filename = strdup(filename);

    if (!fp)
    {
        die("fopen");
    }

    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while (linelen > 0 &&
               (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        {
            linelen--;
        }
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave(void)
{
    unsigned int len;
    FILE *fp;
    char *buf;

    if (!E.filename)
    {
        E.filename = editorPrompt("Save as %s", NULL);
        if (!E.filename)
        {
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    buf = editorRowsToString(&len);
    fp = fopen(E.filename, "w");
    if (!fp)
    {
        die("fopen");
    }
    if (fwrite(buf, sizeof(char), len, fp) == len)
    {
        editorSetStatusMessage("%d bytes written to disk", len);
        E.dirty = 0;
    }
    else
    {
        editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
    }

    fclose(fp);
    free(buf);
}

void editorFindCallback(char *query, int key)
{
    int current;
    int i;
    static int last_match = -1;
    static int direction = 1;

    static int save_hl_line;
    static char *saved_hl = NULL;

    if (saved_hl)
    {
        memcpy(E.row[save_hl_line].hl, saved_hl, E.row[save_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b')
    {
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == KEY_RIGHT || key == KEY_DOWN)
    {
        direction = 1;
    }
    else if (key == KEY_LEFT || key == KEY_UP)
    {
        direction = -1;
    }
    else
    {
        last_match = -1;
        direction = 1;
    }
    if (last_match == -1)
    {
        direction = 1;
    }
    current = last_match;
    for (i = 0; i < E.numrows; i++)
    {
        Erow *row;
        char *match;

        current += direction;
        if (current == -1)
        {
            current = E.numrows - 1;
        }
        else if (current == E.numrows)
        {
            current = 0;
        }
        row = &E.row[current];
        match = strstr(row->render, query);
        if (match)
        {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;
            saved_hl = malloc(row->rsize);
            save_hl_line = current;
            memcpy(saved_hl, row->hl, row->rsize);

            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind(void)
{
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    char *query = editorPrompt("/%s", editorFindCallback);

    if (query)
    {
        free(query);
    }
    else
    {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

void init(void)
{
    initscr();
    start_color();
    noecho();
    raw();
    nonl();
    keypad(stdscr, TRUE);
    timeout(300);
    ESCDELAY = 10;

    /* setup color pairs */
    init_pair(HL_SELECT, COLOR_BLACK, COLOR_WHITE);
    init_pair(HL_MATCH, COLOR_WHITE, COLOR_BLUE);

    /* initialize global editor */
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.screenrows = LINES - 2;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.mode = NORMAL;
    E.selection_x = 0;
    E.selection_y = 0;
    E.buffer_size = 80;
    E.copy_buffer = malloc(E.buffer_size);
    *E.copy_buffer = '\0';
}

char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';

    while (1)
    {
        int c;

        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        c = getch();
        if (c == KEY_DC || c == KEY_BACKSPACE || c == CTRL_KEY('h') || c == 127)
        {
            if (buflen != 0)
            {
                buf[--buflen] = '\0';
            }
            else
            {
                editorSetStatusMessage("");
                free(buf);
                return NULL;
            }
        }
        else if (c == KEY_EXIT || c == 27)
        {
            editorSetStatusMessage("");
            if (callback)
            {
                callback(buf, c);
            }
            free(buf);
            return NULL;
        }
        else if (c == '\r')
        {
            if (buflen != 0)
            {
                editorSetStatusMessage("");
                if (callback)
                {
                    callback(buf, c);
                }
                return buf;
            }
        }
        else if (c == -1)
        {
            continue;
        }
        else if (!iscntrl(c) && c < 128)
        {
            if (buflen == bufsize - 1)
            {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if (callback)
        {
            callback(buf, c);
        }
    }
}

void editorMoveCursor(int key)
{
    Erow row;
    if (!E.numrows)
    {
        return;
    }
    row = E.row[E.cy];
    switch (key)
    {
    case 'h':
        if (E.cx != 0)
        {
            E.cx--;
        }
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size ? E.row[E.cy].size - 1 : 0;
        }
        break;
    case 'l':
        if (E.cx < (E.row[E.cy].size ? E.row[E.cy].size - 1 : 0))
        {
            E.cx++;
        }
        else if (E.cx == (E.row[E.cy].size ? E.row[E.cy].size - 1 : 0))
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case 'k':
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;
    case 'j':
        if (E.cy < E.numrows - 1)
        {
            E.cy++;
        }
        break;
    }

    row = E.row[E.cy];
    if (E.cx > (row.size ? row.size - 1 : 0))
    {
        E.cx = row.size ? row.size - 1 : 0;
    }
}

void editorProcessKeypressNormal(int c)
{
    switch (c)
    {
    case 'h':
    case 'j':
    case 'k':
    case 'l':
        editorMoveCursor(c);
        break;
    case 'q':
        clear();
        endwin();
        exit(0);
        break;
    case 'i':
        E.mode = INSERT;
        break;
    case 'a':
        if (E.numrows)
        {
            E.cx++;
        }
        E.mode = INSERT;
        break;
    case 'A':
        if (E.numrows)
        {
            E.cx = E.row[E.cy].size;
        }
        E.mode = INSERT;
        break;
    case 'o':
        if (!E.numrows)
        {
            break;
        }
        editorInsertRow(E.cy + 1, "", 0);
        E.cy++;
        E.cx = 0;
        E.mode = INSERT;
        break;
    case 'O':
        if (!E.numrows)
        {
            break;
        }
        editorInsertRow(E.cy, "", 0);
        E.cy = E.cy ? E.cy-- : 0;
        E.cx = 0;
        E.mode = INSERT;
        break;
    case 'x':
        if (!E.numrows)
        {
            break;
        }

        E.copy_buffer[0] = E.row[E.cy].chars[E.cx];
        E.copy_buffer[1] = '\0';
        E.cx++;

        editorSetStatusMessage("Copied %c", E.copy_buffer[0]);

        editorDelChar();
        break;
    case 'd':
    {
        Erow row;
        int c2 = getch();
        if (c2 == 'd')
        {
            if (E.numrows == 0)
            {
                break;
            }
            editorDelRow(E.cy);
            if (E.numrows != 0)
            {
                row = E.row[E.cy];
                if (E.cx > (row.size ? row.size - 1 : 0))
                {
                    E.cx = row.size ? row.size - 1 : 0;
                }
            }
            else
            {
                E.cx = 0;
            }
            editorRefreshScreen();
        }
        else
        {
            editorProcessKeypressNormal(c2);
        }
    }
    break;
    case '/':
        editorFind();
        break;
    case 'v':
        E.mode = VISUAL_CHAR;
        E.selection_x = E.cx;
        E.selection_y = E.cy;
        break;
    case 'p':
    {
        int i;
        for (i = 0; E.copy_buffer[i] != '\0'; i++)
        {
            editorInsertChar(E.copy_buffer[i]);
        }
    }
    }
}

void editorProcessKeypressInsert(int c)
{
    switch (c)
    {
    case CTRL_KEY('q'):
        clear();
        endwin();
        exit(0);
        break;
    case KEY_NPAGE:
    case KEY_PPAGE:
    {
        int times;
        if (c == KEY_PPAGE)
        {
            E.cy = E.rowoff;
        }
        else if (c == KEY_NPAGE)
        {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows)
            {
                E.cy = E.numrows;
            }
        }
        times = E.screenrows;
        while (times--)
        {
            c == KEY_PPAGE ? editorMoveCursor(KEY_UP) :
                             editorMoveCursor(KEY_DOWN);
        }
    }
    case KEY_HOME:
        E.cx = 0;
        break;
    case KEY_END:
        E.cx = E.row[E.cy].size ? E.row[E.cy].size - 1 : 0;
        break;
    case CTRL_KEY('f'):
        editorFind();
        break;
    case KEY_RESIZE:
        E.screenrows = LINES - 1;
        break;
    case ERR:
        break;
    case '\r':
    case KEY_ENTER:
        editorInsertNewline();
        break;
    case KEY_BACKSPACE:
    case 127:
    case CTRL_KEY('h'):
    case KEY_DC:
        if (c == KEY_DC)
        {
            editorMoveCursor(KEY_RIGHT);
        }
        editorDelChar();
        break;
    case CTRL_KEY('l'):
        break;
    case KEY_EXIT:
    case 27:
        break;
    case CTRL_KEY('s'):
        editorSave();
        break;
    case '\t':
    {
        int i;
        for (i = 0; i < TABSTOP; i++)
        {
            editorInsertChar(' ');
        }
        break;
    }
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_DOWN:
    case KEY_UP:
        break;
    case 'j':
    {
        int c2 = getch();
        if (c2 == 'k')
        {
            E.mode = NORMAL;
        }
        else
        {
            editorInsertChar(c);
            editorRefreshScreen();
            editorProcessKeypressInsert(c2);
        }
        break;
    }
    default:
        editorInsertChar(c);
        break;
    }
}

void editorProcessKeypressVisualChar(int c)
{
    switch (c)
    {
    case 'h':
        editorMoveCursor(c);
        break;
    case 'k':
        editorMoveCursor(c);
        break;
    case 'l':
        editorMoveCursor(c);
        break;
    case 'j':
    {
        int c2 = getch();
        if (c2 == 'k')
        {
            E.mode = NORMAL;
        }
        else
        {
            editorMoveCursor('j');
            editorRefreshScreen();
            editorProcessKeypressVisualChar(c2);
        }
        break;
    }
    }
}

void editorProcessKeypress(void)
{
    int c = getch();
    switch (E.mode)
    {
    case NORMAL:
        editorProcessKeypressNormal(c);
        break;
    case INSERT:
        editorProcessKeypressInsert(c);
        break;
    case VISUAL_CHAR:
        editorProcessKeypressVisualChar(c);
        break;
    }
}

void editorDrawRows(void)
{
    int y;

    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[40];
                int welcomeLen = snprintf(
                    welcome,
                    sizeof(welcome),
                    "Ocean editor -- version %s",
                    VERSION
                );
                int padding = (COLS - welcomeLen) / 2;
                if (padding)
                {
                    mvprintw(y, 0, "~");
                }
                mvprintw(y, padding + 1, "%s", welcome);
            }
            else
            {
                mvprintw(y, 0, "~");
            }
        }
        else
        {
            char *c = &E.row[filerow].render[E.coloff];
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            int current_color = HL_NORMAL;
            int j;
            int len = E.row[filerow].rsize - E.coloff;

            if (len < 0)
            {
                len = 0;
            }
            if (len > COLS)
            {
                len = COLS;
            }

            for (j = 0; j < len; j++)
            {
                int selection_start_y =
                    E.selection_y < E.cy ? E.selection_y : E.cy;
                int selection_end_y =
                    E.selection_y > E.cy ? E.selection_y : E.cy;
                int selection_start_x =
                    E.selection_x < E.cx ? E.selection_x : E.cx + E.coloff;
                int selection_end_x =
                    E.selection_x > E.cx ? E.selection_x : E.cx + E.coloff;
                if (E.mode == VISUAL_CHAR &&
                    (selection_start_y == selection_end_y &&
                     y == selection_start_y && j >= selection_start_x &&
                     j <= selection_end_x))
                {
                    attroff(COLOR_PAIR(current_color));
                    current_color = HL_SELECT;
                    attron(COLOR_PAIR(current_color));
                }
                else if (E.mode == VISUAL_CHAR &&
                         selection_start_y != selection_end_y &&
                         ((y == selection_start_y && j >= selection_start_x) ||
                          (y == selection_end_y && j <= selection_end_x) ||
                          (y > selection_start_y && y < selection_end_y)))
                {
                    attroff(COLOR_PAIR(current_color));
                    current_color = HL_SELECT;
                    attron(COLOR_PAIR(current_color));
                }
                else if (hl[j] != current_color)
                {
                    attroff(COLOR_PAIR(current_color));
                    current_color = hl[j];
                    attron(COLOR_PAIR(current_color));
                }
                mvaddch(y, j, c[j]);
            }

            attroff(COLOR_PAIR(current_color));
        }
    }
}

void editorScroll(void)
{
    E.rx = 0;
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff)
    {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + COLS)
    {
        E.coloff = E.rx - COLS + 1;
    }
}

void editorDrawStatusBar(void)
{
    char status[80], rstatus[80], mode[20];
    int len, rlen;
    switch (E.mode)
    {
    case NORMAL:
        snprintf(mode, sizeof(mode), "NORMAL");
        break;
    case INSERT:
        snprintf(mode, sizeof(mode), "INSERT");
        break;
    case VISUAL_CHAR:
        snprintf(mode, sizeof(mode), "VISUAL");
        break;
    }
    len = snprintf(
        status,
        sizeof(status),
        "[%s] %.20s - %d lines %s",
        mode,
        E.filename ? E.filename : "[No Name]",
        E.numrows,
        E.dirty ? "(modified)" : ""
    );
    rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

    attron(A_REVERSE);
    while (len < COLS)
    {
        if (COLS - len == rlen)
        {
            break;
        }
        mvprintw(E.screenrows, len, " ");
        len++;
    }
    mvprintw(E.screenrows, 0, "%s", status);
    mvprintw(E.screenrows, len, "%s", rstatus);

    attroff(A_REVERSE);
}

void editorDrawMessageBar(void)
{
    if (time(NULL) - E.statusmsg_time < 5)
    {
        mvprintw(E.screenrows + 1, 0, "%s", E.statusmsg);
    }
}

void editorRefreshScreen(void)
{
    clear();
    editorScroll();
    editorDrawRows();
    editorDrawStatusBar();
    editorDrawMessageBar();
    move(E.cy - E.rowoff, E.rx - E.coloff);
    refresh();
}

void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

int main(int argc, char *argv[])
{
    init();
    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    while (1)
    {
        editorRefreshScreen();

        editorProcessKeypress();
    }

    return 0;
}
