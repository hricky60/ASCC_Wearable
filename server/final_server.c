// FINAL SERVER CODE
// -----------------
// Includes camera, speaker, and microphone components
// Uses TAGS to determine the data being read from socket

#include <stdio.h> 
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <sys/types.h> 
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "./include/final_speaker_server.h"


#define PORT 3333
#define SA struct sockaddr
#define SERVER_ADDR "192.168.90.246"

#define WAV_PATH "/home/rickysuave/Documents/OSUClasses/EmbeddedResearch/ESP32/C-Programs/server/audio/"
#define CAM_PATH "/home/rickysuave/Documents/OSUClasses/EmbeddedResearch/ESP32/C-Programs/server/images/"

#define DETECTED_RESPONSE "True"
#define FALSE_FALL_RESPONSE "False"

#define MAX 32 * 1024
#define IMAGE_NUM 10

const int headerSize = 44;

void wavHeader(char* header, int wavSize){

	header[0] = 'R';
	header[1] = 'I';
	header[2] = 'F';
	header[3] = 'F';
	
	unsigned int fileSize = wavSize + headerSize - 8;
	header[4] = (char)(fileSize & 0xFF);
	header[5] = (char)((fileSize >> 8) & 0xFF);
	header[6] = (char)((fileSize >> 16) & 0xFF);
	header[7] = (char)((fileSize >> 24) & 0xFF);
	
	header[8] = 'W';
	header[9] = 'A';
	header[10] = 'V';
	header[11] = 'E';
	header[12] = 'f';
	header[13] = 'm';
	header[14] = 't';
	header[15] = ' ';
	
	//Length of format data [16-19]
	header[16] = 0x10;
	header[17] = 0x00;
	header[18] = 0x00;
	header[19] = 0x00;
	
	//Type of format [20-21]
	header[20] = 0x01;
	header[21] = 0x00;
	
	//Number of channels [22-23]
	header[22] = 0x01;
	header[23] = 0x00;
	
	//Sample rate [24-27]
	header[24] = 0x28;
	header[25] = 0xA0;
	header[26] = 0x00;
	header[27] = 0x00;

	//(Sample rate * Bits per Sample * Num of Channels) / 8 [28-31]
	header[28] = 0xA0;
	header[29] = 0x80;
	header[30] = 0x02;
	header[31] = 0x00;

	//(Bits per Sample * Num of Channels) / 8 [32-33]
	header[32] = 0x04;
	header[33] = 0x00;
	
	//Bits per Sample [34-35]
	header[34] = 0x20;
	header[35] = 0x00;
	
	header[36] = 'd';
	header[37] = 'a';
	header[38] = 't';
	header[39] = 'a';
	
	//Data size [40-43]
	header[40] = (char)(wavSize & 0xFF);
	header[41] = (char)((wavSize >> 8) & 0xFF);
	header[42] = (char)((wavSize >> 16) & 0xFF);
	header[43] = (char)((wavSize >> 24) & 0xFF);  
}

char* replacewith(char *str, char find, char replace){
	
    char *current_pos = strchr(str,find);
    while (current_pos){
        *current_pos = replace;
        current_pos = strchr(current_pos,find);
    }
    
    //remove newline character from end of current time string
    char *end_string = str + (strlen(str)-1);
    *end_string = '\0';
    
    return str;
}

char* current_time_2_string(int data_type){
	
    char *currtime_string;
    char wav[5] = ".wav";
    char jpg[5] = ".jpg";
    static char capture[40];
    memset(capture, '\0', sizeof(capture));
    capture[0] = 0x63;
    capture[1] = 0x61;
    capture[2] = 0x70;
    capture[3] = 0x5f;
	
    time_t current_time = time(NULL);
    if (current_time == ((time_t)-1)){
        fprintf(stderr, "Failure to obtain the current time.\n");
        exit(EXIT_FAILURE);
    }

    // Convert to local time format
    currtime_string = ctime(&current_time);

    if (currtime_string == NULL){
		
        fprintf(stderr, "Failure to convert the current time.\n");
        exit(EXIT_FAILURE);
    }
    
    char space = ' ';
    char underscore = '_';
    currtime_string = replacewith(currtime_string, space, underscore);
    
    if(data_type == 0)
		strncat(currtime_string, jpg, sizeof(jpg));
    else
		strncat(currtime_string, wav, sizeof(wav));
    
    strncat(capture, currtime_string, strlen(currtime_string));
    printf("Current time string %s\n", capture);
    
    return capture;
}

// Function designed for chat between client and server. 
int func(int sockfd) 
{

	unsigned char buff[MAX];
	void *data_tag = malloc(8);
	void *data_size = malloc(4);
	
	FILE *file;
	const char *currtime_string;
	char file_name[150] = "";
	char num[6] = "";
	char header[headerSize];
	char *tag;
	
	uint32_t diff, size, count;
	ssize_t retsize;
	
	int error_num;
	int error_num_size = sizeof(error_num);
	int data_type = -1;
	int flag = 0;
	int pic_count = 0;
	while(1){
	
		printf("*** Starting new read of data ***\n\n");
		
		if(flag == 0)
		{
			printf("Reading data tag...\n");
			READ: retsize = read(sockfd, data_tag, 7);
			if(retsize <= 0){
				//getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error_num, &error_num_size);
				printf("Error: Could not read data tag\n");
				printf("Errno: %d\n", errno);
				//printf("Errno: %d\n", error_num);
				if(errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED)
					return errno;
				goto READ;
			}
			
			tag = (char *)data_tag;
			*(tag + 7 * sizeof(char)) = 0;
			printf("Read \"%s\" tag...\n", tag);
		}

		
		if(strstr(tag, "app_mic") != NULL){
			printf("MIC tag read from socket\n");
			data_type = 1;
			sprintf(file_name, "%s", WAV_PATH);
			strcat(file_name, current_time_2_string(data_type));
		}
		else if(strstr(tag, "app_cam") != NULL){
			printf("CAM tag read from socket\n");
			data_type = 0;
			flag = 1;
			sprintf(file_name, "%s", CAM_PATH);
			sprintf(num, "num%d", pic_count);
			strcat(file_name, num);
			strcat(file_name, current_time_2_string(data_type));
			printf("==========================================\n");
			printf("FILENAME: %s\n", file_name);
			printf("==========================================\n");
		}
		else{
			printf("Error: Incorrect data tag \"%s\"was read from socket\n", tag);
			break;
		}
		currtime_string = file_name;
		
		printf("Reading data size....\n");
		READ2: retsize = read(sockfd, data_size, sizeof(uint32_t));
		if(retsize <= 0){
			//getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error_num, &error_num_size);
			printf("Error: Could not read data size\n");
			printf("Errno: %d\n", errno);
			//printf("Errno: %d\n", error_num);
			if(errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED)
				return errno;
			goto READ2;

		}
		size = *(uint32_t*)data_size;
		printf("Size of data captured: %u Bytes\tHex value: 0x%08x\tNum Bytes read: %lu\n", size, size, retsize);
		
		file = fopen(currtime_string, "wb");
		if(file == NULL){
			printf("ERROR: fopen failed with errno = %d\n", errno);
			exit(EXIT_FAILURE);
		}
		printf("File created...\n");
		
		if(data_type == 1){
			wavHeader(header, (int)size);
			fwrite(header, sizeof(char), headerSize, file);
		}
		
		printf("Closing file\n");
		fclose(file);
		printf("Closed file\n");

		count = 0;
		diff = 0;
		while(1){
			
			printf("Opening file for appending\n");
			file = fopen(currtime_string, "a");
			printf("File opened for appending\n");
			
			bzero(buff, MAX);
			printf("Zeroed buffer for data input\n");
			
			diff = size - count;
			READ3: if (diff > 0 && diff < MAX)
				retsize = read(sockfd, buff, diff);
			else
				retsize = read(sockfd, buff, sizeof(buff));
			
			if(retsize <= 0){
				printf("Error: No data read from socket\n");
				printf("Errno: %d\n", errno);
				if(errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED)
					return errno;
				goto READ3;
			}
			
			fwrite(buff, sizeof(char), retsize, file);
			fclose(file);
			
			count += (uint32_t)retsize;
			printf("Size read from socket: %lu\n", retsize);
			printf("Data read so far: %d\n", count);
			printf("Data downloading....\t%d%%\n\n", count * 100 / size);
			
			if (count == size){
				printf("Read all contents from socket\n\n");
				break;
			}else if (count > size){
				printf("Error: Total bytes read from socket larger than data size\nTotal Bytes read: %u Bytes\n\n", count);
				diff = count - size;
				break;
			}
		}
		
		printf("*** Data Read Complete *** \n\n");
		
		if(retsize <= 0)
			break;
		
		if(data_type == 0)
			pic_count += 1;
		
		if(data_type == 1 || pic_count == IMAGE_NUM){
			if(write(sockfd, DETECTED_RESPONSE, sizeof(DETECTED_RESPONSE)) == -1){
					printf("Error: Write to socket failed...\n");
					break;
			}
			
			printf("*** Completed transmission of data and sent %s tag ***\n\n", DETECTED_RESPONSE);
			flag = 0;
			
			if(data_type == 0){
				wav_read_send(sockfd);
				pic_count = 0;
			}
		}
	}
	
	return retsize;
} 

// Driver function 
int main() 
{ 
	int sockfd, connfd, len; 
	struct sockaddr_in servaddr, cli;
	
	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("Error: Socket creation failed...\n"); 
		exit(0); 
	}
	else
		printf("Socket successfully created...\n"); 
	
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = inet_addr(SERVER_ADDR); // INADDR_ANY;
	servaddr.sin_port = htons(PORT); 

	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) { 
		printf("Error: Socket bind failed...\n"); 
		exit(0); 
	} 
	else
		printf("Socket successfully binded to %s:%d...\n", SERVER_ADDR, PORT);

	// Now server is ready to listen and verification 
	if ((listen(sockfd, 5)) != 0){ 
		printf("Error: Listen failed\n");  
	} 
	else
		printf("Server listening...\n");

	len = sizeof(cli); 
	while(1){

		ACCEPT: printf("Waiting for a client connection...\n");

		// Accept a new client connection at the binded address:port
		connfd = accept(sockfd, (SA*)&cli, &len); 
		if (connfd < 0){ 
			printf("Error: Server accept failed...\n"); 
			break; 
		} 
		else
			printf("Server accepted the client\n");

		// Setting a timeout for the receive function of the socket
		/*struct timeval tv;
		int timeout_in_seconds = 60;
		tv.tv_sec = timeout_in_seconds;
		tv.tv_usec = 0;
		setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);*/

		//Setting KEEP_ALIVE option on socket so heartbeat probing is enabled
		//Heartbeat interval will be every minute and consecutively checked every 5 seconds after 3 times
		int alive = 1;
		int time_interval_sec = 60;
		int time_after = 5;
		int retries = 3;
		setsockopt(connfd, SOL_SOCKET, SO_KEEPALIVE, &alive, sizeof(int));
		setsockopt(connfd, SOL_TCP, TCP_KEEPIDLE, &time_interval_sec, sizeof(int));
		setsockopt(connfd, SOL_TCP, TCP_KEEPINTVL, &time_after, sizeof(int));
		setsockopt(connfd, SOL_TCP, TCP_KEEPCNT, &retries, sizeof(int));

		// Function for chatting between client and server 
		int err = func(connfd);
		if(errno == EAGAIN || errno == EWOULDBLOCK){
			printf("ERROR: Socket connection did not responsed on time\n");
			shutdown(connfd, 0);
			close(connfd);
			goto ACCEPT;
		}
		else if (err < 0){
			printf("Error: Reading of data failed\n");
			break;
		}
	}
	
	// After chatting close the socket
	shutdown(sockfd, 0);
	close(sockfd);
}
