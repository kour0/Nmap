#include "ping.h"

// Calcul du checksum pour l'en-tête ICMP avec les données fournies : b étant l'en-tête ICMP et len sa taille
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char *) buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

// Envoi d'un paquet ICMP Echo Request
void send_echo_request(int sockfd, struct sockaddr_in *dest_addr) {
    static int seq_no = 0; // Numéro de séquence pour les paquets ICMP
    char packet[TOTAL_ICMP_SIZE]; // Paquet ICMP
    struct icmp *icmp_hdr = (struct icmp *) packet; // En-tête ICMP

    // Configuration de l'en-tête ICMP
    memset(packet, 0, sizeof(packet));
    icmp_hdr->icmp_type = ICMP_ECHO;
    icmp_hdr->icmp_code = 0;
    icmp_hdr->icmp_cksum = 0;
    icmp_hdr->icmp_seq = htons(seq_no++);
    icmp_hdr->icmp_id = htons(getpid());

    // Insérer le timestamp juste après l'en-tête ICMP
    struct timeval *tv_send = (struct timeval *)(packet + ICMP_HEADER_SIZE);
    gettimeofday(tv_send, NULL);

    // Remplir le reste du paquet avec des données
    memset(packet + ICMP_HEADER_SIZE + TIMESTAMP_SIZE, 'A', ICMP_DATA_SIZE);

    icmp_hdr->icmp_cksum = checksum(packet, sizeof(packet));

    // Envoi du paquet
    if (sendto(sockfd, packet, sizeof(packet), 0, (struct sockaddr *) dest_addr, sizeof(*dest_addr)) <= 0) {
        perror("sendto failed");
    }

    // Afficher les informations du paquet ICMP Echo Request envoyé
    printf("Ping sent\n");
    printf("Sent %lu bytes\n", sizeof(packet));
    printf("Sequence number: %d\n", ntohs(icmp_hdr->icmp_seq));
    printf("ID: %d\n", ntohs(icmp_hdr->icmp_id));
    printf("Checksum: %d\n", ntohs(icmp_hdr->icmp_cksum));
    printf("Data: %.*s\n", ICMP_DATA_SIZE, packet + sizeof(struct icmp));

}

// Réception de la réponse ICMP Echo Reply
void receive_echo_reply(int sockfd) {
    struct timeval tv;
    fd_set readfds;
    char buffer[1024];
    int ret;

    tv.tv_sec = TIMEOUT_SECONDS;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    // Attente de la réponse ICMP Echo Reply avec un délai de &tv
    ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);

    if (ret == -1) {
        perror("select");
    } else if (ret == 0) {
        printf("Ping timed out.\n"); // Délai d'attente dépassé
    } else {
        if (FD_ISSET(sockfd, &readfds)) { // Réception d'un paquet ICMP
            // Récupération du paquet ICMP
            struct sockaddr_in from;
            socklen_t len = sizeof(from);
            if (recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *) &from, &len) >
                0) { // Lecture du paquet ICMP
                // Extraire le timestamp envoyé
                struct timeval tv_recv, *tv_send;
                gettimeofday(&tv_recv, NULL);

                struct ip *ip_hdr = (struct ip *) buffer;
                struct icmp *icmp_hdr = (struct icmp *) (buffer + sizeof(struct ip));

                // Vérification du type de message ICMP et de l'ID
                if (icmp_hdr->icmp_type == ICMP_ECHOREPLY && ntohs(icmp_hdr->icmp_id) == getpid()) {
                    printf("Ping reply received\n");

                    // On récupère le paquet ICMP Echo Reply
                    printf("Received %d bytes\n", ntohs(ip_hdr->ip_len));
                    printf("TTL: %d\n", ip_hdr->ip_ttl);

                    // On vérifie si le paquet ICMP Echo Reply est valide en vérifiant le checksum
                    unsigned short checksum_received = icmp_hdr->icmp_cksum;
                    icmp_hdr->icmp_cksum = 0;
                    unsigned short checksum_calculated = checksum(icmp_hdr,
                                                                  ntohs(ip_hdr->ip_len) - sizeof(struct ip));
                    icmp_hdr->icmp_cksum = checksum_received;
                    if (checksum_received != checksum_calculated) {
                        printf("Invalid checksum\n");
                        return;
                    }

                    // Affichage de l'adresse IP de l'émetteur
                    char sender_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from.sin_addr, sender_ip, sizeof(sender_ip));
                    printf("Received from: %s\n", sender_ip);

                    // Affichage du timestamp envoyé
                    tv_send = (struct timeval *)(buffer + (ip_hdr->ip_hl * 4) + ICMP_HEADER_SIZE);
                    long rtt_ms = (tv_recv.tv_sec - tv_send->tv_sec) * 1000 + (tv_recv.tv_usec - tv_send->tv_usec) / 1000;
                    printf("RTT: %ld ms\n", rtt_ms);


                } else {
                    printf("Received packet was not a ping reply or did not match the sent ID\n");

                    // On affiche le type de message ICMP reçu
                    if (icmp_hdr->icmp_type == ICMP_ECHOREPLY) {
                        printf("Received ICMP Echo Reply\n");
                    } else if (icmp_hdr->icmp_type == ICMP_ECHO) {
                        printf("Received ICMP Echo Request\n");
                    } else {
                        printf("Received ICMP type: %d\n", icmp_hdr->icmp_type);
                    }

                    // On affiche l'ID du message ICMP reçu
                    printf("Received ICMP ID: %d\n", ntohs(icmp_hdr->icmp_id));
                }
            } else {
                perror("recvfrom failed");
            }
        }
    }
}

/*int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <destination IP>\n", argv[0]);
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &dest_addr.sin_addr);

    send_echo_request(sockfd, &dest_addr);
    receive_echo_reply(sockfd);

    close(sockfd);
    return 0;
}*/
