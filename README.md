# UDP File Transfer

This program transfers a file between a sender and a receiver by using [Stop-and-wait](https://en.wikipedia.org/wiki/Stop-and-wait_ARQ) and [Go-Back-N](https://en.wikipedia.org/wiki/Go-Back-N_ARQ) algorithms. UDP is used as the underlying transport layer protocol.

###### Usage

The receiver must be started first. The sender process needs the ip address and port of the receiver process.

```
./receiver [-p port] [-m mode]
./sender [-p port] [-h hostname] [-f filename] [-m mode]
```

*mode* is a positive integer denoting the **N** value of the Go-Back-N algorithm. Setting mode=1 will therefore transform the algorithm into Stop-and-wait.
