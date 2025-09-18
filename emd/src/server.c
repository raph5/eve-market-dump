
#define SERVER_BACKLOG 16

struct server {
  int sock_fd;
};

err_t server_tcp_init(in_port_t port, struct server *serv) {
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd == -1) {
    errmsg_fmt("server_tcp_init: socket: %s", strerror(errno));
    return E_ERR;
  }

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(port),
    .sin_addr = (struct in_addr) { .s_addr = htonl(INADDR_LOOPBACK) },
  };
  int rv = bind(sock_fd, (struct sockaddr *) &addr, sizeof(addr));
  if (rv == -1) {
    errmsg_fmt("server_tcp_init: bind: %s", strerror(errno));
    return E_ERR;
  }

  rv = listen(sock_fd, SERVER_BACKLOG);
  if (rv == -1) {
    errmsg_fmt("server_tcp_init: listen: %s", strerror(errno));
    return E_ERR;
  }

  assert(serv != NULL);
  *serv = (struct server) { .sock_fd = sock_fd };
  return E_OK;
}

err_t server_tcp_deinit(const struct server *serv) {
  assert(serv != NULL);

  int rv = close(serv->sock_fd);
  if (rv == -1) {
    errmsg_fmt("server_tcp_deinit: close: %s", strerror(errno));
    return E_ERR;
  }

  return E_OK;
}

err_t server_tcp_handle(const struct server *serv) {
  assert(serv != NULL);

  struct sockaddr conn_addr;
  socklen_t conn_addr_len = sizeof(conn_addr);
  int conn_fd = accept(serv->sock_fd, &conn_addr, &conn_addr_len);
  if (conn_fd == -1) {
    errmsg_fmt("server_tcp_handle: accept: %s", strerror(errno));
    return E_ERR;
  }
  
  char request_buf[1024];
  ssize_t request_len = recv(conn_fd, request_buf, sizeof(request_buf), 0);
  if (request_len == -1) {
    errmsg_fmt("server_tcp_handle: recv: %s", strerror(errno));
    return E_ERR;
  }

  struct string response = string_new("HTTP/1.1 200 E_OK\r\nContent-Type: text/plain\r\n\r\nHello there?\r\n");
  ssize_t rv = send(conn_fd, response.buf, response.len, 0);
  if (rv == -1) {
    errmsg_fmt("server_tcp_handle: send: %s", strerror(errno));
    return E_ERR;
  }

  rv = close(conn_fd);
  if (rv == -1) {
    errmsg_fmt("server_tcp_handle: close: %s", strerror(errno));
    return E_ERR;
  }

  return E_OK;
}
