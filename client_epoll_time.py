# client.py
import select
import socket
import sys
import threading
import time
from Queue import Queue

BUFFER_SIZE = 1024
PORT = 8005
HOSTNAME = '192.168.1.4'

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
    message = raw_input('message to send: ')
    sessionLenth = int(raw_input('how long should each session last (sec): '))
    numOfSessions = int(raw_input('number of clients to simulate: '))
    logFileName = 'client data - %s.csv' % numOfSessions

    # use a queue to hold data that needs to be logged. The logData process below will write it to file when it gets
    # the chance
    dataQueue = Queue()
    logDataThread = threading.Thread(target=logData, args=(dataQueue, logFileName,))
    logDataThread.start()
    dataQueue.put('requests,data sent,average response time\n')

    # set up an epoll object with edge triggering
    epoll = select.epoll()
    # keep track of each client connection
    clientConnections = {}
    # keep track of the client side statistics for each connection
    connectionStatistics = {}

    # connect clients to server
    for session in range(0, numOfSessions):
        try:
            # create a socket object
            clientSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # connect socket to the server
            clientSocket.connect((HOSTNAME, PORT))
        except socket.error, message:
            print 'Failed to create socket. Error code: ' + str(message[0]) + '-' + message[1]
            sys.exit()
        # tell epoll we're interested when the client socket is ready for writing so we can spam the server
        epoll.register(clientSocket, select.EPOLLOUT | select.EPOLLET)
        clientConnections[clientSocket.fileno()] = clientSocket
        connectionStatistics[clientSocket.fileno()] = [0, 0, 0.0, 0.0, 0.0]

    # note the time when we start sending messages, this is our reference point
    sessionStartTime = time.time()

    while len(clientConnections) > 0:
        # query the epoll object to get a list of events that we're interested in. The 1 indicates we're willing to
        # wait up to 1 second for an event to occur
        events = epoll.poll(1)
        for fileno, event in events:
            # when a client socket is available for writing, send a message to the server
            if event & select.EPOLLOUT:
                # check that we're still within the time duration to send messages, otherwise close the connection
                if (time.time() - sessionStartTime) < sessionLenth:
                    clientConnections[fileno].send(message)
                    # tell epoll we're interested when the client socket has data waiting to be read
                    epoll.modify(fileno, select.EPOLLIN | select.EPOLLET)
                    # update the number of requests, amount of data sent and the time the message was sent
                    requestsSent, bytesSent, timeSent, timeAck, totalResponseTime = connectionStatistics[fileno]
                    requestsSent += 1
                    bytesSent += len(message)
                    timeSent = time.time()
                    connectionStatistics[fileno] = [requestsSent, bytesSent, timeSent, timeAck, totalResponseTime]
                else:
                    clientConnections[fileno].close()
                    epoll.unregister(fileno)
                    clientConnections.pop(fileno, None)
            # read data off a socket when it is ready to read
            elif event & select.EPOLLIN:
                message = clientConnections[fileno].recv(BUFFER_SIZE)
                # tell epoll we're interested when the client socket is ready for writing
                epoll.modify(fileno, select.EPOLLOUT | select.EPOLLET)
                # update the time the message was acknowledged by the server and calculate response time
                requestsSent, bytesSent, timeSent, timeAck, totalResponseTime = connectionStatistics[fileno]
                timeAck = time.time()
                totalResponseTime += (timeAck - timeSent)
                connectionStatistics[fileno] = [requestsSent, bytesSent, timeSent, timeAck, totalResponseTime]

    # after all client connections have closed, update the average response time for each connection
    for connection in connectionStatistics:
        requestsSent, bytesSent, timeSent, timeAck, totalResponseTime = connectionStatistics[connection]
        averageResponseTime = totalResponseTime/requestsSent
        dataQueue.put('%s,%s,%s\n' % (requestsSent, bytesSent, averageResponseTime))

    # close the log file
    dataQueue.put('kill')
    # give the thread time to finish when logging for many clients
    logDataThread.join()

if __name__ == '__main__':
    setUpClients()
