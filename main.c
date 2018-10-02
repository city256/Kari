#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h> /* close() */
#include <string.h> /* memset() */
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>

//#include "pkt_support.h"
#include "n_table.h"


typedef struct ReceiveQueue {
	Broadcast_PKT *buf;
	int qsize;
	int front;
	int rear;
	int count;
}ReceiveQueue;

#define INTERVAL 1
#define QUEUE_SIZE 20
#define NEXT(index,QSIZE)   ((index+1)%QSIZE)
#define BROAD_ADDR "255.255.255.255"

uint32_t seqno, host_Addr;
int recv_sock, send_sock;
struct sockaddr_in destAddr, srcAddr, hostAddr;
Broadcast_PKT recv_pkt, send_pkt, gen_pkt;
struct ifreq ifr;
char iface[] = IF_NAME;
location_table *ltb;

void send_socket_init();
void sendto_sock(Broadcast_PKT pkt, struct sockaddr_in destAddr);
void recv_socket_init();
Broadcast_PKT *recvfrom_sock(Broadcast_PKT *recv_pkt, struct sockaddr_in hostAddr);
void recv_pkt_handler(Broadcast_PKT *pkt, ReceiveQueue *queue, location_table *lt);
void pkt_generator(Broadcast_PKT *send_pkt, ReceiveQueue *queue, int interval);

void InitReciveQueue(ReceiveQueue *queue, int qsize);
int IsReciveEmpty(ReceiveQueue *queue);
int IsReciveFull(ReceiveQueue *queue);
void EnqueueRecive(ReceiveQueue *queue, Broadcast_PKT *data);
Broadcast_PKT DequeueRecive(ReceiveQueue *queue);
void DisposeQueueRecive(ReceiveQueue *queue);




void *pkt_recv_thread(ReceiveQueue *queue) {
	recv_socket_init();

	while (1) {

		recv_pkt_handler(recvfrom_sock(&recv_pkt, hostAddr), queue, ltb);
		
	}
	close(recv_sock);
}

void *pkt_send_thread(ReceiveQueue *queue) {
	send_socket_init();
	
	while (1) {
		if(!IsEmpty(queue))
			sendto_sock(DequeueRecive(queue), destAddr);
	}
	close(send_sock);
}


void main() {
	ReceiveQueue queue;

	ltb = newTable();
	pthread_t send_thr, recv_thr;
	InitReciveQueue(&queue, QUEUE_SIZE);


	if (send_thr = pthread_create(&send_thr, NULL, pkt_send_thread, &queue)) {
		perror("thread create failed\n");
		exit(0);
	}
	if (recv_thr = pthread_create(&recv_thr, NULL, pkt_recv_thread, &queue)) {
		perror("thread create failed\n");
		exit(0);
	}
	
	while (1) {
		pkt_generator(&gen_pkt, &queue, INTERVAL);	
	}
		
	DisposeQueueRecive(&queue);

}
 
///////////////////////////////////////////////////////////////////////////////////////////

void recv_socket_init() {

	int broadcast = 1;
	int rc;

	recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (recv_sock < 0) {
		printf("cannot open recv socket \n");
		exit(1);
	}
	
	if (setsockopt(recv_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast) == -1) {
		perror("setsockopt (SO_BROADCAST)");
		exit(1);
	}

	/* bind local server port */
	hostAddr.sin_family = AF_INET;
	hostAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	hostAddr.sin_port = htons(LOCAL_SERVER_PORT);

	rc = bind(recv_sock, (struct sockaddr *) &hostAddr, sizeof(hostAddr));
	if (rc < 0) {
		printf("cannot bind recv port 2\n");
		close(recv_sock);
		exit(1);
	}
	//printf("recv socket init done\n");
}

Broadcast_PKT *recvfrom_sock(Broadcast_PKT *recv_pkt, struct sockaddr_in hostAddr) {
	int destLen;

	destLen = sizeof(hostAddr);
	if (recvfrom(recv_sock, (char*)recv_pkt, sizeof(Broadcast_PKT), 0, (struct sockaddr *) &hostAddr, &destLen) < 0) {
		printf("cannot receive data \n");
		close(recv_sock);
	}
	
	return recv_pkt;
}

void send_socket_init() {
	
	struct hostent *h;
	int broadcast = 1;
	

	h = gethostbyname(BROAD_ADDR);

	printf("sending data to '%s'\n", inet_ntoa(*(struct in_addr *)h->h_addr_list[0]));

	destAddr.sin_family = AF_INET;
	memcpy((char *)&destAddr.sin_addr.s_addr, h->h_addr_list[0], h->h_length);
	destAddr.sin_port = htons(LOCAL_SERVER_PORT);

	/* socket creation */
	send_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (send_sock < 0) {
		printf("cannot open socket \n");
		exit(1);
	}
	strcpy(ifr.ifr_name, iface);
	if (ioctl(send_sock, SIOCGIFADDR, &ifr) < 0) {
		printf("ioctl error\n");
		exit(1);
	}
	

	if (setsockopt(send_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast) == -1) {
		perror("setsockopt (SO_BROADCAST)");
		exit(1);
	}

	printf("send socket init done\n");
}


void sendto_sock(Broadcast_PKT pkt, struct sockaddr_in destAddr) {
	
	if (sendto(send_sock, (char*)&pkt, sizeof(Broadcast_PKT) + 1, 0, (struct sockaddr *) &destAddr, sizeof(destAddr)) < 0) {
		printf("cannot send data \n");
		close(send_sock);
		exit(1);
	}
	//printf("send done\n");
	//close(send_sock);
}



void pkt_generator(Broadcast_PKT *send_pkt, ReceiveQueue *queue, int interval) {
	
	
	send_pkt->lox = 1;
	send_pkt->loy = 2;
	send_pkt->loz = 3;
	send_pkt->type = 0;
	send_pkt->orig_addr = inet_addr(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	if (seqno == 0) {	// 초기 시퀀스 번호는 0
		seqno++;
		send_pkt->orig_seqno = seqno;
	}
	else {				// 보낼대 마다 1씩 증가
		seqno++;
		send_pkt->orig_seqno = seqno;
	}
	
	EnqueueRecive(queue, send_pkt);	
	
	sleep(interval);
}

void recv_pkt_handler(Broadcast_PKT *pkt, ReceiveQueue *queue, location_table *lt) 
{
	lc_table * target_t = NULL;

	//printf("tbl entry = %d \n", lt->count);
	printf("[%s] : seqno=%d  [%d , %d , %d] entry=%d \n", inet_ntoa(srcAddr.sin_addr), pkt->orig_seqno, pkt->lox, pkt->loy, pkt->loz, lt->count);
	//printf("recv :[seq %d] [x %d] [y %d] [z %d] [src %s] \n", pkt->orig_seqno, pkt->lox, pkt->loy, pkt->loz, inet_ntoa(srcAddr.sin_addr));

	// 자신이 보낸 패킷이 아니면 받아서 비교함 
	if (pkt->orig_addr != inet_addr(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr)) && pkt->orig_addr != inet_addr("0.0.0.0"))
	{
		target_t = lc_table_find(lt, pkt->orig_addr);  // 테이블에서 해당 패킷의 IP 주소 검색 있으면 해당 엔트리, 없으면 NULL	
		// 테이블이 엔트리가 0개가 아니면 
			// 패킷의 IP 주소가 엔트리에 있으면 
		if (target_t != NULL) {
			// 패킷의 시퀀스 번호와 테이블의 시퀀스 번호 대조
			if (pkt->orig_seqno > target_t->orig_seqno) {
				lc_table_update(lt, target_t, pkt->orig_seqno, pkt->lox, pkt->loy, pkt->loz);
				EnqueueRecive(queue, pkt);
				//printf("update en Q\n");
			}
		}
		else {	// 패킷의 IP 주소가 엔트리에 없으면 테이블에 엔트리 삽입 
			lc_table_insert(lt, pkt);
			EnqueueRecive(queue, pkt);
			//printf("new entry en Q\n");
		}
		
	}
	

	
}

void InitReciveQueue(ReceiveQueue *queue, int qsize)
{
	queue->buf = (Broadcast_PKT*)malloc(sizeof(send_pkt)*qsize);
	queue->qsize = qsize;
	queue->front = queue->rear = 0; //front와 rear를 0으로 설정
	queue->count = 0;//보관 개수를 0으로 설정
}

int IsFull(ReceiveQueue *queue)
{
	return queue->count == queue->qsize;//보관 개수가 qsize와 같으면 꽉 찬 상태
}

int IsEmpty(ReceiveQueue *queue)
{
	return queue->count == 0;    //보관 개수가 0이면 빈 상태
}

void EnqueueRecive(ReceiveQueue *queue, Broadcast_PKT *data)
{	
	
	if (IsFull(queue))//큐가 꽉 찼을 때
	{
		printf("Q is Full\n");
		return;
	}
	memcpy(&queue->buf[queue->rear], data, sizeof(Broadcast_PKT));
	queue->rear = NEXT(queue->rear, queue->qsize); //rear를 다음 위치로 설정
	queue->count++;//보관 개수를 1 증가

}

Broadcast_PKT DequeueRecive(ReceiveQueue *queue)
{
	Broadcast_PKT re;
	if (IsEmpty(queue))//큐가 비었을 때
	{
		printf("Q is Empty\n");
		return;
	}
	re = queue->buf[queue->front];//front 인덱스에 보관한 값을 re에 설정
	queue->front = NEXT(queue->front, queue->qsize);//front를 다음 위치로 설정
	queue->count--;//보관 개수를 1 감소
	return re;
}

void DisposeQueueRecive(ReceiveQueue *queue)
{
	free(queue->buf);
}

