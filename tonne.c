/*** Includes ***/
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


/*** Defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)


/*** Data ***/

struct termios original_termios;


/*** Terminal configuration ***/

// Exit function. Prints the error message and exits with code 1
void die(const char *s){
    perror(s);
    exit(1);
}

void disableTermRawMode(){
    // We try to reset the flags to their original value to reset 'raw' mode
    // If we get an error we end the program with exit status = 1
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1){
        die("tcsetattr");
    }
}

void enableTermRawMode(){

    // Check the value of the flags on the terminal and save them on the "original_termios" struct
    // If we get an error we end the program
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) die("tcgetattr");

    // Register "disableTermRawMode" to always run on exit
    atexit(disableTermRawMode);

    // Define raw as a struct that holds flags
    struct termios raw = original_termios;

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


/*** Input ***/

void processKeypress(){
    char c = readKey();
    switch (c){
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
}


/*** Output ***/
void refreshScreen(){
    // We wri¿ite 4 bytes to the STDOUT
    // we send an escape sequence ('\x1b' followed by '[')
    // The escape sequence we send is 'J' that is for clearing the screen
    // We send it with the argument '2' which means clear the whole screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
}


/*** Init ***/

int main(){
    enableTermRawMode();

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