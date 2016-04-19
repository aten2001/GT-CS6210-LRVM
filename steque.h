#ifndef STEQUE_H
#define STEQUE_H

typedef void* steque_item;

typedef struct steque_node_t{
  steque_item item;
  struct steque_node_t* next;
} steque_node_t;

typedef struct{
  steque_node_t* front;
  steque_node_t* back;
  int N;
}steque_t;


/* Initializes the data structure */
void steque_init(steque_t* header);

/* Return 1 if empty, 0 otherwise */
int steque_isempty(steque_t* header);

/* Returns the number of elements in the steque */
int steque_size(steque_t* header);

/* Adds an element to the "back" of the steque */
void steque_enqueue(steque_t* header, steque_item item);

/* Adds an element to the "front" of the steque */
void steque_push(steque_t* header, steque_item item);

/* Removes an element to the "front" of the steque */
steque_item steque_pop(steque_t* header);

/* Removes the element on the "front" to the "back" of the steque */
void steque_cycle(steque_t* header);

/* Returns the element at the "front" of the steque without removing it*/
steque_item steque_front(steque_t* header);

/* Empties the steque and performs any necessary memory cleanup */
void steque_destroy(steque_t* header);

#endif
