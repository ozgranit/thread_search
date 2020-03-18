#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

#define SUCCESS 0
#define FAIL 1

/***************** queue struct & funcs *****************/

typedef struct node{
	
	struct node* next;
	struct node* prev;
	char* data;
	
} node;

typedef struct queue{
	/*each FIFO queue is a list of nodes*/
	node* head;
	node* tail;

} queue;

static int is_empty(queue* q){
	/*should never have tail = null and head with val, or vice versa*/
	if(q->head != NULL && q->tail == NULL){
		assert(0);
	}
	if(q->head == NULL && q->tail != NULL){
		assert(0);
	}
	/***/
	if(q->head == NULL){
		/*true*/
		return 1;
	}
	/*false*/
	return 0;
}

/*queue only holds ptr to data; doesnt alocate it. will free data when deleted*/
static int enqueue(queue* q, char* new_data) {
	
	node* new_node;
	new_node = (node*) malloc(sizeof(node));
	
	/*if malloc failed*/
	if(new_node == NULL){
		printf("inside enqueue: malloc failed, out of memory\n");
		return FAIL;
	}
	
	new_node->data = new_data;
	new_node->next = q->tail;
	new_node->prev = NULL;
	
	/*check queue not empty, then fix tail ptr*/
	if(!is_empty(q)){
		q->tail->prev = new_node;
	}
	/*q is empty, fix head&tail ptr*/
	else{
		q->head =new_node;
	}
	q->tail =new_node;
	
	return SUCCESS;
}

static node* dequeue(queue* q){
	node* tmp;
	
	/*safer*/
	if(q == NULL){
		fprintf(stderr,"inside dequeue: called dequeue on null queue\n");
		assert(0);
		return NULL;
	}
	if(q->head == NULL){
		fprintf(stderr,"inside dequeue: called dequeue on queue with null head\n");
		assert(0);
		return NULL;
	}
	
	tmp = q->head;
	/*if last node in q*/
	if(q->head == q->tail){
		q->head = NULL;
		q->tail = NULL;
	}
	else{
		/*change head*/
		q->head = q->head->prev;
		q->head->next = NULL;
		tmp->prev = NULL;
	}
	
	return tmp;
}

/*delete all the nodes in q*/
static void delete_queue(queue* q){
	node* cur;
	node* next;
	/*safer - ok to delete empty q*/
	if(q == NULL){
		return;
	}
	
	cur = q->tail;
	while(cur != NULL){
		next = cur->next;		
		free(cur->data);
		free(cur);
		cur = next;
	}
	q->head = NULL;
	q->tail = NULL;
}

/*create new empty q*/
static queue* make_queue(){
	
	queue* new_q;
	new_q = (queue*) malloc(sizeof(queue));
	
	/*if malloc failed*/
	if(new_q == NULL){
		fprintf(stderr, "inside make queue: malloc failed, out of memory\n");
		return NULL;
	}
	
	/*initialize queue*/
	new_q->head = NULL;
	new_q->tail = NULL;

	return new_q;
}

/***************** end queue *****************/
/***************** aux funcs *****************/
int is_directory(const char *path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0)
       return 0;
   return S_ISDIR(statbuf.st_mode);
}

char* concat(const char *s1, const char *s2){
    char* result;
	/* +1 for the null-terminator, +1 for "/" */
	result = malloc(strlen(s1) + strlen(s2) + 2); 
    if(result == NULL){
		fprintf(stderr, "inside concat: malloc failed, out of memory\n");
		return NULL;
	}
    strcpy(result, s1);
    strcat(result, s2);
	strcat(result, "/");
    return result;
}
/***************** end aux *****************/
/***global var***/
static char* search_term;
static queue* dir_queue;
pthread_mutex_t  lock;
pthread_cond_t  is_empty_cv;
static int waiting_threads, num_of_threads;
pthread_t* thread_ids;
int found_files;

/***************** thread funcs *****************/
void signal_handler(int sig){
	int i;
	
	for(i = 0; i < num_of_threads; i++){
		if(!pthread_equal(thread_ids[i], pthread_self())){
			pthread_cancel(thread_ids[i]);
		}
	}
	printf("Search stopped, found %d files\n", found_files);
	exit(SUCCESS);
}
void cleanup_handler(void *plock) {
    pthread_mutex_unlock(plock);
}

void *thread_search(void *thread_param){
	
	DIR* dir;
	struct dirent *dp;
	char *file_name, *dir_name, *full_file_name;
	long i;
	long my_id = (long)thread_param;
	pthread_cleanup_push(cleanup_handler, &lock);
	
	
	while(1){
		/*locked operation*/
		pthread_mutex_lock(&lock);
		while(is_empty(dir_queue)){			
			waiting_threads++;
			if(waiting_threads == num_of_threads){
				for(i = 0; i < num_of_threads; i++){
					if(my_id != i){
						/*some thread_cancels will fail if a thread exited on error -
						so dont exit on error of pthread_cancel*/
						pthread_cancel(thread_ids[i]);
					}
				}
				pthread_mutex_unlock(&lock);
				pthread_exit((void*) SUCCESS);
			}
			else{
				pthread_cond_wait(&is_empty_cv, &lock);
				waiting_threads--;
			}
		}
		dir_name = (dequeue(dir_queue))->data;
		pthread_mutex_unlock(&lock);
		/*********/
		dir = opendir(dir_name);
		if(!dir){
			if (ENOENT == errno) {
				/* Directory does not exist */
				fprintf(stderr, "Directory does not exist\n");
				waiting_threads++;
				pthread_cond_signal(&is_empty_cv);
				pthread_exit((void*) FAIL);
			}
			else {
				/* opendir() failed for some other reason */
				fprintf(stderr, "opendir() on Directory failed for some reason\n");
				waiting_threads++;
				pthread_cond_signal(&is_empty_cv);
				pthread_exit((void*) FAIL);
			}
		}
		
		while((dp = readdir(dir)) != NULL){
			if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")){
				/* do nothing */
				continue;
			} 
		
			file_name = dp->d_name;
			full_file_name = concat(dir_name,file_name);
			if(is_directory(full_file_name)){
				/*locked operation*/
				pthread_mutex_lock(&lock);
				if(enqueue(dir_queue, full_file_name)){
					waiting_threads++;
					pthread_mutex_unlock(&lock);
					pthread_cond_signal(&is_empty_cv);
					pthread_exit((void*) FAIL);
				}
				pthread_mutex_unlock(&lock);
				/***/
				pthread_cond_signal(&is_empty_cv);
			}
			else{
				if(strstr(file_name, search_term) != NULL){
					/*filename contains search_term*/
					/*remove "/" */
					full_file_name[strlen(full_file_name)-1] = 0;
					pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
					/*wait for printing to finish before cancel*/
					pthread_mutex_lock(&lock);
					printf("%s\n",full_file_name);
					found_files++;
					pthread_mutex_unlock(&lock);
					pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				}
				/*didnt enqueue it, need to free*/
				free(full_file_name);
			}
		}
		free(dir_name);
		closedir(dir);
	}
	/*unreachable code*/
	pthread_cleanup_pop(0);
	pthread_exit((void*) SUCCESS);
}
/***************** end thread funcs *****************/

int main(int argc, char *argv[]){
	struct sigaction sa;
	char *dir_name;
	long i;
	int rc, return_val;
	void *thread_rv;
	/*define handeling of SIGINT*/
	sa.sa_handler = &signal_handler;
	sigaction(SIGINT, &sa, NULL);
	
	if(argc != 4){
		fprintf(stderr, "Wrong number of arguments\n");
		exit(FAIL);
	}
	
	search_term = argv[2];
	num_of_threads = atoi(argv[3]);
	if(num_of_threads <= 0){
		fprintf(stderr, "%s Is not a positive int\n", argv[3]);
		exit(FAIL);
	}
	/*check for empty dir_name*/
	if(!strcmp(argv[1], "")){
		fprintf(stderr, "Directory does not exist\n");
		exit(FAIL);
	}
	/*dir_name not empty, safe to access strlen-1*/
	if(argv[1][strlen(argv[1])-1] == '/'){
		/*argv[1] ends withe '/'*/
		dir_name = malloc(strlen(argv[1]) + 1); 
		if(dir_name == NULL){
			fprintf(stderr, "inside main: dir_name==null malloc failed, out of memory\n");
			exit(FAIL);
		}
		strcpy(dir_name, argv[1]);
	}
	else{
		/*no '/' at the end of argv[1]*/
		/* get root dir+"/" */
		dir_name = concat(argv[1], ""); 
	}
	
	dir_queue = make_queue();
	if(enqueue(dir_queue, dir_name)){
		exit(FAIL);
	}
	/* Initialize mutex */
	pthread_cond_init (&is_empty_cv, NULL);
	rc = pthread_mutex_init(&lock,NULL);
	if(rc){
		fprintf(stderr, "ERROR in pthread_mutex_init(): %s\n", strerror(rc));
		exit(FAIL);
	}

	/*Create n searching threads*/
	thread_ids = (pthread_t*) malloc(sizeof(pthread_t)*num_of_threads);
	if(thread_ids == NULL){
		fprintf(stderr, "inside main:thread_ids == NULL, malloc failed, out of memory\n");
		exit(FAIL);
	}
	waiting_threads = 0;
	found_files = 0;
	/*each thread search, thread_param = i */
	for(i = 0; i < num_of_threads; i++){
        rc = pthread_create(&thread_ids[i], NULL, thread_search, (void*)i);
        if(rc){
			fprintf(stderr, "Failed creating thread: %s\n", strerror(rc));
			exit(FAIL);
        }
    }
	
	return_val = FAIL;
	for(i = 0; i < num_of_threads; i++){
		pthread_join(thread_ids[i], &thread_rv);
		if((long) thread_rv == SUCCESS){
			return_val = SUCCESS;
		}
    }
	/*return_val = FAIL iff ALL threads failed*/
	
	
	delete_queue(dir_queue);
	pthread_mutex_destroy(&lock);
	pthread_cond_destroy(&is_empty_cv);
	printf("Done searching, found %d files\n", found_files);
	return return_val;
}

