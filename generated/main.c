#include "main.h"

// -- Implementation --

int main(int argc, char* argv[]) {
    so_String _so_argv[argc];
    so_args_init(argc, argv, _so_argv);
    os_Exit(cli_Run(os_Args));
    return 0;
}
