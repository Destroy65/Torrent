
// Trivial Torrent

// TODO: some includes here

#include "file_io.h"
#include "logger.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <assert.h>
#include <net/if.h>
#include <sys/ioctl.h>

/**
 * This is the magic number (already stored in network byte order).
 * See https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
 */
static const uint32_t MAGIC_NUMBER = 0xde1c3231; // = htonl(0x31321cde);

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;

enum { RAW_MESSAGE_SIZE = 13 };

//Struct to manage data better
struct /*__attribute__((__packed__))*/ msg_t {
	uint32_t magic_number;
	uint8_t message_code;
	uint64_t block_number;
};

int main(int argc, char **argv) {

	set_log_level(LOG_DEBUG);

	log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "J. DOE and J. DOE");
	int n = 0;
	struct torrent_t torrent;
	switch(argc){
		case 2:
			log_printf(LOG_INFO, "Client mode");
			//Parse ttorrent file name to downloaded file name
			assert(argv[1] != NULL);
			for (int i = 0; i < (int)strlen(argv[1]); i++) {
				if(argv[1][i] == '.') {
					n = i;
				}
			}
			assert(n != 0);
			char* downloaded_file_name = malloc((size_t)n);
			if (downloaded_file_name == NULL) {
				log_printf(LOG_INFO, "ERROR: memory problems");
				exit(EXIT_FAILURE);
			}
		
			if (strncpy(downloaded_file_name, argv[1], (size_t)n) == NULL) {
				log_printf(LOG_INFO, "Path unsuccesfully parsed");
			}
			if (create_torrent_from_metainfo_file(argv[1], &torrent, downloaded_file_name) == -1) {
				log_printf(LOG_INFO, "ERROR: Torrent not created");
				free(downloaded_file_name);
				exit(EXIT_FAILURE);
			}
			log_printf(LOG_INFO, "Torrent succesfully created");
			free(downloaded_file_name);
			struct sockaddr_in addr;
			int s;
			for(uint64_t i = 0; i < torrent.peer_count; i++) {
				//Check if we have all blocks
				int all_b_nice = 1;
				for (uint64_t j = 0; j < torrent.block_count; j++) {
					if (torrent.block_map[j] == 0) {
						all_b_nice = 0;
						break;
					}
				}
				if (all_b_nice == 1) {
					log_printf(LOG_INFO, "All Blocks Nice");
					if (destroy_torrent(&torrent) == -1) {
						perror("ERROR: Torrent not destroyed");
						exit(EXIT_FAILURE);
					}
					exit(EXIT_SUCCESS);
				}
				//Create socket and handle error
				if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
					perror("ERROR: Socket not created");
					if (destroy_torrent(&torrent) == -1) {
						perror("ERROR: Torrent not destroyed");
					}
					exit(EXIT_FAILURE);
				}
				//Connect socket and handle error
				log_printf(LOG_INFO, "Socket succesfully created");
				addr.sin_family = AF_INET;
				addr.sin_addr.s_addr = *(in_addr_t*)torrent.peers[i].peer_address;
				addr.sin_port = torrent.peers[i].peer_port;
				if (connect(s, (struct sockaddr*)&addr, (socklen_t)sizeof(addr)) == -1) {
					perror("ERROR: Socket not connected");
					if (close(s) == -1) {
						perror("ERROR: Socket not closed");
					}
					continue;
				}
				//Prepare data to send
				log_printf(LOG_INFO, "Socket succesfully connected");
				uint8_t buffer[RAW_MESSAGE_SIZE];
				struct msg_t sibus;
				struct block_t block;
				sibus.magic_number = MAGIC_NUMBER;
				for (uint64_t j = 0; j < torrent.block_count; j++) {
					if (torrent.block_map[j] != 0) {
						log_printf(LOG_INFO, "NICE Block Bro");
						continue;
					}
					sibus.message_code = MSG_REQUEST;
					sibus.block_number = j;
					uint64_t temp = ((uint64_t)htonl((uint32_t)sibus.block_number) << 32) + (uint64_t)htonl((uint32_t)(sibus.block_number >> 32));
					if (memcpy(&buffer, &sibus.magic_number, sizeof(sibus.magic_number)) == NULL ||
						memcpy(&buffer[sizeof(sibus.magic_number)], &sibus.message_code, sizeof(sibus.message_code)) == NULL ||
						memcpy(&buffer[sizeof(sibus.magic_number) + sizeof(sibus.message_code)], &temp, sizeof(temp)) == NULL) {
							log_printf(LOG_INFO, "ERROR: MSG_T unsuccesfully copied");
							continue;
					}
					//Send data and handle error
					if (send(s, &buffer, RAW_MESSAGE_SIZE,0) == -1) {
						perror("ERROR: Message not sent");
						continue;
					}
					log_printf(LOG_INFO, "Message succesfully sent");
					//Receive response and handle error
					if (recv(s, &buffer, RAW_MESSAGE_SIZE, MSG_WAITALL) == -1) {
						perror("ERROR: Message not received");
						continue;
					}
					log_printf(LOG_INFO, "Message succesfully received");
					//Pass data to struct
					if (memcpy(&sibus.magic_number, &buffer, sizeof(sibus.magic_number)) == NULL ||
						memcpy(&sibus.message_code, &buffer[sizeof(sibus.magic_number)], sizeof(sibus.message_code)) == NULL ||
						memcpy(&temp, &buffer[sizeof(sibus.magic_number) + sizeof(sibus.message_code)], sizeof(temp)) == NULL) {
							log_printf(LOG_INFO, "ERROR: MSG_T unsuccesfully copied");
							continue;
					}
					sibus.block_number = ((uint64_t)ntohl((uint32_t)temp) << 32) + (uint64_t)ntohl((uint32_t)(temp >> 32));
					//Handle possible errors with data
					if (sibus.magic_number != MAGIC_NUMBER) {
						log_printf(LOG_INFO, "Block unsuccesfully received");
						continue;
					}
					if (sibus.message_code == MSG_RESPONSE_NA) {
						log_printf(LOG_INFO, "Block unsuccesfully received");
						continue;
					}
					//Receive and store data blocks and handle errors
					else if (sibus.message_code == MSG_RESPONSE_OK) {
						log_printf(LOG_INFO, "Block received");
						block.size = get_block_size(&torrent, j);
						if (recv(s, &block.data, block.size, MSG_WAITALL) == -1) {
							perror("ERROR: Block unsuccesfully received");
							continue;
						}
						log_printf(LOG_INFO, "Block succesfully received\n");
						if (store_block(&torrent, j, &block) == -1) {
							perror("ERROR: Block unsuccesfully stored");
							continue;
						}
						log_printf(LOG_INFO, "Block succesfully stored");
					}
					else {
						log_printf(LOG_INFO, "Message format error");
						continue;
					}	
				}
				if (close(s) == -1) {
					perror("ERROR: Socket not closed");
				}
			}
			if (destroy_torrent(&torrent) == -1) {
				perror("ERROR: Torrent not destroyed");
				exit(EXIT_FAILURE);
			}
			exit(EXIT_SUCCESS);
			break;
		case 4:
			log_printf(LOG_INFO, "Server mode");
			//Parse ttorrent file name to downloaded file name
			if (strcmp(argv[1], "-l") == -1) {
				log_printf(LOG_INFO, "ERROR: Invalid arguments");
				exit(EXIT_FAILURE);
			}
			assert(argv[3] != NULL);
			for (int i = 0; i < (int)strlen(argv[3]); i++) {
				if (argv[3][i] == '.') {
					n = i;
				}
			}
			assert(n != 0);
			char* file_name = malloc((size_t)n);
			if (file_name == NULL) {
				log_printf(LOG_INFO, "ERROR: memory problems");
				exit(EXIT_FAILURE);
			}
			if (strncpy(file_name, argv[3], (size_t)n) == NULL) {
				log_printf(LOG_INFO, "ERROR: Path unsuccesfully parsed");
			}
			if (create_torrent_from_metainfo_file(argv[3], &torrent, file_name) == -1) {
				perror("ERROR: Torrent not created");
				free(file_name);
				exit(EXIT_FAILURE);
			}
			//Create socket and handle errors
			int slt, nws;
			if ((slt = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
					perror("ERROR: Socket not created");
					if (destroy_torrent(&torrent) == -1) {
						perror("ERROR: Torrent not destroyed");
					}
					exit(EXIT_FAILURE);
			}
			log_printf(LOG_INFO, "Socket succesfully created");
			//Set socket to Non-Block and handle errors
			if (fcntl(slt, F_SETFL, O_NONBLOCK) == -1) {
				perror("ERROR: Non-Blocking flag not set");
				if (close(slt) == -1) {
					perror("ERROR: Socket not closed");
				}
				if (destroy_torrent(&torrent) == -1) {
					perror("ERROR: Torrent not destroyed");
				}
				exit(EXIT_FAILURE);
			}
			log_printf(LOG_INFO, "Non-Blocking flag succesfully set");
			//Bind socket and handle errors
			struct sockaddr_in saddr;
			struct sockaddr_storage caddr;
			socklen_t addrl;
			memset(&saddr, 0, sizeof(saddr));
			saddr.sin_family = AF_INET;
			saddr.sin_addr.s_addr = 0x0;
			saddr.sin_port = htons((uint16_t)atoi(argv[2]));
			addrl = sizeof(saddr);
			if (bind(slt, (struct sockaddr*)&saddr, addrl) == -1) {
				perror("ERROR: Socket not bind");
				if (close(slt) == -1) {
					perror("ERROR: Socket not closed");
				}
				if (destroy_torrent(&torrent) == -1) {
					perror("ERROR: Torrent not destroyed");
				}
				exit(EXIT_FAILURE);
			}
			log_printf(LOG_INFO, "Socket succesfully bind");
			//Set socket to listen and handle errors
			int s_max = 5;
			if (listen(slt, SOMAXCONN) == -1) {
				perror("ERROR: Socket is not listening");
				if (close(slt) == -1) {
					perror("ERROR: Socket not closed");
				}
				if (destroy_torrent(&torrent) == -1) {
					perror("ERROR: Torrent not destroyed");
				}
				exit(EXIT_FAILURE);
			}
			log_printf(LOG_INFO, "Socket is listening");
			//Prepare to manage data
			struct pollfd *ps = malloc(sizeof(*ps) * (uint32_t)s_max); ////
			if (ps == NULL) {
				log_printf(LOG_INFO, "ERROR: memory problems");
				if (close(slt) == -1) {
					perror("ERROR: Socket not closed");
				}
				if (destroy_torrent(&torrent) == -1) {
					perror("ERROR: Torrent not destroyed");
				}
				exit(EXIT_FAILURE);
			}
			ps[0].fd = slt;
			ps[0].events = POLLIN;
			int s_count = 1;
			struct msg_t *sibus = malloc(sizeof(*sibus) * (uint32_t)s_max);
			if (sibus == NULL) {
				log_printf(LOG_INFO, "ERROR: memory problems");
				if (close(slt) == -1) {
					perror("ERROR: Socket not closed");
				}
				if (destroy_torrent(&torrent) == -1) {
					perror("ERROR: Torrent not destroyed");
				}
				free(ps);
				exit(EXIT_FAILURE);
			}
			uint64_t temp;
			struct block_t block;
			uint8_t buffer[RAW_MESSAGE_SIZE];
			//Start polling
			while (1) {
				log_printf(LOG_INFO, "POLLING");
				//Poll and handle errors
				if (poll(ps, (nfds_t)s_count, -1) == -1) {
					perror("ERROR: Poll failed");
					for (int i = 0; i < s_count; i++) {
						if (close(ps[i].fd) == -1) {
							perror("ERROR: Socket not closed");
						}
					}
					free(ps);
					free(sibus);
					if (destroy_torrent(&torrent) == -1) {
						perror("ERROR: Torrent not destroyed");
					}
					exit(EXIT_FAILURE);
				}
				log_printf(LOG_INFO, "Event has occurred");
				//Check if event occurrs
				for (int i = 0; i < s_count; i++) {
					//Accept new connection and handle errors
					if ((ps[i].fd == slt) && (ps[i].revents == POLLIN)) {
						addrl = sizeof(caddr);
						if (s_count < s_max) {
							if ((nws = accept(slt, (struct sockaddr*)&caddr, &addrl)) == -1) {
								perror("ERROR: Connection not accept");
								continue;
							}
							if (fcntl(slt, F_SETFL, O_NONBLOCK) == -1) {
								perror("ERROR: Non-Blocking flag not set");
								if (close(nws) == -1) {
									perror("ERROR: Socket not closed");
								}
								continue;
							}
							log_printf(LOG_INFO, "Connection accepted");
							ps[s_count].fd = nws;
							ps[s_count].events = POLLIN;
							s_count++;
							ps[i].revents = 0;
						}
					}
					//Receive request and handle errors
					else if ((ps[i].fd != slt) && (ps[i].revents == POLLIN)) {
						memset(&buffer, 0, RAW_MESSAGE_SIZE);
						ssize_t rcv = recv(ps[i].fd, &buffer, RAW_MESSAGE_SIZE, 0);
						if (rcv == -1) {
							perror("ERROR: Message not received");
							continue;
						}
						//Close connection if client does
						if (rcv == 0) {
							log_printf(LOG_INFO, "Connection Closed");
							if (close(ps[i].fd) == -1) {
								perror("ERROR: Socket not closed");
							}
							if (i != (s_count-1)) {
								ps[i].fd = ps[s_count-1].fd;
								ps[i].events = ps[s_count-1].events;
								ps[i].revents = ps[s_count-1].revents;
								sibus[i].block_number = sibus[s_count-1].block_number;
								sibus[i].message_code = sibus[s_count-1].message_code;
								sibus[i].magic_number = sibus[s_count-1].magic_number;
							}
							ps[s_count-1].fd = 0;
							ps[s_count-1].events = 0;
							ps[s_count-1].revents = 0;
							s_count--;
							i--;
							continue;
						}
						log_printf(LOG_INFO, "Message succesfully received");
						ps[i].events = POLLOUT;
						//Pass data to struct
						if (memcpy(&sibus[i].magic_number, &buffer, sizeof(sibus[i].magic_number)) == NULL ||
							memcpy(&sibus[i].message_code, &buffer[sizeof(sibus[i].magic_number)], sizeof(sibus[i].message_code)) == NULL ||
							memcpy(&temp, &buffer[sizeof(sibus[i].magic_number) + sizeof(sibus[i].message_code)], sizeof(temp)) == NULL) {
								log_printf(LOG_INFO, "ERROR: MSG_T unsuccesfully copied");
								continue;
						}
						sibus[i].block_number = ((uint64_t)ntohl((uint32_t)temp) << 32) + (uint64_t)ntohl((uint32_t)(temp >> 32));
						//Check valid message and close connection if it doesn't is
						if ((sibus[i].magic_number != MAGIC_NUMBER) || 
							(sibus[i].message_code != MSG_REQUEST) ||
							(sibus[i].block_number > torrent.block_count)) {
							log_printf(LOG_INFO, "Invalid message received");
							if (close(ps[i].fd) == -1) {
								perror("ERROR: Socket not closed");
							}
							if (i != (s_count-1)) {
								ps[i].fd = ps[s_count-1].fd;
								ps[i].events = ps[s_count-1].events;
								ps[i].revents = ps[s_count-1].revents;
								sibus[i].block_number = sibus[s_count-1].block_number;
								sibus[i].message_code = sibus[s_count-1].message_code;
								sibus[i].magic_number = sibus[s_count-1].magic_number;
							}
							ps[s_count-1].fd = 0;
							ps[s_count-1].events = 0;
							ps[s_count-1].revents = 0;
							s_count--;
							i--;
							continue;
						}
						log_printf(LOG_INFO, "Valid message received");
						//Check if we have the requested data
						if (torrent.block_map[sibus[i].block_number] == 0) {
							sibus[i].message_code = MSG_RESPONSE_NA;
						}
						else {
							sibus[i].message_code = MSG_RESPONSE_OK;
						}
						temp = ((uint64_t)htonl((uint32_t)sibus[i].block_number) << 32) + (uint64_t)htonl((uint32_t)(sibus[i].block_number >> 32));
						if (memcpy(&buffer, &sibus[i].magic_number, sizeof(sibus[i].magic_number)) == NULL ||
							memcpy(&buffer[sizeof(sibus[i].magic_number)], &sibus[i].message_code, sizeof(sibus[i].message_code)) == NULL ||
							memcpy(&buffer[sizeof(sibus[i].magic_number) + sizeof(sibus[i].message_code)], &temp, sizeof(temp)) == NULL) {
							log_printf(LOG_INFO, "MSG_T unsuccesfully copied");
							continue;
						}
						//Response to client and handle errors
						if (send(ps[i].fd, &buffer, RAW_MESSAGE_SIZE, 0) == -1) {
							log_printf(LOG_INFO, "ERROR: Message not sent");
							continue;
						}
						log_printf(LOG_INFO, "Message succesfully sent");
						ps[i].revents = 0;
					
					}
					//Send data to client and handle errors
					else if ((ps[i].fd != slt) && (ps[i].revents == POLLOUT)) {
						if (sibus[i].message_code == MSG_RESPONSE_NA) {
							ps[i].events = POLLIN;
						}
						else {
							if (load_block(&torrent, sibus[i].block_number, &block) == -1) {
								perror("ERROR: Block not loaded");
								sibus[i].message_code = MSG_RESPONSE_NA;
								continue;
							}
							log_printf(LOG_INFO, "Block succesfully loaded");
							if (send(ps[i].fd, &block.data, block.size, 0) == -1) {
								perror("Block not sent");
							}
							else {
								log_printf(LOG_INFO, "Block succesfully sent");
							}
							ps[i].events = POLLIN;
						}
						ps[i].revents = 0;
					}
				}
			}
			exit(EXIT_SUCCESS);
		default:
			//Not valid execution
			log_printf(LOG_INFO, "ERROR: Invalid arguments");
			log_printf(LOG_INFO, "Client mode format: ttorrent <metainfo_file>");
			log_printf(LOG_INFO, "Server mode format: ttorrent -l <port> <metinfo_file>");
			exit(EXIT_FAILURE);
	}
	return 0;
}