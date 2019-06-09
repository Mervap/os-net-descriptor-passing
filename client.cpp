//
// Created by dumpling on 30.04.19.
//

#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>

#include "fd_wrapper.h"

static const int BUF_SIZE = 4096;

const char *SOCKET_ADDRESS = "/tmp/2DFL35-passing-socket-descriptor";

void print_err(const std::string &);

void send_all(int, const char *, int);

void print_help() {
    std::cout << "Usage: ./client" << std::endl;
}

fd_wrapper open_connection() {

    fd_wrapper sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd.get() == -1) {
        throw std::runtime_error("Can't create socket");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_ADDRESS);

    if (connect(sfd.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
        throw std::runtime_error("Can't connect to the server");
    }

    return sfd;
}

void send_and_receive(const fd_wrapper &reader, const fd_wrapper &writer, std::string &message) {

    auto m = message.c_str();
    int size = message.length();
    send_all(writer.get(), m, size);

    char buf[BUF_SIZE];
    int bytes_read;
    int sum = 0;
    while (sum < size) {
        bytes_read = read(reader.get(), buf, BUF_SIZE);

        if (bytes_read == -1) {
            throw std::runtime_error("Read failed");
        }

        if (bytes_read == 0) {
            break;
        }

        for (int i = 0; i < bytes_read; ++i) {
            std::cout << buf[i];
        }
        sum += bytes_read;
    }
}

int recv_fd(int socket) {

    int fd, buf_size;
    msghdr socket_message{};
    iovec io_vector[1];
    cmsghdr *control_message = nullptr;
    char message_buffer[1];
    char buf[CMSG_SPACE(sizeof(fd))];

    /* start clean */
    memset(&socket_message, 0, sizeof(msghdr));
    buf_size = CMSG_SPACE(sizeof(fd));
    memset(buf, 0, buf_size);

    /* setup a place to fill in message contents */
    io_vector[0].iov_base = message_buffer;
    io_vector[0].iov_len = 1;
    socket_message.msg_iov = io_vector;
    socket_message.msg_iovlen = 1;

    /* provide space for the ancillary data */
    socket_message.msg_control = buf;
    socket_message.msg_controllen = buf_size;

    if (recvmsg(socket, &socket_message, MSG_CMSG_CLOEXEC) == -1) {
        throw std::runtime_error("Recvmsg failed");
    }

    if (message_buffer[0] != 'F') {
        throw std::runtime_error("Wrong message failed");
    }

    if (socket_message.msg_flags & MSG_CTRUNC) {
        throw std::runtime_error("Lack of space in the buffer");
    }

    /* iterate ancillary elements */
    for (control_message = CMSG_FIRSTHDR(&socket_message);
         control_message != nullptr;
         control_message = CMSG_NXTHDR(&socket_message, control_message)) {
        if ((control_message->cmsg_level == SOL_SOCKET) &&
            (control_message->cmsg_type == SCM_RIGHTS)) {
            fd = *((int *) CMSG_DATA(control_message));
            memcpy(&fd, CMSG_DATA(control_message), sizeof(fd));
            return fd;
        }
    }

    throw std::runtime_error("Control message is empty");
}

int main(int argc, char **argv) {

    if (argc > 1) {
        if (std::string(argv[1]) == "help") {
            print_help();
            return EXIT_SUCCESS;
        } else {
            print_err("Wrong argument, use help");
            return EXIT_FAILURE;
        }
    }

    if (argc > 2) {
        print_err("Wrong arguments, use help");
        return EXIT_FAILURE;
    }


    std::string message;
    try {
        fd_wrapper sender = open_connection();
        fd_wrapper pipe_out(recv_fd(sender.get()));
        fd_wrapper pipe_in(recv_fd(sender.get()));

        if (pipe_in.get() == -1 || pipe_out.get() == -1) {
            throw std::runtime_error("Wrong fd was received");
        }

        while (getline(std::cin, message)) {
            message.push_back('\n');
            send_and_receive(pipe_in, pipe_out, message);
        }
    } catch (std::runtime_error &e) {
        print_err(e.what());
        return EXIT_FAILURE;
    }

}

