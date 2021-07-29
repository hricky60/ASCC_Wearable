// Speaker Server Code
// -------------------
// Includes up-to-date speaker server code
// Uses wav header struct for efficiency 

#include <stdio.h> 
#include <errno.h>
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

#define MAX 81000
#define PORT 3333
#define SA struct sockaddr

#define PATH "/home/rickysuave/Documents/OSUClasses/EmbeddedResearch/ESP32/C-Programs/server/audio/"
#define FILENAME "mono_reminder.wav"
#define SERVER_ADDR "192.168.1.144"

struct wav_header_t {
	uint32_t riff;			// Contains "RIFF" or 0x52494646 in big-endian form
	uint32_t total_size;
	uint32_t wave;			// Contains "WAVE" or 0x57415645 in big-endian form
	uint32_t fmt;			// Contains "fmt" or 0x666d7420 in big endian form
	uint32_t fmt_size;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_size;	// Number of bytes for one sample including both channels
	uint16_t bits_per_sample;
};

void printWavHeader(struct wav_header_t header){

	printf("\n===================\n");
	printf("   WAV Header    \n");
	printf("===================\n");

	printf("Total Size: %d\n", header.total_size);
	printf("FMT Size: %d\n", header.fmt_size);
	printf("Audio Format: %d\n", header.audio_format);
	printf("# of Channels: %d\n", header.num_channels);
	printf("Sample Rate: %d\n", header.sample_rate);
	printf("Bits per sample: %d\n", header.bits_per_sample);

	printf("===================\n\n");
}

// Function designed for chat between client and server. 
void wav_read_send(int sockfd) 
{
	const char *done = "done";

	FILE *wav;
	
	int file_len = strlen(PATH) + strlen(FILENAME);
	char *wavFile = (char *)malloc(file_len * sizeof(char));
	sprintf(wavFile, "%s", PATH);
	strcat(wavFile, FILENAME);
	
	printf("Opening %s\n", wavFile);
	wav = fopen(wavFile, "r");
	if (wav == NULL){
		fprintf(stderr, "ERROR Failed to open WAV file: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	// Read in WAV header
	struct wav_header_t header_pt1;
	ssize_t retsize = fread(&header_pt1, sizeof(char), sizeof(header_pt1), wav);
	if (retsize <= 0 || retsize != sizeof(header_pt1)){
		fprintf(stderr, "ERROR Failed to read header of WAV file: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (header_pt1.riff != 0x46464952 || header_pt1.wave != 0x45564157 || header_pt1.fmt != 0x20746d66){
		printf("ERROR Failed to store header of WAV file correctly\n");
		printf("RIFF: %x    WAVE: %x    FMT: %x\n", header_pt1.riff, header_pt1.wave, header_pt1.fmt);
		exit(1);
	}

	printWavHeader(header_pt1);

	char tmpChar = 0;
	int data_read = 0;
	do{

		if (fread(&tmpChar, sizeof(char), sizeof(char), wav) < 0){
			fprintf(stderr, "ERROR Failed to read from WAV file: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (tmpChar == 'd' && data_read == 0)
			data_read += 1;
		else if (tmpChar == 'a' && (data_read == 1 || data_read == 3))
			data_read += 1;
		else if (tmpChar == 't' && data_read == 2)
			data_read += 1;
		else
			data_read = 0;

	} while(data_read != 4);

	uint32_t data_size = 0; 
	retsize = fread(&data_size, sizeof(uint32_t), 1, wav);
	
	// Start Read of Audio Data
	printf("WAV data size before padding = %d\n", data_size);
	uint32_t new_data_size = 2*data_size;
	printf("WAV data size after padding = %d\n", new_data_size);


	printf("------Sending data size to ESP------\n");
	if(write(sockfd, &new_data_size, sizeof(new_data_size)) < 0){
			fprintf(stderr, "ERROR Data size write to socket failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
	}
	
	uint32_t tmp_size;
	READ4: retsize = read(sockfd, &tmp_size, sizeof(uint32_t));
	if(retsize <= 0){
		printf("ERROR Could not read confirmation of data size\n");
		printf("Errno: %d\n", errno);
		if(errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED){
			return errno;
		}
		goto READ4:
	}
	
	if(tmp_size != new_data_size){
		fprintf(stderr, "ERROR Data size of %d returned from client is incorrect: %s\n", tmp_size, strerror(errno));
		exit(EXIT_FAILURE);
	}else{
		printf("Correct audio size of %d sent to client\n", tmp_size);
		/*if(write(sockfd, done, 4) < 0){
			fprintf(stderr, "ERROR Done string write to socket failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}*/
	}
	printf("------Completed data size transfer------\n\n");

	sleep(3);

	printf("------Beginning read of WAV data------\n");
	int len = data_size;
	char *tmp = malloc(2*sizeof(char));
	char *audio_buf = (char *)malloc(new_data_size);
	char right_ch_padding[2] = {0x00, 0x00};
	do
	{
		retsize = fread(tmp, sizeof(char), 2*sizeof(char), wav);
		if(retsize < 0){
			fprintf(stderr, "ERROR Failed to read data from WAV file: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		memcpy(audio_buf, right_ch_padding, 2*sizeof(char));
		audio_buf += 2*sizeof(char);
		memcpy(audio_buf, tmp, retsize);
		audio_buf += retsize;

		len = len - retsize;
	} while(len != 0);
	printf("------Completed read of data from WAV file------\n\n");
	fclose(wav);
	audio_buf -= new_data_size; // Reset address to beginning of buffer


	/*printf("------Reading data pointer address of ESP------\n");
	char *data_address;

	printf("------Completed read of data pointer address------\n\n");
	*/

	printf("------Sending WAV data to ESP------\n");
	if(write(sockfd, audio_buf, new_data_size) < 0){
			fprintf(stderr, "ERROR Data write to socket failed: %s", strerror(errno));
			exit(EXIT_FAILURE);
	}

	printf("Waiting for audio transfer completion...\n");
	char buf[5] = {0x00, 0x00, 0x00, 0x00, '\0'};
	READ5: retsize = read(sockfd, buf, 5);
	printf("Read: %c%c%c%c%c\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
	if(retsize == 5){
		if(buf[0]=='d' && buf[1]=='o'  && buf[2]=='n' && buf[3]=='e'){
			printf("------Completed audio transfer------\n\n");
		}
	}else if(retsize <= 0){
		fprintf(stderr, "Error in audio transfer: %s\n", strerror(errno));
		if(errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED)
			return errno;
		goto READ5;
	}

	printf("------Beginning audio output onto speaker------\n");
	for(int i=0; i<1; i++){
		printf("Waiting for audio output completion...\n");
		READ6: retsize = read(sockfd, buf, 5);
		printf("Read: %c%c%c%c%c\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
		if(retsize == 5){
			if(buf[0]=='d' && buf[1]=='o'  && buf[2]=='n' && buf[3]=='e'){
				printf("Completed audio output cycle num. %d\n", i);
			}
		}else{
			fprintf(stderr, "Error in audio output: %s\n", strerror(errno));
			if(errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED)
				return errno;
			goto READ6;
		}
	}
	printf("------Completed all audio cycles------\n\n");

	// Free all allocated memory
	free(audio_buf);
	free(tmp);
	free(wavFile);
} 

/*int tcp_connect(){

	int sockfd, connfd, len; 
	struct sockaddr_in servaddr;
	
	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1){ 
		printf("Error: Socket creation failed...\n"); 
		exit(0);
	}
	else
		printf("Socket successfully created..\n");

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
		printf("Socket successfully binded to %s:%d..\n", SERVER_ADDR, PORT);

	return sockfd;
}

// Driver function 
int main() 
{
	int len, connfd;
	struct sockaddr_in cli;

	int sockfd = tcp_connect();
	
	while(1){
		
		// Now server is ready to listen and verification 
		if ((listen(sockfd, 5)) != 0) { 
			printf("Error: Listen failed...\n"); 
			exit(0); 
		} 
		else
			printf("Server listening..\n"); 
		len = sizeof(cli); 

		// Accept the data packet from client and verification 
		connfd = accept(sockfd, (SA *)&cli, &len); 
		if (connfd < 0) { 
			printf("Error: Server accept failed...\n"); 
			exit(0); 
		} 
		else
			printf("Server accepted the client...\n"); 

		// Function for chatting between client and server 
		wav_read_send(connfd);
		
		printf("Server closing....\n");
		break;
	}
	
	// After chatting close the socket 
	close(sockfd);
}*/
