/*** Includes ***/
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


/*** Defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)


/*** Data ***/

// Global state struct
struct editorConfig{
    int screenrows;
    int screencols;

    struct termios original_termios;
};

struct editorConfig E;


/*** Terminal configuration ***/

// Exit function. Prints the error message and exits with code 1
void die(const char *s){

    // We clear the screen and reset the cursor before printing the error
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableTermRawMode(){
    // We try to reset the flags to their original value to reset 'raw' mode
    // If we get an error we end the program with exit status = 1
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1){
        die("tcsetattr");
    }
}

void enableTermRawMode(){

    // Check the value of the flags on the terminal and save them on the "original_termios" struct
    // If we get an error we end the program
    if (tcgetattr(STDIN_FILENO, &E.original_termios) == -1) die("tcgetattr");

    // Register "disableTermRawMode" to always run on exit
    atexit(disableTermRawMode);

    // Define raw as a struct that holds flags
    struct termios raw = E.original_termios;

    // ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary. We use the bitwise-NOT operator (~) on this value to get 11111111111111111111111111110111. We then bitwise-AND this value with the flags field, which forces the fourth bit in the flags field to become 0, and causes every other bit to retain its current value.
    // We set the echo flag to 0 on "raw"
    // We also set the ICANON flag to 0 to disable canonnical mode and process the keys without the need to press return
    // We also set the ISIG flag to 0 to disable CTRL-C and CTRL-Z
    // TODO (in the future I might want to keep CTRL-Z like vim does)
    // Also unset IEXTEN
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    // We now use input flags (iflag)
    // We disable the IXON flag to turn off CTRL-S and CTRL-Q
    // we also unset the ICRNL flag that turns a carriage return into newline
    // The BRKINT flag is unset to stop the program when a break condition happens ¿?
    // INPCK is unset to enable parity checking. Not particularly useful but part of enabling "raw" mode
    // ISTRIP is unset to stop the terminal stripping the 8 bit of each input byte. It is normally unset by default but it doesnt hurt to make sure (TODO research why this flag is ever useful)
    raw.c_iflag &= ~(ISTRIP | INPCK | BRKINT | ICRNL | IXON);

    // We also need to modify some output flags
    // We unset the OPOST flag to disable newline input into carriage return + newline translation
    raw.c_oflag &= ~(OPOST);

    // we run an or operator on the CS8 bit mask to set the character size to 8 bits per byte. It is normally already set (TODO research when it is useful to have bytes that arent 8 bits > Bytes correspond to the syze of your character set, not necessarily always 8 bits. I should research if this is a relatively high level consideration since at the low level bytes are *physically* 8 bits)
    raw.c_cflag |= (CS8);

    // We set the minimum number of bytes that 'read()' needs to read before it returns
    // We set it to 0 to have it return even if there are no bytes (so the process doesnt hang on the user input) 
    // TODO search what exactly is raw.c_cc . As far as i understood it they were flags for the terminal but now we are changing the behaviour of a function from STDIN_FILENO so Im not following 100%
    raw.c_cc[VMIN] = 0;

    // We set the max time 'read()' has to wait for new input
    raw.c_cc[VTIME] = 1;

    // Sets the new flags to the terminal
    // TCSAFLUSH argument specifies when to apply the change: in this case, it waits for all pending output to be written to the terminal, and also discards any input that hasn’t been read.
    // If tcsetattr() fails and returns -1 we end the program with an error
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");

}

char readKey(){
    int nread;
    char c;
    // We read STDIN q byte at a time and set the c variable to the value read
    // TODO -> think if this can just be an if. That way we dont get locked
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        // If read fails and it is not because of the timeout (timeout fails set errno var to EAGAIN) we kill the program
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}


int getCursorPosition(int *rows, int *cols){
    // We write an escape sequence ('\x1b[') that queries the terminal status info ('n')
    // We specifically ask for cursor position (argument '6')
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    printf("\r\n");
    char c;
    // We read the response on stdin
    while (read(STDIN_FILENO, &c, 1) == 1){
        if (iscntrl(c)){
            printf("%d\r\n", c);
        }else{
            printf("%d ('%c')\r\n", c, c);
        }
    }
    readKey();
    return -1;
}


// We pass references to rows and cols so that we can get both values from one function
int getWindowSize(int *rows, int *cols){
    struct winsize ws;

    // ioctl with the TIOCGWINSZ request gets the size of the terminal
    // ioctl -> Input/Output ConTroL
    // TIOCGWINSZ -> Terminal, Input, Output, Control, Get, WINdow, SiZe
    // TODO -> search why STDOUT_FILENO is necessary here
    // We also fail if there are no columns
    if (1||ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        // If ioctl fails, we have a fallback, we send 2 escape sequences for moving the cursor 999 spaces to the right and 999 spaces down
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }else{
        // we set the references passed to the value read
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


/*** Input ***/

void processKeypress(){
    char c = readKey();
    switch (c){
        case CTRL_KEY('q'):
            // We clear the screen and reset the cursor before exiting
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}


/*** Output ***/

void drawRows(){
    int y;
    // For all rows we write a tilde at the start of the line
    for (y=0;y<E.screenrows;y++){
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}


void refreshScreen(){
    // We write 4 bytes to the STDOUT
    // we send an escape sequence ('\x1b' (escape character) followed by '[')
    // The escape sequence we send is 'J' that is for clearing the screen
    // We send it with the argument '2' which means clear the whole screen
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // We write 3 bytes to stdout
    // we write an escape sequence followed by the H byte
    // The H byte on the escape sequence is for setting the cursor position. It takes 2 args, we dont use any to set it to 1,1
    write(STDOUT_FILENO, "\x1b[H", 3);

    drawRows();
    // We reset the cursor after drawing the lines
    write(STDOUT_FILENO, "\x1b[H", 3);

}


/*** Init ***/

void initEditor(){
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(){
    enableTermRawMode();
    initEditor();

    // while always
    while (1){
        refreshScreen();
        processKeypress();
    }

    return 0;
}


/*** TODO ***/

// - [ ] Check what a struct is
// - [ ] Check what & before the struct name is for
// - [ ] Check why we put parenthesis on the flags (Even when we have only 1 flag and not doing operations between many) to do the bitwise NOT
//        > Might just be the way to state that we are using the variable to do bitwise operations, since we do the same thing with (k) & 0x1f
// - [ ] Search the difference between a function and a macro defined with '#define'