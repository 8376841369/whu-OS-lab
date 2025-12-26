#include "userlib.h"

int main(int argc, char* argv[])
{
    char tmp[128];
    while(1) {
        memset(tmp, 0, 128);
        stdin(tmp, 128);
        stdout(tmp, strlen(tmp));
    }
}