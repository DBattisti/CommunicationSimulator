/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/**
 * \file sample_agent_common.c
 * \brief Main application implementation.
 *
 * Copyright (C) 2012 Signove Tecnologia Corporation.
 * All rights reserved.
 * Contact: Signove Tecnologia Corporation (contact@signove.com)
 *
 * $LICENSE_TEXT:BEGIN$
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation and appearing
 * in the file LICENSE included in the packaging of this file; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 * $LICENSE_TEXT:END$
 *
 * \author Elvis Pfutzenreuter
 * \date Apr 17, 2012
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <errno.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <errno.h>

#include <ieee11073.h>
#include "communication/plugin/plugin_tcp_agent.h"
#include "specializations/pulse_oximeter.h"
#include "specializations/blood_pressure_monitor.h"
#include "specializations/weighing_scale.h"
#include "specializations/glucometer.h"
#include "agent.h"

intu8 AGENT_SYSTEM_ID_VALUE[] = { 0x11, 0x33, 0x55, 0x77, 0x99,
					0xbb, 0xdd, 0xff};

//MINHA MODIFICAÇÃO
#define MY_PORT		9999
#define MAXBUF		1024


// char* socket()
// {   int sockfd;
// 	struct sockaddr_in self;
// 	char buffer[MAXBUF];

// 	/*---Create streaming socket---*/
//     if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
// 	{
// 		perror("Socket");
// 		exit(errno);
// 	}

// 	/*---Initialize address/port structure---*/
// 	bzero(&self, sizeof(self));
// 	self.sin_family = AF_INET;
// 	self.sin_port = htons(MY_PORT);
// 	self.sin_addr.s_addr = INADDR_ANY;

// 	/*---Assign a port number to the socket---*/
//     if ( bind(sockfd, (struct sockaddr*)&self, sizeof(self)) != 0 )
// 	{
// 		perror("socket--bind");
// 		exit(errno);
// 	}

// 	/*---Make it a "listening socket"---*/
// 	if ( listen(sockfd, 20) != 0 )
// 	{
// 		perror("socket--listen");
// 		exit(errno);
// 	}

// 	/*---Forever... ---*/
// 	while (1)
// 	{	int clientfd,size_answer;
// 		char answer[MAXBUF];
// 		struct sockaddr_in client_addr;
// 		int addrlen=sizeof(client_addr);

// 		/*---accept a connection (creating a data pipe)---*/
// 		clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
// 		printf("%s:%d connected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
		
// 		if ((size_answer = read(clientfd,answer,MAXBUF)) < 0) {
//             perror("Erro ao receber dados do cliente: ");
//             return NULL;
//         }
// 		answer[size_answer] = '\0';
//         printf("O cliente falou: %s\n", answer);
		
// 		/*---Echo back anything sent---*/
// 		send(clientfd, buffer, recv(clientfd, buffer, MAXBUF, 0), 0);

// 		/*---Close data connection---*/
// 		close(clientfd);
// 	}

// 	/*---Clean up (should never get here!)---*/
// 	close(sockfd);
// 	return answer;
// }


/**
 * Generate data for oximeter event report
 */
void *oximeter_event_report_cb()
{
	time_t now;
	struct tm nowtm;
	struct oximeter_event_report_data* data =
		malloc(sizeof(struct oximeter_event_report_data));

	time(&now);
	localtime_r(&now, &nowtm);

	data->beats = 60.5 + random() % 20;
	data->oximetry = 90.5 + random() % 10;
	data->century = nowtm.tm_year / 100 + 19;
	data->year = nowtm.tm_year % 100;
	data->month = nowtm.tm_mon + 1;
	data->day = nowtm.tm_mday;
	data->hour = nowtm.tm_hour;
	data->minute = nowtm.tm_min;
	data->second = nowtm.tm_sec;
	data->sec_fractions = 50;

	return data;
}

/**
 * Generate data for blood pressure event report
 */
void *blood_pressure_event_report_cb()
{
	time_t now;
	struct tm nowtm;
	struct blood_pressure_event_report_data* data =
		malloc(sizeof(struct blood_pressure_event_report_data));

	time(&now);
	localtime_r(&now, &nowtm);

	/////////
	//MINHA MODIFICAÇÃO -- Socket para receber os dados do app
	/////////
	int sockfd;
	struct sockaddr_in self;
	char buffer[MAXBUF];
	float sistolica = 0;
	float diastolica = 0;
	float frequencia = 0;

	/*---Create streaming socket---*/
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("Socket");
		exit(errno);
	}

	/*---Initialize address/port structure---*/
	bzero(&self, sizeof(self));
	self.sin_family = AF_INET;
	self.sin_port = htons(MY_PORT);
	self.sin_addr.s_addr = INADDR_ANY;

	/*---Assign a port number to the socket---*/
    if ( bind(sockfd, (struct sockaddr*)&self, sizeof(self)) != 0 )
	{
		perror("socket--bind");
		exit(errno);
	}

	/*---Make it a "listening socket"---*/
	if ( listen(sockfd, 20) != 0 )
	{
		perror("socket--listen");
		exit(errno);
	}
	printf("\n\nESPERANDO OS DADOS DO APLICATIVO...\n");
	int aux = 0;
	while (aux < 3){
		int clientfd,size_answer;
		char answer[MAXBUF];
		struct sockaddr_in client_addr;
		socklen_t addrlen=sizeof(client_addr);

		/*---accept a connection (creating a data pipe)---*/
		clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
		printf("%s:%d connected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
		
		if ((size_answer = read(clientfd,answer,MAXBUF)) < 0) {
	        perror("Erro ao receber dados do cliente: ");
	        return NULL;
	    }

	    answer[size_answer] = '\0';
	    printf("O cliente falou: %s\n", answer);

	    switch (aux){
	    	case 0:
	    		sistolica = atof(answer);
	    		break;
	    	case 1:
	    		diastolica = atof(answer);
	    		break;
	    	case 2:
	    		frequencia = atof(answer);
	    		break;
	    }
		
		
		/*---Echo back anything sent---*/
		send(clientfd, buffer, recv(clientfd, buffer, MAXBUF, 0), 0);

		/*---Close data connection---*/
		close(clientfd);
		aux++;
	}

	/*---Clean up (should never get here!)---*/
	close(sockfd);

	/////////
	//MINHA MODIFICAÇÃO
	/////////


	data->systolic = sistolica;
	data->diastolic = diastolica;
	//data->mean = 90 + random() % 10;
	data->mean = (2*data->diastolic + data->systolic)/3;
	data->pulse_rate = frequencia;

	data->century = nowtm.tm_year / 100 + 19;
	data->year = nowtm.tm_year % 100;
	data->month = nowtm.tm_mon + 1;
	data->day = nowtm.tm_mday;
	data->hour = nowtm.tm_hour;
	data->minute = nowtm.tm_min;
	data->second = nowtm.tm_sec;
	data->sec_fractions = 50;

	return data;
}

/**
 * Generate data for weight scale event report
 */
void *weightscale_event_report_cb()
{
	time_t now;
	struct tm nowtm;
	struct weightscale_event_report_data* data =
		malloc(sizeof(struct weightscale_event_report_data));

	time(&now);
	localtime_r(&now, &nowtm);

	/////////
	//MINHA MODIFICAÇÃO -- Socket para receber os dados do app
	/////////
	int sockfd;
	struct sockaddr_in self;
	char buffer[MAXBUF];

	/*---Create streaming socket---*/
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("Socket");
		exit(errno);
	}

	/*---Initialize address/port structure---*/
	bzero(&self, sizeof(self));
	self.sin_family = AF_INET;
	self.sin_port = htons(MY_PORT);
	self.sin_addr.s_addr = INADDR_ANY;

	/*---Assign a port number to the socket---*/
    if ( bind(sockfd, (struct sockaddr*)&self, sizeof(self)) != 0 )
	{
		perror("socket--bind");
		exit(errno);
	}

	/*---Make it a "listening socket"---*/
	if ( listen(sockfd, 20) != 0 )
	{
		perror("socket--listen");
		exit(errno);
	}

	printf("\n\nESPERANDO OS DADOS DO APLICATIVO...\n");
	
	int clientfd,size_answer;
	char answer[MAXBUF];
	struct sockaddr_in client_addr;
	socklen_t addrlen=sizeof(client_addr);

	/*---accept a connection (creating a data pipe)---*/
	clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
	printf("%s:%d connected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
	
	if ((size_answer = read(clientfd,answer,MAXBUF)) < 0) {
        perror("Erro ao receber dados do cliente: ");
        return NULL;
    }
	answer[size_answer] = '\0';
    printf("O cliente falou: %s\n", answer);
	
	/*---Echo back anything sent---*/
	send(clientfd, buffer, recv(clientfd, buffer, MAXBUF, 0), 0);

	/*---Close data connection---*/
	close(clientfd);
	

	/*---Clean up (should never get here!)---*/
	close(sockfd);
	

	/////////
	//MINHA MODIFICAÇÃO
	/////////

	// data->weight = 70.2 + random() % 20;
	//Transforma a string answer para float 
	data->weight = atof(answer);
	data->bmi = 20.3 + random() % 10;

	// data->weight = 70.2 + random() % 20;
	// data->bmi = 20.3 + random() % 10;

	data->century = nowtm.tm_year / 100 + 19;
	data->year = nowtm.tm_year % 100;
	data->month = nowtm.tm_mon + 1;
	data->day = nowtm.tm_mday;
	data->hour = nowtm.tm_hour;
	data->minute = nowtm.tm_min;
	data->second = nowtm.tm_sec;
	data->sec_fractions = 50;

	return data;
}

/**
 * Generate data for Glucometer event report
 */
void *glucometer_event_report_cb()
{
	time_t now;
	struct tm nowtm;
	struct glucometer_event_report_data* data =
		malloc(sizeof(struct glucometer_event_report_data));

	time(&now);
	localtime_r(&now, &nowtm);

	data->capillary_whole_blood = 10.2 + random() % 20;

	data->century = nowtm.tm_year / 100 + 19;
	data->year = nowtm.tm_year % 100;
	data->month = nowtm.tm_mon + 1;
	data->day = nowtm.tm_mday;
	data->hour = nowtm.tm_hour;
	data->minute = nowtm.tm_min;
	data->second = nowtm.tm_sec;
	data->sec_fractions = 50;

	return data;
}

/**
 * Generate data for MDS
 */
struct mds_system_data *mds_data_cb()
{
	struct mds_system_data* data = malloc(sizeof(struct mds_system_data));
	memcpy(&data->system_id, AGENT_SYSTEM_ID_VALUE, 8);
	return data;
}
