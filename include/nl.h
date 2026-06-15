#ifndef NL_H
#define NL_H

int init_socket();
int close_socket(int fd);

int show(int fd);
void set_iffup(int fd, unsigned ifindex, int up);
void set_if_mtu(int fd, unsigned int ifindex, int mtu);

int listen_sk(int fd);
#endif 