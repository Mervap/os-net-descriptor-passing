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
#include <sys/epoll.h>
#include <sys/un.h>

#include "fd_wrapper.h"

static const int LISTEN_BACKLOG = 50;
static const int BUF_SIZE = 4096;

const char *SOCKET_ADDRESS = "/tmp/09F29-passing-socket-descriptor";

void send_all(int, const char *, int);

void print_help() {
    std::cout << "Usage: ./server" << std::endl;
}

fd_wrapper start_server() {

    fd_wrapper sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd.get() == -1) {
        throw std::runtime_error("Can't create socket");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_ADDRESS);

    if (bind(sfd.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
        throw std::runtime_error("Bind failed");
    }

    return sfd;
}

void echo(const fd_wrapper &reader, const fd_wrapper &writer) {
    char buf[BUF_SIZE];
    int bytes_read;

    while (true) {
        bytes_read = read(reader.get(), buf, BUF_SIZE);

        if (bytes_read == -1) {
            throw std::runtime_error("Read failed");
        }

        if (bytes_read == 0) {
            break;
        }

        send_all(writer.get(), buf, bytes_read);
    }
}

// https://stackoverflow.com/questions/37885831/ubuntu-linux-send-file-descriptor-with-unix-domain-socket
int send_fd(int socket, int fd) {
    msghdr socket_message{};
    iovec io_vector[1];
    cmsghdr *control_message = nullptr;
    char message_buffer[1];

    /* at least one vector of one byte must be sent */
    message_buffer[0] = 'F';
    io_vector[0].iov_base = message_buffer;
    io_vector[0].iov_len = 1;

    /* initialize message */
    memset(&socket_message, 0, sizeof(struct msghdr));
    socket_message.msg_iov = io_vector;
    socket_message.msg_iovlen = 1;

    /* provide space for the fd data */
    int buf_size;
    char buf[CMSG_SPACE(sizeof(fd))];

    buf_size = CMSG_SPACE(sizeof(fd));
    memset(buf, 0, buf_size);
    socket_message.msg_control = buf;
    socket_message.msg_controllen = buf_size;

    /* initialize data element for fd passing */
    control_message = CMSG_FIRSTHDR(&socket_message);
    control_message->cmsg_level = SOL_SOCKET;
    control_message->cmsg_type = SCM_RIGHTS;
    control_message->cmsg_len = CMSG_LEN(sizeof(fd));
    memcpy(CMSG_DATA(control_message), &fd, sizeof(fd));

    return sendmsg(socket, &socket_message, 0);
}


void wait_for_connections(const fd_wrapper &listener) {

    if (listen(listener.get(), LISTEN_BACKLOG) == -1) {
        throw std::runtime_error("Listen failed");
    }

    while (true) {
        fd_wrapper client = accept(listener.get(), nullptr, nullptr);

        if (client.get() == -1) {
            throw std::runtime_error("Accept failed");
        }

        int pipe_in_fd[2], pipe_out_fd[2];

        if (pipe(pipe_in_fd) == -1 || pipe(pipe_out_fd) == -1) {
            throw std::runtime_error("Create new pipe failed");
        }

        fd_wrapper pipe_in(pipe_in_fd[0]);
        fd_wrapper pipe_out(pipe_out_fd[1]);

        if (send_fd(client.get(), pipe_in_fd[1]) == -1 || send_fd(client.get(), pipe_out_fd[0]) == -1) {
            throw std::runtime_error("Can't send fd");
        }

        if (close(pipe_in_fd[1]) == -1 || close(pipe_out_fd[0]) == -1) {
            print_err("Can't close fd");
        }

        int pid = fork();
        if (pid == -1) {
            throw std::runtime_error("Fork failed");
        }

        if (pid == 0) {
            try {
                echo(pipe_in, pipe_out);
            } catch (std::runtime_error &e) {
                print_err(e.what());
            }
            exit(EXIT_SUCCESS);
        }
    }
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

    if (unlink(SOCKET_ADDRESS) == -1) {
        print_err("Can't unlink the path for the socket");
    }

    try {
        fd_wrapper listener = start_server();
        wait_for_connections(listener);
    } catch (std::runtime_error &e) {
        print_err(e.what());
        return EXIT_FAILURE;
    }
}

