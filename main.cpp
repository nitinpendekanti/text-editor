/*** includes ***/

#include <iostream>
#include <unistd.h>     // read() and STDIN_FILENO
#include <termios.h>    // struct termios, tcgetattr(), tcsetattr(), ISIG, ICANON, TCAFLUSH
#include <stdlib.h>     // atexit
#include <ctype.h>      // iscntrl()
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <algorithm>

/*** defines ***/

#define NILO_VESION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT = 1001,
    ARROW_DOWN = 1002,
    ARROW_UP = 1003,
    PAGE_UP = 1004,
    PAGE_DOWN = 1005,
    HOME_KEY = 1006,
    END_KEY = 1007,
    DELETE_KEY = 1008
};

/*** data ***/

struct editorConfig {
    int cx;
    int cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char* s) {
    perror(s);                            // prints error message from global errno variable
    exit(1);                              // exit program with status failure
    write(STDOUT_FILENO, "\x1b[2J", 4);   // clear the entire screen
    write(STDOUT_FILENO, "\x1b[H", 3);    // reposition cursor to the top-left
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)   // sets terminal attributes from orig_termios
        die("tcsetattr");                                            // calls die if tcsetattr() fails
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)       // stores terminal attributes in orig_termios
        die("tcgetattr");                                     // calls die if tcgetattr() fails

    atexit(disableRawMode);                                   // at program exit, restore terminal attributes

    struct termios raw {E.orig_termios};                      // struct storing terminal attributes (to be modified)
                                                                
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // turn off ctrl-s & ctrl-q, fix ctrl-m                
    raw.c_oflag &= ~(OPOST);                                  // turns off all output processing features
    raw.c_cflag |= (CS8);                                     // ensures character size if 8 bits or one byte
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);          // turn off echo, canonical mode, ctrl-c/z/v signal

    raw.c_cc[VMIN] = 0;                                       // minimum number of bytes before read() can return
    raw.c_cc[VTIME] = 1;                                      // maximum amount of time to wait before read returns (~100ms)

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)       // sets terminal attributes from raw (modified terminal attributes)
        die("tcsetattr");                                     // calls die if tcsetattr() fails
}

int editorReadKey() {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) 
            die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';

                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                        case '1':
                            return HOME_KEY;
                        case '4':
                        case '8':
                            return END_KEY;
                        case '3':
                            return DELETE_KEY;
                    }
                }
            }
            else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        } else if(seq[0] == 'H') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i {0};

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        ++i;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *newc = (char*)realloc(ab->b, ab->len + len);  // reallocates more memory for buffer

    if (newc == NULL) return;                           // return if reallocation fails
    memcpy(&newc[ab->len], s, len);                     // copies s with length len at newc[ab->len : ab->len + len]
    ab->b = newc;                                       // set beginning of string to new memory location
    ab->len += len;                                     // increases length of string
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf* ab) {
    for (int y = 0; y < E.screenrows; ++y) {
        if (y != E.screenrows / 3) {
            abAppend(ab, "~", 1);         // tilde indicating start of line
        } else {
            char welcomeMsg[80];
            int welcomeMsgLen = snprintf(welcomeMsg, sizeof(welcomeMsg), "Nilo Editor -- version %s", NILO_VESION);

            // add padding to center welcome message
            int padding = (E.screencols - welcomeMsgLen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                --padding;
            }
            while (padding--) abAppend(ab, " ", 1);

            welcomeMsgLen = std::min(welcomeMsgLen, E.screencols);
            abAppend(ab, welcomeMsg, welcomeMsgLen);
        }
        
        abAppend(ab, "\x1b[K", 3);       // clear a row before drawing
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);     // cairrage return + new line (excluding last line)
        }
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;          // append buffer

    abAppend(&ab, "\x1b[?25l", 6);       // hide cursor before clearing the screen
    abAppend(&ab, "\x1b[H", 3);          // reposition cursor to the top-left

    editorDrawRows(&ab);                 // draw rows

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));
    
    abAppend(&ab, "\x1b[?25h", 6);       // display cursor after clearing the screen

    write(STDOUT_FILENO, ab.b, ab.len);  // write out buffer information to stdout
    abFree(&ab);                         // free append buffer
}

/*** input ***/

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_UP:
            E.cy = std::max(E.cy - 1, 0);
            break;
        case ARROW_LEFT:
            E.cx = std::max(E.cx - 1, 0);
            break;
        case ARROW_DOWN:
            E.cy = std::min(E.cy + 1, E.screenrows - 1);
            break;
        case ARROW_RIGHT:
            E.cx = std::min(E.cx + 1, E.screencols - 1);
            break;
    }
}

void editorProcessKeypress() {
    int c {editorReadKey()};

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); // clear the entire screen
            write(STDOUT_FILENO, "\x1b[H", 3);  // reposition cursor to the top-left
            exit(0);
            break;
        case PAGE_DOWN:
        case PAGE_UP:
            {
            int times = E.screenrows;
            while (times--)
              editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        case DELETE_KEY:
            break;
    }
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (true) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}