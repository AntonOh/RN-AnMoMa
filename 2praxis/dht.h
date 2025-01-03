#ifndef DHT_H
#define DHT_H

#include <stdlib.h>

typedef struct node_info {
    int PRED_ID;
    char *PRED_IP; 
    int PRED_PORT; 
    int SUCC_ID;
    char* SUCC_IP;
    int SUCC_PORT; 
    char* MY_IP; 
    int MY_PORT; 
    int MY_ID;
} node_info;

node_info* prep_node_info_getenv();

#endif