//
// Created by dumpling on 20.05.19.
//

#include <iostream>
#include <cstring>
#include <limits>
#include <sys/socket.h>
#include <unistd.h>

void print_err(const std::string &message) {
    std::cerr << "\033[31m" << message;
    if (errno) {
        std::cerr << ": " << std::strerror(errno);
    }
    std::cerr << "\033[0m" << std::endl;
}

void send_all(int fd, const char *buf, int size) {
    int total = 0;
    while (total < size) {
        int was_send = write(fd, buf + total, size - total);
        if (was_send == -1) {
            throw std::runtime_error("Write failed");
        }

        total += was_send;
    }
}