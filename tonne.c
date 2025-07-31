/*** Includes ***/
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


/*** Defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

#define TONNE_VERSION "0.0.1"

enum editorKeys{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};


/*** Data ***/

// Datatype for storing a row
typedef struct erow {
    int size;
    char *chars;
} erow;

// Global state struct
struct editorConfig{
    // Size of the terminal
    int screenrows;
    int screencols;

    // Cursor position
    int cx, cy;

    // Number of rows
    int numrows;

    // the row of text to be displayed
    erow row;

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

int readKey(){
    int nread;
    char c;
    // We read STDIN q byte at a time and set the c variable to the value read
    // TODO -> think if this can just be an if. That way we dont get locked
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        // If read fails and it is not because of the timeout (timeout fails set errno var to EAGAIN) we kill the program
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    // check for escape sequences
    if (c == '\x1b'){
        // We define a sequence of 3 bytes to hold the escape sequence
        char seq[3];
        // We read the first byte after the escape sequence
        // (which should be '[' as we will later check)
        // if there isn't any (like we just pressed the escape key), we just return the escape byte
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        // We read another byte to check the escape sequence identifier
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        // If the second byte in the sequence is [ we continue to read the escape sequence
        if (seq[0] == '['){
            // If the second char on the escape sequence is between 0 and 9 we check the third char on the sequence
            if (seq[1] >= '0' && seq[1] <= '9'){
                // We read another character, if there is no 3rd character we return the escape character
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                // We then check if the 3rd caracter read was '~' and decide the action based on the 2nd character
                if (seq[2] == '~'){
                    // we decide the action based on the second character of the sequence
                    switch (seq[1]){
                        // Home and end key could recieved as one of 2 numbers each
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }


            // We now check the character to detect what escape sequence was pressed
            switch (seq[1]){
                // We map the characters returned by the arrow keys to wasd to move the cursor around
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                // Home and end escape sequence could also be \x1b[H or \x1b[F
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        // seq 0 may not always be [ sometimes home and end escape sequences start with 'O'
        }else if (seq[0] == 'O'){
            switch (seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        // If we dont recognize the escape sequence we return the escape character
        return '\x1b';

    } // We dont really need an else here, we can just return c

    return c;
}


int getCursorPosition(int *rows, int *cols){
    char buf[32];
    unsigned int i = 0;
    // We write an escape sequence ('\x1b[') that queries the terminal status info ('n')
    // We specifically ask for cursor position (argument '6')
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    // For the length of the buffer
    while (i<sizeof(buf)-1){
        // Read one byte of the buffer
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        // When we find the R byte at the end of the message we are looking for, we exit
        if (buf[i] == 'R') break;
        i++;
    }
    // we set the last read byte to 0 so print interprets it as the end of a string
    buf[i] = '\0';

    // If what we read is not an escape sequence, we return an error
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    // We use sscanf to read the size values that are in the format rrr;ccc, if we dont read 2 bytes we return an error
    if (sscanf(&buf[2], "%d;%d", rows, cols) !=2) return -1;

    return 0;
}


// We pass references to rows and cols so that we can get both values from one function
int getWindowSize(int *rows, int *cols){
    struct winsize ws;

    // ioctl with the TIOCGWINSZ request gets the size of the terminal
    // ioctl -> Input/Output ConTroL
    // TIOCGWINSZ -> Terminal, Input, Output, Control, Get, WINdow, SiZe
    // TODO -> search why STDOUT_FILENO is necessary here
    // We also fail if there are no columns
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
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


/*** Apend Buffer ***/

// We define an append buffer struct to use to append all changes to and then put them on screen on only one write call
struct abuf{
    char *b;
    int len;
};

// We define an empty abuf to use as a constructor
#define ABUF_INIT {NULL, 0}


void abAppend(struct abuf *ab, const char *s, int len){
    // We allocate more space to the string b held in ab
    // We make the new allocation equal to the length of the buffer plus the length of 's' that was sent as a parameter to this function
    // We also set the value of new to the string b held in ab
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    // we set the value of new after ab->len bytes to the value of s
    memcpy(&new[ab->len], s, len);
    // We now set b to be equal to new
    ab->b = new;
    // we set len to ab->len + the length of s
    ab->len += len;
}


// Function for emptying the append buffer
void abFree(struct abuf *ab){
    free(ab->b);
}

/*** Input ***/

void moveCursor(int key){
    switch (key){
        // Move left
        case ARROW_LEFT:
            if (E.cx != 0){ // cant go left if cursor is on the left
                E.cx--;
            }
            break;
        // Move right
        case ARROW_RIGHT:
            if (E.cx != E.screencols-1){ // cant go right if we are at the furthest position (-1 because cx is 0 indexed)
                E.cx++;
            }
            break;
        // Move up
        case ARROW_UP:
            if (E.cy != 0){ // cant go up if cursor is at the top
                E.cy--;
            }
            break;
        // Move down
        case ARROW_DOWN:
            if (E.cy != E.screenrows-1){ // cant go down if we are at the bottom of the terminal (-1 because cx is 0 indexed)
                E.cy++;
            }
            break;
    }
}

void processKeypress(){
    int c = readKey();

    // We decide what to do with special keypresses depending on the type of keypress
    switch (c){
        case CTRL_KEY('q'):
            // We clear the screen and reset the cursor before exiting
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        // Home key moves the cursor to the column 0
        case HOME_KEY:
            E.cx = 0;
            break;
        // End key moves the cursor to the last column
        case END_KEY:
            E.cx = E.screencols-1;
            break;

        // For the page up and page down keys
        case PAGE_UP:
        case PAGE_DOWN:
            {
                // We run a loop for as many rows as the terminal has
                int times = E.screenrows;
                while (times--)
                    // we send the arrow key character to the move cursor functions 'times' times
                    // depending on the page key that was pressed we choose the arrow key to be sent
                    moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;


        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
            moveCursor(c);
            break;
    }
}


/*** Output ***/

void drawRows(struct abuf *ab){
    int y;
    // For all rows we write a tilde at the start of the line
    // We write them to a buffer that will then write every line in one go
    for (y=0;y<E.screenrows;y++){

        if (y==E.screenrows/3){
            // We define a char array to store the welcome message
            char welcome_message[80];

            // We format the char array with the version number defined and get the length
            int welcome_len = snprintf(welcome_message, sizeof(welcome_message),
                "Tonne Editor -- version %s", TONNE_VERSION);
                // If the message foesnt fit on the screen, we truncate it
                if (welcome_len > E.screencols) welcome_len = E.screencols;

                // We get the padding needed for the message to be on the middle of the screen
                int padding = (E.screencols - welcome_len)/2;
                if (padding){
                    // We add a tilde character at the start of the line
                    abAppend(ab, "~", 1);
                    // We reduce the padding counter
                    padding--;
                }
                // we add a space to the write buffer while reducing padding counter by 1 for each space
                while (padding--) abAppend(ab, " ", 1);

                abAppend(ab, welcome_message, welcome_len);
        }else{
            abAppend(ab, "~", 1);
        }


        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows-1){
            abAppend(ab, "\r\n", 2);
        }
    }
}


void refreshScreen(){
    // we initiate an append buffer
    struct abuf ab = ABUF_INIT;

    // We add an escape sequence "reset mode" to hide the cursor during the screen draw
    abAppend(&ab, "\x1b[?25l",6);

    // We write 4 bytes to the buffer
    // we send an escape sequence ('\x1b' (escape character) followed by '[')
    // The escape sequence we send is 'J' that is for clearing the screen
    // We send it with the argument '2' which means clear the whole screen
    // abAppend(&ab, "\x1b[2J", 4);
    // we remove this line in favour of clearing the line when we draw it on drawRows function

    // We write 3 bytes to the buffer
    // we write an escape sequence followed by the H byte
    // The H byte on the escape sequence is for setting the cursor position. It takes 2 args, we dont use any to set it to 1,1
    abAppend(&ab, "\x1b[H", 3);

    drawRows(&ab);

    // We create a character array
    char buf[32];
    // We assign the escape sequence to position cursor to our buffer array with the coordinates we want the cursor to be in
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
    // We add the characters to the write buffer
    abAppend(&ab, buf, strlen(buf));

    // We add an escape sequence to show the ursor again
    abAppend(&ab, "\x1b[?25h", 6);

    // We write all the bytes on the buffer to the screen
    write(STDOUT_FILENO, ab.b, ab.len);
    // We free the append buffer
    abFree(&ab);

}


/*** Init ***/

void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;

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
// - [ ] Search for the difference between 'R' and "R", and '\0' and "\0"
//        > From the error message it seems '' is for chars and "" is read as an int