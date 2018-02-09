#include <iostream>
#include "Server.h"

using namespace std;

int main()
{
    system("ulimit -n 99999");

    int port;
    cout << "Listening port: ";
	cin >> port;

    Server *server = new Server(port);
    server->startServer();
    cout << "Hello world!" << endl;
    return 0;
}
