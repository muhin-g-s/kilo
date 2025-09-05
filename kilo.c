#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN
};

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
	int cx, cy;

	int screenrows;
  int screencols;

	int numrows;
  erow *row;

	int rowoff;
	int coloff;

  struct termios orig_termios;
};

struct editorConfig E;

void clearScreen() {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}

void die(const char *s) {
	clearScreen();

	perror(s);

	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode() {
	atexit(disableRawMode);

	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)) die("tcsetattr");
}

struct abuf {
  char *b;
  int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}
void abFree(struct abuf *ab) {
  free(ab->b);
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (
			linelen > 0 && 
			(
				line[linelen - 1] == '\n' ||
      	line[linelen - 1] == '\r'
			)
		) {
			linelen--;
		}
   
		editorAppendRow(line, linelen);
  }

  free(line);
  fclose(fp);
}

int editorReadKey() {
	int nread;
	char c;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

	switch (c) {
		case 'a': return ARROW_LEFT;
    case 'd': return ARROW_RIGHT;
    case 'w': return ARROW_UP;
    case 's': return ARROW_DOWN;
  }

	if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      switch (seq[1]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

void editorMoveCursor(int key) {
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
			}
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
  }

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();
  switch (c) {
    case CTRL_KEY('q'):
			clearScreen();
      exit(0);
      break;

	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}

void drawWelcomeMessage(struct abuf *ab) {
	const int LEN_WELCOME_MSG = 30;

	char welcome[LEN_WELCOME_MSG];
	int rowLen = snprintf(welcome, sizeof(welcome),
		"Kilo editor -- version %s", KILO_VERSION);

	if (rowLen > E.screencols) {
		rowLen = E.screencols;
		abAppend(ab, welcome, rowLen);
		return;
	}

	const int space = ((E.screencols - rowLen) / 2) - 1;

	abAppend(ab, "~", 1);

	for(int i = 0; i < space; i++) {
		abAppend(ab, " ", 1);
	}

	abAppend(ab, welcome, rowLen);
}

void drawRow(struct abuf *ab, int rownum) {
	int filerow = rownum + E.rowoff;

	if (filerow >= E.numrows) {
		if (E.numrows == 0 && rownum == E.screenrows / 3) {
			drawWelcomeMessage(ab);
		} else {
			abAppend(ab, "~", 1);
		}
	} else {
		int len = E.row[filerow].size - E.coloff;
		if (len < 0) len = 0;
		if (len > E.screencols) len = E.screencols;
		abAppend(ab, &E.row[filerow].chars[E.coloff], len);
	}
	abAppend(ab, "\x1b[K", 3);
	if (rownum < E.screenrows - 1) {
		abAppend(ab, "\r\n", 2);
	}
}

void drawRows(struct abuf *ab) {
	for(int y = 0; y < E.screenrows; y++) 
		drawRow(ab, y);
}

void editorScroll() {
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

	if (E.cx < E.coloff) {
		E.coloff = E.cx;
	}
	if (E.cx >= E.coloff + E.screencols) {
		E.coloff = E.cx - E.screencols + 1;
	}
}

void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  drawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab);
}

void initEditor() {
	E.numrows = 0;
	E.row = NULL;

	E.rowoff = 0;
	E.coloff = 0;

	E.cx = 0;
  E.cy = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	 if (argc >= 2) {
    editorOpen(argv[1]);
  }

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	
	return 0;
}
