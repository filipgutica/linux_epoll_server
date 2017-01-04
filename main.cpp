#include <iostream>
#include "Server.h"

using namespace std;

int main()
{
    system("ulimit -n 99999");
    Server *server = new Server();
    server->startServer();
    cout << "Hello world!" << endl;
    return 0;
}
