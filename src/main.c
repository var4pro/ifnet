#include <stdio.h>
#include "nl.h"
#include <net/if.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

void print_usage(const char *prog_name) {
    printf("usage:\n");
    printf("  %s show                     - show ip and ifaces\n", prog_name);
    printf("  %s listen                   - listen to netlink updates\n", prog_name);
    printf("  %s set <ifname> up          - set iface up/down\n", prog_name);
    printf("  %s set <ifname> mtu <mtu>   - set iface mtu to <mtu>\n", prog_name);
    printf("  %s set <ifname> speed <N>   - set iface speed to <N> bytes/sec\n", prog_name);
    printf("  %s del <ifname> qdisc       - delete qdisc on iface\n", prog_name);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    int fd = init_socket();
    if (strcmp(argv[1], "show") == 0) {
        show(fd);
    } 

    else if (strcmp(argv[1], "del") == 0) {
        if (argc < 4) {
            fprintf(stderr, "del usage: del <ifname> qdisc\n");
        }

        if (strcmp(argv[3], "qdisc") == 0) {
            char *ifname = argv[2];
            unsigned int ifi = if_nametoindex(ifname);

            del_qdisc(fd, ifi);
        }
    }
    
    else if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            fprintf(stderr, "set requires ifname and action\n");
            print_usage(argv[0]);
            close_socket(fd);
            return 1;
        }

        char *ifname = argv[2];
        unsigned int ifi = if_nametoindex(ifname);

        char *act = argv[3]; 
        if (strcmp(act, "up") == 0) {
            set_iffup(fd, ifi, 1);

        } else if (strcmp(act, "down") == 0) {
            set_iffup(fd, ifi, 0);

        } else if (strcmp(act, "mtu") == 0) {
            if (argc < 5) {
                fprintf(stderr, "mtu requires new mtu size\n");
                return 1;
            }

            char *endptr;
            long val = strtol(argv[4], &endptr, 10);
            if (endptr == argv[4] || *endptr != '\0') {
                fprintf(stderr, "error: invalid mtu value '%s' (must be a number)\n", argv[4]);
                return 1;
            }

            if (val < 68 || val > 65535) {
                fprintf(stderr, "error: mtu %ld is out of range (allowed: 68 - 65535)\n", val);
                return 1;
            }
            int mtu_size = (int)val;
            set_if_mtu(fd, ifi, mtu_size);

        } else if (strcmp(act, "speed") == 0) { 
            if (argc < 5) {
                fprintf(stderr, "speed requires value in bytes/sec\n");
                close_socket(fd);
                return 1;
            }

            char *endptr;
            long val = strtol(argv[4], &endptr, 10);
            if (endptr == argv[4] || *endptr != '\0') {
                fprintf(stderr, "error: invalid speed value '%s' (must be a number)\n", argv[4]);
                close_socket(fd);
                return 1;
            }

            if (val <= 0 || val > INT_MAX) {
                fprintf(stderr, "error: speed %ld is out of range\n", val);
                close_socket(fd);
                return 1;
            }

            unsigned int speed_bytes = (unsigned int)val;
            set_tc(fd, ifi, speed_bytes);
        
        } else {
            fprintf(stderr, "invalid action: %s use <up|down|mtu|speed>\n", act);
            return 1;
        } 

    } else if (strcmp(argv[1], "listen") == 0) {
        listen_sk(fd);
    }
    
    else {
        fprintf(stderr, "invalid command '%s'\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
    close_socket(fd);

    return 0;
}