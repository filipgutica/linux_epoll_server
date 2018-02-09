# client.py
import select
import socket
import sys
import threading
import time
from Queue import Queue

BUFFER_SIZE = 1024
PORT = 7000
HOSTNAME = '127.0.0.1'

def logData(queue, fileName):
    logFile = open(fileName, 'w')
    logFile.flush()

    while True:
        info = queue.get()
        if info == 'kill':
            break
        logFile.write(info)
        # write to file in real time
        logFile.flush()
    logFile.close()
    return

def setUpClients():

    ip = raw_input('server ip: ')
    port = int(raw_input('server port: '))

    message = raw_input('message to send: ')
    numOfMessages = int(raw_input('number of times to send message: '))
    numOfSessions = int(raw_input('number of clients to simulate: '))
    logFileName = 'client data - %s.csv' % numOfSessions

    # use a queue to hold data that needs to be logged. The logData process below will write it to file when it gets
    # the chance
    dataQueue = Queue()
    logDataThread = threading.Thread(target=logData, args=(dataQueue, logFileName,))
    logDataThread.start()
    dataQueue.put('requests made,data sent (bytes),average response time (sec),session duration (sec)\n')

    # set up an epoll object with edge triggering
    epoll = select.epoll()
    # keep track of each client connection
    clientConnections = {}
    # keep track of the client side statistics for each connection
    connectionStatistics = {}

    # connect clients to server
    for session in range(0, numOfSessions):
        # note the time when we start sending messages, this is our reference point
        sessionStartTime = time.time()
        try:
            # create a socket object
            clientSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # connect socket to the server
            clientSocket.connect((ip, port))
        except socket.error, message:
            print 'Failed to create socket. Error code: ' + str(message[0]) + '-' + message[1]
            sys.exit()
        # tell epoll we're interested when the client socket is ready for writing so we can spam the server
        epoll.register(clientSocket, select.EPOLLOUT | select.EPOLLET)
        clientConnections[clientSocket.fileno()] = clientSocket
        connectionStatistics[clientSocket.fileno()] = [0, 0, 0, 0, 0, sessionStartTime]

    while len(clientConnections) > 0:
        # query the epoll object to get a list of events that we're interested in. The 1 indicates we're willing to
        # wait up to 1 second for an event to occur
        events = epoll.poll(1)
        for fileno, event in events:
            # when a client socket is available for writing, send a message to the server
            if event & select.EPOLLOUT:
                # send messages as long as the number of requests sent is < the limit specified
                clientConnections[fileno].send(message)
                # tell epoll we're interested when the client socket has data waiting to be read
                epoll.modify(fileno, select.EPOLLIN | select.EPOLLET)
                # update the number of requests, amount of data sent and the time the message was sent
                data = connectionStatistics[fileno]
                data[0] += 1
                data[1] += len(message)
                data[2] = time.time()
                connectionStatistics[fileno] = data
            # read data off a socket when it is ready to read
            elif event & select.EPOLLIN:
                message = clientConnections[fileno].recv(BUFFER_SIZE)
                # tell epoll we're interested when the client socket is ready for writing
                epoll.modify(fileno, select.EPOLLOUT | select.EPOLLET)
                # update the time the message was acknowledged by the server and calculate response time
                data = connectionStatistics[fileno]
                data[3] = time.time()
                data[4] += data[3] - data[2]
                connectionStatistics[fileno] = data
                # close connection once we have sent the number of messages specified to the server
                if data[0] == numOfMessages:
                    clientConnections[fileno].close()
                    epoll.unregister(fileno)
                    clientConnections.pop(fileno, None)
                    # calculate how long this session lasted
                    data[5] = time.time() - data[5]
                    data = connectionStatistics[fileno]

    # after all client connections have closed, update the average response time for each connection
    for connection in connectionStatistics:
        data = connectionStatistics[connection]
        averageResponseTime = data[4]/data[0]
        dataQueue.put('%s,%s,%s,%s\n' % (data[0], data[1], averageResponseTime, data[5]))

    # close the log file
    dataQueue.put('kill')
    # give the thread time to finish when logging for many clients
    logDataThread.join()

if __name__ == '__main__':
    setUpClients()
