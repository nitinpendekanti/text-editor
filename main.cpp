/*** includes ***/

#include <iostream>
#include <unistd.h>     // read() and STDIN_FILENO
#include <termios.h>    // struct termios, tcgetattr(), tcsetattr(), ISIG, ICANON, TCAFLUSH
#include <stdlib.h>     // atexit
#include <ctype.h>      // iscntrl()
#include <errno.h>

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char* s) {
    perror(s); // prints error message from global errno variable
    exit(1);   // exit program with status failure
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)    // sets terminal attributes from orig_termios
        die("tcsetattr");                                           // calls die if tcsetattr() fails
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)         // stores terminal attributes in orig_termios
        die("tcgetattr");                                     // calls die if tcgetattr() fails

    atexit(disableRawMode);                                   // at program exit, restore terminal attributes

    struct termios raw {orig_termios};                        // struct storing terminal attributes (to be modified)
                                                                
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // turn off ctrl-s & ctrl-q, fix ctrl-m                
    raw.c_oflag &= ~(OPOST);                                  // turns off all output processing features
    raw.c_cflag |= (CS8);                                     // ensures character size if 8 bits or one byte
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);          // turn off echo, canonical mode, ctrl-c/z/v signal

    raw.c_cc[VMIN] = 0;                                       // minimum number of bytes before read() can return
    raw.c_cc[VTIME] = 1;                                      // maximum amount of time to wait before read returns (~100ms)

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)       // sets terminal attributes from raw (modified terminal attributes)
        die("tcsetattr");                                     // calls die if tcsetattr() fails
}

/*** init ***/

int main() {
    enableRawMode();

    while (true) {
        char c {'\0'};
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        read(STDIN_FILENO, &c, 1);

        if (iscntrl(c)) { // control characters (ASCII 0 - 31 & 127)
            std::cout << int(c) << '\r\n';
        } else { // ASCII 32 - 126
            std::cout << int(c) << " ('" << c << "')\r\n";
        }

        if (c == 'q') break;
    }


    return 0;
}