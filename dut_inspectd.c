/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NETLINK_USER 31

struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
int sock_fd;
struct msghdr msg;

int main()
{
    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USER);
    if (sock_fd < 0)
        return -1;

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid();

    int ret = bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));
    if (ret)
        perror("Bind");
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;
    dest_addr.nl_groups = 0;

    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(getpagesize()));
    memset(nlh, 0, NLMSG_SPACE(getpagesize()));
    nlh->nlmsg_len = NLMSG_SPACE(getpagesize());
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    sendmsg(sock_fd, &msg, 0);

    while(1) {
        size_t n = recvmsg(sock_fd, &msg, 0);
        if (n <= 0)
            break;
        unsigned len = ((struct nlmsghdr *)msg.msg_iov->iov_base)->nlmsg_len;
        fwrite(NLMSG_DATA(nlh), 1, len, stdout);
    }
    close(sock_fd);

    return 0;
}