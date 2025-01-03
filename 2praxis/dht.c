#include <stdlib.h>
#include "dht.h"

node_info* prep_node_info_getenv() {
    node_info* my_struct = malloc(sizeof(node_info));
    my_struct->PRED_ID = atoi(getenv("PRED_ID"));
    my_struct->PRED_IP = getenv("PRED_IP");
    my_struct->PRED_PORT = atoi(getenv("PRED_PORT"));

    my_struct->SUCC_ID = atoi(getenv("SUCC_ID"));
    my_struct->SUCC_IP = getenv("SUCC_IP");
    my_struct->SUCC_PORT = atoi(getenv("SUCC_PORT"));
    return my_struct;
}