#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 6001

int main() 
{
    int sockfd;
    struct sockaddr_in other_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&other_addr, 0, sizeof(other_addr));
    other_addr.sin_family = AF_INET;
    other_addr.sin_addr.s_addr = INADDR_ANY;
    other_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&other_addr, sizeof(other_addr)) == -1) 
    {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }



    close(sockfd);
    return 0;
}