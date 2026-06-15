#include <stdio.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <unistd.h>
#include <linux/if.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <linux/if_ether.h>

#define ETHER_ALEN 6

typedef struct addr_info {
    int ifindex;
    char addr[INET6_ADDRSTRLEN];
    char brd[INET6_ADDRSTRLEN];
    char local[INET6_ADDRSTRLEN];

    struct addr_info *next;
} addrn_t;

typedef struct if_info {
    int ifindex;
    char ifname[IFNAMSIZ];

    unsigned char mac[ETHER_ALEN];
    unsigned char brd[ETHER_ALEN];
    unsigned int mtu;
    int is_up;

    addrn_t *addrs; 

    struct if_info *next;
} ifn_t;

// get net interfaces (return head of linked list)
ifn_t *get_if(int fd, char *buf, int bufsz, int *max_ifindex) {
    struct {
        struct nlmsghdr nlh; 
        struct ifinfomsg ifi; 
    } req = {
        .nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)), // macro for aligning
        .nlh.nlmsg_type = RTM_GETLINK,
        .nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
        .nlh.nlmsg_seq = 1,
        .ifi.ifi_family = AF_UNSPEC
    };
    if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        perror("no data"); close(fd);
        return NULL;
    };

    ifn_t* head = ((void *)0); ifn_t* tail = ((void *)0);
    int eod = 0;
    while (!eod) {
        int l = recv(fd, buf, bufsz, 0);
        if (l < 0) {
            break;
        }
        
        struct nlmsghdr* nh = (struct nlmsghdr *)buf;
        while (NLMSG_OK(nh, l)) {
            if (nh->nlmsg_type == NLMSG_DONE) {
                eod = 1; break;
            }

            if (nh->nlmsg_type == NLMSG_ERROR) {
                eod = 1; perror("kernel returned error");
                break;
            }

            if (nh->nlmsg_type == RTM_NEWLINK) {
                // jump from header to data using macro | hdr |> data (ifinfomsg)
                struct ifinfomsg* ifi = NLMSG_DATA(nh);

                ifn_t* new = malloc(sizeof(ifn_t));
                memset(new, 0, sizeof(ifn_t));

                if (ifi->ifi_index > *max_ifindex) { *max_ifindex = ifi->ifi_index; }
                new->ifindex = ifi->ifi_index;
                new->is_up = (ifi->ifi_flags & IFF_UP) ? 1 : 0;

                struct rtattr *rta = IFLA_RTA(ifi); int len = IFLA_PAYLOAD(nh); 
                while (RTA_OK(rta, len)) {                    
                    switch (rta->rta_type)
                    {
                    case IFLA_IFNAME:
                        strncpy(new->ifname, (char *)RTA_DATA(rta), IFNAMSIZ - 1);
                        break;
                    
                    case IFLA_ADDRESS:;
                        memcpy(new->mac, (unsigned char *)RTA_DATA(rta), ETH_ALEN);
                        break;

                    case IFLA_BROADCAST:;
                        memcpy(new->brd, (unsigned char *)RTA_DATA(rta), ETH_ALEN);
                        break;

                    case IFLA_MTU:;
                        new->mtu = *(unsigned int *)RTA_DATA(rta);
                        break;
                    }
                    rta = RTA_NEXT(rta, len);
                }
                
                if (head == NULL) {
                    head = new;
                    tail = new;
                } else {
                    tail->next = new;
                    tail = new; 
                }
            }
            nh = NLMSG_NEXT(nh, l);            
        }
    };

    return head;
};  

// get addrs binded on ifaces (return ptr to addr list)
addrn_t **get_addr(int fd, char *buf, int bufsz, int max_ifindex) {
    addrn_t **addrl = calloc(max_ifindex + 1, sizeof(addrn_t *));
    // ifaddrmsg for addr info
    struct {
        struct nlmsghdr nlh; 
        struct ifaddrmsg ifi; 
    } req_a = {
        .nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg)), // macro for aligning
        .nlh.nlmsg_type = RTM_GETADDR,
        .nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
        .nlh.nlmsg_seq = 1,
        .ifi.ifa_family = AF_UNSPEC
    };

    if (send(fd, &req_a, req_a.nlh.nlmsg_len, 0) < 0) {
        fprintf(stderr, "no data"); close(fd);
        return NULL;
    };

    int eod = 0;
    while (!eod) {
        int l = recv(fd, buf, bufsz, 0);
        if (l < 0) {
            break;
        }
        struct nlmsghdr* nh = (struct nlmsghdr *)buf;
        while (NLMSG_OK(nh, l)) {
            if (nh->nlmsg_type == NLMSG_DONE) {
                eod = 1; break;
            }

            if (nh->nlmsg_type == NLMSG_ERROR) {
                eod = 1; perror("kernel returned error");
                break;
            }

            if (nh->nlmsg_type == RTM_NEWADDR) {
                struct ifaddrmsg* ifa = NLMSG_DATA(nh);

                if ((int)ifa->ifa_index > max_ifindex) {
                    continue;
                }

                addrn_t* new = malloc(sizeof(addrn_t));
                memset(new, 0, sizeof(addrn_t));

                new->ifindex = ifa->ifa_index;

                struct rtattr *rta = IFA_RTA(ifa);
                int len = IFA_PAYLOAD(nh); 
                while (RTA_OK(rta, len)) {                    
                    switch (rta->rta_type)
                    {
                    case IFA_ADDRESS:
                        inet_ntop(ifa->ifa_family, RTA_DATA(rta), new->addr, INET6_ADDRSTRLEN);
                        break;
                    
                    case IFA_LOCAL:
                        inet_ntop(ifa->ifa_family, RTA_DATA(rta), new->local, INET6_ADDRSTRLEN);
                        break;
                    
                    case IFA_BROADCAST:
                        inet_ntop(ifa->ifa_family, RTA_DATA(rta), new->brd, INET6_ADDRSTRLEN);
                        break;
                    }
                    rta = RTA_NEXT(rta, len);
                }

                new->next = addrl[ifa->ifa_index];
                addrl[ifa->ifa_index] = new;
            }
            nh = NLMSG_NEXT(nh, l);
        }
    };

    return addrl;
}

// free resources
int free_if(addrn_t **addrl, ifn_t *head, int max_ifindex) {
    for (int i = 0; i <= max_ifindex; i++) {
    addrn_t *ip = addrl[i];
        while (ip != NULL) {
            addrn_t *tmp = ip->next;
            free(ip);
            ip = tmp;
        }
    }
    free(addrl);
    ifn_t *curr_if = head;
    while (curr_if != NULL) {
        ifn_t *tmp = curr_if->next;
        free(curr_if);
        curr_if = tmp;
    }

    return 0; 
}

// show info (ifaces and ips)
int show(int fd) {
    int bufsz = 1 << 12;
    char buf[bufsz]; int max_ifi = 0;

    ifn_t * ifhead = get_if(fd, buf, bufsz, &max_ifi);

    memset(buf, 0, bufsz);
    addrn_t **addrl = get_addr(fd, buf, bufsz, max_ifi);

    // print info logic
    ifn_t* curr_if = ifhead;
    while (curr_if != NULL) {
        printf("%d : %s < %s > mtu %d\n", 
               curr_if->ifindex, 
               curr_if->ifname, 
               curr_if->is_up ? "UP" : "DOWN",
               curr_if->mtu);
        
        printf("\tlink %02x:%02x:%02x:%02x:%02x:%02x brd %02x:%02x:%02x:%02x:%02x:%02x\n",
               curr_if->mac[0], curr_if->mac[1], curr_if->mac[2],
               curr_if->mac[3], curr_if->mac[4], curr_if->mac[5],
               curr_if->brd[0], curr_if->brd[1], curr_if->brd[2],
               curr_if->brd[3], curr_if->brd[4], curr_if->brd[5]);

        addrn_t *curr_ip = addrl[curr_if->ifindex];
        while (curr_ip != NULL) {
            printf("\tinet %s\n", (curr_ip->local[0] != '\0') ? curr_ip->local : curr_ip->addr);
            curr_ip = curr_ip->next;
        }

        printf("\n"); 
        curr_if = curr_if->next;
    };

    free_if(addrl, ifhead, max_ifi);
    return 0;
};

// set iface mtu to [mtu] val
void set_if_mtu(int fd, unsigned int ifindex, int mtu) {
    struct {
        struct nlmsghdr nlh; 
        struct ifinfomsg ifi; 
        char attrbuf[RTA_SPACE(sizeof(int))];
    } req = {
        .nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)) + RTA_SPACE(sizeof(int)), 
        .nlh.nlmsg_type = RTM_NEWLINK,
        .nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
        .nlh.nlmsg_seq = 1,
        .ifi.ifi_family = AF_UNSPEC,
        .ifi.ifi_index = (int)ifindex
    };

    int plen = RTA_LENGTH(sizeof(int));

    struct rtattr *rta = (struct rtattr *)req.attrbuf;
    rta->rta_type = IFLA_MTU;
    rta->rta_len = plen;

    memcpy(RTA_DATA(rta), &mtu, sizeof(mtu));

    if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        perror("netlink sending error");
        return;
    }

    char buf[1 << 12];
    int l = recv(fd, buf, sizeof(buf), 0);
    if (l < 0) {
        fprintf(stderr, "no data received");
        return;
    }

    struct nlmsghdr *nh = (struct nlmsghdr *)buf;
    if (nh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
        if (err->error != 0) {
            fprintf(stderr, "mtu changing err with code %d\n", err->error);
            return;
        }
    }
    printf("mtu changed to %d\n", mtu);
}

// set [flags] to iface [mask]
void set_ifa(int fd, unsigned ifindex, uint32_t flags, uint32_t mask) {    
    struct {
        struct nlmsghdr nlh; 
        struct ifinfomsg ifi; 
    } req = {
        .nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)), 
        .nlh.nlmsg_type = RTM_NEWLINK,
        .nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
        .nlh.nlmsg_seq = 1,
        .ifi.ifi_family = AF_UNSPEC,
        .ifi.ifi_index = (int)ifindex,
        .ifi.ifi_change = mask,
        .ifi.ifi_flags = flags,
    };

    if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        perror("netlink sending error\n");
        return;
    }

    char buf[1 << 12];
    int l = recv(fd, buf, sizeof(buf), 0);
    if (l < 0) {
        fprintf(stderr, "no data received\n");
        return;
    }

    struct nlmsghdr *nh = (struct nlmsghdr *)buf;
    if (nh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
        if (err->error == 0) {
            printf("ifa changed\n");
        } else { 
            printf("error: %s (%d)\n", strerror(-err->error), -err->error);
        }
    }
};

// listen to nl events
int listen_sk(int fd) {
    struct sockaddr_nl sa = {
        .nl_family = AF_NETLINK,
        .nl_pid = 0,
        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR,
    };

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "sk binding error\n");
        return -1;
    }

    char buf[1 << 12];
    while (1) {
        memset(buf, 0, sizeof(buf));

        int l = recv(fd, buf, sizeof(buf), 0);
        if (l < 0) {
            perror("err receiving nl message");
            break;
        }

        if (l == 0) {
            perror("nl socket closed by kernel\n");
            break;
        }

        struct nlmsghdr *nh = (struct nlmsghdr *)buf;
        while (NLMSG_OK(nh, l)) {
            switch (nh->nlmsg_type) {
                
                case RTM_NEWLINK: {
                    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(nh);
                    char ifname[IFNAMSIZ] = "unknown";

                    struct rtattr *rta = IFLA_RTA(ifi);
                    int rtal = IFLA_PAYLOAD(nh);

                    while (RTA_OK(rta, rtal)) {
                        if (rta->rta_type == IFLA_IFNAME) {
                            strncpy(ifname, (char *)RTA_DATA(rta), sizeof(ifname) - 1);
                            break;
                        }
                        rta = RTA_NEXT(rta, rtal);
                    }

                    char *status = (ifi->ifi_flags & IFF_UP) ? "\033[32mUP\033[0m" : "\033[31mDOWN\033[0m";
                    
                    printf("link > interface \033[1;34m%s\033[0m (idx: %d) changed status -> %s\n", 
                           ifname, ifi->ifi_index, status);
                    break;
                }

                case RTM_NEWADDR: {
                    struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nh);
                    char ip_str[INET_ADDRSTRLEN] = "0.0.0.0";

                    struct rtattr *rta = IFA_RTA(ifa);
                    int rtal = IFA_PAYLOAD(nh);

                    while (RTA_OK(rta, rtal)) {
                        if (rta->rta_type == IFA_LOCAL) {
                            inet_ntop(AF_INET, RTA_DATA(rta), ip_str, sizeof(ip_str));
                            break;
                        }
                        rta = RTA_NEXT(rta, rtal);
                    }

                    printf("addr > \033[32m+ added IP\033[0m: \033[1;33m%s/%d\033[0m on iface: %d\n", 
                           ip_str, ifa->ifa_prefixlen, ifa->ifa_index);
                    break;
                }

                case RTM_DELADDR: {
                    struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nh);
                    char ip_str[INET_ADDRSTRLEN] = "0.0.0.0";

                    struct rtattr *rta = IFA_RTA(ifa);
                    int rtal = IFA_PAYLOAD(nh);

                    while (RTA_OK(rta, rtal)) {
                        if (rta->rta_type == IFA_LOCAL) {
                            inet_ntop(AF_INET, RTA_DATA(rta), ip_str, sizeof(ip_str));
                            break;
                        }
                        rta = RTA_NEXT(rta, rtal);
                    }

                    printf("[ADDR] \033[31m- deleted IP\033[0m: \033[1;33m%s/%d\033[0m from iface: %d\n", 
                           ip_str, ifa->ifa_prefixlen, ifa->ifa_index);
                    break;
                }

                case NLMSG_DONE:
                    break;

                case NLMSG_ERROR:
                    fprintf(stderr, "error nl packet\n");
                    break;
            }

            nh = NLMSG_NEXT(nh, l);
        }
    }

    return 0;
}

// set IFF_UP flag to [up] (link set [] up/down)
void set_iffup(int fd, unsigned ifindex, int up) {
    set_ifa(fd, ifindex, up  ? IFF_UP : 0, IFF_UP);
};

// init listening socket
int init_socket() {
    int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (fd < 0) {
        perror("socket creating error");
    };
    return fd;
}

// close listening socket
int close_socket(int fd) {
    return close(fd);
}