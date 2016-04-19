#include <stdlib.h>
#include <stdio.h>
#include "steque.h"

void steque_init(steque_t *header){
  header->front = NULL;
  header->back = NULL;
  header->N = 0;
}

void steque_enqueue(steque_t* header, steque_item item){
  steque_node_t* node;

  node = (steque_node_t*) malloc(sizeof(steque_node_t));
  node->item = item;
  node->next = NULL;
  
  if(header->back == NULL)
    header->front = node;
  else
    header->back->next = node;

  header->back = node;
  header->N++;
}

void steque_push(steque_t* header, steque_item item){
  steque_node_t* node;

  node = (steque_node_t*) malloc(sizeof(steque_node_t));
  node->item = item;
  node->next = header->front;

  if(header->back == NULL)
    header->back = node;
  
  header->front = node;
  header->N++;
}

int steque_size(steque_t* header){
  return header->N;
}

int steque_isempty(steque_t *header){
  return header->N == 0;
}

steque_item steque_pop(steque_t* header){
  steque_item ans;
  steque_node_t* node;
  
  if(header->front == NULL){
    fprintf(stderr, "Error: underflow in steque_pop.\n");
    fflush(stderr);
    exit(EXIT_FAILURE);
  }

  node = header->front;
  ans = node->item;

  header->front = header->front->next;
  if (header->front == NULL) header->back = NULL;
  free(node);

  header->N--;

  return ans;
}

void steque_cycle(steque_t* header){
  if(header->back == NULL)
    return;
  
  header->back->next = header->front;
  header->back = header->front;
  header->front = header->front->next;
  header->back->next = NULL;
}

steque_item steque_front(steque_t* header){
  if(header->front == NULL){
    fprintf(stderr, "Error: underflow in steque_front.\n");
    fflush(stderr);
    exit(EXIT_FAILURE);
  }
  
  return header->front->item;
}

void steque_destroy(steque_t* header){
  while(!steque_isempty(header))
    steque_pop(header);
}
