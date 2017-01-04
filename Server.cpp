#include "Server.h"
#include <time.h>       /* time */

// Epoll event queue for accepting sockets
struct epoll_event Server::events[EPOLL_QUEUE_LEN], Server::event;

// Epoll event queue for worker threads
struct epoll_event Server::worker_events[NUM_WORKERS][EPOLL_QUEUE_LEN];
struct epoll_event Server::worker_event[NUM_WORKERS];

// Total number of clients
int numClients;

// Number of clients per each thread
int numClientsInThread[NUM_WORKERS];

/*int main()
{
  system("ulimit -n 999999");
  Server *server = new Server();

  server->startServer();

  return 0;
}*/

Server::Server()
{
    // Seed the rand() generator
    srand (time(NULL));
    numClients = 0;
    port = SERVER_PORT;
    addr_size = sizeof(struct sockaddr_in);

    // Allocate memory for thread param sructures
    acceptThrdParams = new thrdParams();
    for (int i = 0; i< NUM_WORKERS; i++)
    {
        workerParams[i] = new thrdParams();
        numClientsInThread[i] = 0;
    }

    numClients = 0;

    // Start thread for updating the console
    pthread_create(&updateConsoleThrd, NULL, &UpdateConsole, (void*)1);

    // Create the listening socket
    fd_server = socket (AF_INET, SOCK_STREAM, 0);
    if (fd_server == -1)
        SystemFatal("socket");

    // set SO_REUSEADDR so port can be resused imemediately after exit, i.e., after CTRL-c
    arg = 1;
    if (setsockopt (fd_server, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) == -1)
        SystemFatal("setsockopt");

    // Make the server listening socket non-blocking
    if (fcntl (fd_server, F_SETFL, O_NONBLOCK | fcntl (fd_server, F_GETFL, 0)) == -1)
        SystemFatal("fcntl");

    // Bind to the specified listening port
    memset (&addr, 0, sizeof (struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind (fd_server, (struct sockaddr*) &addr, sizeof(addr)) == -1)
        SystemFatal("bind");

    // Listen for fd_news; SOMAXCONN is 128 by default
    if (listen (fd_server, SOMAXCONN) == -1)
        SystemFatal("listen");

    // Create the epoll file descriptor
    epoll_fd = epoll_create(EPOLL_QUEUE_LEN);
    if (epoll_fd == -1)
        SystemFatal("epoll_create");

    // Add the server socket to the epoll Server::event loop
    Server::event.events = EPOLLIN;
    Server::event.data.fd = fd_server;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, fd_server, &Server::event) == -1)
        SystemFatal("epoll_ctl");

}

void Server::startServer()
{
    // Thread IDs for worker threads
    pthread_t workerThrds[NUM_WORKERS];
    // Thread ID for accept thread
    pthread_t acceptThrd;

    // File descriptors for the accept thread
    acceptThrdParams->epoll_fd = epoll_fd;
    acceptThrdParams->fd_new = fd_new;
    acceptThrdParams->fd_server = fd_server;

    for (int i = 0; i < NUM_WORKERS; i++)
    {
        // Create an epoll_fd for each worker thread
        worker_fds[i] = epoll_create(EPOLL_QUEUE_LEN);
        if (worker_fds[i] == -1)
        {
            SystemFatal("epoll_create");
        }

        // Put the worker epoll fds in the accept thread params
        acceptThrdParams->worker_fds[i] = worker_fds[i];
        // Each worker thread has its own worker params, and should have its on epoll_fd
        workerParams[i]->worker_fds[i] = worker_fds[i];
        // Register Epoll events for each worker thread's epoll event queue
        Server::worker_event[i].events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET | EPOLLRDHUP;
    }

    // Create thread for accepting connections
    pthread_create(&acceptThrd, NULL, &acceptConnections, (void*)acceptThrdParams);

    // Create worker threads for I/O
    for (int i = 0; i< NUM_WORKERS; i++)
    {
      // Assign each worker thread an index to keep track of which thread we are in
      workerParams[i]->thrdNumber = i;
      pthread_create(&workerThrds[i], NULL, &worker, (void*)workerParams[i]);
    }

    for (int i = 0; i< NUM_WORKERS; i++)
    {
      // Wait for each worker threads to complete
      pthread_join(workerThrds[i], NULL);
    }

    close(fd_server);
    delete acceptThrdParams;

    for (int i = 0; i < NUM_WORKERS; i++)
      delete workerParams[i];

    return;
}

void *acceptConnections(void *param)
{
    thrdParams *acceptThrdParams = (thrdParams*)param;
    struct sockaddr_in remote_addr;

    while(TRUE)
    {
        int num_fds = epoll_wait (acceptThrdParams->epoll_fd, Server::events, EPOLL_QUEUE_LEN, -1);

        if (num_fds < 0)
        {
            std::cout << "Error in epoll_wait!" ;
            exit(-1);
        }

        for (int i = 0; i < num_fds; i++)
        {
            // Case 1: Error condition
            if (Server::events[i].events & (EPOLLHUP | EPOLLERR))
            {
                std::cout << "EPOLL ERROR" << std::endl;
                close(Server::events[i].data.fd);
                continue;
            }


            // Case 2: Server is receiving a connection request
            if (Server::events[i].data.fd == acceptThrdParams->fd_server)
            {
                socklen_t addr_size = sizeof(remote_addr);
                acceptThrdParams->fd_new = accept (acceptThrdParams->fd_server, (struct sockaddr*) &remote_addr, &addr_size);
                if (acceptThrdParams->fd_new == -1)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        perror("accept");
                    }
                    continue;
                }

                // Make the fd_new non-blocking
                if (fcntl (acceptThrdParams->fd_new, F_SETFL, O_NONBLOCK | fcntl(acceptThrdParams->fd_new, F_GETFL, 0)) == -1)
                {
                    std::cout << "fcntl";
                    exit(-1);
                }

                // randomly add new socket descriptors to one of the N threads' epoll queue
                int n = rand() % NUM_WORKERS;
                Server::worker_event[n].data.fd = acceptThrdParams->fd_new;
                if (epoll_ctl (acceptThrdParams->worker_fds[n], EPOLL_CTL_ADD, acceptThrdParams->fd_new, &Server::worker_event[n]) == -1)
                {
                    std::cout << "epoll_ctl";
                    exit(-1);
                }

                numClientsInThread[n]++;
                continue;
            }
        }

    }
}

void* worker(void* param)
{
    thrdParams *workerParams = (thrdParams*)param;
    int index = workerParams->thrdNumber;
    std::cout<< "thread  index " << index << std::endl;
    while(TRUE)
    {
        int num_fds = epoll_wait (workerParams->worker_fds[index], Server::worker_events[index], EPOLL_QUEUE_LEN, -1);

        if (num_fds < 0)
        {
            std::cout << "Error in epoll_wait!" ;
            exit(-1);
        }

        for (int i = 0; i < num_fds; i++)
        {

            // Case 1: Error condition
            if (Server::worker_events[index][i].events & (EPOLLHUP | EPOLLERR))
            {
                numClientsInThread[index]--;
                std::cout << "EPOLL ERROR" << std::endl;
                close(Server::worker_events[index][i].data.fd);
                continue;
            }
            assert (Server::worker_events[index][i].events & EPOLLIN);

            // Case 2: Data available to read
            if (Server::worker_events[index][i].events & (EPOLLIN))
            {
                int fd = Server::worker_events[index][i].data.fd;
                readSocket(fd);
            }

            // Case 3: Socket disconnect
            if ( Server::worker_events[index][i].events & (EPOLLRDHUP))
            {
                close(Server::worker_events[index][i].data.fd);
                numClientsInThread[index]--;
            }

        }

    }
}

void readSocket(int fd)
{
    int	n, bytes_to_read;
    char	*bp, buf[BUFLEN];

    memset(buf, 0, sizeof(buf));

    bp = buf;
    bytes_to_read = BUFLEN;

    while ((n = recv (fd, bp, bytes_to_read, 0)) < BUFLEN)
    {
      //n = read(fd, bp, BUFLEN);

      bp += n;
  		bytes_to_read -= n;

      if (n == 0)
      {
          // Socket is disconnected
          //close(fd);
          return;
      }
      if (n == -1)
      {
          if (errno != EAGAIN && errno != EWOULDBLOCK)
          {
              // Error
              //close(fd);
              return;
          }
      }

      if (strchr(bp, EOF) != NULL)
      {
        break;
      }
    }

    send (fd, buf, BUFLEN, 0);

    return;
}

// Unused At the moment Incase we want to thread this further
void *readSocket(void *param)
{
    int	n, bytes_to_read;
    char	*bp, buf[BUFLEN];
    thrdParams* data = (thrdParams*) param;
    int fd = data->fd;
    int index = data->eventIndex;

    bp = buf;
    bytes_to_read = BUFLEN;

    n = recv (fd, bp, bytes_to_read, 0);

    if (n == 0)
    {
        // Socket is disconnected
        return NULL;
    }
    if (n == -1)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            // Error
            return NULL;
        }
    }

    send (fd, buf, BUFLEN, 0);

    return NULL;

}



void* UpdateConsole(void *param)
{
    while(TRUE)
    {
        usleep(100000);
        numClients = 0;
        for (int i = 0; i < NUM_WORKERS; i++)
        {
          std::cout << "Clients in thread: " << i << ", " << numClientsInThread[i] << std::endl;
          numClients += numClientsInThread[i];
        }

        std::cout << "Total Clients Connected: " << numClients <<  std::endl;
    }
}



void Server::SystemFatal(const char *message)
{
    std::cout << message << std::endl;
      exit (EXIT_FAILURE);
}
