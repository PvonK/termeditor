/*** Includes ***/
// The included files use these macros to decide what features are imported
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>


/*** Defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

#define TONNE_VERSION "0.0.1"

#define TONNE_TAB_STOP 8

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

    // Variables to define how special characters will be rendered
    int rsize;
    char *render;
} erow;

// Global state struct
struct editorConfig{
    // Size of the terminal
    int screenrows;
    int screencols;

    // Offset between the file rows and the terminal rows
    int rowoffset;

    // Offset for row characters that are shown
    int coloffset;

    // Cursor position
    int cx, cy;
    // index for the position of the cursor inside the renderization of a row
    int rx;

    // Number of rows
    int numrows;

    // A pointer to the first row of text to be displayed
    erow *row;
    // Name of the open file
    char *filename;

    struct termios original_termios;

    // status bar message string
    char statusmsg[80];
    // status bar message timeout
    time_t statusmsg_time;
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

/*** row operations ***/

int rowCxToRx(erow *row, int cx){

    int rx = 0;
    int j;
    // we iterate the row up to the place the cursor is at
    for (j = 0; j < cx; j++){
        // If one of the characters before the cursor positon is a tab
        if (row->chars[j] == '\t')
            // We move rx forwards until the next column that is a multiple of 8 (assuming TAB_STOP is 8)
            // We do this by adding 8 and then subtracting however much we are past the previous multiple of 8
            rx += (TONNE_TAB_STOP-1) - (rx % TONNE_TAB_STOP);
        // We add one to rx (this is why we subtract one from tabstop on the if, because this always adds one afterwards)
        rx++;
    }
    return rx;
}

void updateRow(erow *row){

    // We create a counter for the number of tabs in the row
    int tabs = 0;
    int j;
    // We count the number of tabs on the row
    for (j=0; j<row->size; j++){
        if (row->chars[j] == '\t') tabs ++;
    }

    // We empty the contents of the render variable inside this row
    free(row->render);

    // We allocate the space to hold the whole row on the render var and add the space for 7 (which is TONNE_TAB_STOP-1) more bytes for each tab
    row->render = malloc(row->size + tabs*(TONNE_TAB_STOP-1) + 1);

    // We copy the values from the row chars to the row render var
    int idx = 0;
    for (j=0;j<row->size;j++){
        // If we are copying a tab characer
        if (row->chars[j] == '\t'){
            // We instead write the tab as spaces spaces
            row->render[idx++] = ' ';
            // We write spaces until the next column that is divisible by TONNE_TAB_STOP(8)
            while (idx%TONNE_TAB_STOP != 0) row->render[idx++] = ' ';
        }else{
            // otherwise, if the character is not rendered differently, we just copy it
            row->render[idx++] = row->chars[j];
        }

    }

    // We set the last value of the render var to a zero byte
    row->render[idx] = '\0';
    // we set rsize to the length of the render var
    row->rsize = idx;

}

void appendRow(char *s, size_t len){

    // We allocate enough space for the rows we need to write
    // We make E.row pointer point to the start of this block of memory
    // TODO if erow is a struct, why does sizeof know its size? what is going on there?
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    // We create an int to hold the index of the new row we are appending
    int at = E.numrows;

    // We set the length of this row
    // We set the size of the row to be written
    E.row[at].size = len;

    // We allocate the memory to hold all characters for this line
    // It is the length of the string read plus one for the 0 byte so it is interpreted as a string
    E.row[at].chars = malloc(len + 1);

    // We move len number of bytes from the pointer 's' onwards into E.row.chars
    memcpy(E.row[at].chars, s, len);

    // We set the last character to a zero byte so it is interpreted as a string and not just as a collection of bytes
    E.row[at].chars[len] = '\0';


    //We initialize the values for the special character rendering variables
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    updateRow(&E.row[at]);

    // We increment the counter for the number of rows
    E.numrows++;

}

void insertCharToRow(erow *row, int at, int c){

    // We cap the position of the position we can add characters
    if (at < 0 || at > row->size) at = row->size;
    // We add 2 bytes to the string (one for the new character and one for the nullbyte)(row.size doesnt account for the nullbyte but the memory allocation does)
    row->chars = realloc(row->chars, row->size+2);
    // move the characters from "at" position to the end of the row one position forward
    memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
    // we increase the var hoilding the size of the row
    row->size++;
    // We set the character at the "at" position to the value of "c"
    row->chars[at] = c;
    // We update the display of the row
    updateRow(row);

}


/*** editor operations ***/

void insertChar(int c){

    // If the cursor is at the end of the file
    if (E.cy == E.numrows){
        // We add a new line at the end
        appendRow("", 0);
    }
    // We add the character on the row we are in
    insertCharToRow(&E.row[E.cy], E.cx, c);
    // We move the cursor forward
    E.cx++;

}


/*** file i/o ***/

void openFile(char *filename){
    // We save the filename to a string
    free(E.filename);
    E.filename = strdup(filename);

    // We create a pointer to the file chosen
    // TODO research what `FILE` is
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    // Create a pointer 'line' that will hold a reference to the first byte of the line to be written
    char *line = NULL;

    // We define the line capacity
    // It will later be set to the amount of memory allocated for the line read
    size_t linecap = 0;

    // Define the length of the variable 'line'
    // We use ssize_t to handle the quantity of bytes on functions like read(). Also to hold the -1 that is returned on error
    ssize_t linelen;
    // We set linelen to the size of the line we read (if it is the end of the line it will be set to -1)
    // We also set the pointer line to the start of the line read
    // We also put that definition inside the condition for a while loop so we read until we get -1
    while ((linelen = getline(&line, &linecap, fp)) != -1){

        // We strip the line breaks and carriage return from the string since we wont display them
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')) linelen--;
        // We then add the row to the buffer
        appendRow(line, linelen);
    }
    free(line);
    fclose(fp);
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

/*** Output ***/
void scroll(){
    E.rx = 0;
    // If we are in a line that is not NONE
    if (E.cy < E.numrows){
        // We set rx to its value according to the number of tabs and the position of the cursor
        E.rx = rowCxToRx(&E.row[E.cy], E.cx);
    }

    // If the cursor is above the first line shown in the editor
    if (E.cy < E.rowoffset){
        // We set the offset to the position of the cursor (scroll up)
        E.rowoffset = E.cy;
    }

    // if the cursor is lower than the offset and the length of the screen
    if (E.cy >= E.rowoffset + E.screenrows){
        // we set the offset to the offset +1. (we do it in a convoluded way but thats essentialy what is happening)
        E.rowoffset = E.cy - E.screenrows + 1;
    }

    // If the cursor is to de left of the first character shown in the editor
    if (E.rx < E.coloffset){
        // We set the offset to the position of the cursor (scroll right)
        E.coloffset = E.rx;
    }

    // if the cursor is lower than the offset and the length of the screen
    if (E.rx >= E.coloffset + E.screencols){
        // we set the offset to the offset + 1 (we do it in a convoluded way but thats essentialy what is happening). Pretty sure i could do coloffset++ and it would work
        E.coloffset = E.rx - E.screencols + 1;
    }


}


/*** Input ***/

void moveCursor(int key){
    // We check if the row position is farther down than the last line of the file, if it is we set the pointer to that line to NULL ¿?. If it isnt, we set the pointer to the corresponding pointer of the line at that index 
    // this is for the cx movement, to check if the line has something, if it doesnt you cant move right
    // Also, apparently i forgot to change the arrow down conditional at some point I fucked up in step 69
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key){
        // Move left
        case ARROW_LEFT:
            if (E.cx != 0){ // cant go left if cursor is on the left
                E.cx--;
	    // If the cursor is on the first column and not on the first line and left is pressed
            } else if(E.cy > 0){
		// we go one row up
		E.cy--;
		// we go to the end of the line
		E.cx = E.row[E.cy].size;
	    }
            break;
        // Move right
        case ARROW_RIGHT:
            // If row isnt null and cx is less than the length of the row we can move.
            // (this is what i proposed on the last step but i proposed row.size, idk the difference between . and ->)(-> is used when accessing a propriety from a pointer, the "." is used when referincing the variable directly)
            if (row && E.cx < row->size){
                E.cx++;
            // If the row exists (we arent on the last row) and we are at the end of the line (TODO what is the difference between row.size and row->size)
            }else if (row && E.cx == row->size){
                // We move to the start of the next line
                E.cy++;
                E.cx = 0;
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
            if (E.cy < E.numrows){ // Can go one row lower than the last line on the file
                E.cy++;
            }
            break;
    }

    // We set row again since cy may have changed
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    // We get the legth of the row we are in (if it exists)
    int rowlen = row ? row->size : 0;
    // If we are too far right we snap back to the end of the line
    if (E.cx > rowlen){
	    E.cx = rowlen;
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
            // If the cursor id not on the last (non existing) line
            if (E.cy < E.numrows){
                // We move the cursor to the last element of the line we are on
                E.cx = E.row[E.cy].size;
            }
            break;

        // For the page up and page down keys
        case PAGE_UP:
        case PAGE_DOWN:
            {
                // We move the cursor to the edge of the screen before we move the cursor for one full page
                if (c == PAGE_UP){
                    E.cy = E.rowoffset;
                }else if (c == PAGE_DOWN){
                    E.cy = E.rowoffset + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }

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
        default:
            insertChar(c);
            break;
    }
}


/*** Output ***/

void drawRows(struct abuf *ab){
    int y;
    // For all rows we write a tilde at the start of the line
    // We write them to a buffer that will then write every line in one go
    for (y=0;y<E.screenrows;y++){
	// We create a variable to find the line of the file to draw
	int filerow = y + E.rowoffset;
        // Only draw tildes and version info on the rows that are lower than the rows drawn from the file. ie. only on lines without content
        if (filerow >= E.numrows){ // I dont quite get the point of this line after step 60. Is it only here to draw tildes on the empty lines under the file? i think so. After step 67 this now makes sense
            if (E.numrows == 0 && y==E.screenrows/3){
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
        }else{
            // We get the size of the string we need to write. It is the size of the row minus the sideways offset we get from scrolling to the side
            int len = E.row[filerow].rsize - E.coloffset;
            // If we scrolled too far right on one line we show 0 bytes from the ones we have scrolled past. We cap the value at 0
            if (len < 0) len = 0;
            // We truncate the length of the string we will draw to the size of the screen
            if (len > E.screencols) len = E.screencols;

            // We add the characters to the append buffer. We only add the ones after the number indicated by the column offset
            abAppend(ab, &E.row[filerow].render[E.coloffset], len);
        }

        abAppend(ab, "\x1b[K", 3);

        // Add a line at the end of the file so we can move the cursor under the last line
        //if (y < E.screenrows-1){  // we removed this if so that there is an empty newline at the bottom of the teminal all the time. For the status bar
            abAppend(ab, "\r\n", 2);
        //}
    }
}

// Function to draw the status bar at the bottom of the screen
void drawStatusBar(struct abuf *ab){

    // We write an escape sequence that switches to inverted colors
    abAppend(ab, "\x1b[7m", 4);

    // We create a string to hold the status of the file and another to keep the status that will be written on the right side of the screen
    char status[80], rstatus[80];
    // We set the status to the filename and number of lines on the file if there is one
    // If there is no file, we set the status to "[No Name]"
    int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);

    // We write to rstatus the numberline we are on
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy+1, E.numrows);

    // if the length of the status message is too big for the screen we cut it off
    if (len > E.screencols) len = E.screencols;

    // We add the content of status to the buffer
    abAppend(ab, status, len);

    // We iterate for all the remaining columns on the screen
    while (len < E.screencols){
        // If we have exactly the space needed for the right status
        if (E.screencols - len == rlen){
            // We write the right status
            abAppend(ab, rstatus, rlen);
            // We exit the loop since we finished drawing the status bar
            break;
        }else{
            // We add a space
            abAppend(ab, " ", 1);
            len++;
        }
    }
    // We switch back to normal colors
    abAppend(ab, "\x1b[m", 3);
    // We add another line
    abAppend(ab, "\r\n", 2);
}

void drawMessageBar(struct abuf *ab){

    // We clear the message bar
    abAppend(ab, "\x1b[K", 3);
    // We set the length of the message to either the length of the string to write or the maximum length of the terminal
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    // if there is a message and it has been less than 5 seconds since the message was set
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        // We write the message
        abAppend(ab, E.statusmsg, msglen);

}

void refreshScreen(){
    scroll();
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
    drawStatusBar(&ab);
    drawMessageBar(&ab);

    // We create a character array
    char buf[32];
    // We assign the escape sequence to position cursor to our buffer array with the coordinates we want the cursor to be in
    // The position of the cursor on E.cy is the position of the cursor on the file (0 indexed)
    // We subtract from the position of the cursor on the file, the offset of the lines to get the position the cursor should be on on the screen
    // We do the same for the offset of the columns, we subtract it from the position of the cursor to get the position on the terminal screen
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoffset) + 1, (E.rx-E.coloffset)+1);
    // We add the characters to the write buffer
    abAppend(&ab, buf, strlen(buf));

    // We add an escape sequence to show the ursor again
    abAppend(&ab, "\x1b[?25h", 6);

    // We write all the bytes on the buffer to the screen
    write(STDOUT_FILENO, ab.b, ab.len);
    // We free the append buffer
    abFree(&ab);

}

void setStatusMessage(const char *fmt, ...){

    // Since this is a variadic function (determined by the '...')
    // We need to define a va_list to hold the pointer to the start of all the arguments passed to the function
    va_list ap;
    // We define the start of the parameter list as starting from the position of fmt
    va_start(ap, fmt);
    // We set E.statusmsg to all the strings entered as parameters
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    // We no longer use the parameters
    va_end(ap);
    // We set statusmsg_time to the current time
    E.statusmsg_time = time(NULL);

}


/*** Init ***/

void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoffset = 0;
    E.coloffset = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    // We remove 2 rows from the total available in the terminal so we have space for the status bar
    E.screenrows --;
    E.screenrows --;
}

// Main has 2 parameters to handle arguments
int main(int argc, char *argv[]){
    enableTermRawMode();
    initEditor();
    // if there is an argument, we open the file
    // TODO is it as simple as that to handle arguments? they are passed by the shell directly to main?
    if (argc >= 2){
        openFile(argv[1]);
    }

    setStatusMessage("HELP: Ctrl+Q = quit");

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
