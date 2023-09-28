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

#define CTRL_KEY(k) ((k)&0x1f)
#define VERSION     "0.1"
#define TABSTOP     8

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
char *editorPrompt(char *prompt);
void editorInsertRow(int at, char *s, size_t len);

typedef struct
{
  int size;
  int rsize;
  char *chars;
  char *render;
} Erow;

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
} Editor;

Editor E;

void
die(const char *s)
{
  perror(s);
  exit(1);
}

int
editorRowCxToRx(Erow *row, int cx)
{
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++)
    {
      if (row->chars[j] == '\t')
        rx += (TABSTOP - 1) - (rx % TABSTOP);
      rx++;
    }
  return rx;
}

void
editorUpdateRow(Erow *row)
{
  int tabs = 0;
  int j;
  int idx;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t')
      tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs * (TABSTOP - 1) + 1);
  idx         = 0;
  for (j = 0; j < row->size; j++)
    {
      if (row->chars[j] == '\t')
        {
          row->render[idx++] = ' ';
          while (idx % TABSTOP != 0)
            row->render[idx++] = ' ';
        }
      else
        {
          row->render[idx++] = row->chars[j];
        }
    }
  row->render[idx] = '\0';
  row->rsize       = idx;
}

void
editorRowInsertChar(Erow *row, int at, int c)
{
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void
editorRowAppendString(Erow *row, char *s, size_t len)
{
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void
editorRowDelChar(Erow *row, int at)
{
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

void
editorInsertChar(int c)
{
  if (E.cy == E.numrows)
    {
      editorInsertRow(E.numrows, "", 0);
    }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void
editorInsertRow(int at, char *s, size_t len)
{
  if (at < 0 || at > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(Erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(Erow) * (E.numrows - at));
  E.row[at].size  = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.row[at].rsize      = 0;
  E.row[at].render     = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void
editorFreeRow(Erow *row)
{
  free(row->render);
  free(row->chars);
}
void
editorDelRow(int at)
{
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(Erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

void
editorDelChar(void)
{
  Erow *row;

  if (E.cy == E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;
  row = &E.row[E.cy];
  if (E.cx > 0)
    {
      editorRowDelChar(row, E.cx - 1);
      E.cx--;
    }
  else
    {
      E.cx = E.row[E.cy - 1].size;
      editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
      editorDelRow(E.cy);
      E.cy--;
    }
}

void
editorInsertNewline(void)
{
  if (E.cx == 0)
    {
      editorInsertRow(E.cy, "", 0);
    }
  else
    {
      Erow *row = &E.row[E.cy];
      editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
      row                   = &E.row[E.cy];
      row->size             = E.cx;
      row->chars[row->size] = '\0';
      editorUpdateRow(row);
    }
  E.cy++;
  E.cx = 0;
}

char *
editorRowsToString(unsigned int *buflen)
{
  char *buf;
  char *p;
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;
  buf     = malloc(totlen);
  p       = buf;
  for (j = 0; j < E.numrows; j++)
    {
      memcpy(p, E.row[j].chars, E.row[j].size);
      p += E.row[j].size;
      *p = '\n';
      p++;
    }
  return buf;
}

void
editorOpen(char *filename)
{
  FILE *fp       = fopen(filename, "r");
  char *line     = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  free(E.filename);
  E.filename = strdup(filename);

  if (!fp)
    die("fopen");

  while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
      while (linelen > 0
             && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        linelen--;
      editorInsertRow(E.numrows, line, linelen);
    }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void
editorSave(void)
{
  unsigned int len;
  FILE *fp;
  char *buf;

  if (!E.filename)
    {
      E.filename = editorPrompt("Save as %s");
      if (!E.filename)
        {
          editorSetStatusMessage("Save aborted");
          return;
        }
    }

  buf = editorRowsToString(&len);
  fp  = fopen(E.filename, "w");
  if (!fp)
    die("fopen");
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

void
init(void)
{
  initscr();
  noecho();
  raw();
  nonl();
  keypad(stdscr, TRUE);
  timeout(100);
  ESCDELAY = 10;

  E.cx             = 0;
  E.cy             = 0;
  E.rx             = 0;
  E.rowoff         = 0;
  E.coloff         = 0;
  E.screenrows     = LINES - 2;
  E.numrows        = 0;
  E.row            = NULL;
  E.dirty          = 0;
  E.filename       = NULL;
  E.statusmsg[0]   = '\0';
  E.statusmsg_time = 0;
}

char *
editorPrompt(char *prompt)
{
  size_t bufsize = 128;
  char *buf      = malloc(bufsize);
  size_t buflen  = 0;
  buf[0]         = '\0';

  while (1)
    {
      int c;

      editorSetStatusMessage(prompt, buf);
      editorRefreshScreen();

      c = getch();
      if (c == KEY_DC || c == KEY_BACKSPACE || c == CTRL_KEY('h') || c == 127)
        {
          if (buflen != 0)
            buf[--buflen] = '\0';
        }
      else if (c == KEY_EXIT || c == 27)
        {
          editorSetStatusMessage("");
          free(buf);
          return NULL;
        }
      else if (c == '\r')
        {
          if (buflen != 0)
            {
              editorSetStatusMessage("");
              return buf;
            }
        }
      else if (!iscntrl(c) && c != -1 && c < 128)
        {
          if (buflen == bufsize - 1)
            {
              bufsize *= 2;
              buf = realloc(buf, bufsize);
            }
          buf[buflen++] = c;
          buf[buflen]   = '\0';
        }
    }
}

void
editorMoveCursor(int key)
{
  Erow row = E.row[E.cy];
  switch (key)
    {
    case KEY_LEFT:
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
    case KEY_RIGHT:
      if (E.cx < row.size)
        {
          E.cx++;
        }
      else if (E.cx == (row.size ? row.size : 0))
        {
          E.cy++;
          E.cx = 0;
        }
      break;
    case KEY_UP:
      if (E.cy != 0)
        {
          E.cy--;
        }
      break;
    case KEY_DOWN:
      if (E.cy < E.numrows - 1)
        {
          E.cy++;
        }
      break;
    }

  row = E.row[E.cy];
  if (E.cx > row.size)
    {
      E.cx = row.size ? row.size - 1 : 0;
    }
}

void
editorProcessKeypress(void)
{
  int c = getch();
  switch (c)
    {
    case CTRL_KEY('q'):
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
              E.cy = E.numrows;
          }
        times = E.screenrows;
        while (times--)
          c == KEY_PPAGE ? editorMoveCursor(KEY_UP)
                         : editorMoveCursor(KEY_DOWN);
      }
    case KEY_HOME:
      E.cx = 0;
      break;
    case KEY_END:
      E.cx = E.row[E.cy].size ? E.row[E.cy].size - 1 : 0;
      break;
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT:
      editorMoveCursor(c);
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
        editorMoveCursor(KEY_RIGHT);
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
    default:
      editorInsertChar(c);
      break;
    }
}

void
editorDrawRows(void)
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
              int welcomeLen = snprintf(welcome, sizeof(welcome),
                                        "Ocean editor -- version %s", VERSION);
              int padding    = (COLS - welcomeLen) / 2;
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
          if (E.row[filerow].rsize - E.coloff >= 0)
            {
              mvprintw(y, 0, "%s", &E.row[filerow].render[E.coloff]);
            }
        }
    }
}

void
editorScroll(void)
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

void
editorDrawStatusBar(void)
{
  char status[80], rstatus[80];
  int len  = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

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

void
editorDrawMessageBar(void)
{
  if (time(NULL) - E.statusmsg_time < 5)
    mvprintw(E.screenrows + 1, 0, "%s", E.statusmsg);
}

void
editorRefreshScreen(void)
{
  clear();
  editorScroll();
  editorDrawRows();
  editorDrawStatusBar();
  editorDrawMessageBar();
  move(E.cy - E.rowoff, E.rx - E.coloff);
  refresh();
}

void
editorSetStatusMessage(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

int
main(int argc, char *argv[])
{
  init();
  if (argc >= 2)
    editorOpen(argv[1]);

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

  while (1)
    {
      editorRefreshScreen();

      editorProcessKeypress();
    }

  return 0;
}
