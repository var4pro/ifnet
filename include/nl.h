#ifndef NL_H
#define NL_H

extern int init_socket();
int close_socket(int fd);

int show(int fd);
void set_iffup(int fd, unsigned ifindex, int up);
void set_if_mtu(int fd, unsigned int ifindex, int mtu);

int listen_sk(int fd);
int set_tc(int fd, unsigned int ifindex, unsigned int speed);
int del_qdisc(int fd, unsigned int ifindex);
#endif 